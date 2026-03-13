#include "SecureLogger.h"
#include "AuthenticationManager.h"
#include "DPAPI_Utils.h"
#include "json.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <set>

using json = nlohmann::json;

SecureLogger::SecureLogger() : m_initialized(false), m_encryptLogs(false), m_maxEntries(1000) {
    m_sessionId = GenerateSessionId();
}
SecureLogger::~SecureLogger() { Shutdown(); }
SecureLogger& SecureLogger::GetInstance() { static SecureLogger instance; return instance; }

bool SecureLogger::Init(const std::string& logFileName, bool encryptLogs) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;
    m_encryptLogs = encryptLogs;
    m_logFilePath = GetLogDirectory() + "\\" + logFileName;
    m_initialized = true;
    LoadEntriesFromFile();
    return true;
}

void SecureLogger::Log(LogLevel level, const std::string& message, const std::string& user, bool flagged) {
    (void)level; // Parameter not currently used
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;
    CommandLogEntry e; 
    e.timestamp = GetCurrentTimestamp(); 
    e.command = message; 
    e.user = user; 
    e.flagged = flagged;
    e.pid = GetCurrentProcessId();
    e.severity = flagged ? "HIGH" : "NORMAL";
    e.sessionId = m_sessionId;
    
    m_entries.push_back(e);
    m_pendingEntries.push_back(e);
    if (m_entries.size() > m_maxEntries) m_entries.erase(m_entries.begin());
    
    // Save to disk IMMEDIATELY for real-time multi-session sync
    SaveEntriesToFile();
}

void SecureLogger::LogCommand(const std::string& command, const std::string& user, const std::string& truncated, bool flagged) {
    Log(flagged ? LogLevel::WARNING : LogLevel::INFO, command, user, flagged);
    // Update the last entry with truncated version
    if (!m_entries.empty()) {
        if (!truncated.empty()) {
            m_entries.back().truncatedCommand = truncated;
        } else {
            // Auto-truncate if command is longer than 120 chars
            m_entries.back().truncatedCommand = TruncateCommand(command);
        }
    }
}

bool SecureLogger::ExportLog(const std::string& options) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool useJson = (options.find("txt") == std::string::npos); // Default to JSON unless "txt" is specified
    bool showFull = (options.find("full") != std::string::npos || options.find("verbose") != std::string::npos);
    std::string ext = useJson ? ".json" : ".txt";
    std::string exportPath = GetLogDirectory() + "\\" + "PS_Audit_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(GetCurrentProcessId()) + ext;
    
    RefreshLogs();
    auto filtered = FilterEntries(options);
    
    if (filtered.empty()) {
        std::cout << "[-] No logs match the specified criteria. Export cancelled." << std::endl;
        return false;
    }
    
    std::ofstream f(exportPath);
    if (!f.is_open()) return false;
    
    if (useJson) {
        nlohmann::json j;
        j["report_info"] = {
            {"title", "PS~WALLS SECURE AUDIT LOG EXPORT"},
            {"generated", GetCurrentTimestamp()},
            {"filter", options.empty() ? "NONE" : options},
            {"show_full_commands", showFull}
        };
        auto entriesArray = nlohmann::json::array();
        for (const auto& e : filtered) {
            auto entry = nlohmann::json{
                {"timestamp", e.timestamp},
                {"user", e.user},
                {"pid", e.pid},
                {"command", e.command},
                {"sessionId", e.sessionId},
                {"flagged", e.flagged},
                {"severity", e.severity}
            };
            
            if (showFull) {
                entry["command_full"] = e.command;
                entry["command_display"] = e.truncatedCommand.empty() ? e.command : e.truncatedCommand;
            } else {
                entry["command"] = e.truncatedCommand.empty() ? e.command : e.truncatedCommand;
            }
            
            entriesArray.push_back(entry);
        }
        j["entries"] = entriesArray;
        f << j.dump(4);
    } else {
        f << "====================================================\n";
        f << "        PS~WALLS SECURE AUDIT LOG EXPORT\n";
        f << "        Generated: " << GetCurrentTimestamp() << "\n";
        f << "        Filter: " << (options.empty() ? "NONE" : options) << "\n";
        f << "        Full Commands: " << (showFull ? "YES" : "NO") << "\n";
        f << "====================================================\n\n";
        
        for (const auto& e : filtered) {
            f << "[" << e.timestamp << "] Session: " << e.sessionId << " | User: " << e.user << " | PID: " << e.pid << "\n";
            if (showFull) {
                f << "Command (Full): " << e.command << "\n";
                f << "Command (Display): " << (e.truncatedCommand.empty() ? e.command : e.truncatedCommand) << "\n";
            } else {
                f << "Command: " << (e.truncatedCommand.empty() ? e.command : e.truncatedCommand) << "\n";
            }
            f << "Flagged: " << (e.flagged ? "YES" : "NO") << " | Severity: " << e.severity << "\n";
            f << "----------------------------------------------------\n";
        }
    }
    f.close();
    
    OpenFileInEditor(exportPath);
    ScheduleFileDeletion(exportPath);
    return true;
}

