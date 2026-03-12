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
    if (m_entries.size() > m_maxEntries) m_entries.erase(m_entries.begin());
    
    // Periodically save to disk to avoid excessive I/O
    static int saveCounter = 0;
    if (++saveCounter >= 10) {
        SaveEntriesToFile();
        saveCounter = 0;
    }
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
    std::string exportPath = GetLogDirectory() + "\\" + "PS_Audit_" + std::to_string(std::time(nullptr)) + ext;
    
    SaveEntriesToFile();
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
}

void SecureLogger::SaveEntriesToFile() {
    if (m_logFilePath.empty()) return;
    try {
        // Load existing on-disk entries first to prevent loss of data
        // when multiple sessions write simultaneously
        nlohmann::json existing = nlohmann::json::array();
        if (!m_encryptLogs && std::filesystem::exists(m_logFilePath)) {
            std::ifstream fin(m_logFilePath);
            std::string raw((std::istreambuf_iterator<char>(fin)),
                             std::istreambuf_iterator<char>());
            if (!raw.empty()) {
                try { existing = nlohmann::json::parse(raw); } catch (...) {}
            }
        }

        // Append ALL entries from this session (NO deduplication)
        // Each execution becomes a separate log entry, even if identical
        for (const auto& e : m_entries) {
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

        std::string content = existing.dump(4);

        if (m_encryptLogs) {
            auto encrypted = DPAPI_Protect(content, L"PSWrapperLogs");
            if (!encrypted.empty()) DPAPI_WriteBinaryFile(m_logFilePath, encrypted);
        } else {
            std::ofstream f(m_logFilePath);
            f << content;
        }
    } catch (...) {}
}

void SecureLogger::LoadEntriesFromFile() {
    if (!std::filesystem::exists(m_logFilePath)) return;
    try {
        std::string content;
        if (m_encryptLogs) {
            std::vector<BYTE> blob;
            if (DPAPI_ReadBinaryFile(m_logFilePath, blob)) content = DPAPI_Unprotect(blob);
        } else {
            std::ifstream f(m_logFilePath);
            content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        
        if (content.empty()) return;
        auto j = nlohmann::json::parse(content);
        m_entries.clear();
        for (const auto& item : j) {
            CommandLogEntry e;
            e.timestamp = item["timestamp"];
            e.command = item["command"];
            e.truncatedCommand = item.contains("truncatedCommand") ? item["truncatedCommand"].get<std::string>() : "";
            e.user = item["user"];
            e.flagged = item["flagged"];
            e.severity = item["severity"];
            e.pid = item["pid"];
            e.sessionId = item.contains("sessionId") ? item["sessionId"].get<std::string>() : "";
            m_entries.push_back(e);
        }
    } catch (...) {}
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
                SaveEntriesToFile(); // This will create an empty or minimal log file
                
                std::cout << ANSI_CYAN << "[*] Current log archived to: " << archivePath << ANSI_RESET << std::endl;
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
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
