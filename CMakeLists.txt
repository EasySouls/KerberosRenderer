cmake_minimum_required(VERSION 3.27)
project(KerberosEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Disable Basis Universal SSE support to avoid C++23 implicit-move
# incompatibility in cppspmd_sse.h (rvalue-ref return binding to lvalue-ref).
set(BASISU_SSE OFF CACHE BOOL "Compile with SSE support so applications can choose to use it." FORCE)

# Vulkan SDK fallback
if(DEFINED ENV{VULKAN_SDK})
    set(KBR_VULKAN_DIR $ENV{VULKAN_SDK})
else()
    set(KBR_VULKAN_DIR "${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/VulkanSDK")
endif()

message(STATUS "Vulkan SDK: ${KBR_VULKAN_DIR}")

include(Config/KerberosBuildSettings.cmake)

target_compile_options(KerberosBuildSettings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /MP>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)

add_subdirectory(KerberosEngine)
add_subdirectory(KerberosEditor)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT KerberosEditor)
