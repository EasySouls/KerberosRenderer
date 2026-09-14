set(FIDELITYFX_ROOT
        "${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/FidelityFX-SDK"
)

set(FIDELITYFX_BIN_ROOT
        "${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/FidelityFX-SDK/PrebuiltSignedDLL"
)

set(FIDELITYFX_INCLUDE_DIR
        "${FIDELITYFX_ROOT}/ffx-api/include"
)

set(FIDELITYFX_VK_DLL
        "${FIDELITYFX_BIN_ROOT}/amd_fidelityfx_vk.dll"
)

set(FIDELITYFX_VK_LIB
        "${FIDELITYFX_BIN_ROOT}/amd_fidelityfx_vk.lib")


if (NOT EXISTS "${FIDELITYFX_VK_DLL}")
    message(FATAL_ERROR
            "FidelityFX Vulkan DLL not found:\n"
            "  ${FIDELITYFX_VK_DLL}"
    )
endif()

if (NOT EXISTS "${FIDELITYFX_VK_LIB}")
    message(FATAL_ERROR
            "FidelityFX Vulkan import library not found:\n"
            "  ${FIDELITYFX_VK_LIB}"
    )
endif()


add_library(Kerberos::FidelityFX STATIC IMPORTED GLOBAL)

set_target_properties(Kerberos::FidelityFX PROPERTIES
        IMPORTED_LOCATION
        "${FIDELITYFX_VK_LIB}"

        INTERFACE_INCLUDE_DIRECTORIES
        "${FIDELITYFX_INCLUDE_DIR}"
)


add_custom_target(KerberosFidelityFXRuntime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FIDELITYFX_VK_DLL}"
        "$<TARGET_FILE_DIR:KerberosEngine>/amd_fidelityfx_vk.dll"
)

