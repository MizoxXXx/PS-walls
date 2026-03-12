#include "AuthenticationManager.h"
#include "DPAPI_Utils.h"
#include "Common.h"
#include "json.hpp"

#include <shlobj.h> 
#include <windows.h>
#include <filesystem>
#include <vector>
#include <iostream>
#include <ctime>

// Note: Library linking (shell32) is handled by CMakeLists.txt

AuthenticationManager::AuthenticationManager()
    : m_authenticated(false), m_passwordRotationDays(30) {
    LoadRotationPeriod();
}

AuthenticationManager::~AuthenticationManager() {}

std::string AuthenticationManager::GetStorageDirectory() const {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path);
    std::string result;
    if (SUCCEEDED(hr) && path) {
        std::wstring wpath(path);
        CoTaskMemFree(path);
        int size = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
        result.resize(size - 1);
        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], size, NULL, NULL);
        result += "\\PowerShellWrapper";
    } else {
        result = ".\\PowerShellWrapper";
    }
    return result;
}

std::string AuthenticationManager::GetKeyFilePath() const {
    std::string dir = GetStorageDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\.wrapper_key.dat";
}

std::string AuthenticationManager::GetStateFilePath() const { 
    std::string dir = GetStorageDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\.wrapper_state.dat";
}

std::string AuthenticationManager::GetRotationFilePath() const {
    std::string dir = GetStorageDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\.wrapper_rotation.dat";
}

std::string AuthenticationManager::GetDirStateFilePath() const {
    std::string dir = GetStorageDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\.wrapper_dir.dat";
}

bool AuthenticationManager::SetKey(const std::string& password, const std::string& backup1, const std::string& backup2) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json j;
    j["timestamp"] = std::time(nullptr);
    std::vector<std::string> keys = {password, backup1, backup2};
    j["keys"] = keys;
    std::string jsonStr = j.dump();
    auto encrypted = DPAPI_Protect(jsonStr, L"PSWrapperKey");
    if (encrypted.empty()) return false;
    return DPAPI_WriteBinaryFile(GetKeyFilePath(), encrypted);
}

bool AuthenticationManager::LoadKeys() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string keyPath = GetKeyFilePath();
    if (!std::filesystem::exists(keyPath)) return false;
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(keyPath, blob)) return false;
    return !DPAPI_Unprotect(blob).empty();
}

bool AuthenticationManager::Authenticate(const std::string& inputPassword, int keyIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (inputPassword.empty()) return false;
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetKeyFilePath(), blob)) return false;
    std::string finalJson = DPAPI_Unprotect(blob);
    if (finalJson.empty()) return false;
    try {
        auto j = nlohmann::json::parse(finalJson);
        const auto& keys = j["keys"];
        if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size())) {
            if (keys[keyIndex] == inputPassword) { m_authenticated = true; return true; }
            return false;
        }
        for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
            if (!keys[i].get<std::string>().empty() && keys[i] == inputPassword) {
                m_authenticated = true; m_lastUsedKeyIndex = i; return true;
            }
        }
    } catch (...) {}
    return false;
}

bool AuthenticationManager::IsPasswordExpired() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetKeyFilePath(), blob)) return true;
    try {
        auto j = nlohmann::json::parse(DPAPI_Unprotect(blob));
        if (j.contains("timestamp")) {
            return std::difftime(std::time(nullptr), j["timestamp"]) > (m_passwordRotationDays * 86400);
        }
    } catch (...) {}
    return true;
}

bool AuthenticationManager::IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_authenticated;
}

void AuthenticationManager::Deauthenticate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_authenticated = false;
}

bool AuthenticationManager::SaveObfuscationState(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto enc = DPAPI_Protect(enabled ? "1" : "0", L"PSWrapperState");
    return !enc.empty() && DPAPI_WriteBinaryFile(GetStateFilePath(), enc);
}

bool AuthenticationManager::LoadObfuscationState(bool& outEnabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetStateFilePath(), blob)) return false;
    std::string s = DPAPI_Unprotect(blob);
    if (s.empty()) return false;
    outEnabled = (s == "1");
    return true;
}

bool AuthenticationManager::SaveLastDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (path.empty()) return false;
    auto enc = DPAPI_Protect(path, L"PSWrapperDir");
    return !enc.empty() && DPAPI_WriteBinaryFile(GetDirStateFilePath(), enc);
}

bool AuthenticationManager::LoadLastDirectory(std::string& outPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetDirStateFilePath(), blob)) return false;
    std::string s = DPAPI_Unprotect(blob);
    if (s.empty()) return false;
    outPath = s;
    return true;
}

void AuthenticationManager::SetPasswordRotationDays(int days) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (days > 0 && days <= 365) { m_passwordRotationDays = days; SaveRotationPeriod(); }
}

int AuthenticationManager::GetPasswordRotationDays() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_passwordRotationDays;
}

int AuthenticationManager::GetDaysUntilExpiry() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetKeyFilePath(), blob)) return 0;
    try {
        auto j = nlohmann::json::parse(DPAPI_Unprotect(blob));
        int elapsed = static_cast<int>(std::difftime(std::time(nullptr), j["timestamp"]) / 86400);
        return std::max(0, m_passwordRotationDays - elapsed);
    } catch (...) {}
    return 0;
}

bool AuthenticationManager::SaveRotationPeriod() {
    auto encrypted = DPAPI_Protect(std::to_string(m_passwordRotationDays), L"PSWrapperRotation");
    return !encrypted.empty() && DPAPI_WriteBinaryFile(GetRotationFilePath(), encrypted);
}

bool AuthenticationManager::LoadRotationPeriod() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BYTE> blob;
    if (!DPAPI_ReadBinaryFile(GetRotationFilePath(), blob)) { m_passwordRotationDays = 30; return false; }
    std::string data = DPAPI_Unprotect(blob);
    try {
        m_passwordRotationDays = std::stoi(data);
        if (m_passwordRotationDays < 1 || m_passwordRotationDays > 365) m_passwordRotationDays = 30;
    } catch (...) { m_passwordRotationDays = 30; return false; }
    return true;
}

void AuthenticationManager::ClearKey() { 
    std::lock_guard<std::mutex> lock(m_mutex);
    DPAPI_SecureDeleteFile(GetKeyFilePath());
    m_authenticated = false;
}