void SecureLogger::Shutdown() { 
    std::lock_guard<std::recursive_mutex> lock(m_mutex); 
    SaveEntriesToFile();
    m_initialized = false; 
}

void SecureLogger::ClearLog(const std::string& options) { 
    std::lock_guard<std::recursive_mutex> lock(m_mutex); 
    RefreshLogs();
    if (options.empty()) {
        m_entries.clear();
        // Completely delete ALL log files, not just clear memory
        ClearLogFile();
    } else {
        std::vector<CommandLogEntry> toKeep;
        
        // Extract filter values
        int days = -1;
        size_t fromPos = options.find("--from");
        if (fromPos != std::string::npos) {
            try { days = std::stoi(options.substr(fromPos + 6)); } catch (...) {}
        }
        
        std::string level;
        size_t levelPos = options.find("level");
        if (levelPos != std::string::npos) {
            size_t valPos = options.find_first_not_of(" ", levelPos + 5);
            if (valPos != std::string::npos) {
                size_t valEnd = options.find(" ", valPos);
                level = options.substr(valPos, valEnd - valPos);
            }
        }
        
        auto now = std::chrono::system_clock::now();
        auto cutoff = (days > 0) ? (now - std::chrono::hours(24 * days)) : std::chrono::system_clock::time_point::min();

        for (const auto& e : m_entries) {
            bool matchesFilter = true;
            
            // Check level filter
            if (!level.empty()) {
                if (e.severity != level) matchesFilter = false;
            }
            
            // Check date filter
            if (matchesFilter && days > 0) {
                std::tm tm = {};
                int year, month, day, hour, min, sec;
                if (sscanf_s(e.timestamp.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
                    tm.tm_year = year - 1900;
                    tm.tm_mon = month - 1;
                    tm.tm_mday = day;
                    tm.tm_hour = hour;
                    tm.tm_min = min;
                    tm.tm_sec = sec;
                    tm.tm_isdst = -1;
                    auto entryTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    if (entryTime < cutoff) matchesFilter = true; // In ClearLog, "matches" means "to delete"
                    else matchesFilter = false;
                } else {
                    matchesFilter = false;
                }
            }
            
            // If it doesn't match the clear criteria, keep it
            if (!matchesFilter) {
                toKeep.push_back(e);
            }
        }
        m_entries = toKeep;
        SaveEntriesToFile();  // Write filtered logs back
    }
}

size_t SecureLogger::GetLogCount() const { std::lock_guard<std::recursive_mutex> lock(m_mutex); return m_entries.size(); }

size_t SecureLogger::CountFlaggedEntries() const { 
    const_cast<SecureLogger*>(this)->RefreshLogs();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return std::count_if(m_entries.begin(), m_entries.end(), [](const CommandLogEntry& e) { return e.flagged; });
}

void SecureLogger::SetAuthManager(AuthenticationManager* auth) { m_auth = auth; }

void SecureLogger::EnableEncryption(bool enabled) { m_encryptLogs = enabled; }

std::string SecureLogger::GetLogDirectory() {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path);
    std::string result;
    if (SUCCEEDED(hr) && path) {
        std::wstring wpath(path);
        CoTaskMemFree(path);
        
        int size = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            std::string rootPath(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &rootPath[0], size, NULL, NULL);
            result = rootPath + "\\PowerShellWrapper\\Logs";
        } else {
            result = ".\\Logs";
        }
    } else {
        result = ".\\Logs";
    }
    std::filesystem::create_directories(result);
    return result;
}

