#include "DPAPI_Utils.h"
#include <fstream>
#include <filesystem>

std::vector<BYTE> DPAPI_Protect(const std::string& plain, const std::wstring& description) {
    DATA_BLOB inBlob{};
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    inBlob.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, description.c_str(), NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        return {};
    }
    std::vector<BYTE> encrypted(outBlob.pbData, outBlob.pbData + outBlob.cbData);
    LocalFree(outBlob.pbData);
    return encrypted;
}

std::string DPAPI_Unprotect(const std::vector<BYTE>& encrypted) {
    if (encrypted.empty()) return "";
    DATA_BLOB inBlob{};
    inBlob.pbData = const_cast<BYTE*>(encrypted.data());
    inBlob.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        return "";
    }
    std::string result(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
    LocalFree(outBlob.pbData);
    return result;
}

bool DPAPI_WriteBinaryFile(const std::string& path, const std::vector<BYTE>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return static_cast<bool>(file);
}

bool DPAPI_ReadBinaryFile(const std::string& path, std::vector<BYTE>& outData) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size < 0) return false;
    outData.resize(size);
    file.read(reinterpret_cast<char*>(outData.data()), size);
    return static_cast<bool>(file);
}

void DPAPI_SecureDeleteFile(const std::string& path) {
    try {
        if (!std::filesystem::exists(path)) return;
        std::error_code ec;
        std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size > 0) {
            std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
            if (file) {
                std::vector<char> zeros(size, 0);
                file.write(zeros.data(), size);
                file.close();
            }
        }
        std::filesystem::remove(path, ec);
    } catch (...) {}
}
