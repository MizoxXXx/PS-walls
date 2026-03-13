cmake_minimum_required(VERSION 3.15)
project(PowerShellWrapper VERSION 1.0 LANGUAGES CXX)

# ============================================================================
# Configuration du projet
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Définir le type de build par défaut
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ============================================================================
# Répertoires d'inclusion
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Fichiers sources
# ============================================================================
set(SOURCES
    src/Common.cpp
    src/DPAPI_Utils.cpp
    src/Obfuscator.cpp
    src/SecureLogger.cpp
    src/AuthenticationManager.cpp
    src/ThreatDetector.cpp
    src/InactivityMonitor.cpp
    src/PowerShellWrapper.cpp
    src/main.cpp
    src/NiceError.cpp
)

set(HEADERS
    include/Common.h
    include/DPAPI_Utils.h
    include/Obfuscator.h
    include/json.hpp
    include/SecureLogger.h
    include/AuthenticationManager.h
    include/ThreatDetector.h
    include/InactivityMonitor.h
    include/PowerShellWrapper.h
    include/NiceError.h
)

# ============================================================================
# Ressources (icône Windows)
# ============================================================================
set(RESOURCES "")  # Initialize empty

if(WIN32)
    # Enable RC language BEFORE using it
    enable_language(RC)
    
    # Use consistent variable name (RESOURCES not RESSOURCES)
    set(RESOURCES ressources/app.rc)
    
    # Vérifier que l'icône existe
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ressources/psSafer.ico")
        message(WARNING "Icône non trouvée: ressources/psSafer.ico")
    endif()
    
    # Vérifier que le manifest existe
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ressources/app.manifest")
        message(WARNING "Manifest non trouvé: ressources/app.manifest")
    endif()
endif()

# ============================================================================
# Création de l'exécutable
# ============================================================================
add_executable(ps_wrapper ${SOURCES} ${HEADERS} ${RESOURCES})

# ============================================================================
# Bibliothèques Windows
# ============================================================================
if(WIN32)
    target_link_libraries(ps_wrapper
        advapi32    # DPAPI, ACL
        shell32     # SHGetKnownFolderPath
        crypt32     # CryptProtectData
        ole32       # CoTaskMemFree
        uuid        # GUID
        user32      # GetConsoleWindow, SetConsoleTitle
        gdi32       # Icon loading
    )
endif()

# ============================================================================
# Options de compilation
# ============================================================================
if(MSVC)
    # Visual Studio
    target_compile_options(ps_wrapper PRIVATE
        /W4                 # Niveau d'avertissement élevé
        /O2                 # Optimisation maximale
        /GL                 # Optimisation globale du programme
        /MP                 # Compilation parallèle
        /utf-8              # Encodage UTF-8
    )
    
    target_link_options(ps_wrapper PRIVATE
        /LTCG               # Link-Time Code Generation
        /SUBSYSTEM:CONSOLE  # Application console
    )
    
    # Définir les propriétés MSVC spécifiques
    set_target_properties(ps_wrapper PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
    
else()
    # MinGW / GCC / Clang
    target_compile_options(ps_wrapper PRIVATE
        -Wall               # Tous les avertissements
        -Wextra             # Avertissements supplémentaires
        -O3                 # Optimisation maximale
        -march=native       # Optimiser pour le CPU actuel
    )
    
    # Lien statique pour distribution autonome
    target_link_options(ps_wrapper PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
    )
endif()

# Définir le nom de sortie global pour tous les compilateurs
set_target_properties(ps_wrapper PROPERTIES
    OUTPUT_NAME "ps~walls"
)

# ============================================================================
# Définitions de préprocesseur
# ============================================================================
target_compile_definitions(ps_wrapper PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN     # Accélérer compilation Windows.h
    NOMINMAX                # Éviter conflits min/max
)

# ============================================================================
# Affichage des informations de build
# ============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  PowerShell Wrapper - Configuration")
message(STATUS "========================================")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Source dir:     ${CMAKE_SOURCE_DIR}")
message(STATUS "Binary dir:     ${CMAKE_BINARY_DIR}")
message(STATUS "========================================")
message(STATUS "")

# ============================================================================
# Installation (optionnel)
# ============================================================================
install(TARGETS ps_wrapper
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# Installer les fichiers de ressources
install(FILES ressources/psSafer.ico
    DESTINATION ressources
    COMPONENT Ressources
)

# ============================================================================
# Tests (optionnel)
# ============================================================================
option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    # Ajouter vos tests ici
endif()

# ============================================================================
# CPack - Création d'installateur (optionnel)
# ============================================================================
set(CPACK_PACKAGE_NAME "PowerShellWrapper")
set(CPACK_PACKAGE_VENDOR "PowerShell Security Tools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PowerShell Educational Wrapper")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE") # Nice that without LICENSE , compilation failed 
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PowerShellWrapper")

include(CPack)
