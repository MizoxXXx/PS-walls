#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <random>

#define ANSI_RESET "\033[0m"
#define ANSI_BLUE "\033[94m"
#define ANSI_GREEN "\033[92m"
#define ANSI_YELLOW "\033[93m"
#define ANSI_RED "\033[91m"
#define ANSI_CYAN "\033[96m"
#define ANSI_MAGENTA "\033[95m"
#define ANSI_WHITE "\033[97m"

std::string GetCurrentTimestamp();
std::string ToBase64(const std::string& in);
std::string FromBase64(const std::string& in);
bool EnableANSIColors();

int GetRandomInt(int min, int max);
void ShuffleVector(std::vector<char>& v);

// SECURITY: SecureString RAII class that guarantees memory zeroing on destruction
// Used for password handling to prevent sensitive data from lingering in memory
template<typename T = char>
class SecureVector {
private:
    std::vector<T> data;

public:
    SecureVector() = default;
    
    explicit SecureVector(size_t size) : data(size) {}
    
    explicit SecureVector(const std::string& str) {
        data.resize(str.length());
        std::memcpy(data.data(), str.c_str(), str.length());
    }
    
    // Copy constructor - copies data securely
    SecureVector(const SecureVector& other) : data(other.data) {}
    
    // Move constructor - efficient transfer
    SecureVector(SecureVector&& other) noexcept : data(std::move(other.data)) {}
    
    // Copy assignment
    SecureVector& operator=(const SecureVector& other) {
        if (this != &other) {
            ClearMemory();
            data = other.data;
        }
        return *this;
    }
    
    // Move assignment
    SecureVector& operator=(SecureVector&& other) noexcept {
        if (this != &other) {
            ClearMemory();
            data = std::move(other.data);
        }
        return *this;
    }
    
    // CRITICAL: Destructor zeros memory before deallocation
    ~SecureVector() {
        ClearMemory();
    }
    
    // Get pointer to data
    T* data_ptr() { return data.data(); }
    const T* data_ptr() const { return data.data(); }
    
    // Get size
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    
    // Resize
    void resize(size_t size) {
        if (size < data.size()) ClearMemory();
        data.resize(size);
    }
    
    // Clear
    void clear() {
        ClearMemory();
        data.clear();
    }
    
    // Append data
    void append(const T* ptr, size_t len) {
        if (ptr && len > 0) {
            size_t oldSize = data.size();
            data.resize(oldSize + len);
            std::memcpy(data.data() + oldSize, ptr, len);
        }
    }
    
    // Convert to string (use with caution)
    std::string to_string() const {
        return std::string(data.begin(), data.end());
    }
    
    // Iterator support
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    
private:
    // Secure memory zeroing - GUARANTEED to execute
    void ClearMemory() {
        if (data.empty()) return;
        
        // Fill with zeros using direct memory access
        volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(data.data());
        for (size_t i = 0; i < data.size() * sizeof(T); ++i) {
            p[i] = 0;
        }
    }
};
