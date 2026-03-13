#ifndef POWERSHELL_WRAPPER_H
#define POWERSHELL_WRAPPER_H

#include "Common.h"
#include "SecureLogger.h"
#include "AuthenticationManager.h"
#include "ThreatDetector.h"
#include "Obfuscator.h"
#include "NiceError.h"
#include "InactivityMonitor.h"

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>

class PowerShellWrapper {
public:
    PowerShellWrapper();
    ~PowerShellWrapper();

    int Run();
    int MainLoop();
    void Shutdown();

    static PowerShellWrapper* s_instance;
    static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);

private:
    void ShowMockingBanner();
    void ShowBanner();
    void ShowHelp();
    std::string ReadPassword();
    std::vector<std::string> GetAutocompleteMatches(const std::string& partial, bool isFirstWord = true);
    size_t FindPreviousWordStart(const std::string& buffer, size_t cursorPos);
    size_t FindNextWordStart(const std::string& buffer, size_t cursorPos);
    size_t FindWordEnd(const std::string& buffer, size_t cursorPos);
    std::string ReadInputWithHistory();
    void EnableANSIColors();
    void OnInactivityTimeout();
    std::string SanitizeForLog(const std::string& input);
    bool LaunchPowerShell();
    void UserInputThread();
    void ProcessMonitorThread();
    void SendToChild(const std::string& s);
    void PowerShellOutputThread();
    void SecureClearString(std::string& str);
    bool ProcessWrapperCommands(const std::string& input);
    void ShowStatus();
    void ShowHistory();
    void AddToHistory(const std::string& cmd);
    std::string Trim(const std::string& s);
    std::string SanitizeOutput(const std::string& s);
    std::string GetCurrentPrompt() const;

private:
    std::atomic<bool> m_shouldExit{false};
    std::atomic<bool> m_childRunning{false};
    std::atomic<bool> m_monitoringEnabled{true};
    std::atomic<bool> m_psReady{false};
    std::atomic<bool> m_masterKeySet{false};
    std::atomic<bool> m_ctrlCPressed{false};
    std::atomic<bool> m_suppressOutput{false}; // Set by Ctrl+C to discard leftover pipe data
    std::chrono::steady_clock::time_point m_lastCtrlCTime; // Track when Ctrl+C was last pressed
    std::mutex m_authMutex;

    HANDLE m_hChildProcess = NULL;
    HANDLE m_hChildStdinWrite = NULL;
    HANDLE m_hChildStdoutRead = NULL;

    std::thread m_inputThread;
    std::thread m_outputThread;
    std::thread m_monitorThread;

    std::deque<std::string> m_commandHistory;
    size_t m_maxHistorySize = 50;
    std::string m_currentUser;

    SecureLogger& m_logger = SecureLogger::GetInstance();
    AuthenticationManager m_authManager;
    ThreatDetector m_threatDetector;
    NiceError m_niceError;
    Obfuscator& m_obfuscator = ObfuscatorManager::GetInstance().GetObfuscator();
    InactivityMonitor m_inactivityMonitor{300};

    std::string m_sentinel = "\u200B\u200C\u200D";
    std::mutex m_syncMutex;
    std::condition_variable m_syncCv;
    std::atomic<bool> m_commandFinished{true};
    std::atomic<bool> m_bootComplete{false};

    int m_failedAuthAttempts = 0;
    std::chrono::steady_clock::time_point m_authLockoutEnd;
    int GetExponentialBackoffSeconds() const;  // Calculate lockout duration with exponential backoff
    std::string m_trackedCwd;
    std::string m_lastCommandSent;
    std::string m_outputAccumulator;
    std::string m_currentSuggestion;
    std::atomic<bool> m_isBinarySuppressed{false};
};

#endif // POWERSHELL_WRAPPER_H
