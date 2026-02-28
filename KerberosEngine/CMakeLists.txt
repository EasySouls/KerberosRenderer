project(KerberosEngine)

# Add third party first
add_subdirectory(ThirdParty)

add_library(KerberosEngine STATIC)

# Collect only Source files
file(GLOB_RECURSE ENGINE_SRC
    Source/*.cpp
    Source/*.h
    Source/*.hpp
    Source/*.ixx
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${ENGINE_SRC})

target_sources(KerberosEngine PRIVATE ${ENGINE_SRC})

# Precompiled header
target_precompile_headers(KerberosEngine PRIVATE Source/kbrpch.hpp)

# Include directories
target_include_directories(KerberosEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/
        ${KBR_VULKAN_DIR}/Include # This shouldn't be public in the future
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/glm
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/lib/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/other_include
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ktx/external
)

# Definitions
target_compile_definitions(KerberosEngine PRIVATE
    _CRT_SECURE_NO_WARNINGS
    GLFW_INCLUDE_NONE
    IMGUI_DOCKING_BRANCH
    YAML_CPP_STATIC_DEFINE
    $<$<CONFIG:Debug>:KBR_DEBUG>
    $<$<CONFIG:Release>:KBR_RELEASE>
    $<$<CONFIG:Dist>:KBR_DIST>
)

# Link engine build settings
target_link_libraries(KerberosEngine PRIVATE KerberosBuildSettings)

# Link third party
target_link_libraries(KerberosEngine
    PUBLIC
        glm
    PRIVATE
        glfw
        imgui
        yaml-cpp
        msdf-atlas-gen
        ImGuizmo
        assimp::assimp
        Jolt::Jolt
        tinygltf
        ktx
)

# Vulkan
target_link_directories(KerberosEngine PRIVATE
    ${KBR_VULKAN_DIR}/Lib
)

target_link_libraries(KerberosEngine PRIVATE
    ${KBR_VULKAN_DIR}/Lib/vulkan-1.lib

    $<$<CONFIG:Debug>:${KBR_VULKAN_DIR}/Lib/shaderc_sharedd.lib>
    $<$<CONFIG:Debug>:${KBR_VULKAN_DIR}/Lib/spirv-cross-cored.lib>
    $<$<CONFIG:Debug>:${KBR_VULKAN_DIR}/Lib/spirv-cross-glsld.lib>
    $<$<CONFIG:Debug>:${KBR_VULKAN_DIR}/Lib/slangd.lib>

    $<$<CONFIG:Release>:${KBR_VULKAN_DIR}/Lib/shaderc_shared.lib>
    $<$<CONFIG:Release>:${KBR_VULKAN_DIR}/Lib/spirv-cross-core.lib>
    $<$<CONFIG:Release>:${KBR_VULKAN_DIR}/Lib/spirv-cross-glsl.lib>
    $<$<CONFIG:Release>:${KBR_VULKAN_DIR}/Lib/slang.lib>
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