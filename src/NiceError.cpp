#include "NiceError.h"
#include "Common.h"
#include <algorithm>
#include <regex>

NiceError::NiceError() { InitializeCommandMappings(); }

void NiceError::InitializeCommandMappings() {
    // Basic File & Directory Operations
    m_linuxToPs["ls"] = "Get-ChildItem or dir";
    m_linuxToPs["ll"] = "Get-ChildItem -Force";
    m_linuxToPs["la"] = "Get-ChildItem -Force";
    m_linuxToPs["cp"] = "Copy-Item";
    m_linuxToPs["mv"] = "Move-Item";
    m_linuxToPs["rm"] = "Remove-Item or del";
    m_linuxToPs["mkdir"] = "New-Item -ItemType Directory or md";
    m_linuxToPs["touch"] = "New-Item -ItemType File";
    m_linuxToPs["pwd"] = "Get-Location";
    m_linuxToPs["cd"] = "Set-Location";

    // Text Processing
    m_linuxToPs["grep"] = "Select-String";
    m_linuxToPs["cat"] = "Get-Content";
    m_linuxToPs["head"] = "Get-Content -TotalCount 10";
    m_linuxToPs["tail"] = "Get-Content -Tail 10";
    m_linuxToPs["sed"] = "(Get-Content file) -replace 'old','new'";
    m_linuxToPs["awk"] = "Import-Csv | Select-Object";
    m_linuxToPs["sort"] = "Sort-Object";
    m_linuxToPs["uniq"] = "Get-Unique";
    m_linuxToPs["wc"] = "Measure-Object";
    m_linuxToPs["less"] = "more";

    // System & Networking
    m_linuxToPs["ps"] = "Get-Process";
    m_linuxToPs["top"] = "Get-Process | Sort-Object CPU -Descending";
    m_linuxToPs["kill"] = "Stop-Process";
    m_linuxToPs["ifconfig"] = "ipconfig or Get-NetIPAddress";
    m_linuxToPs["ip"] = "Get-NetIPAddress";
    m_linuxToPs["netstat"] = "Get-NetTCPConnection";
    m_linuxToPs["ping"] = "Test-Connection";
    m_linuxToPs["curl"] = "Invoke-WebRequest or iwr";
    m_linuxToPs["wget"] = "Invoke-WebRequest or iwr";
    m_linuxToPs["ssh"] = "ssh (built-in) or Start-Process ssh";
    m_linuxToPs["sudo"] = "Start-Process powershell -Verb runAs";
    m_linuxToPs["su"] = "Start-Process powershell -Verb runAs -Credential (Get-Credential)";

    // Miscellaneous
    m_linuxToPs["man"] = "Get-Help";
    m_linuxToPs["help"] = "Get-Help";
    m_linuxToPs["clear"] = "Clear-Host or cls";
    m_linuxToPs["history"] = "Get-History or h (wrapper cmd)";
    m_linuxToPs["find"] = "Get-ChildItem -Recurse -Filter";
    m_linuxToPs["locate"] = "Get-ChildItem -Recurse -Filter";
    m_linuxToPs["which"] = "Get-Command or gcm";
    m_linuxToPs["alias"] = "Get-Alias";
    m_linuxToPs["export"] = "$env:VARIABLE = 'value'";
    m_linuxToPs["df"] = "Get-Volume";
    m_linuxToPs["du"] = "Get-ChildItem -Recurse | Measure-Object -Property Length -Sum";
    m_linuxToPs["chown"] = "Set-Acl or icacls";
    m_linuxToPs["chmod"] = "Set-Acl or icacls";
    m_linuxToPs["diff"] = "Compare-Object";
    m_linuxToPs["tar"] = "Compress-Archive or Expand-Archive";
    m_linuxToPs["zip"] = "Compress-Archive";
    m_linuxToPs["unzip"] = "Expand-Archive";
    m_linuxToPs["whoami"] = "whoami.exe or $env:USERNAME";
    m_linuxToPs["id"] = "whoami.exe /user /groups";
    m_linuxToPs["date"] = "Get-Date";
    m_linuxToPs["env"] = "Get-ChildItem Env:";
    m_linuxToPs["uptime"] = "(Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime";
    m_linuxToPs["file"] = "Get-Item or (Get-Item <file>).Extension for now";
}

std::string NiceError::ExtractCommandFromError(const std::string& errorOutput) {
    std::vector<std::regex> patterns = { std::regex("'([^']+)'"), std::regex("\"([^\"]+)\"") };
    if (errorOutput.find("not recognized") == std::string::npos) return "";
    for (const auto& pattern : patterns) {
        std::smatch match;
        if (std::regex_search(errorOutput, match, pattern)) return match[1].str();
    }
    return "";
}

std::string NiceError::GetSuggestion(const std::string& errorOutput) {
    std::string cmd = ExtractCommandFromError(errorOutput);
    if (cmd.empty()) return "";
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    auto it = m_linuxToPs.find(cmd);
    if (it != m_linuxToPs.end()) return std::string(ANSI_CYAN) + "[++] Try instead: " + ANSI_WHITE + it->second + ANSI_RESET + "\n";
    return "";
}
