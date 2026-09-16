set(FIDELITYFX_ROOT
        "${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/FidelityFX-SDK"
)

set(FIDELITYFX_BIN_ROOT
        "${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/FidelityFX-SDK/PrebuiltSignedDLL"
)

set(FIDELITYFX_INCLUDE_DIR
        "${FIDELITYFX_ROOT}/ffx-api/include"
)

set(FFX_API_BACKEND
        VK_X64
        CACHE STRING
        "FidelityFX API backend to build"
)

set(FFX_FSR2 ON CACHE BOOL "" FORCE)
set(FFX_FSR3 ON CACHE BOOL "" FORCE)
set(FFX_FSR3UPSCALER ON CACHE BOOL "" FORCE)
set(FFX_OF ON CACHE BOOL "" FORCE)
set(FFX_FI ON CACHE BOOL "" FORCE)
set(FFX_AUTO_COMPILE_SHADERS ON CACHE BOOL "" FORCE)
set(FFX_BUILD_AS_DLL OFF CACHE BOOL "" FORCE)

add_subdirectory(
        "${FIDELITYFX_ROOT}/ffx-api"
        "${CMAKE_BINARY_DIR}/FidelityFX/ffx-api"
        EXCLUDE_FROM_ALL
)

add_library(Kerberos::FidelityFX ALIAS amd_fidelityfx_vk)

target_include_directories(amd_fidelityfx_vk
        PUBLIC
        "${FIDELITYFX_INCLUDE_DIR}"
)

add_custom_target(KerberosFidelityFXRuntime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:amd_fidelityfx_vk>"
        "$<TARGET_FILE_DIR:KerberosEngine>"
        DEPENDS amd_fidelityfx_vk
)
