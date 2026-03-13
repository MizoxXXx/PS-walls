#include "PowerShellWrapper.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip> // Formatting input and output streams
#include <conio.h> // Console for Input/Output
#include <filesystem>
#include <shlobj.h>
#include <chrono>

PowerShellWrapper* PowerShellWrapper::s_instance = nullptr;

PowerShellWrapper::PowerShellWrapper() {
    char username[256];
    DWORD size = sizeof(username);
    m_currentUser = (GetUserNameA(username, &size)) ? username : "Unknown";
    EnableANSIColors();

    HWND console = GetConsoleWindow();
    if (console != NULL) {
        HICON hIcon = (HICON)LoadImageW(
            GetModuleHandleW(NULL),
            MAKEINTRESOURCEW(101),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            0
        );
        if (hIcon) {
            SendMessageW(console, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(console, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
    m_inactivityMonitor.SetCallback([this]() {
        OnInactivityTimeout();
    });

    s_instance = this;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

BOOL WINAPI PowerShellWrapper::ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        if (s_instance) {
            bool commandRunning = !s_instance->m_commandFinished.load();
            if (commandRunning) {
                std::cout << "\n" << ANSI_YELLOW << "[!] Ctrl+C detected - interrupting..." << ANSI_RESET << "\n";
                std::cout.flush();
                
                // Signal the child process to interrupt its current operation
                if (s_instance->m_hChildProcess) {
                    GenerateConsoleCtrlEvent(CTRL_C_EVENT, GetProcessId(s_instance->m_hChildProcess));
                }

                {
                    std::lock_guard<std::mutex> lock(s_instance->m_syncMutex);
                    s_instance->m_outputAccumulator.clear();
                    s_instance->m_commandFinished = true;
                    s_instance->m_suppressOutput = true; // Suppress ALL leftover pipe data
                    s_instance->m_lastCtrlCTime = std::chrono::steady_clock::now(); // Record Ctrl+C timestamp
                }
                s_instance->m_syncCv.notify_one();
                
                // AGGRESSIVE pipe drain - discard ALL buffered output until pipe is truly empty
                // Commands like ping can have delayed output that arrives well after Ctrl+C
                const int MAX_DRAIN_TIME = 3000;  // Extended to 3 seconds
                const int SILENCE_TIMEOUT = 300;  // Need 300ms of silence to confirm pipe is empty
                
                auto drainStart = std::chrono::steady_clock::now();
                auto lastDataTime = drainStart;
                
                while (std::chrono::steady_clock::now() - drainStart < std::chrono::milliseconds(MAX_DRAIN_TIME)) {
                    DWORD avail = 0;
                    // Try to read anything in the pipe
                    if (PeekNamedPipe(s_instance->m_hChildStdoutRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                        // Data is available - read and discard it
                        std::vector<char> drainBuf(avail + 1);
                        DWORD drainRead = 0;
                        if (ReadFile(s_instance->m_hChildStdoutRead, drainBuf.data(), avail, &drainRead, nullptr) && drainRead > 0) {
                            lastDataTime = std::chrono::steady_clock::now();
                            // Discard the data - don't store it
                        }
                    } else {
                        // Pipe is empty right now - check if we've been silent long enough
                        auto silenceDuration = std::chrono::steady_clock::now() - lastDataTime;
                        if (silenceDuration >= std::chrono::milliseconds(SILENCE_TIMEOUT)) {
                            // Confirmed: pipe is truly empty
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                
                // Additional wait to ensure PowerShell is truly idle
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else {
                std::cout << "\n" << ANSI_YELLOW << "[!] Ctrl+C" << ANSI_RESET << "\n";
                std::cout.flush();
                s_instance->m_ctrlCPressed = true;
            }
            return TRUE;
        }
    }
    return FALSE;
}

PowerShellWrapper::~PowerShellWrapper() {
    Shutdown();
}

void PowerShellWrapper::ShowMockingBanner() {
    std::cout << ANSI_RED << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          ⚠️  INTRUSION DETECTED ⚠️\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";
    std::cout << "            .---.\n";
    std::cout << "           /     \\\n";
    std::cout << "          | () () |     \n";
    std::cout << "           \\  ^  /     \n";
    std::cout << "            |||||       \n";
    std::cout << "            |||||  \n\n";
    std::cout << "    ███████╗ ██████╗  ██████╗ ██╗     ██╗\n";
    std::cout << "    ██╔════╝██╔═══██╗██╔═══██╗██║     ██║\n";
    std::cout << "    █████╗  ██║   ██║██║   ██║██║     ██║\n";
    std::cout << "    ██╔══╝  ██║   ██║██║   ██║██║     ╚═╝\n";
    std::cout << "    ██║     ╚██████╔╝╚██████╔╝███████╗██╗\n";
    std::cout << "    ╚═╝      ╚═════╝  ╚═════╝ ╚══════╝╚═╝\n\n";
    std::cout << "  ╔══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ═╗\n";
    std::cout << "     YOU THOUGHT YOU WERE CLEVER, DIDN'T YOU?          ║\n";
    std::cout << "  ║                                                    ║\n";
    std::cout << "     ❌ Bypass FAILED                                  ║\n";
    std::cout << "  ║  ❌ Exploit DENIED                                 ║\n";
    std::cout << "     ❌ Your Dignity ??                                ║\n";
    std::cout << "  ║                                                    ║\n";
    std::cout << "     My walls are IMPENETRABLE!                        ║\n";
    std::cout << "  ╚══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ══ ═╝\n\n";
    std::cout << "     Better luck next time, NOOOB! 😈\n\n";
    std::cout << "════════════════════════════════════════════════════════\n";
    std::cout << ANSI_RESET << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

int PowerShellWrapper::Run() {
    ShowBanner();
     if (m_authManager.LoadKeys()) {
          m_masterKeySet = true;
     }

    if (m_trackedCwd.empty()) {
        if (!m_authManager.LoadLastDirectory(m_trackedCwd)) {
            PWSTR path = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &path))) {
                std::wstring wpath(path);
                CoTaskMemFree(path);
                int size = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
                if (size > 0) {
                    m_trackedCwd.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &m_trackedCwd[0], size, NULL, NULL);
                }
            }
        }
    }

    bool savedState = false;
    if (m_authManager.LoadObfuscationState(savedState)) { }

    if (!m_masterKeySet) {
        std::cout << ANSI_YELLOW << "\n[!] FIRST TIME SETUP REQUIRED" << ANSI_RESET << "\n";
        std::cout << ANSI_CYAN << "[*] You must set a master password before proceeding" << ANSI_RESET << "\n\n";
        while (!m_masterKeySet && !m_shouldExit) {
            std::cout << ANSI_BLUE << "PS~walls> " << ANSI_RESET;
            std::cout.flush();
            std::string input = ReadInputWithHistory();
            input = Trim(input);
            if (m_shouldExit) return 0;
            if (input.empty()) continue;
            std::istringstream iss(input);
            std::string cmd;
            iss >> cmd;
            if (cmd == "setkey") {
                std::cout << "Enter NEW Main Master Key: ";
                std::string pwd = Trim(ReadPassword());
                if (!pwd.empty()) {
                    m_authManager.SetKey(pwd);
                    m_masterKeySet = true;
                    std::cout << ANSI_GREEN << "[+] Master password set successfully" << ANSI_RESET << "\n";
                    std::cout << ANSI_CYAN << "[*] You can now use 'openme' to authenticate" << ANSI_RESET << "\n\n";
                    SecureClearString(pwd);
                    break;
                } else {
                    std::cout << ANSI_RED << "[-] Password cannot be empty" << ANSI_RESET << "\n";
                }
            } else if (cmd == "exit" || cmd == "quit") {
                m_shouldExit = true;
                return 0;
            } else if (cmd == "help") {
                ShowHelp();
            } else {
                std::cout << ANSI_RED << "[-] You must run 'setkey <password>' first" << ANSI_RESET << "\n";
            }
        }
    }

    m_logger.Init("commandsLog.json", false);
    m_logger.SetAuthManager(&m_authManager);
    m_obfuscator.SetEnabled(false);

    bool savedObfState = false;
    if (m_authManager.LoadObfuscationState(savedObfState)) {
        m_obfuscator.SetEnabled(savedObfState);
        std::string stateStr = savedObfState ? "Enabled" : "Disabled";
        std::cout << ANSI_CYAN << "[*] Obfuscation (" << stateStr << ") restored from last session" << ANSI_RESET << std::endl;
    }

    if (!LaunchPowerShell()) {
        std::cerr << ANSI_RED << "[FATAL] Failed to launch PowerShell" << ANSI_RESET << std::endl;
        return 1;
    }

    std::cout << ANSI_YELLOW << "[*] Starting PowerShell..." << ANSI_RESET << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "\r" << ANSI_GREEN << "[+] PowerShell ready   " << ANSI_RESET << "\n\n";
    m_psReady = true;

    m_inactivityMonitor.Start();

    m_commandFinished = false; // Mark as busy during boot
    m_outputThread = std::thread(&PowerShellWrapper::PowerShellOutputThread, this);
    m_inputThread = std::thread(&PowerShellWrapper::UserInputThread, this);
    m_monitorThread = std::thread(&PowerShellWrapper::ProcessMonitorThread, this);

    return MainLoop();
}

int PowerShellWrapper::MainLoop() {
    if (m_inputThread.joinable()) m_inputThread.join();
    if (m_outputThread.joinable()) m_outputThread.join();
    if (m_monitorThread.joinable()) m_monitorThread.join();
    DWORD exitCode = 0;
    if (m_hChildProcess) GetExitCodeProcess(m_hChildProcess, &exitCode);
    return static_cast<int>(exitCode);
}

void PowerShellWrapper::Shutdown() {
    m_shouldExit = true;
    m_inactivityMonitor.Stop();
    if (m_hChildProcess && m_childRunning) {
        TerminateProcess(m_hChildProcess, 0);
        WaitForSingleObject(m_hChildProcess, 2000);
    }
    if (m_inputThread.joinable()) m_inputThread.detach(); // By detaching it, we allow the main process to exit without human action
    if (m_outputThread.joinable()) m_outputThread.join();
    if (m_monitorThread.joinable()) m_monitorThread.join();
    if (m_hChildStdinWrite) CloseHandle(m_hChildStdinWrite);
    if (m_hChildStdoutRead) CloseHandle(m_hChildStdoutRead);
    if (m_hChildProcess) CloseHandle(m_hChildProcess);
    m_logger.Shutdown();
    std::cout << "\n" << ANSI_CYAN << "    ========================================\n";
    std::cout << "      " << ANSI_WHITE << "PS~walls terminated successfully" << ANSI_CYAN << "  \n";
    std::cout << "    ========================================" << ANSI_RESET << "\n\n";
}

void PowerShellWrapper::ShowBanner() {
    std::cout << "\n";
    std::cout << ANSI_WHITE << "     ██████╗ ███████╗" << ANSI_MAGENTA << "    ~~    " << ANSI_CYAN << "██╗    ██╗ █████╗ ██╗     ██╗     ███████╗" << "\n";
    std::cout << ANSI_WHITE << "     ██╔══██╗██╔════╝" << ANSI_MAGENTA << "   ~~~    " << ANSI_CYAN << "██║    ██║██╔══██╗██║     ██║     ██╔════╝" << "\n";
    std::cout << ANSI_WHITE << "     ███████╗███████╗" << ANSI_MAGENTA << "  ~~~~    " << ANSI_CYAN << "██║ █╗ ██║███████║██║     ██║     ███████╗" << "\n";
    std::cout << ANSI_WHITE << "     ██╔═══╝ ╚════██║" << ANSI_MAGENTA << "   ~~~    " << ANSI_CYAN << "██║███╗██║██╔══██║██║     ██║     ╚════██║" << "\n";
    std::cout << ANSI_WHITE << "     ██║     ███████║" << ANSI_MAGENTA << "    ~~    " << ANSI_CYAN << "╚███╔███╔╝██║  ██║███████╗███████╗███████║" << "\n";
    std::cout << ANSI_WHITE << "     ╚═╝     ╚══════╝" << ANSI_MAGENTA << "          " << ANSI_CYAN << " ╚══╝╚══╝ ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝" << "\n";
    std::string tagline = "When Powershell becomes safer";
    int taglineSpacing = (77 - static_cast<int>(tagline.length())) / 2;
    std::cout << ANSI_CYAN << "\n";
    std::cout << std::string(taglineSpacing, ' ') << ANSI_WHITE << tagline << ANSI_CYAN << "\n";
    std::cout << ANSI_MAGENTA << "    ═══════════════════════════════════════════════════════════════════════\n";
    std::cout << ANSI_RESET << "\n"; 
    std::cout << ANSI_MAGENTA << "    💢" << ANSI_BLUE << "───────────────────────────────────────────────────────────────────" << ANSI_MAGENTA << "💢\n";
    std::cout << ANSI_BLUE << "        " << ANSI_WHITE << "Version:    " << ANSI_GREEN << "v1.0" 
            << ANSI_BLUE << "  │  " << ANSI_WHITE << "Operator: " << ANSI_GREEN << m_currentUser;
    int spacing = 60 - m_currentUser.length();
    if (spacing > 0) std::cout << std::string(spacing, ' ');
    std::cout << "\n";
    
    // Status/Mode line - dynamically centered
    std::string statusLine = std::string(ANSI_BLUE) + "        " + std::string(ANSI_WHITE) + "Status:  " + std::string(ANSI_YELLOW) + "⚡ Ready" 
                           + std::string(ANSI_BLUE) + "   │  " + std::string(ANSI_WHITE) + "Mode: " + std::string(ANSI_CYAN) + "Enigma";
    std::string statusLineClean = "        Status:  ⚡ Ready   │  Mode: Enigma";  // 45 chars
    int statusSpacing = (77 - static_cast<int>(statusLineClean.length())) / 2;
    if (statusSpacing > 0) std::cout << std::string(statusSpacing, ' ');
    std::cout << ANSI_BLUE << "        " << ANSI_WHITE << "Status:  " << ANSI_YELLOW << "⚡ Ready" 
            << ANSI_BLUE << "   │  " << ANSI_WHITE << "Mode: " << ANSI_CYAN << "Enigma" << ANSI_RESET << "\n";
    std::cout << ANSI_MAGENTA << "    💢" << ANSI_BLUE << "───────────────────────────────────────────────────────────────────" << ANSI_MAGENTA << "💢\n";
    std::cout << ANSI_RESET << "\n";
    std::cout << ANSI_MAGENTA << ANSI_GREEN << "       ╭─ " << ANSI_WHITE << "💢 CORE CAPABILITIES" << ANSI_GREEN << " ────────────────────────────────╮\n";
    std::cout << ANSI_GREEN << "       │\n";
    std::cout << "       │  " << ANSI_CYAN << "🛡️  " << ANSI_WHITE << "Real-time Threat Detection" << ANSI_RESET << " & Command Monitoring\n";
    std::cout << "       │  " << ANSI_CYAN << "🔐  " << ANSI_WHITE << "DPAPI-Encrypted Credential" << ANSI_RESET << " Storage System\n";
    std::cout << "       │  " << ANSI_CYAN << "⏱️  " << ANSI_WHITE << "Auto-Obfuscation" << ANSI_RESET << " on Inactivity (5min timeout)\n";
    std::cout << "       │  " << ANSI_CYAN << "📊  " << ANSI_WHITE << "Secure Audit Logging" << ANSI_RESET << " with JSON Export\n";
    std::cout << "       │  " << ANSI_CYAN << "⚔️  " << ANSI_WHITE << "Pattern-Based Malicious" << ANSI_RESET << " Command Blocking\n";
    std::cout << ANSI_GREEN << "       │\n";
    std::cout << "       ╰────────────────────────────────────────────────────────╯\n";
    std::cout << ANSI_RESET << "\n";
    std::cout << ANSI_MAGENTA << ANSI_YELLOW << "       ╭─ " << ANSI_WHITE << "💢 QUICK START GUIDE" << ANSI_YELLOW << " ──────────────────────────────────────╮\n";
    std::cout << ANSI_YELLOW << "       │\n";
    std::cout << "       │  " << ANSI_CYAN << "➤ setkey" << ANSI_WHITE << " <password>" << ANSI_RESET 
            << "  Set master password (in first-use)\n";
    std::cout << ANSI_YELLOW << "       │  " << ANSI_CYAN << "➤ openme" << ANSI_WHITE << " <password>" << ANSI_RESET 
            << "  Authenticate to disable onfuscation \n";
    std::cout << ANSI_YELLOW << "       │  " << ANSI_CYAN << "➤ help" << ANSI_WHITE << "              " << ANSI_RESET 
            << " Display full command reference\n";
    std::cout << ANSI_YELLOW << "       │\n";
    std::cout << "       ╰─────────────────────────────────────────────────────────────╯\n";
    std::cout << ANSI_RESET << "\n";
}

void PowerShellWrapper::ShowHelp() {
    std::cout << "\n" << ANSI_CYAN << "    === COMMAND REFERENCE ===" << ANSI_RESET << "\n\n";
    std::cout << ANSI_YELLOW << "    [Authentication]" << ANSI_RESET << "\n";
    std::cout << "      setkey              - Set or change master password (requires old password if set)\n";
    std::cout << "      openme <pass>       - Authenticate session to unlock system commands\n\n";
    std::cout << ANSI_YELLOW << "    [Obfuscation]" << ANSI_RESET << "\n";
    std::cout << "      enable              - Activate obfuscation and lock session\n";
    std::cout << "      disable             - Deactivate obfuscation (requires authentication)\n";
    std::cout << "      status              - Show detailed system and security status\n\n";
    std::cout << ANSI_YELLOW << "    [Logging & Configuration]" << ANSI_RESET << "\n";
    std::cout << "      status          - Show system status\n";
    std::cout << "      timer <mins>    - Set inactivity timeout (auth required)\n";
    std::cout << "      rotate          - Manage log and key rotation (auth required)\n";
    std::cout << "                        Usage: rotate keys|logs <days>\n";
    std::cout << "      history         - View command history (auth required)\n";
    std::cout << "      clearlog        - Clear log entries (auth required)\n";
    std::cout << "      exportlog       - Export logs to file with investigation options (auth required)\n";
    std::cout << "      walls           - Launch a new wrapper session window\n";
    std::cout << "      exit/quit       - Terminate session wrapper\n\n";
    std::cout << ANSI_YELLOW << "    [Extras]" << ANSI_RESET << "\n";
    std::cout << "      exportlog [options] : use --help or -h for details\n";
    std::cout << "      clearlog  [options] : use --help or -h for details\n";
}

std::string PowerShellWrapper::ReadPassword() {
    std::string password;
    char ch;
    while ((ch = _getch()) != '\r') {
        if (ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b"; // Erase the asterisk from display
            }
        } else if (ch == 3) {
             return "";
        } else {
            password += ch;
            std::cout << '*';
        }
    }
    std::cout << std::endl;
    return password;
}

std::string PowerShellWrapper::SanitizeOutput(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    
    for (unsigned char c : s) {
        // Keep: whitespace, ANSI escape, printable ASCII, UTF-8
        if (c == 9 || c == 10 || c == 13 ||  // whitespace
            c == 27 ||                        // ANSI escape
            (c >= 32 && c <= 126) ||           // printable ASCII
            c >= 128) {                         // UTF-8/high-bit
            result += c;
        }
        // All other control chars (0-31 except whitespace/ESC) are dropped
    }
    
    return result;
}

std::vector<std::string> PowerShellWrapper::GetAutocompleteMatches(const std::string& partial, bool isFirstWord) {
    std::vector<std::string> matches;
    std::string p = Trim(partial);

    // 1. Prioritize internal wrapper commands only for the first word
    if (isFirstWord && p.find_last_of("\\/") == std::string::npos) {
        std::vector<std::string> internalCmds = { "setkey", "openme", "help", "exit", "status", "enable", "disable", "timer", "history", "clearlog", "exportlog", "rotate", "walls" };
        for (const auto& cmd : internalCmds) {
            if (_strnicmp(cmd.c_str(), p.c_str(), p.length()) == 0) {
                matches.push_back(cmd);
            }
        }
    }

    // 2. Local filesystem autocomplete
    namespace fs = std::filesystem;
    try {
        fs::path searchBase;
        if (m_trackedCwd.empty()) {
            searchBase = fs::current_path();
        } else {
            searchBase = fs::path(m_trackedCwd);
        }

        // SECURITY FIX: Use fs::canonical() instead of weakly_canonical() to prevent symlink attacks
        fs::path canonicalBase;
        try {
            canonicalBase = fs::canonical(searchBase); 
            // Check if base path contains symlinks
            if (fs::is_symlink(canonicalBase)) return matches;
        } catch (...) {
            canonicalBase = fs::current_path();
        }
        std::string searchPrefix = partial;
        fs::path subdir = "";
        size_t lastSep = partial.find_last_of("\\/");
        if (lastSep != std::string::npos) {
            subdir = partial.substr(0, lastSep + 1);
            searchPrefix = partial.substr(lastSep + 1);
            
            // Security: Block ".." traversal attempts
            if (subdir.string().find("..") != std::string::npos) return matches;
            
            // Handle absolute paths
            if (subdir.string().find(":") != std::string::npos || (subdir.string().size() >= 1 && (subdir.string()[0] == '\\' || subdir.string()[0] == '/'))) {
                searchBase = subdir;
            } else {
                searchBase = canonicalBase / subdir;
            }

            try {
                fs::path canonicalSearch = fs::canonical(searchBase);
                // SECURITY: Reject if path contains symlinks
                for (const auto& p : canonicalSearch) {
                    if (fs::is_symlink(p)) return matches;
                }
                std::string canonicalSearchStr = canonicalSearch.string();
                std::string canonicalBaseStr = canonicalBase.string();
                std::replace(canonicalSearchStr.begin(), canonicalSearchStr.end(), '/', '\\');
                std::replace(canonicalBaseStr.begin(), canonicalBaseStr.end(), '/', '\\');
                // Only enforce sandbox if NOT an absolute path or explicitly allowed
                if (subdir.string().find(":") == std::string::npos && canonicalSearchStr.find(canonicalBaseStr) != 0) return matches;
            } catch (...) {
                return matches;
            }
        } else {
            searchBase = canonicalBase;
        }
        if (!fs::exists(searchBase) || !fs::is_directory(searchBase)) return matches;

        for (const auto& entry : fs::directory_iterator(searchBase)) {
            std::string filename = entry.path().filename().string();
            if (_strnicmp(filename.c_str(), searchPrefix.c_str(), searchPrefix.length()) == 0) {
                std::string match = subdir.string() + filename;
                if (fs::is_directory(entry.path())) match += "\\"; // add \ if directory 
                matches.push_back(match);
            }
        }
    } catch (...) {}
    return matches;
}

size_t PowerShellWrapper::FindPreviousWordStart(const std::string& buffer, size_t cursorPos) {
    if (cursorPos == 0) return 0;
    while (cursorPos > 0 && buffer[cursorPos - 1] == ' ') cursorPos--;
    while (cursorPos > 0 && buffer[cursorPos - 1] != ' ') cursorPos--;
    return cursorPos;
}

size_t PowerShellWrapper::FindNextWordStart(const std::string& buffer, size_t cursorPos) {
    while (cursorPos < buffer.length() && buffer[cursorPos] != ' ') cursorPos++;
    while (cursorPos < buffer.length() && buffer[cursorPos] == ' ') cursorPos++;
    return cursorPos;
}

size_t PowerShellWrapper::FindWordEnd(const std::string& buffer, size_t cursorPos) {
    while (cursorPos < buffer.length() && buffer[cursorPos] == ' ') cursorPos++;
    while (cursorPos < buffer.length() && buffer[cursorPos] != ' ') cursorPos++;
    return cursorPos;
}

std::string PowerShellWrapper::ReadInputWithHistory() {
    std::string buffer;
    char ch;
    int historyIndex = -1;
    std::string currentBuffer;
    size_t cursorPos = 0;
    std::vector<std::string> tabMatches;
    int tabIndex = -1;
    std::string tabOriginalBuffer;
    std::string tabPartialWord;
    size_t tabWordStart = 0;

    while (true) {
        // Wait for key press or check for external signals (Ctrl+C)
        if (!_kbhit()) {
            if (m_ctrlCPressed.load()) {
                m_ctrlCPressed = false;
                std::cout << "\n";
                return "";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue; 
        }
        m_inactivityMonitor.ResetTimer();
        ch = _getch();

        // Reset TAB completion if a different key is pressed
        if (ch != 9) {
            if (!tabMatches.empty()) {
                tabMatches.clear();
                tabIndex = -1;
            }
        }
        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        // Handle TAB Autocomplete
        if (ch == 9) {
            if (tabMatches.empty()) {
                tabOriginalBuffer = buffer;
                size_t spacePos = buffer.find_last_of(" ");
                if (spacePos == std::string::npos) {
                    tabWordStart = 0;
                    tabPartialWord = buffer;
                } else {
                    tabWordStart = spacePos + 1;
                    tabPartialWord = buffer.substr(tabWordStart);
                }
                tabMatches = GetAutocompleteMatches(tabPartialWord, tabWordStart == 0);
                if (tabMatches.empty()) continue;
                tabIndex = 0;
            } else {
                tabIndex = (tabIndex + 1) % tabMatches.size();
            }
            std::string match = tabMatches[tabIndex];
            while (buffer.length() > 0) {
                std::cout << "\b \b";
                buffer.pop_back();
            }
            if (tabWordStart > 0) {
                buffer = tabOriginalBuffer.substr(0, tabWordStart) + match;
            } else {
                buffer = match;
            }
            cursorPos = buffer.length();
            std::cout << buffer;
            continue;
        }

        // Navigation through command history and buffers
        // Handle Arrows and Special Keys
        if (ch == -32 || ch == 0 || (unsigned char)ch == 224) {
            char nextCh = _getch();
            if (ch == -32 || (unsigned char)ch == 224) {
                // Navigation through command history
                if (nextCh == 72) { // UP
                    if (m_commandHistory.empty()) continue;
                    if (historyIndex == -1) {
                        currentBuffer = buffer;
                        historyIndex = (int)m_commandHistory.size() - 1;
                    } else if (historyIndex > 0) {
                        historyIndex--;
                    }
                    // Get new command from history
                    buffer = m_commandHistory[historyIndex];
                    cursorPos = buffer.length();
                    
                    // Clear the line cleanly using carriage return and clearing
                    std::cout << "\r";
                    std::cout << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET;
                    std::cout << buffer;
                    
                    // Clear any remaining characters from old display
                    std::cout << "                                                                    ";  
                    std::cout << "\r" << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET << buffer;
                    std::cout.flush();
                } else if (nextCh == 80) { // DOWN
                    if (historyIndex == -1) continue;
                    
                    // Fetch new buffer content
                    if (historyIndex < (int)m_commandHistory.size() - 1) {
                        historyIndex++;
                        buffer = m_commandHistory[historyIndex];
                    } else {
                        historyIndex = -1;
                        buffer = currentBuffer;
                    }
                    cursorPos = buffer.length();
                    
                    // Clear the line cleanly using carriage return and clearing
                    std::cout << "\r";
                    std::cout << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET;
                    std::cout << buffer;
                    
                    // Clear any remaining characters from old display
                    std::cout << "                                                                    "; 
                    std::cout << "\r" << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET << buffer;
                    std::cout.flush();
                } 
                // Cursor movement (standard and word-by-word)
                else if (nextCh == 75 || nextCh == 115) { // LEFT or Ctrl+LEFT
                    if (ctrlPressed || nextCh == 115) {
                        size_t oldPos = cursorPos;
                        cursorPos = FindPreviousWordStart(buffer, cursorPos);
                        for (size_t i = 0; i < oldPos - cursorPos; i++) std::cout << "\b";
                    } else if (cursorPos > 0) {
                        cursorPos--;
                        std::cout << "\b";
                    } else std::cout << "\a";
                } else if (nextCh == 77 || nextCh == 116) { // RIGHT or Ctrl+RIGHT
                    if (ctrlPressed || nextCh == 116) {
                        size_t oldPos = cursorPos;
                        cursorPos = FindNextWordStart(buffer, cursorPos);
                        for (size_t i = oldPos; i < cursorPos; i++) std::cout << buffer[i];
                    } else if (cursorPos < buffer.length()) {
                        std::cout << buffer[cursorPos];
                        cursorPos++;
                    } else std::cout << "\a";
                } 
                // Line editing
                else if (nextCh == 71) { // HOME
                    while (cursorPos > 0) { std::cout << "\b"; cursorPos--; }
                } else if (nextCh == 79) { // END
                    while (cursorPos < buffer.length()) { std::cout << buffer[cursorPos]; cursorPos++; }
                } else if (nextCh == 83 || nextCh == (char)147) { // DELETE or Ctrl+DELETE (scan 83 or 0x93)
                    if (ctrlPressed || nextCh == (char)147) {
                        if (cursorPos < buffer.length()) {
                            size_t wordEnd = FindWordEnd(buffer, cursorPos);
                            size_t deleteCount = wordEnd - cursorPos;
                            buffer.erase(cursorPos, deleteCount);
                            std::cout << buffer.substr(cursorPos);
                            for (size_t i = 0; i < deleteCount; i++) std::cout << " ";
                            for (size_t i = 0; i < buffer.length() - cursorPos + deleteCount; i++) std::cout << "\b";
                        }
                    } else if (cursorPos < buffer.length()) {
                        buffer.erase(cursorPos, 1);
                        std::cout << buffer.substr(cursorPos) << " ";
                        for (size_t i = 0; i < buffer.length() - cursorPos + 1; i++) std::cout << "\b";
                    }
                }
            }
            continue; 
        }
        if (ch == 3) return ""; // Ctrl+C: Cancel current input line
        if (ch == 12) { // Ctrl+L 
            system("cls"); // or Clear-Host
            std::cout << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET << buffer;
            for (size_t i = buffer.length(); i > cursorPos; i--) std::cout << "\b";
            continue;
        }
        if (ch == 18) { // Ctrl+R: Reverse history search
            if (m_commandHistory.empty()) continue;
            std::string searchTerm;
            int searchIndex = -1;
            std::string matchedCmd;
            
            // Erase current line to show search prompt
            for (size_t i = 0; i < cursorPos; i++) std::cout << "\b";
            for (size_t i = 0; i < buffer.length(); i++) std::cout << " ";
            for (size_t i = 0; i < buffer.length(); i++) std::cout << "\b";
            
            auto showSearch = [&]() {
                // Build the complete search display line
                std::string searchDisplay = "(reverse-i-search)`" + searchTerm + "': ";
                if (!matchedCmd.empty()) searchDisplay += matchedCmd;
                
                // Calculate how much space we need to clear (at least 120 chars to be safe)
                static size_t lastDisplayLen = 0;
                size_t clearLen = std::max(lastDisplayLen, searchDisplay.length()) + 10;
                lastDisplayLen = searchDisplay.length();
                
                // Go to start of line and clear it completely
                std::cout << "\r";
                std::cout << ANSI_YELLOW << searchDisplay << ANSI_RESET;
                
                // Clear any remaining garbage to the right
                for (size_t i = searchDisplay.length(); i < clearLen; i++) std::cout << " ";
                
                // Move cursor back to end of actual content
                std::cout << "\r";
                std::cout << ANSI_YELLOW << searchDisplay << ANSI_RESET;
                std::cout.flush();
            };

            auto doSearch = [&]() {
                matchedCmd.clear();
                int startFrom = (searchIndex >= 0) ? searchIndex - 1 : (int)m_commandHistory.size() - 1;
                for (int i = startFrom; i >= 0; i--) {
                    // First priority: commands that start with the search term
                    if (m_commandHistory[i].find(searchTerm) == 0) {
                        searchIndex = i;
                        matchedCmd = m_commandHistory[i];
                        return;
                    }
                }
                // Second pass: if no prefix match, look for substring matches
                startFrom = (searchIndex >= 0) ? searchIndex - 1 : (int)m_commandHistory.size() - 1;
                for (int i = startFrom; i >= 0; i--) {
                    if (m_commandHistory[i].find(searchTerm) != std::string::npos) {
                        searchIndex = i;
                        matchedCmd = m_commandHistory[i];
                        return;
                    }
                }
                searchIndex = -1; // No match found
            };

            showSearch();

            bool accepted = false;
            while (true) {
                if (!_kbhit()) {
                    if (m_ctrlCPressed.load()) { m_ctrlCPressed = false; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                int sch = _getch();
                if (sch == 27) break; // Escape: cancel
                if (sch == 3) break;  // Ctrl+C: cancel
                if (sch == 18) {      // Ctrl+R again: find next match
                    if (!searchTerm.empty()) { doSearch(); showSearch(); }
                    continue;
                }
                if (sch == '\r') { accepted = true; break; } // Enter: accept
                if (sch == '\b' || sch == 127) { // Backspace
                    if (!searchTerm.empty()) {
                        searchTerm.pop_back();
                        searchIndex = -1; // Reset search position
                        if (!searchTerm.empty()) doSearch();
                        else matchedCmd.clear();
                        showSearch();
                    }
                    continue;
                }
                if (sch >= 32 && sch < 127) { // Printable character
                    searchTerm += (char)sch;
                    doSearch();
                    showSearch();
                }
            }

            // Restore the prompt line
            std::string prompt = GetCurrentPrompt();
            
            // Clear the entire search display line (use wider clearance to ensure no garbage)
            std::cout << "\r";
            for (size_t i = 0; i < 200; i++) std::cout << " ";  // Clear 200 chars to be absolutely safe
            std::cout << "\r";
            
            // Show fresh prompt
            std::cout << ANSI_BLUE << prompt << ANSI_RESET;

            if (accepted && !matchedCmd.empty()) {
                buffer = matchedCmd;
                cursorPos = buffer.length();
                std::cout << buffer;
            } else {
                // Restore original buffer
                std::cout << buffer;
                for (size_t i = buffer.length(); i > cursorPos; i--) std::cout << "\b";
            }
            std::cout.flush();
            continue;
        }
        if (ch == '\r') { std::cout << std::endl; break; } // return to the line start 
        if (ch == 127 || (ch == 8 && ctrlPressed)) { // Ctrl+Backspace
            if (cursorPos > 0) {
                size_t wordStart = FindPreviousWordStart(buffer, cursorPos);
                size_t deleteCount = cursorPos - wordStart;
                buffer.erase(wordStart, deleteCount);
                for (size_t i = 0; i < deleteCount; i++) std::cout << "\b";
                cursorPos = wordStart;
                std::string tail = buffer.substr(cursorPos);
                std::cout << tail << std::string(deleteCount, ' ');
                for (size_t i = 0; i < tail.length() + deleteCount; i++) std::cout << "\b";
            }
            continue;
        }
        if (ch == '\b') { // backspace without erasing anything
            if (cursorPos > 0) {
                buffer.erase(cursorPos - 1, 1);
                cursorPos--;
                std::cout << "\b" << buffer.substr(cursorPos) << " ";
                for (size_t i = 0; i < buffer.length() - cursorPos + 1; i++) std::cout << "\b";
            } else std::cout << "\a";
            continue;
        }

        if (ch < 32 && ch != '\r' && ch != '\t' && ch != '\b') continue;

        std::string tempBuffer = buffer;
        if (cursorPos < tempBuffer.length()) tempBuffer.insert(cursorPos, 1, ch);
        else tempBuffer += ch;

        bool showClear = true;
        if (!m_authManager.IsAuthenticated() && m_obfuscator.IsEnabled()) {
            showClear = false;
            std::vector<std::string> whitelist = {"setkey", "openme", "help", "exit", "quit", "status", "enable", "timer", "history", "clearlog", "exportlog"};
            std::string checkStr = Trim(tempBuffer);
            for (const auto& w : whitelist) {
                if (checkStr.length() >= w.length()) {
                    if (checkStr.substr(0, w.length()) == w) { showClear = true; break; }
                } else {
                    if (w.substr(0, checkStr.length()) == checkStr) { showClear = true; break; }
                }
            }
        }

        if (cursorPos < buffer.length()) {
            buffer.insert(cursorPos, 1, ch);
            std::string toRedraw;
            if (showClear) toRedraw = buffer.substr(cursorPos);
            else for (size_t k = cursorPos; k < buffer.length(); k++) toRedraw += (char)('a' + GetRandomInt(0, 25));
            std::cout << toRedraw;
            for (size_t i = 0; i < toRedraw.length() - 1; i++) std::cout << "\b";
            cursorPos++;
        } else {
            buffer += ch;
            cursorPos++;
            if (showClear) std::cout << ch;
            else std::cout << (char)('a' + GetRandomInt(0, 25));
        }
        std::cout.flush();
    }

    if (!m_authManager.IsAuthenticated() && m_obfuscator.IsEnabled()) {
        bool isWhitelisted = false;
        std::vector<std::string> whitelist = {"setkey", "openme", "help", "exit", "quit", "status", "enable"};
        std::string bufTrim = Trim(buffer);
        for (const auto& w : whitelist) {
            if (bufTrim.find(w) == 0) {
                // Ensure it's not chained with other commands using common delimiters or injection chars
                if (bufTrim.size() == w.size() || isspace(bufTrim[w.size()]) || 
                    bufTrim[w.size()] == '-' || bufTrim[w.size()] == ';' || 
                    bufTrim[w.size()] == '|' || bufTrim[w.size()] == '&' || 
                    bufTrim[w.size()] == '`' || bufTrim[w.size()] == '<' || 
                    bufTrim[w.size()] == '>') {
                    isWhitelisted = true;
                    break;
                }
            }
        }
        if (!isWhitelisted && !buffer.empty()) {
            std::string garbage;
            for (size_t i = 0; i < buffer.size(); ++i) garbage += (char)('a' + GetRandomInt(0, 25));
            return garbage;
        }
    }
    return buffer;
}

void PowerShellWrapper::EnableANSIColors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

// for this function we can add later a counter that display the time after inactivity when user activate the session
void PowerShellWrapper::OnInactivityTimeout() {
    int inactivityMinutes = m_inactivityMonitor.GetInactivityDuration() / 60;
    if (m_authManager.IsAuthenticated()) {
        std::cout << "\n\n" << ANSI_RED << "═══════════════════════════════════════════════════════" << ANSI_RESET << "\n";
        std::cout << ANSI_RED << "[SECURITY] " << ANSI_WHITE << "Session expired due to inactivity (" << inactivityMinutes << " minutes)" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "💡 Use 'openme' to authenticate again." << ANSI_RESET << "\n";
        std::cout << ANSI_RED << "═══════════════════════════════════════════════════════" << ANSI_RESET << "\n\n";
        std::cout.flush();
        m_authManager.Deauthenticate();
        bool savedObfState = true;
        if (m_authManager.LoadObfuscationState(savedObfState)) m_obfuscator.SetEnabled(savedObfState);
        else m_obfuscator.SetEnabled(true);
        m_monitoringEnabled = true;
        m_logger.Log(LogLevel::WARNING, "Session auto-closed after " + std::to_string(inactivityMinutes) + " minutes of inactivity", m_currentUser);
        
        // Force command state to finished and wake up input thread if it was waiting
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_commandFinished = true;
        }
        m_syncCv.notify_one();
        std::cout.flush();
    } else {
        // If not authenticated, just reset timer to avoid tight-loop callbacks
        m_inactivityMonitor.ResetTimer();
    }
}

// prevent creds to be logged while allowing safe commands like 'pwd'
std::string PowerShellWrapper::SanitizeForLog(const std::string& input) {
    if (input.empty()) return "";
    
    std::string lowercaseLine = input;
    std::transform(lowercaseLine.begin(), lowercaseLine.end(), lowercaseLine.begin(), ::tolower);
    
    // Explicitly allow standalone 'pwd' command which is safe
    if (lowercaseLine == "pwd" || lowercaseLine.find("pwd ") == 0) {
        return input;
    }

    std::string sanitized = input;
    std::vector<std::string> sensitiveKeywords = {"password", "token", "secret", "key", "auth"};
    
    for (const auto& keyword : sensitiveKeywords) {
        size_t pos = lowercaseLine.find(keyword);
        if (pos != std::string::npos) {
            // Check if it's likely an assignment or parameter: keyword=, keyword:, or -keyword
            bool isAssignment = false;
            if (pos + keyword.length() < lowercaseLine.length()) {
                char next = lowercaseLine[pos + keyword.length()];
                if (next == '=' || next == ':' || next == ' ') isAssignment = true;
            }
            
            if (isAssignment) {
                sanitized = sanitized.substr(0, pos + keyword.length()) + " [REDACTED]";
                break; 
            }
        }
    }
    
    if (sanitized.length() > 200) sanitized = sanitized.substr(0, 200) + "... [TRUNCATED]";
    return sanitized;
}

bool PowerShellWrapper::LaunchPowerShell() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hChildStdoutWrite = nullptr;
    HANDLE hChildStdinRead = nullptr;
    if (!CreatePipe(&m_hChildStdoutRead, &hChildStdoutWrite, &sa, 0)) return false;
    if (!SetHandleInformation(m_hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(m_hChildStdoutRead); CloseHandle(hChildStdoutWrite); return false;
    }
    if (!CreatePipe(&hChildStdinRead, &m_hChildStdinWrite, &sa, 0)) {
        CloseHandle(m_hChildStdoutRead); CloseHandle(hChildStdoutWrite); return false;
    }
    if (!SetHandleInformation(m_hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(m_hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        CloseHandle(hChildStdinRead); CloseHandle(m_hChildStdinWrite); return false;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdOutput = hChildStdoutWrite; // Redirect stdout to pipe
    si.hStdError = hChildStdoutWrite; // Redirect stderr to same pipe
    si.hStdInput = hChildStdinRead;  // Redirect stdin from pipe
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; // Use these handles
    si.wShowWindow = SW_HIDE; // hide powershell window
    // SECURITY FIX: Removed -ExecutionPolicy Bypass to enforce system PowerShell security policies
    std::wstring cmdLine = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoLogo -NoProfile";
    PROCESS_INFORMATION pi{};
    
    std::wstring wCwd;
    if (!m_trackedCwd.empty()) {
        int size = MultiByteToWideChar(CP_UTF8, 0, m_trackedCwd.c_str(), -1, NULL, 0);
        if (size > 0) {
            wCwd.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, m_trackedCwd.c_str(), -1, &wCwd[0], size);
        }
    }

    if (!CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, wCwd.empty() ? nullptr : wCwd.c_str(), &si, &pi)) {
        CloseHandle(m_hChildStdoutRead); CloseHandle(m_hChildStdinWrite); return false;
    }
    SetConsoleTitleW(L"PS~WALLS");
    m_hChildProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    m_childRunning = true;

    // Force initialization commands through the normal sync channel
    // Use the sentinel to ensure we've fully processed the boot sequence
    std::string bootCommands = 
        "function global:prompt { return '' }; "
        "try { $PSStyle.OutputRendering = [System.Management.Automation.OutputRendering]::PlainText } catch { }; "
        "Remove-Module PSReadline -ErrorAction SilentlyContinue; " // avoids interference 
        "Write-Host '" + m_sentinel + "' $PWD.Path";
    
    SendToChild(bootCommands);
    return true;
}

void PowerShellWrapper::UserInputThread() {
    while (!m_psReady && !m_shouldExit) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (!m_shouldExit && m_childRunning) {
        std::string pendingSuggestion;
        {
            std::unique_lock<std::mutex> lock(m_syncMutex);
            m_syncCv.wait(lock, [this] { return m_commandFinished.load() || m_shouldExit; });
            if (m_shouldExit) break;
            pendingSuggestion = m_currentSuggestion;
            m_currentSuggestion.clear();
        }
        if (!pendingSuggestion.empty()) {
            std::cout << ANSI_GREEN << pendingSuggestion << ANSI_RESET << "\n";
        }
        std::cout << ANSI_BLUE << GetCurrentPrompt() << ANSI_RESET;
        std::cout.flush();
        std::string input = ReadInputWithHistory();
        m_inactivityMonitor.ResetTimer();
        input = Trim(input);
        if (input.empty()) continue;
        AddToHistory(input);
        bool suspicious = false;
        Severity threatSev = Severity::NONE;
        std::string threatReason;
        try {
            auto result = m_threatDetector.AnalyzeCommand(input);
            threatSev = result.first;
            threatReason = result.second;
            if (threatSev >= Severity::MEDIUM) suspicious = true;
        } catch (...) {
            suspicious = true;
            threatSev = Severity::HIGH;
            threatReason = "Detector Error";
        }

        if (ProcessWrapperCommands(input)) continue;

        // Restricted Mode: Block system commands if not authenticated
        if (!m_authManager.IsAuthenticated()) {
            // Implement exponential backoff for auth failures
            if (m_failedAuthAttempts >= 3) {
                auto now = std::chrono::steady_clock::now();
                if (now < m_authLockoutEnd) {
                    auto remainingSecs = std::chrono::duration_cast<std::chrono::seconds>(m_authLockoutEnd - now).count();
                    std::cout << ANSI_RED << "[-] Too many failed attempts. Locked for " << remainingSecs << " seconds." << ANSI_RESET << std::endl;
                    continue;
                } else {
                    // Reset lockout only after timeout expires
                    m_failedAuthAttempts = 0;
                }
            }
            
            std::cout << ANSI_RED << "[-] CRITICAL: Authentication required to execute system commands." << ANSI_RESET << std::endl;
            std::cout << ANSI_YELLOW << "💡 Use 'openme <password>' to unlock session." << ANSI_RESET << std::endl;

            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            continue;
        }

        if (suspicious) {
            m_logger.LogCommand(SanitizeForLog(input), m_currentUser, "", true);
            
            if (threatSev >= Severity::HIGH) {
                ShowMockingBanner();
                std::cout << ANSI_RED << "[CRITICAL] Security Threat: " << threatReason << ANSI_RESET << std::endl;
                std::cout << ANSI_RED << "[BLOCKED] This command is restricted and cannot be executed." << ANSI_RESET << std::endl;
                { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                m_syncCv.notify_one();
                continue;
            }

            if (m_monitoringEnabled) {
                std::cout << ANSI_RED << "[ALERT] Suspicious pattern: " << threatReason << ANSI_RESET << std::endl;
                std::cout << ANSI_YELLOW << "Continue anyway? (yes/no): " << ANSI_RESET;
                std::string confirm = Trim(ReadInputWithHistory());
                if (confirm != "yes") {
                    std::cout << ANSI_GREEN << "[CLEARED] Command execution cancelled." << ANSI_RESET << std::endl;
                    { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                    m_syncCv.notify_one();
                    continue;
                }
                std::cout << ANSI_YELLOW << "[!] Proceeding with caution..." << ANSI_RESET << std::endl;
            }
        } else m_logger.LogCommand(SanitizeForLog(input), m_currentUser, "", false);
        
        if (m_threatDetector.IsPowerShellLaunchAttempt(input)) {
            std::cout << ANSI_YELLOW << "[SECURITY] PowerShell launch detected. Starting new secured session..." << ANSI_RESET << std::endl;
            wchar_t exePath[MAX_PATH] = {0};
            if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
                STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOW;
                PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
                if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                    std::cout << ANSI_GREEN << "[+] New secured session started" << ANSI_RESET << std::endl;
                    m_logger.Log(LogLevel::INFO, "New wrapper instance launched via redirection", m_currentUser);
                }
            }
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            continue;
        }
        m_isBinarySuppressed = false;
        m_commandFinished = false;
        
        // If Ctrl+C was just pressed, keep suppressing output during the full recovery period
        auto timeSinceCtrlC = std::chrono::steady_clock::now() - m_lastCtrlCTime;
        if (timeSinceCtrlC < std::chrono::milliseconds(3500)) {
            // Wait until the drain operation is fully complete (3s drain + 500ms extra + time already elapsed)
            auto remainingTime = std::chrono::milliseconds(3500) - timeSinceCtrlC;
            if (remainingTime.count() > 0) {
                std::this_thread::sleep_for(remainingTime);
            }
        }
        
        // Clear any remaining accumulated output before releasing output suppression
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_outputAccumulator.clear();
            m_isBinarySuppressed = false;  // Reset binary suppression flag
        }
        
        // ONLY now re-enable output - after we're completely sure the pipe is drained and cleared
        m_suppressOutput = false;
        
        // Command injection prevention: Sanitize input by escaping single quotes
        // This ensures user input cannot break out of the PowerShell command context
        std::string sanitized = input;
        // Simple escaping: replace single quotes with two single quotes (PowerShell escape)
        size_t pos = 0;
        while ((pos = sanitized.find("'", pos)) != std::string::npos) {
            sanitized.replace(pos, 1, "''");
            pos += 2;
        }
        std::string cmdWithSentinel = sanitized + "; Write-Host '" + m_sentinel + "' $PWD.Path";
        SendToChild(cmdWithSentinel);
    }
}

void PowerShellWrapper::ProcessMonitorThread() {
    WaitForSingleObject(m_hChildProcess, INFINITE);
    m_childRunning = false; m_shouldExit = true;
}

void PowerShellWrapper::SendToChild(const std::string& s) {
    m_lastCommandSent = s;
    std::string toSend = s + "\r\n";
    DWORD written = 0;
    WriteFile(m_hChildStdinWrite, toSend.c_str(), static_cast<DWORD>(toSend.size()), &written, nullptr);
    FlushFileBuffers(m_hChildStdinWrite);
}

void PowerShellWrapper::PowerShellOutputThread() {
    std::vector<char> buffer(65536); DWORD bytesRead = 0;
    while (!m_shouldExit && m_childRunning) {
        DWORD available = 0;
        if (!PeekNamedPipe(m_hChildStdoutRead, NULL, 0, NULL, &available, NULL) || available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!ReadFile(m_hChildStdoutRead, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytesRead, nullptr) || bytesRead == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        buffer[bytesRead] = '\0';
        m_outputAccumulator += std::string(buffer.data(), bytesRead);

        // If Ctrl+C interrupted a command, skip ALL processing
        if (m_suppressOutput.load()) {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_outputAccumulator.clear();
            continue;
        }

        // 1. Handle Command Echo Removal (only if output is not suppressed)
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            if (!m_lastCommandSent.empty()) {
                size_t pos = m_outputAccumulator.find(m_lastCommandSent);
                if (pos != std::string::npos) {
                    m_outputAccumulator.erase(0, pos + m_lastCommandSent.length());
                    if (m_outputAccumulator.size() >= 2 && m_outputAccumulator.substr(0, 2) == "\r\n") m_outputAccumulator.erase(0, 2);
                    else if (m_outputAccumulator.size() >= 1 && m_outputAccumulator[0] == '\n') m_outputAccumulator.erase(0, 1);
                    m_lastCommandSent.clear();
                }
            }
        }

        // 2. Handle Sentinel and CWD Tracking (skip if output is suppressed)
        if (m_suppressOutput.load()) {
            continue;  // Don't process any sentinel data when suppressing
        }
        
        size_t sentinelPos = m_outputAccumulator.find(m_sentinel);
        if (sentinelPos != std::string::npos) {
            // Print and clear everything BEFORE the sentinel immediately to avoid duplication
            std::string beforeSentinel = m_outputAccumulator.substr(0, sentinelPos);
            if (!beforeSentinel.empty()) {
                // Filter out error traces that leak internal wrapper code
                std::istringstream bss(beforeSentinel);
                std::string bLine;
                std::string cleanBefore;
                while (std::getline(bss, bLine)) {
                    std::string tBLine = Trim(bLine);
                    // Skip error trace lines showing our internal wrapper
                    if (tBLine.find("+") == 0 && (tBLine.find(m_sentinel) != std::string::npos || tBLine.find("& {") != std::string::npos)) continue;
                    if (tBLine.find("Write-Host '") != std::string::npos && tBLine.find(m_sentinel) != std::string::npos) continue;
                    // Skip leaked native PowerShell prompts
                    if (tBLine == "PS>" || tBLine == "PS" || (tBLine.find("PS ") == 0 && tBLine.back() == '>')) continue;
                    cleanBefore += bLine + "\n";
                }
                if (!cleanBefore.empty()) {
                    std::string sanitized = SanitizeOutput(cleanBefore);
                    std::string suggestion = m_niceError.GetSuggestion(sanitized);
                    if (m_obfuscator.IsEnabled()) sanitized = m_obfuscator.ObfuscateOutput(sanitized);
                    std::cout << sanitized; std::cout.flush();
                    if (!suggestion.empty()) {
                        std::lock_guard<std::mutex> lock(m_syncMutex);
                        m_currentSuggestion = suggestion;
                    }
                }
                m_outputAccumulator.erase(0, sentinelPos);
                sentinelPos = 0; // The sentinel is now at the very beginning
            }

            // Look for the end of the completion sequence (Sentinel + Path + Newline)
            size_t pathEnd = m_outputAccumulator.find('\n', m_sentinel.length());
            if (pathEnd != std::string::npos) {
                std::string pathLine = m_outputAccumulator.substr(m_sentinel.length(), pathEnd - m_sentinel.length());
                std::string rawPath = Trim(pathLine);
                
                size_t drivePos = rawPath.find(":");
                if (drivePos != std::string::npos && drivePos > 0) {
                    m_trackedCwd = rawPath.substr(drivePos - 1);
                    size_t cleanEnd = m_trackedCwd.find_first_of("\r\n>");
                    if (cleanEnd != std::string::npos) m_trackedCwd = m_trackedCwd.substr(0, cleanEnd);
                    m_trackedCwd = Trim(m_trackedCwd);
                    m_authManager.SaveLastDirectory(m_trackedCwd);
                }

                // Sequence complete: remove sentinel line from accumulator
                m_outputAccumulator.erase(0, pathEnd + 1);
                if (!m_bootComplete) m_bootComplete = true;

                // Post-sentinel flush: drain trailing error output before showing prompt
                // Error traces can be slow, so we wait longer if we detect an error
                bool hasErrorStr = (beforeSentinel.find(" : The term '") != std::string::npos || beforeSentinel.find("Exception") != std::string::npos || beforeSentinel.find("Error") != std::string::npos);
                int maxWait = hasErrorStr ? 500 : 50;
                {
                    auto flushStart = std::chrono::steady_clock::now();
                    while (std::chrono::steady_clock::now() - flushStart < std::chrono::milliseconds(maxWait)) {
                        DWORD avail = 0;
                        if (PeekNamedPipe(m_hChildStdoutRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                            std::vector<char> flushBuf(avail + 1);
                            DWORD flushRead = 0;
                            if (ReadFile(m_hChildStdoutRead, flushBuf.data(), avail, &flushRead, nullptr) && flushRead > 0) {
                                m_outputAccumulator += std::string(flushBuf.data(), flushRead);
                                flushStart = std::chrono::steady_clock::now(); // Reset timer if data keeps arriving
                            }
                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        }
                    }
                    // Print any trailing output (filtered)
                    if (!m_outputAccumulator.empty()) {
                        std::istringstream tss(m_outputAccumulator);
                        std::string tLine;
                        std::string trailingClean;
                        while (std::getline(tss, tLine)) {
                            std::string trimmed = Trim(tLine);
                            // Skip lines that leak internal wrapper code
                            if (trimmed.find("+") == 0 && (trimmed.find(m_sentinel) != std::string::npos || trimmed.find("& {") != std::string::npos)) continue;
                            if (trimmed.find("Write-Host '") != std::string::npos && trimmed.find(m_sentinel) != std::string::npos) continue;
                            // Skip leaked native PowerShell prompts
                            if (trimmed == "PS>" || trimmed == "PS" || (trimmed.find("PS ") == 0 && trimmed.back() == '>')) continue;
                            trailingClean += tLine + "\n";
                        }
                        if (!trailingClean.empty()) {
                            std::string sanitized = SanitizeOutput(trailingClean);
                            std::string suggestion = m_niceError.GetSuggestion(sanitized);
                            if (m_obfuscator.IsEnabled()) sanitized = m_obfuscator.ObfuscateOutput(sanitized);
                            std::cout << sanitized; std::cout.flush();
                            if (!suggestion.empty()) {
                                std::lock_guard<std::mutex> lock(m_syncMutex);
                                m_currentSuggestion = suggestion;
                            }
                        }
                        m_outputAccumulator.clear();
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_syncMutex);
                    m_commandFinished = true;
                }
                m_syncCv.notify_one();
            }
            continue; 
        } else {
            // Backup Sync: Check for AMSI or Parser blocks that prevent the sentinel from reaching us
            if (m_outputAccumulator.find("ScriptContainedMaliciousContent") != std::string::npos ||
                m_outputAccumulator.find("ParserError") != std::string::npos ||
                m_outputAccumulator.find("blocked by your antivirus software") != std::string::npos) {
                
                if (!m_bootComplete) m_bootComplete = true; // Safety for failed boots
                
                {
                    std::lock_guard<std::mutex> lock(m_syncMutex);
                    if (!m_commandFinished) {
                        m_commandFinished = true;
                        m_syncCv.notify_one();
                    }
                }
            }
        }

        // 3. Process and Print Output
        if (!m_bootComplete) {
            continue;
        }

        // If Ctrl+C interrupted a command, AGGRESSIVELY discard ALL pipe data
        // Don't process anything when output suppression is active
        if (m_suppressOutput.load()) {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_outputAccumulator.clear(); // Clear what we already read
            // Note: The read loop above continues checking PeekNamedPipe, which drains the kernel pipe buffer
            continue;
        }

        std::string processed;
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            processed = m_outputAccumulator;
            m_outputAccumulator.clear(); 
        }

        // --- ROBUST ECHO & PROMPT FILTERING ---
        // Strip lines that are just PowerShell echoing back the command or showing a raw prompt.
        std::istringstream oss(processed);
        std::string line;
        std::string cleanOutput;
        while (std::getline(oss, line)) {
            std::string tLine = Trim(line);
            
            // Skip actual prompts (PS>) or echoed commands (PS> cmd)
            if (tLine.find("PS>") == 0 || tLine.find("PS ") == 0) {
                 // Check if it's echoing the last command or sentinel
                 if (tLine.find(m_sentinel) != std::string::npos) continue;
                 
                 // If the line IS just the prompt, skip it
                 if (tLine == "PS>" || tLine == "PS>>") continue;
                 
                 // If it's an echo of the command we just sent (wrapped in & { ... })
                 if (tLine.find("& {") != std::string::npos) continue;
            }
            
            // Skip error trace lines that leak internal wrapper code
            if (tLine.find("+") == 0 && (tLine.find(m_sentinel) != std::string::npos || tLine.find("& {") != std::string::npos)) continue;
            
            // Skip the Write-Host line itself if it leaked without a prompt prefix
            if (tLine.find("Write-Host '") != std::string::npos && tLine.find(m_sentinel) != std::string::npos) continue;

            cleanOutput += line + "\n";
        }
        
        if (cleanOutput.empty()) continue;

        // Cleanup any remaining inline artifacts
        std::vector<std::string> artifacts = {"PS>", "PS>>", "PS~walls >", "; Write-Host"};
        for (const auto& art : artifacts) {
            size_t pos;
            while ((pos = cleanOutput.find(art)) != std::string::npos) {
                cleanOutput.erase(pos, art.length());
            }
        }

        std::string sanitized = SanitizeOutput(cleanOutput);
        std::string suggestion = m_niceError.GetSuggestion(sanitized);
        if (m_obfuscator.IsEnabled()) sanitized = m_obfuscator.ObfuscateOutput(sanitized);
        
        std::cout << sanitized; std::cout.flush();
        if (!suggestion.empty()) {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_currentSuggestion = suggestion;
        }
    }
}

void PowerShellWrapper::SecureClearString(std::string& str) {
    if (!str.empty()) { for (char& c : str) c = (char)GetRandomInt(0, 255); std::fill(str.begin(), str.end(), '\0'); str.clear(); str.shrink_to_fit(); }
}

int PowerShellWrapper::GetExponentialBackoffSeconds() const {
    // SECURITY: Exponential backoff to prevent brute force attacks
    // Allow first 5 attempts without lockout, then progressive backoff
    // Attempts:     1-5    6      7      8       9+
    // Lockout(s):    0     15     30     60      120     300 (max 5min)
    if (m_failedAuthAttempts <= 5) return 0;   // First 5 attempts free
    if (m_failedAuthAttempts == 6) return 15;  // 15 seconds
    if (m_failedAuthAttempts == 7) return 30;  // 30 seconds
    if (m_failedAuthAttempts == 8) return 60;  // 1 minute
    if (m_failedAuthAttempts == 9) return 120; // 2 minutes
    return 300;  // Cap at 5 minutes
}

bool PowerShellWrapper::ProcessWrapperCommands(const std::string& input) {
    std::string trimmedInput = Trim(input);
    if (trimmedInput.empty()) return false;

    // Block hidden sentinel injection
    // m_sentinel is a long, secret, random string (generated when the program starts)
    if (trimmedInput.find(m_sentinel) != std::string::npos) {
        std::cout << ANSI_RED << "[!] CRITICAL Error: Illegal character sequence detected in input." << ANSI_RESET << std::endl;
        m_logger.Log(LogLevel::WARNING, "Sentinel injection attempt blocked", m_currentUser);
        return true; 
    } 

    // Identify the core command before any delimiters or spaces
    std::string cmd;
    size_t firstSep = trimmedInput.find_first_of(" ;|&");
    if (firstSep != std::string::npos) {
        cmd = trimmedInput.substr(0, firstSep);
    } else {
        cmd = trimmedInput;
    }

    // Use a stringstream for the remaining arguments if needed
    std::string remainder;
    if (firstSep != std::string::npos) remainder = Trim(trimmedInput.substr(firstSep));
    std::istringstream iss(remainder);

    
    // Never log openme/setkey — they carry passwords
    static const std::vector<std::string> kInternalCmds = {
        "help","enable","disable","status","exportlog",
        "clearlog","history","timer","rotate","walls",
        "exit","quit"
    };
    bool isInternal = std::find(kInternalCmds.begin(), kInternalCmds.end(), cmd) != kInternalCmds.end();
    if (isInternal) {
        m_logger.LogCommand("[CMD] " + SanitizeForLog(trimmedInput), m_currentUser, "", false);
    }

    if (cmd == "help") { 
        ShowHelp(); 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    
    // Intercept Get-Command for internal commands
    std::string lowerCmd = cmd;
    for (char& c : lowerCmd) c = std::tolower(c);
    if (lowerCmd == "gcm" || lowerCmd == "get-command") {
        std::string target;
        if (iss >> target) {
            std::string lowerTarget = target;
            for (char& c : lowerTarget) c = std::tolower(c);
            std::vector<std::string> internalCmds = { "setkey", "openme", "help", "exit", "quit", "status", "enable", "disable", "timer", "history", "clearlog", "exportlog", "rotate", "walls" };
            if (std::find(internalCmds.begin(), internalCmds.end(), lowerTarget) != internalCmds.end()) {
                std::cout << "\n    " << ANSI_CYAN << "Owned by " << ANSI_GREEN << "ps~walls" << ANSI_RESET << "\n";
                std::cout << "    " << ANSI_CYAN << "Available only in this project 😎😎" << ANSI_RESET << "\n\n";
                { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                m_syncCv.notify_one();
                return true;
            }
        }
    }

    if (cmd == "setkey") {
        std::string extra;
        if (iss >> extra) {
            std::cout << ANSI_RED << "[-] Error: 'setkey' does not accept arguments on the same line." << ANSI_RESET << "\n";
            std::cout << ANSI_CYAN << "[*] Just type 'setkey' and press enter to be prompted securely." << ANSI_RESET << "\n" << std::endl;
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            return true;
        }

        if (m_masterKeySet) {
            std::cout << "Enter OLD Master Key to verify: ";
            SecureVector<char> oldPwd(Trim(ReadPassword()));  // SECURITY: Use SecureVector
            if (oldPwd.empty() || !m_authManager.Authenticate(oldPwd.to_string(), -1)) {
                std::cout << ANSI_RED << "[-] Identification failed. Access denied." << ANSI_RESET << std::endl;
                { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                m_syncCv.notify_one();
                return true;
            }
            m_authManager.Deauthenticate(); // Reset auth state after verification
        }
        std::cout << "Enter NEW Main Master Key: "; 
        SecureVector<char> mainKey(Trim(ReadPassword()));  // SECURITY: Use SecureVector
        if (mainKey.empty()) {
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            return true;
        }
        std::cout << "Enter Backup Key 1 (Optional): "; 
        SecureVector<char> bak1(Trim(ReadPassword()));  // SECURITY: Use SecureVector
        std::cout << "Enter Backup Key 2 (Optional): "; 
        SecureVector<char> bak2(Trim(ReadPassword()));  // SECURITY: Use SecureVector
        
        if (m_authManager.SetKey(mainKey.to_string(), bak1.to_string(), bak2.to_string())) { 
            m_masterKeySet = true; 
            std::cout << ANSI_GREEN << "[+] Keys updated successfully" << ANSI_RESET << std::endl; 
        }
        // SecureVector destructor automatically zeros memory
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true;
    }
    if (cmd == "openme") {
        auto now = std::chrono::steady_clock::now();
        if (now < m_authLockoutEnd) { std::cout << ANSI_RED << "[-] Locked out." << ANSI_RESET << std::endl; return true; }
        
        SecureVector<char> pwd;  // SECURITY: Use SecureVector for automatic memory zeroing
        std::string pwdStr;
        if (!(iss >> pwdStr)) {
            std::cout << "Enter Master Key: "; 
            pwdStr = Trim(ReadPassword());
        }
        pwd = SecureVector<char>(pwdStr);  // Copy into secure vector
        SecureClearString(pwdStr);  // Clear the temporary string
        
        if (!pwd.empty()) {
            std::string pwdForAuth(pwd.data_ptr(), pwd.size());  // Temporary for authentication
            if (m_authManager.Authenticate(pwdForAuth, -1)) {
                std::cout << ANSI_GREEN << "[+] Authenticated" << ANSI_RESET << std::endl;
                m_obfuscator.SetEnabled(false);
                m_failedAuthAttempts = 0;
                SecureClearString(pwdForAuth);  // Clear before unlocking
                // Send a sentinel command to ensure the output thread syncs up and updates CWD
                std::string syncCmd = "Write-Host '" + m_sentinel + "' $PWD.Path";
                SendToChild(syncCmd);
                // Do NOT set m_commandFinished = true here, as the sentinel will do it.
            } else {
                SecureClearString(pwdForAuth);  // Clear on failure too
                m_failedAuthAttempts++;
                // SECURITY: 5 free attempts, then exponential backoff starting at 15 seconds
                int lockoutSeconds = GetExponentialBackoffSeconds();
                if (lockoutSeconds > 0) {
                    m_authLockoutEnd = std::chrono::steady_clock::now() + std::chrono::seconds(lockoutSeconds);
                    std::cout << ANSI_RED << "[-] Authentication failed. Locked for " << lockoutSeconds << " seconds." << ANSI_RESET << std::endl;
                } else {
                    std::cout << ANSI_RED << "[-] Authentication failed. Attempt " << m_failedAuthAttempts << "/5" << ANSI_RESET << std::endl;
                }
                { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                m_syncCv.notify_one();
            }
            // SecureVector<char> pwd will auto-destruct and zero memory
        } else {
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
        }
        return true;
    }
    if (cmd == "enable") { 
        m_obfuscator.SetEnabled(true); 
        m_authManager.SaveObfuscationState(true); 
        m_authManager.Deauthenticate(); // Lock session when enabling obfuscation
        m_monitoringEnabled = true;
        std::cout << "\n" << ANSI_YELLOW << "╔══════════════════════════════════════════════════════╗" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "║ " << ANSI_GREEN << "[+] Obfuscation ENABLED" << ANSI_YELLOW << "                              ║" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "║ " << ANSI_RED << "[!] Session LOCKED" << ANSI_YELLOW << "                                   ║" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "╚══════════════════════════════════════════════════════╝" << ANSI_RESET << "\n" << std::endl; 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "disable") { 
        if (!m_authManager.IsAuthenticated()) {
            std::cout << ANSI_YELLOW << "[!] Authentication required to disable obfuscation" << ANSI_RESET << "\n";
            std::string pwd;
            if (!(iss >> pwd)) {
                std::cout << "Enter Master Key: "; 
                pwd = Trim(ReadPassword());
            }
            if (pwd.empty() || !m_authManager.Authenticate(pwd, -1)) {
                std::cout << ANSI_RED << "[-] Authentication failed" << ANSI_RESET << std::endl;
                SecureClearString(pwd);
                { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
                m_syncCv.notify_one();
                return true;
            }
            SecureClearString(pwd);
        }
        m_obfuscator.SetEnabled(false); 
        m_authManager.SaveObfuscationState(false); 
        m_monitoringEnabled = true; 
        
        // Still monitor even if obfuscation is off? User said disable obfuscation but session still locked?
        // Wait, the user said "disable command disable obfuscation but the session still locked".
        // Actually, disable just disables obfuscation. If they authenticated, the session is unlocked.
        std::cout << "\n" << ANSI_YELLOW << "╔══════════════════════════════════════════════════════╗" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "║ " << ANSI_GREEN << "[+] Obfuscation DISABLED" << ANSI_YELLOW << "                             ║" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "║ " << ANSI_CYAN << "[*] System Monitoring still ACTIVE" << ANSI_YELLOW << "                         ║" << ANSI_RESET << "\n";
        std::cout << ANSI_YELLOW << "╚══════════════════════════════════════════════════════╝" << ANSI_RESET << "\n" << std::endl; 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "status") { 
        ShowStatus(); 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "exportlog") { 
        if (!m_authManager.IsAuthenticated()) {
            std::cout << ANSI_RED << "[-] Authentication required to export logs." << ANSI_RESET << std::endl;
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            return true;
        }
        std::string opts = remainder;
        if (opts == "--help" || opts == "-h") {
            std::cout << "\n" << ANSI_CYAN << "    --- EXPORTLOG HELP ---" << ANSI_RESET << "\n";
            std::cout << "    Usage: exportlog [options]\n";
            std::cout << "    Options:\n";
            std::cout << "      json            - Export in JSON format (default)\n";
            std::cout << "      txt             - Export in plain text format\n";
            std::cout << "      full            - Include complete untruncated command details (by default shows truncated at 120 chars)\n";
            std::cout << "      verbose         - Alias for 'full' - shows both truncated display and full command\n";
            std::cout << "      flagged         - Include only flagged/malicious entries\n";
            std::cout << "      --from <days>   - Export logs from the last X days\n";
            std::cout << "    Examples:\n";
            std::cout << "      exportlog txt --from 7 full       - Export 7 days of logs in TEXT with full commands\n";
            std::cout << "      exportlog json full flagged       - Export flagged entries with full details\n";
            std::cout << "      exportlog verbose                 - Export all with verbose details\n\n";
        } else {
            if (m_logger.ExportLog(opts)) {
                std::cout << ANSI_GREEN << "[+] Logs exported successfully." << ANSI_RESET << std::endl;
            }
        }
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "clearlog") { 
        if (m_authManager.IsAuthenticated()) { 
            std::string opts = remainder;
            if (opts == "--help" || opts == "-h") {
                std::cout << "\n" << ANSI_CYAN << "    --- CLEARLOG HELP ---" << ANSI_RESET << "\n";
                std::cout << "    Usage: clearlog [options]\n";
                std::cout << "    Options:\n";
                std::cout << "      --from <days>   - Clear logs older than X days\n";
                std::cout << "      level <SEVERITY>- Clear logs matching severity (INFO, LOW, MEDIUM, HIGH , CRITICAL)\n";
                std::cout << "    Example: clearlog --from 30 level INFO\n\n";
            } else {
                // Ask user for confirmation before clearing logs
                std::string displayOpts = opts.empty() ? "ALL logs" : "logs matching: " + opts;
                std::cout << ANSI_YELLOW << "[!] WARNING: You are about to clear " << displayOpts << ANSI_RESET << "\n";
                std::cout << ANSI_YELLOW << "[!] This action cannot be undone. Are you sure? (yes/no): " << ANSI_RESET;
                std::string confirmation = Trim(ReadInputWithHistory());
                if (confirmation == "yes") {
                    m_logger.ClearLog(opts);
                    std::cout << ANSI_GREEN << "[+] Logs cleared successfully." << ANSI_RESET << std::endl;
                } else {
                    std::cout << ANSI_CYAN << "[*] Clear log operation cancelled." << ANSI_RESET << std::endl;
                }
            }
        } else {
            std::cout << ANSI_RED << "[-] Authentication required to clear logs." << ANSI_RESET << std::endl;
        }
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "history") { 
        if (!m_authManager.IsAuthenticated()) {
            std::cout << ANSI_RED << "[-] Authentication required to view history." << ANSI_RESET << std::endl;
            { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
            m_syncCv.notify_one();
            return true;
        }
        m_logger.RefreshLogs();
        ShowHistory(); 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "timer") { 
        if (m_authManager.IsAuthenticated()) { 
            int mn; 
            if (iss >> mn) {
                if (mn < 0 || mn > 10) std::cout << ANSI_RED << "[-] Error: Timer must be between 0 and 10 minutes." << ANSI_RESET << std::endl;
                else if (mn == 0) {
                    m_inactivityMonitor.SetInactivityDuration(0);
                    std::cout << ANSI_GREEN << "[+] Timer disabled (0 minutes)." << ANSI_RESET << std::endl;
                } else {
                    m_inactivityMonitor.SetInactivityDuration(mn * 60);
                    std::cout << ANSI_GREEN << "[+] Timer set to " << mn << " minutes." << ANSI_RESET << std::endl;
                }
            }
            else {
                m_inactivityMonitor.SetInactivityDuration(5 * 60);
                std::cout << ANSI_GREEN << "[+] Timer set to default (5 minutes)." << ANSI_RESET << std::endl;
            }
        } 
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "rotate") { 
        if (m_authManager.IsAuthenticated()) { 
            std::string rotationType;
            int days = -1;
            
            // Parse: rotate keys|logs <days>
            if (iss >> rotationType) {
                // First param must be keys or logs
                if (rotationType == "logs" || rotationType == "keys") {
                    // Check if second param is a number (days)
                    if (iss >> days) {
                        if (days < 1 || days > 365) {
                            std::cout << ANSI_RED << "[-] Error: Rotation period must be between 1 and 365 days." << ANSI_RESET << std::endl;
                        } else {
                            m_authManager.SetPasswordRotationDays(days);
                            std::cout << ANSI_GREEN << "[+] Password rotation set to " << days << " days." << ANSI_RESET << std::endl;
                            m_logger.RotateLogs(rotationType);
                            std::cout << ANSI_GREEN << "[+] " << rotationType << " rotated successfully." << ANSI_RESET << std::endl;
                        }
                    } else {
                        std::cout << ANSI_RED << "[-] Usage: rotate keys|logs <days>" << ANSI_RESET << std::endl;
                        std::cout << ANSI_YELLOW << "    Example: rotate logs 30" << ANSI_RESET << std::endl;
                    }
                } else {
                    std::cout << ANSI_RED << "[-] Invalid rotation type. Must be 'keys' or 'logs'." << ANSI_RESET << std::endl;
                    std::cout << ANSI_RED << "[-] Usage: rotate keys|logs <days>" << ANSI_RESET << std::endl;
                    std::cout << ANSI_YELLOW << "    Example: rotate logs 30" << ANSI_RESET << std::endl;
                }
            } else {
                std::cout << ANSI_RED << "[-] Usage: rotate keys|logs <days>" << ANSI_RESET << std::endl;
                std::cout << ANSI_YELLOW << "    Examples:\n";
                std::cout << "    - rotate logs 30    : Rotate logs every 30 days\n";
                std::cout << "    - rotate keys 90    : Rotate encryption keys every 90 days" << ANSI_RESET << std::endl;
            }
        } else {
            std::cout << ANSI_RED << "[-] Authentication required to rotate logs/keys." << ANSI_RESET << std::endl;
        }
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    if (cmd == "walls") {
        std::cout << ANSI_YELLOW << "[*] Initializing new secured session perimeters..." << ANSI_RESET << std::endl;
        wchar_t exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
            STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOW;
            PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
            if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                std::cout << ANSI_GREEN << "[+] New session spawned. Original session remains active." << ANSI_RESET << std::endl;
            }
        }
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true;
    }
    if (cmd == "exit" || cmd == "quit") { 
        m_shouldExit = true; 
        if (m_hChildProcess && m_childRunning) {
            TerminateProcess(m_hChildProcess, 0); 
        }
        { std::lock_guard<std::mutex> lock(m_syncMutex); m_commandFinished = true; }
        m_syncCv.notify_one();
        return true; 
    }
    return false;
}

void PowerShellWrapper::ShowStatus() {
    std::cout << "\n" << ANSI_CYAN << "    ======================================\n";
    std::cout << "          " << ANSI_WHITE << "SYSTEM STATUS" << ANSI_CYAN << "           \n";
    std::cout << "    ======================================" << ANSI_RESET << "\n";
    
    std::cout << ANSI_YELLOW << "    PowerShell:      " << ANSI_RESET 
                << (m_childRunning ? ANSI_GREEN "[ACTIVE]" : ANSI_RED "[STOPPED]") << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Monitoring:      " << ANSI_RESET 
                << (m_monitoringEnabled ? ANSI_GREEN "[ENABLED]" : ANSI_YELLOW "[DISABLED]") << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Obfuscation:     " << ANSI_RESET 
                << (m_obfuscator.IsEnabled() ? ANSI_GREEN "[ENABLED]" : ANSI_YELLOW "[DISABLED]") << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Authenticated:   " << ANSI_RESET 
                << (m_authManager.IsAuthenticated() ? ANSI_GREEN "[YES]" : ANSI_RED "[NO]") << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Session ID:      " << ANSI_RESET 
                << ANSI_CYAN << m_logger.GetSessionId() << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Log Entries:     " << ANSI_RESET 
                << ANSI_WHITE << m_logger.GetLogCount() << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Flagged:         " << ANSI_RESET 
                << ANSI_RED << m_logger.CountFlaggedEntries() << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Operator:        " << ANSI_RESET 
                << ANSI_WHITE << m_currentUser << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Inactivity:      " << ANSI_RESET 
                << ANSI_CYAN << m_inactivityMonitor.GetSecondsUntilTrigger() << "s" << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Pwd Rotation:    " << ANSI_RESET 
                << ANSI_CYAN << m_authManager.GetPasswordRotationDays() << " days" << ANSI_RESET << "\n";
    std::cout << ANSI_YELLOW << "    Pwd Expires In:  " << ANSI_RESET 
                << ANSI_CYAN << m_authManager.GetDaysUntilExpiry() << " days" << ANSI_RESET << "\n";
    
    if (m_hChildProcess) {
        std::cout << ANSI_YELLOW << "    PID:             " << ANSI_RESET 
                    << ANSI_WHITE << GetProcessId(m_hChildProcess) << ANSI_RESET << "\n";
    }
    std::cout << std::endl;
}

void PowerShellWrapper::ShowHistory() {
    int count = 1; for (const auto& cmd : m_commandHistory) std::cout << count++ << ": " << SanitizeForLog(cmd) << "\n";
}

void PowerShellWrapper::AddToHistory(const std::string& cmd) {
    if (!cmd.empty()) { m_commandHistory.push_back(cmd); if (m_commandHistory.size() > m_maxHistorySize) m_commandHistory.pop_front(); }
}

std::string PowerShellWrapper::Trim(const std::string& s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b - a);
}

std::string PowerShellWrapper::GetCurrentPrompt() const {
    if (m_authManager.IsAuthenticated() && !m_trackedCwd.empty()) {
        return "PS~walls " + m_trackedCwd + "> ";
    }
    return "PS~walls> ";
}