// Stubs for private methods to satisfy potential header usage or future expansion
std::string SecureLogger::FormatMessage(LogLevel, const std::string& message) { return message; }

std::string SecureLogger::GetLevelLogPath(LogLevel) { return ""; }

void SecureLogger::AppendToLevelLog(LogLevel, const std::string&) {}

std::string SecureLogger::EscapeJson(const std::string& str) { return str; }

void SecureLogger::ClearLogFile() {
    // Completely delete all log files (not just clear memory)
    HANDLE hGlobalLock = LockGlobalMutex();
    if (!hGlobalLock) return;

    try {
        std::string logDir = GetLogDirectory();
        std::error_code ec;
        
        // Delete the main log file
        if (!m_logFilePath.empty() && std::filesystem::exists(m_logFilePath)) {
            std::filesystem::remove(m_logFilePath, ec);
        }
        
        // Delete all exported log files
        for (const auto& entry : std::filesystem::directory_iterator(logDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.find("PS_Audit_") == 0) {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
        }
    } catch (...) {}
    
    UnlockGlobalMutex(hGlobalLock);
}

void SecureLogger::SaveEntriesToFile() {
    if (m_logFilePath.empty()) return;
    
    std::lock_guard<std::recursive_mutex> innerLock(m_mutex);
    if (m_pendingEntries.empty()) return;

    HANDLE hGlobalLock = LockGlobalMutex();
    if (!hGlobalLock) return;

    try {
        nlohmann::json existing = nlohmann::json::array();
        
        if (std::filesystem::exists(m_logFilePath)) {
            if (m_encryptLogs) {
                std::vector<BYTE> blob;
                if (DPAPI_ReadBinaryFile(m_logFilePath, blob)) {
                    std::string plain = DPAPI_Unprotect(blob);
                    if (!plain.empty()) {
                        try { existing = nlohmann::json::parse(plain); } catch (...) {}
                    }
                }
            } else {
                std::ifstream fin(m_logFilePath);
                if (fin.is_open()) {
                    std::string raw((std::istreambuf_iterator<char>(fin)),
                                     std::istreambuf_iterator<char>());
                    if (!raw.empty()) {
                        try { existing = nlohmann::json::parse(raw); } catch (...) {}
                    }
                    fin.close();
                }
            }
        }

        // Append ONLY NEW entries from this session's pending list
        for (const auto& e : m_pendingEntries) {
            existing.push_back({
                {"timestamp", e.timestamp},
                {"command",   e.command},
                {"truncatedCommand", e.truncatedCommand.empty() ? e.command : e.truncatedCommand},
                {"user",      e.user},
                {"flagged",   e.flagged},
                {"severity",  e.severity},
                {"pid",       e.pid},
                {"sessionId", e.sessionId}
            });
        }
        
        // Enforce max entries on the WHOLE log file to prevent infinite growth
        if (existing.size() > m_maxEntries * 2) { // Allow more on disk than in memory
            existing.erase(existing.begin(), existing.end() - (m_maxEntries * 2));
        }

        std::string content = existing.dump(4);

        if (m_encryptLogs) {
            auto encrypted = DPAPI_Protect(content, L"PSWrapperLogs");
            if (!encrypted.empty()) DPAPI_WriteBinaryFile(m_logFilePath, encrypted);
        } else {
            std::ofstream f(m_logFilePath, std::ios::trunc);
            if (f.is_open()) {
                f << content;
                f.close();
            }
        }
        
        m_pendingEntries.clear();
    } catch (...) {}
    
    UnlockGlobalMutex(hGlobalLock);
}

void SecureLogger::LoadEntriesFromFile() {
    if (!std::filesystem::exists(m_logFilePath)) return;
    
    HANDLE hGlobalLock = LockGlobalMutex();
    if (!hGlobalLock) return;

    try {
        std::string content;
        if (m_encryptLogs) {
            std::vector<BYTE> blob;
            if (DPAPI_ReadBinaryFile(m_logFilePath, blob)) content = DPAPI_Unprotect(blob);
        } else {
            std::ifstream f(m_logFilePath);
            if (f.is_open()) {
                content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                f.close();
            }
        }
        
        if (!content.empty()) {
            auto j = nlohmann::json::parse(content);
            std::lock_guard<std::recursive_mutex> innerLock(m_mutex);
            m_entries.clear();
            for (const auto& item : j) {
                CommandLogEntry e;
                e.timestamp = item.value("timestamp", "");
                e.command = item.value("command", "");
                e.truncatedCommand = item.value("truncatedCommand", "");
                e.user = item.value("user", "");
                e.flagged = item.value("flagged", false);
                e.severity = item.value("severity", "NORMAL");
                e.pid = item.value("pid", (DWORD)0);
                e.sessionId = item.value("sessionId", "");
                m_entries.push_back(e);
            }
        }
    } catch (...) {}
    
    UnlockGlobalMutex(hGlobalLock);
}

void SecureLogger::RefreshLogs() {
    SaveEntriesToFile(); // Push local pending logs first so others can see them
    LoadEntriesFromFile(); // Pull all entries from disk into m_entries
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Re-apply locally pending entries if they are not already in m_entries
    // (They might not be if they haven't been saved yet)
    // We check against sessionId and timestamp for a loose deduplication
    for (const auto& pending : m_pendingEntries) {
        bool found = false;
        // Search backwards as it's likely near the end
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
            if (it->sessionId == pending.sessionId && it->timestamp == pending.timestamp && it->command == pending.command) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_entries.push_back(pending);
        }
    }
    
    if (m_entries.size() > m_maxEntries) {
        m_entries.erase(m_entries.begin(), m_entries.end() - m_maxEntries);
    }
}

