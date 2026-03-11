#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <wincrypt.h>
#include <iostream>

#pragma comment(lib, "crypt32.lib")

std::vector<BYTE> DPAPI_Protect(const std::string& plain, const std::wstring& description = L"ProtectedData");
std::string DPAPI_Unprotect(const std::vector<BYTE>& encrypted);
bool DPAPI_WriteBinaryFile(const std::string& path, const std::vector<BYTE>& data);
bool DPAPI_ReadBinaryFile(const std::string& path, std::vector<BYTE>& outData);
void DPAPI_SecureDeleteFile(const std::string& path);
