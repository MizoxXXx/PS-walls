#pragma once

#include "Common.h"
#include <string>
#include <vector>
#include <regex>
#include <array>
#include <utility>
#include <algorithm>
#include <cmath>

enum class Severity {
    NONE = 0,
    INFO,       // Silent logging only — no alert, no prompt
    LOW,
    MEDIUM,
    HIGH,
    CRITICAL
};

struct ThreatPattern {
    std::regex pattern;
    std::string reason;
    Severity severity;
    std::string tag;
};

class ThreatDetector {
private:
    std::vector<ThreatPattern> m_patterns;

public:
    ThreatDetector();
    std::pair<Severity, std::string> AnalyzeCommand(const std::string& rawCommand);
    bool IsPowerShellLaunchAttempt(const std::string& command);

private:
    bool LooksEncodedOrObfuscated(const std::string& s) const;
    bool HasDownloadAndExecute(const std::string& s) const;
    double ShannonEntropy(const std::string& s) const;
    std::string ToLower(const std::string& s) const;
    std::string NormalizeForDetection(const std::string& cmd) const;
};
