#include "Obfuscator.h"
#include "Common.h"
#include <algorithm>
#include <chrono>

Obfuscator::Obfuscator() : m_enabled(false) {
    m_exemptKeywords = {"setkey", "openme"};
    m_criticalErrorPatterns = {"is not recognized", "access is denied"};
    GenerateMappings();
}

Obfuscator::~Obfuscator() { SecureClear(); }

void Obfuscator::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_enabled = enabled;
    if (enabled) GenerateMappingsInternal();
}

bool Obfuscator::IsEnabled() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_enabled;
}

void Obfuscator::GenerateMappings() {
    std::lock_guard<std::mutex> lk(m_mutex);
    GenerateMappingsInternal();
}

void Obfuscator::GenerateMappingsInternal() {
    std::vector<char> all;
    for (char c = 'A'; c <= 'Z'; c++) all.push_back(c);
    for (char c = 'a'; c <= 'z'; c++) all.push_back(c);
    ShuffleVector(all);
    for (int i = 0; i < 26; i++) { m_upperMapping[i] = all[i]; m_lowerMapping[i] = all[26+i]; }
}

std::string Obfuscator::ObfuscateOutput(const std::string& output) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_enabled) return output;
    std::string obf;
    for (char c : output) {
        if (c >= 'A' && c <= 'Z') obf += m_upperMapping[c - 'A'];
        else if (c >= 'a' && c <= 'z') obf += m_lowerMapping[c - 'a'];
        else if (c >= '0' && c <= '9') obf += '0';
        else obf += c;
    }
    return obf;
}

void Obfuscator::SecureClear() { m_enabled = false; }

ObfuscatorManager::ObfuscatorManager() { m_obfuscator = std::make_unique<Obfuscator>(); }
ObfuscatorManager::~ObfuscatorManager() {}
ObfuscatorManager& ObfuscatorManager::GetInstance() { static ObfuscatorManager instance; return instance; }
Obfuscator& ObfuscatorManager::GetObfuscator() { return *m_obfuscator; }
