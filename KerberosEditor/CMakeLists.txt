project(KerberosEditor)

add_executable(KerberosEditor)

file(GLOB_RECURSE EDITOR_SRC CMAKE_CONFIGURE_DEPENDS
    Source/*.cpp
    Source/*.hpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${EDITOR_SRC})

target_sources(KerberosEditor PRIVATE ${EDITOR_SRC} "Source/Notification/NotificationManager.cpp" "Source/Windows/AssetsPanel.cpp" "Source/Windows/HierarchyPanel.cpp")

target_include_directories(KerberosEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    # ${CMAKE_SOURCE_DIR}/KerberosEngine/Source
)

target_link_libraries(KerberosEditor PRIVATE
    KerberosEngine
    KerberosBuildSettings
)
add_dependencies(KerberosEditor KerberosScriptCoreLibBuild)

if(WIN32)
    target_compile_definitions(KerberosEditor PRIVATE KBR_PLATFORM_WINDOWS)

    add_custom_command(TARGET KerberosEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Debug/nethost.dll,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Release/nethost.dll>"
            "$<TARGET_FILE_DIR:KerberosEditor>/nethost.dll"
        VERBATIM
    )
endif()

set_target_properties(KerberosEditor PROPERTIES FOLDER "KerberosEditor")
