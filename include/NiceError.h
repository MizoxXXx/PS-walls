#pragma once
#include <string>
#include <map>

class NiceError {
public:
    NiceError();
    std::string GetSuggestion(const std::string& errorOutput);

private:
    std::map<std::string, std::string> m_linuxToPs;
    void InitializeCommandMappings();
    std::string ExtractCommandFromError(const std::string& errorOutput);
};
