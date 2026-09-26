include(GoogleTest)

add_executable(KerberosEngineTests
    ProjectLoadTests.cpp
)

target_compile_features(KerberosEngineTests PRIVATE cxx_std_23)
target_compile_definitions(KerberosEngineTests PRIVATE
    KBR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(KerberosEngineTests PRIVATE
    KerberosEngine
    GTest::gtest_main
)

set_target_properties(KerberosEngineTests PROPERTIES
    FOLDER "Tests"
)

add_custom_command(TARGET KerberosEngineTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Debug/nethost.dll,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Release/nethost.dll>"
        "$<TARGET_FILE_DIR:KerberosEngineTests>/nethost.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:amd_fidelityfx_vk>"
        "$<TARGET_FILE_DIR:KerberosEngineTests>"
    VERBATIM
)

gtest_discover_tests(KerberosEngineTests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DISCOVERY_MODE PRE_TEST
)
