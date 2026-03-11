#pragma once

#include <string>
#include <array>
#include <random>
#include <mutex>
#include <memory>
#include <vector>

class Obfuscator {
public:
    Obfuscator();
    ~Obfuscator();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    std::string ObfuscateForDisplay(const std::string& text) const;
    bool IsExemptFromObfuscation(const std::string& text) const;
    void RegenerateMapping();
    std::string ObfuscateInput(const std::string& input) const;
    std::string ObfuscateOutput(const std::string& output) const;
    std::string ObfuscatePrompt(const std::string& prompt) const;
    void SecureClear();

private:
    bool m_enabled;
    mutable std::mutex m_mutex;
    std::array<char, 26> m_upperMapping;
    std::array<char, 26> m_lowerMapping;
    std::mt19937 m_rng;
    std::vector<std::string> m_exemptKeywords;
    std::vector<std::string> m_criticalErrorPatterns;

    void GenerateMappings();
    void GenerateMappingsInternal();
    bool ContainsCriticalError(const std::string& text) const;
    bool IsExemptCommand(const std::string& text) const;
    char ObfuscateChar(char c) const;
    std::string ToLower(const std::string& str) const;
    std::string Trim(const std::string& str) const;
    bool ContainsPattern(const std::string& text, const std::string& pattern) const;
};

class ObfuscatorManager {
public:
    static ObfuscatorManager& GetInstance();
    Obfuscator& GetObfuscator();
    ObfuscatorManager(const ObfuscatorManager&) = delete;
    ObfuscatorManager& operator=(const ObfuscatorManager&) = delete;

private:
    ObfuscatorManager();
    ~ObfuscatorManager();
    std::unique_ptr<Obfuscator> m_obfuscator;
    std::mutex m_mutex;
};
