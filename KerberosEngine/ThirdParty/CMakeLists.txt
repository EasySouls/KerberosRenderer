set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(GLFW EXCLUDE_FROM_ALL)

# YAML-CPP
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(yaml-cpp EXCLUDE_FROM_ALL)

# Assimp
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
add_subdirectory(Assimp EXCLUDE_FROM_ALL)

# KTX
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(BASISU_SUPPORT_SSE OFF CACHE BOOL "" FORCE)
add_subdirectory(ktx EXCLUDE_FROM_ALL)

# dfdutils uses include_directories() (directory-scoped) rather than
# target_include_directories(PUBLIC), so <KHR/khr_df.h> is not propagated
# to consumers of ktx (e.g. ktx.h includes it with angle brackets).
# Similarly, ktx/lib/CMakeLists.txt line 301 uses ${CMAKE_SOURCE_DIR}/external/dfdutils
# which only resolves correctly when KTX is the top-level project.
# Patch the ktx target here without modifying any third-party sources.
target_include_directories(ktx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ktx/external/dfdutils>
)

# FreeType
add_subdirectory(freetype)
add_library(Freetype::Freetype ALIAS freetype)

# msdf-atlas-gen
set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
add_subdirectory(msdf-atlas-gen EXCLUDE_FROM_ALL)

# EnTT
set(ENTT_INCLUDE_NATVIS ON CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(entt EXCLUDE_FROM_ALL)

# Jolt Physics
set(JPH_DEBUG_RENDERER ON CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(JoltPhysics/Build EXCLUDE_FROM_ALL)

# ImGui
add_library(imgui STATIC)

target_sources(imgui PRIVATE
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_include_directories(imgui PRIVATE
    ${KBR_VULKAN_DIR}/Include
)

target_link_libraries(imgui PUBLIC glfw)

# Disable warnings for ImGui
if(MSVC)
    target_compile_options(imgui PRIVATE /w)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

# ImGuizmo
add_library(ImGuizmo STATIC)

target_compile_definitions(ImGuizmo PRIVATE "IMGUI_API=__declspec(dllexport)")

target_sources(ImGuizmo PRIVATE
    ImGuizmo/ImGuizmo.cpp
    ImGuizmo/ImGuizmo.h
)

target_include_directories(ImGuizmo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo
)

target_include_directories(ImGuizmo PRIVATE
    ${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/imgui
)

target_link_libraries(ImGuizmo PUBLIC imgui)

# Header-only libraries

# glm
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/glm
)

# tinygltf
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf
)

# stb_image
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stb
)

# spdlog
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/spdlog
)

# filewatch
add_library(filewatch INTERFACE)
target_include_directories(filewatch INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/filewatch
)

set_target_properties(
    glfw imgui yaml-cpp msdf-atlas-gen freetype ktx
    ImGuizmo assimp Jolt tinygltf stb spdlog glm
    filewatch
    PROPERTIES FOLDER "KerberosEngine/ThirdParty"
)