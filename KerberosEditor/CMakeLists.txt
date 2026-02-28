project(KerberosEditor)

add_executable(KerberosEditor)

file(GLOB_RECURSE EDITOR_SRC
    Source/*.cpp
    Source/*.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${EDITOR_SRC})

target_sources(KerberosEditor PRIVATE ${EDITOR_SRC})

target_include_directories(KerberosEditor PRIVATE
    ${CMAKE_SOURCE_DIR}/KerberosEngine/Source
)

target_link_libraries(KerberosEditor PRIVATE
    KerberosEngine
    KerberosBuildSettings
)

if(WIN32)
    target_compile_definitions(KerberosEditor PRIVATE KBR_PLATFORM_WINDOWS)
endif()

set_target_properties(KerberosEditor PROPERTIES FOLDER "KerberosEditor")