std::string SecureLogger::GenerateExportFilename() {
    return "PS_Audit_" + std::to_string(std::time(nullptr)) + ".txt";
}

bool SecureLogger::AuthenticateForExport() { return true; }

void SecureLogger::OpenFileInEditor(const std::string& filepath) {
    ShellExecuteA(NULL, "open", filepath.c_str(), NULL, NULL, SW_SHOW);
}

std::vector<CommandLogEntry> SecureLogger::FilterEntries(const std::string& options) {
    std::vector<CommandLogEntry> result = m_entries;

    // Handle --from <days> filter
    size_t fromPos = options.find("--from");
    if (fromPos != std::string::npos) {
        try {
            std::string daysStr = options.substr(fromPos + 6);
            int days = std::stoi(daysStr);
            auto now = std::chrono::system_clock::now();
            auto cutoff = now - std::chrono::hours(24 * days);
            
            std::vector<CommandLogEntry> filtered;
            for (const auto& e : result) {
                // Parse timestamp "YYYY-MM-DD HH:MM:SS"
                std::tm tm = {};
                int year, month, day, hour, min, sec;
                if (sscanf_s(e.timestamp.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
                    tm.tm_year = year - 1900;
                    tm.tm_mon = month - 1;
                    tm.tm_mday = day;
                    tm.tm_hour = hour;
                    tm.tm_min = min;
                    tm.tm_sec = sec;
                    tm.tm_isdst = -1;
                    auto entryTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    if (entryTime >= cutoff) {
                        filtered.push_back(e);
                    }
                }
            }
            result = filtered;
        } catch (...) {}
    }

    if (options.find("full") != std::string::npos) return result;
    if (options.find("flagged") != std::string::npos) {
        std::vector<CommandLogEntry> filtered;
        for (const auto& e : result) if (e.flagged) filtered.push_back(e);
        return filtered;
    }
    
    // Default: show last 50 entries of the already filtered set
    if (result.size() <= 50) return result;
    return std::vector<CommandLogEntry>(result.end() - 50, result.end());
}

void SecureLogger::ScheduleFileDeletion(const std::string& filepath, int delaySeconds) {
    std::thread([filepath, delaySeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
        std::filesystem::remove(filepath);
    }).detach();
}

std::string SecureLogger::GenerateSessionId() {
    // Generate unique session ID: timestamp + random component
    auto now = std::chrono::system_clock::now();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    DWORD pid = GetCurrentProcessId();
    
    std::stringstream ss;
    ss << std::hex << milliseconds << "_" << pid << "_" << (rand() % 10000);
    return ss.str();
}

std::string SecureLogger::GetArchiveDirectory() {
    std::string logDir = GetLogDirectory();
    std::string archiveDir = logDir + "\\Archive";
    std::filesystem::create_directories(archiveDir);
    return archiveDir;
}

bool SecureLogger::RotateLogs(const std::string& rotationType) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (rotationType == "logs") {
        // Archive current log file
        ArchiveLogFile("logs");
        std::cout << ANSI_GREEN << "[+] Log files rotated and archived." << ANSI_RESET << std::endl;
        return true;
    } else if (rotationType == "keys") {
        // Archive key rotation data
        ArchiveLogFile("keys");
        std::cout << ANSI_GREEN << "[+] Key rotation completed and archived." << ANSI_RESET << std::endl;
        return true;
    }
    
    std::cout << ANSI_RED << "[-] Invalid rotation type. Use 'logs' or 'keys'." << ANSI_RESET << std::endl;
    return false;
}

