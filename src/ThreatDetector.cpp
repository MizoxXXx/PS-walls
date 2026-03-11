#include "ThreatDetector.h"

ThreatDetector::ThreatDetector() {
    auto rx = [](const char* pat) { return std::regex(pat, std::regex::icase); };
    m_patterns = { 
        // CRITICAL - Immediate Blocking + Mocking
        { rx(R"(\bmimikatz\b)"), "Credential dumping (Mimikatz)", Severity::CRITICAL, "mimi" },
        { rx(R"(\bNet\.Sockets\.TCPClient\b|\bSystem\.Net\.Sockets\b)"), "Reverse shell / Network socket", Severity::CRITICAL, "net_socket" },
        { rx(R"(\bInvoke-Shellcode\b)"), "Shellcode injection", Severity::CRITICAL, "injection" },
        { rx(R"(\bGet-GPPPassword\b)"), "GPO password extraction", Severity::CRITICAL, "gpp" },
        { rx(R"(\bInvoke-Mimikatz\b)"), "In-memory Mimikatz", Severity::CRITICAL, "mimi_mem" },

        // HIGH - Immediate Blocking + Mocking
        { rx(R"(\b(IEX|Invoke-Expression)\b.*(-e|-enc|-encodedcommand)\b)"), "Encoded command execution", Severity::HIGH, "iex_encoded" },
        { rx(R"(\bNew-Object\s+IO\.StreamReader\b)"), "Persistent network stream", Severity::HIGH, "stream" },
        { rx(R"(\b(powershell|pwsh)(\.exe)?\s+(-e|-enc|-encodedcommand)\b)"), "Powershell encoded command", Severity::HIGH, "ps_encoded" },
        { rx(R"(\b(DownloadString|DownloadFile|DownloadData)\b)"), "Remote file download", Severity::HIGH, "download" },
        { rx(R"(\bInvoke-WebRequest\b|\biwr\b)"), "Web request (possible download)", Severity::HIGH, "iwr" },

        // MEDIUM - Interactive Alert
        { rx(R"(\b(IEX|Invoke-Expression)\b)"), "Dynamic code execution", Severity::MEDIUM, "iex" },
        { rx(R"(\b(EncodedCommand|enc)\b)"), "Base64 encoding flag", Severity::MEDIUM, "base64_flag" },
        { rx(R"(\b(Set-ExecutionPolicy|ep)\b)"), "Bypassing execution policy", Severity::MEDIUM, "policy_bypass" },
        { rx(R"(\bRegister-ScheduledTask\b)"), "Persistence via Task Scheduler", Severity::MEDIUM, "persistence" },
        { rx(R"(\bAdd-Type\b)"), "Compiling C# code in memory", Severity::MEDIUM, "add_type" },

        // LOW - Logging + Minimal Alert
        { rx(R"(\b(Whoami|hostname|ipconfig|whois)\b)"), "Information gathering", Severity::INFO, "recon" },
        { rx(R"(\b(ls\s+.*-r|dir\s+.*-s)\b)"), "Recursive file discovery", Severity::INFO, "discovery" },
        { rx(R"(\bSystem\.Convert\]::ToBase64String\b)"), "Base64 processing", Severity::INFO, "encoding" },
        { rx(R"([;&|]\s*\w+)"), "Command chaining / Sequential execution", Severity::LOW, "chaining" },
        
        // REVERSE SHELL & MALICIOUS COMBO PATTERNS
        { rx(R"(\bTCPClient\b|\bTcpListener\b)"), "Network socket / Reverse shell component", Severity::HIGH, "socket" },
        { rx(R"(\bIO\.Stream(Writer|Reader)\b)"), "In-memory stream processing", Severity::HIGH, "stream_io" },
        { rx(R"(\bSystem\.Text\.Encoding\b)"), "Raw text encoding (payload processing)", Severity::MEDIUM, "encoding_raw" },
        { rx(R"(\b(LHOST|LPORT)\b)"), "Shell variable pattern (Malware)", Severity::MEDIUM, "shell_var" }
    };
}

std::pair<Severity, std::string> ThreatDetector::AnalyzeCommand(const std::string& rawCmd) {
    std::string cmd = NormalizeForDetection(rawCmd);
    
    Severity maxSev = Severity::NONE;
    std::string reason = "";

    // 1. Pattern Matching (Check ALL to find highest severity)
    for (const auto& p : m_patterns) {
        if (std::regex_search(cmd, p.pattern)) {
            if (p.severity > maxSev) {
                maxSev = p.severity;
                reason = p.reason;
            }
        }
    }

    // 2. Heuristics
    if (HasDownloadAndExecute(cmd)) {
        if (Severity::HIGH > maxSev) {
            maxSev = Severity::HIGH;
            reason = "Download and execute pattern";
        }
    }
    
    if (LooksEncodedOrObfuscated(cmd)) {
        if (Severity::MEDIUM > maxSev) {
            maxSev = Severity::MEDIUM;
            reason = "High entropy or encoded payload";
        }
    }

    // New Heuristic: High entropy + Networking + Execution length
    if (cmd.length() > 500) {
        bool hasNet = (cmd.find("tcpclient") != std::string::npos || cmd.find("sockets") != std::string::npos);
        bool hasEx = (cmd.find("iex") != std::string::npos || cmd.find("invoke-expression") != std::string::npos);
        if (hasNet && hasEx) {
             maxSev = Severity::CRITICAL;
             reason = "Long complex malicious payload (Reverse Shell signature)";
        }
    }

    return { maxSev, reason };
}

bool ThreatDetector::IsPowerShellLaunchAttempt(const std::string& cmd) {
    std::string l = ToLower(cmd);
    return l.find("powershell") != std::string::npos || l.find("pwsh") != std::string::npos;
}

bool ThreatDetector::LooksEncodedOrObfuscated(const std::string& s) const {
    if (s.length() < 20) return false;
    
    // Check for large Base64-like blocks
    std::regex b64(R"([A-Za-z0-9+/]{40,})");
    if (std::regex_search(s, b64)) return true;

    // Check entropy
    if (ShannonEntropy(s) > 4.5) return true;

    return false;
}

bool ThreatDetector::HasDownloadAndExecute(const std::string& s) const {
    bool hasIEX = (s.find("iex") != std::string::npos || s.find("invoke-expression") != std::string::npos);
    bool hasDownload = (s.find("downloadstring") != std::string::npos || s.find("downloadfile") != std::string::npos);
    return hasIEX && hasDownload;
}

double ThreatDetector::ShannonEntropy(const std::string& s) const {
    if (s.empty()) return 0.0;
    std::array<int, 256> counts = {0};
    for (unsigned char c : s) counts[c]++;
    
    double entropy = 0.0;
    for (int count : counts) {
        if (count > 0) {
            double p = (double)count / s.length();
            entropy -= p * log2(p);
        }
    }
    return entropy;
}

std::string ThreatDetector::ToLower(const std::string& s) const {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}

std::string ThreatDetector::NormalizeForDetection(const std::string& cmd) const {
    // Basic normalization: remove backticks (PS obfuscation)
    std::string res = cmd;
    res.erase(std::remove(res.begin(), res.end(), '`'), res.end());
    return ToLower(res);
}
