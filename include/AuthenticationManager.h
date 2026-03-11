#ifndef AUTHENTICATION_MANAGER_H
#define AUTHENTICATION_MANAGER_H

#include <string>
#include <mutex>

class AuthenticationManager {
public:
    AuthenticationManager();
    ~AuthenticationManager();

    bool SetKey(const std::string& password, const std::string& backup1 = "", const std::string& backup2 = "");
    bool Authenticate(const std::string& inputPassword, int keyIndex = -1);
    bool IsAuthenticated() const;
    void Deauthenticate();
    void ClearKey();
    bool LoadKeys();
    bool IsPasswordExpired() const;
    bool SaveObfuscationState(bool enabled);
    bool LoadObfuscationState(bool& outEnabled);
    bool SaveLastDirectory(const std::string& path);
    bool LoadLastDirectory(std::string& outPath);
    void SetPasswordRotationDays(int days);
    int GetPasswordRotationDays() const;
    int GetDaysUntilExpiry() const;
    int GetLastUsedKeyIndex() const { return m_lastUsedKeyIndex; }

private:
    std::string GetStorageDirectory() const;
    std::string GetKeyFilePath() const;
    std::string GetStateFilePath() const;
    std::string GetRotationFilePath() const;
    std::string GetDirStateFilePath() const;
    bool SaveRotationPeriod();
    bool LoadRotationPeriod();

    mutable std::mutex m_mutex;
    bool m_authenticated;
    int m_passwordRotationDays; 
    int m_lastUsedKeyIndex; 
};

#endif 
