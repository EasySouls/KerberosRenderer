cmake_minimum_required(VERSION 3.30)

if(NOT DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Attempt to find the scanner in the system path or compiler directory
    find_program(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS NAMES clang-scan-deps clang-scan-deps.exe)
    
    if(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
        message(STATUS "Found clang-scan-deps: ${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
    else()
        message(FATAL_ERROR "clang-scan-deps NOT FOUND! C++20 Modules cannot be compiled. Ensure LLVM bin folder is in your PATH.")
    endif()
endif()

project(KerberosRenderer LANGUAGES CXX)

message(STATUS "--- MODULE DIAGNOSTICS ---")
message(STATUS "CMake Version:      ${CMAKE_VERSION}")
message(STATUS "Compiler ID:        ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Compiler Version:   ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Simulated MSVC:     ${CMAKE_CXX_SIMULATE_VERSION}")
message(STATUS "ScanDeps Tool:      ${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
message(STATUS "Internal Scanner:   ${CMAKE_CXX_SCANDEP_SOURCE}")
message(STATUS "Compiler Launcher:  ${CMAKE_CXX_COMPILER_LAUNCHER}")
message(STATUS "--------------------------")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Keep source-built dependencies deterministic. Individual dependencies can
# still override this when they expose a dedicated option.
option(BUILD_SHARED_LIBS "Build libraries as shared libraries by default." OFF)

option(KBR_ENABLE_ASAN "Enable AddressSanitizer for Kerberos targets." OFF)
option(KBR_ENABLE_FUZZING "Enable libFuzzer instrumentation for Kerberos targets (Clang only)." OFF)
option(KBR_ENABLE_THIRDPARTY_INSTRUMENTATION
       "Enable selected sanitizer/fuzz instrumentation for third-party targets too."
       OFF)

if(MSVC)
    # Use the DLL runtime consistently for the engine and static third-party
    # libraries. This is independent from whether a library is a DLL.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(DEFINED ENV{VULKAN_SDK})
    set(KBR_VULKAN_DIR "$ENV{VULKAN_SDK}" CACHE PATH "Vulkan SDK root directory")
else()
    set(KBR_VULKAN_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/KerberosEngine/ThirdParty/VulkanSDK"
        CACHE PATH "Vulkan SDK root directory")
endif()
message(STATUS "Vulkan SDK: ${KBR_VULKAN_DIR}")

include("${CMAKE_CURRENT_SOURCE_DIR}/Config/KerberosBuildSettings.cmake")

add_subdirectory(KerberosEngine)
add_subdirectory(KerberosScriptCoreLib)
add_subdirectory(KerberosEditor)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT KerberosEditor)
