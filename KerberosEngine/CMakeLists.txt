project(KerberosEngine)

# Add third party first
add_subdirectory(ThirdParty)

add_library(KerberosEngine STATIC)

# Collect only Source files
file(GLOB_RECURSE ENGINE_REGULAR_SOURCES CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

file (GLOB_RECURSE ENGINE_MODULE_SOURCES CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cppm"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.ixx"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${ENGINE_REGULAR_SOURCES})

target_sources(KerberosEngine PRIVATE ${ENGINE_REGULAR_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${ENGINE_MODULE_SOURCES})

target_sources(KerberosEngine
    PUBLIC
    FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        FILES ${ENGINE_MODULE_SOURCES})

# Precompiled header is intentionally disabled during the mixed module/header migration.
# Legacy TUs continue to use the old header path, while module TUs are kept isolated
# from the legacy PCH to avoid duplicate standard-library and logger declarations.
# target_precompile_headers(KerberosEngine PRIVATE Source/kbrpch.hpp)

# Include directories
target_include_directories(KerberosEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
        ${KBR_VULKAN_DIR}/Include # This shouldn't be public in the future
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/glm
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/lib/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/other_include
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/external
)

# Definitions
target_compile_definitions(KerberosEngine PUBLIC
    _CRT_SECURE_NO_WARNINGS
    GLFW_INCLUDE_NONE
    IMGUI_DOCKING_BRANCH
    KBR_USE_MINIAUDIO
    NOMINMAX
)

# Enable these later, but a few libraries are compiled as part of the engine that have warnings
#target_compile_options(KerberosEngine PRIVATE
#    $<$<CXX_COMPILER_ID:MSVC>:/Wall /W4 /WX /MP /permissive->
#    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Werror -Wpedantic>
#    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Werror -Wpedantic>
#)

# Link engine build settings
target_link_libraries(KerberosEngine PRIVATE KerberosBuildSettings)

# Link third party
target_link_libraries(KerberosEngine
    PUBLIC
        glm
    PRIVATE
        glfw
        imgui
        yaml-cpp::yaml-cpp
        msdf-atlas-gen
        ImGuizmo
        assimp::assimp
        Jolt::Jolt
        tinygltf
        ktx
        dotnet
)

# Vulkan
target_link_directories(KerberosEngine PRIVATE
    ${KBR_VULKAN_DIR}/Lib
)

target_link_libraries(KerberosEngine PRIVATE
    ${KBR_VULKAN_DIR}/Lib/vulkan-1.lib

    $<$<CONFIG:Debug>:
        ${KBR_VULKAN_DIR}/Lib/shaderc_sharedd.lib
        ${KBR_VULKAN_DIR}/Lib/spirv-cross-cored.lib
        ${KBR_VULKAN_DIR}/Lib/spirv-cross-glsld.lib
        ${KBR_VULKAN_DIR}/Lib/slangd.lib
    >

    $<$<NOT:$<CONFIG:Debug>>:
        ${KBR_VULKAN_DIR}/Lib/shaderc_shared.lib
        ${KBR_VULKAN_DIR}/Lib/spirv-cross-core.lib
        ${KBR_VULKAN_DIR}/Lib/spirv-cross-glsl.lib
        ${KBR_VULKAN_DIR}/Lib/slang.lib
    >
)

# Windows system libs
if(WIN32)
    target_compile_definitions(KerberosEngine PRIVATE KBR_PLATFORM_WINDOWS)

    target_link_libraries(KerberosEngine PRIVATE
        dxgi
        d3dcompiler
        dxguid
        Ws2_32
        Winmm
        Version
        Bcrypt
    )
endif()

set_target_properties(KerberosEngine PROPERTIES FOLDER "KerberosEngine")