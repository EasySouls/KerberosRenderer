include(GoogleTest)

add_executable(KerberosEngineTests
    ProjectLoadTests.cpp
)

add_executable(KerberosIntegrationTests
    TestApplication.cpp
    ProjectIntegrationTests.cpp
)

add_executable(KerberosEditorE2ETests
    TestApplication.cpp
    EditorEndToEndTests.cpp
)

target_compile_features(KerberosEngineTests PRIVATE cxx_std_23)
target_compile_definitions(KerberosEngineTests PRIVATE
    KBR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set(KBR_TEST_TARGETS
    KerberosEngineTests
    KerberosIntegrationTests
    KerberosEditorE2ETests
)

target_compile_features(KerberosIntegrationTests PRIVATE cxx_std_23)
target_compile_features(KerberosEditorE2ETests PRIVATE cxx_std_23)

target_compile_definitions(KerberosIntegrationTests PRIVATE
    KBR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_definitions(KerberosEditorE2ETests PRIVATE
    KBR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(KerberosEngineTests PRIVATE
    KerberosEngine
    GTest::gtest_main
)

target_link_libraries(KerberosIntegrationTests PRIVATE
    KerberosEngine
    GTest::gtest_main
)

target_link_libraries(KerberosEditorE2ETests PRIVATE
    KerberosEngine
    GTest::gtest_main
)

set_target_properties(${KBR_TEST_TARGETS} PROPERTIES
    FOLDER "Tests"
)

foreach(test_target IN LISTS KBR_TEST_TARGETS)
    add_custom_command(TARGET ${test_target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Debug/nethost.dll,${CMAKE_SOURCE_DIR}/KerberosEngine/ThirdParty/dotnet/lib/Release/nethost.dll>"
        "$<TARGET_FILE_DIR:${test_target}>/nethost.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:amd_fidelityfx_vk>"
        "$<TARGET_FILE_DIR:${test_target}>"
    VERBATIM)
endforeach()

gtest_discover_tests(KerberosEngineTests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(KerberosIntegrationTests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(KerberosEditorE2ETests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DISCOVERY_MODE PRE_TEST
)
