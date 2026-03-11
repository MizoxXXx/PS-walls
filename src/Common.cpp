#include "Common.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstring>
#include <random>
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#include <process.h>
#endif

std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_MSC_VER)
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string ToBase64(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string FromBase64(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64_chars[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

#if defined(_WIN32)
bool EnableANSIColors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return false;
    if (dwMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
}
#else
bool EnableANSIColors() { return true; }
#endif

// Robust Randomization
static std::mt19937& GetEngine() {
    static std::mt19937 engine([]() {
        std::random_device rd;
        std::vector<uint32_t> seedData(8);
        for (auto& s : seedData) s = rd();
        
        // Add entropy from time and PID in case rd() is weak/deterministic
        seedData.push_back(static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
#ifdef _WIN32
        seedData.push_back(static_cast<uint32_t>(GetCurrentProcessId()));
#endif
        
        std::seed_seq seq(seedData.begin(), seedData.end());
        return std::mt19937(seq);
    }());
    return engine;
}

int GetRandomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(GetEngine());
}

void ShuffleVector(std::vector<char>& v) {
    std::shuffle(v.begin(), v.end(), GetEngine());
}
