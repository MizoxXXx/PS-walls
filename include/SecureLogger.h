#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <iostream>
#include <chrono>
#include "Common.h"
#include "DPAPI_Utils.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

enum class LogLevel {
    INFO,
    WARNING,
    ERR,
    DEBUG
};

struct CommandLogEntry {
    std::string timestamp;
    std::string command;           // Full command
    std::string truncatedCommand;  // Truncated version for status display
    std::string user;
    bool flagged;
    std::string severity;
    DWORD pid;
    std::string sessionId;         // Unique session identifier
    
    CommandLogEntry() : flagged(false), severity("NORMAL"), pid(0), sessionId("") {}
};

class AuthenticationManager;

class SecureLogger {
public:
    static SecureLogger& GetInstance();
    SecureLogger(const SecureLogger&) = delete;
    SecureLogger& operator=(const SecureLogger&) = delete;

    bool Init(const std::string& logFileName = "commandsLog.json", bool encryptLogs = false);
    void Shutdown();
    void EnableEncryption(bool enabled);
    void SetMaxEntries(size_t max) { m_maxEntries = max; }
    void LogCommand(const std::string& command, const std::string& user, const std::string& truncated = "", bool flagged = false);
    void Log(LogLevel level, const std::string& message, const std::string& user = "SYSTEM", bool flagged = false);
    bool ExportLog(const std::string& options = "");
    void ClearLog(const std::string& options = "");
    bool RotateLogs(const std::string& rotationType = "logs");  // "logs" or "keys"
    size_t GetLogCount() const;
    size_t CountFlaggedEntries() const;
    void SetAuthManager(AuthenticationManager* auth);
    const std::vector<CommandLogEntry>& GetEntries() const { return m_entries; }
    std::string GetSessionId() const { return m_sessionId; }

private:
    SecureLogger();
    ~SecureLogger();
    bool m_initialized;
    bool m_encryptLogs;
    size_t m_maxEntries = 1000;
    std::string m_logFilePath;
    std::ofstream m_logFile;
    mutable std::recursive_mutex m_mutex;
    std::vector<CommandLogEntry> m_entries;
    AuthenticationManager* m_auth = nullptr;
    std::string m_sessionId;  // Unique per-session identifier

    std::string FormatMessage(LogLevel level, const std::string& message);
    std::string GetLevelLogPath(LogLevel level);
    void AppendToLevelLog(LogLevel level, const std::string& formattedMsg);
    std::string GetLogDirectory();
    std::string GetArchiveDirectory();
    std::string GenerateSessionId();
    std::string TruncateCommand(const std::string& command, size_t maxLength = 150);
    std::string EscapeJson(const std::string& str);
    void SaveEntriesToFile();    
    void ClearLogFile();  // Completely delete all log files from disk
    void LoadEntriesFromFile();
    std::string GenerateExportFilename();
    bool AuthenticateForExport();
    void OpenFileInEditor(const std::string& filepath);
    std::vector<CommandLogEntry> FilterEntries(const std::string& options);
    void ArchiveLogFile(const std::string& archiveType);  // "logs" or "keys"
    void ScheduleFileDeletion(const std::string& filepath, int delaySeconds = 300);
    std::string GetCurrentTimestamp();
};
