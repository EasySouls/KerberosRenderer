project(KerberosScriptCoreLib LANGUAGES CSharp)

set(KBR_SCRIPT_CORE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/KerberosScriptCoreLib.csproj")
set(KBR_SCRIPT_CORE_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/artifacts/$<CONFIG>")
set(KBR_SCRIPT_CORE_TARGET_DIR "${CMAKE_SOURCE_DIR}/KerberosEditor/Resources/Scripts")

add_custom_target(KerberosScriptCoreLibBuild
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KBR_SCRIPT_CORE_STAGING_DIR}"
    COMMAND dotnet build "${KBR_SCRIPT_CORE_PROJECT}" --configuration "$<IF:$<CONFIG:Debug>,Debug,Release>" --output "${KBR_SCRIPT_CORE_STAGING_DIR}" --nologo
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KBR_SCRIPT_CORE_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${KBR_SCRIPT_CORE_STAGING_DIR}" "${KBR_SCRIPT_CORE_TARGET_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building KerberosScriptCoreLib and copying to KerberosEditor/Resources/Scripts"
    VERBATIM
)

set_target_properties(KerberosScriptCoreLibBuild PROPERTIES FOLDER "KerberosScripting")
