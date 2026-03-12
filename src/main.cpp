#include "PowerShellWrapper.h"
#include <windows.h>
#include <iostream>
#include <locale>
#include <ctime>

int main() {
#ifdef _WIN32
    // Console UTF-8 setup
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initial random seed
    srand(static_cast<unsigned int>(time(NULL)));

    // Enable ANSI colors for the initial output if needed
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }

    try {
        std::setlocale(LC_ALL, ".UTF-8");
    } catch (...) {}
#endif

    PowerShellWrapper wrapper;
    return wrapper.Run();
}