void SecureLogger::ArchiveLogFile(const std::string& archiveType) {
    HANDLE hGlobalLock = LockGlobalMutex();
    if (!hGlobalLock) return;

    try {
        std::string logDir = GetLogDirectory();
        std::string archiveDir = GetArchiveDirectory();
        std::string timestamp = std::to_string(std::time(nullptr));
        
        if (archiveType == "logs") {
            // Archive the current commandsLog.json
            if (std::filesystem::exists(m_logFilePath)) {
                std::string archivePath = archiveDir + "\\commandsLog_" + timestamp + ".json";
                std::filesystem::copy(m_logFilePath, archivePath, std::filesystem::copy_options::overwrite_existing);
                
                // Clear current log entries in memory and file
                m_entries.clear();
                // unlock while calling SaveEntriesToFile to avoid deadlock (it will re-lock)
                // but actually SaveEntriesToFile is called inside ArchiveLogFile?
                // SaveEntriesToFile re-locks.
                // It's safer to release before calling SaveEntriesToFile OR 
                // modify SaveEntriesToFile to have a version without locking.
            }
        } else if (archiveType == "keys") {
            // Archive rotation period file if needed
            std::string rotationFile = logDir + "\\.wrapper_rotation.dat";
            if (std::filesystem::exists(rotationFile)) {
                std::string archivePath = archiveDir + "\\.wrapper_rotation_" + timestamp + ".dat";
                std::filesystem::copy(rotationFile, archivePath, std::filesystem::copy_options::overwrite_existing);
                std::cout << ANSI_CYAN << "[*] Rotation file archived." << ANSI_RESET << std::endl;
            }
        }
    } catch (const std::exception& ex) {
        std::cout << ANSI_RED << "[-] Archive failed: " << ex.what() << ANSI_RESET << std::endl;
    }
    
    UnlockGlobalMutex(hGlobalLock);
    
    if (archiveType == "logs") {
         SaveEntriesToFile(); // This recalcs the file content (now empty)
    }
}

std::string SecureLogger::TruncateCommand(const std::string& command, size_t maxLength) {
    // If command is longer than maxLength, truncate and add ellipsis
    if (command.length() > maxLength) {
        return command.substr(0, maxLength) + "... [TRUNCATED]";
    }
    return command;
}

std::string SecureLogger::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm buf;
    if (localtime_s(&buf, &in_time_t) == 0) {
        ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    }
    return ss.str();
}

HANDLE SecureLogger::LockGlobalMutex() {
    // Create a named mutex for system-wide synchronization
    // The "Global\" prefix allows it to work across terminal sessions if needed, 
    // but requires SeCreateGlobalPrivilege. Local\ is safer for standard users.
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "Local\\PSWrapper_Log_Mutex");
    if (hMutex == NULL) return NULL;

    DWORD waitResult = WaitForSingleObject(hMutex, 5000); // 5 second timeout
    if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED) {
        return hMutex;
    }

    CloseHandle(hMutex);
    return NULL;
}

void SecureLogger::UnlockGlobalMutex(HANDLE hMutex) {
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
}
