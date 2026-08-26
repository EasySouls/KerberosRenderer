include_guard(GLOBAL)

add_library(KerberosBuildSettings INTERFACE)

target_compile_features(KerberosBuildSettings INTERFACE cxx_std_23)

target_compile_options(KerberosBuildSettings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/MP>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:GNU>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall>
    $<$<CXX_COMPILER_ID:Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wpedantic>
)

target_compile_definitions(KerberosBuildSettings INTERFACE
    $<$<CONFIG:Debug>:KBR_DEBUG>
    $<$<CONFIG:Release>:KBR_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:KBR_RELEASE>
    $<$<CONFIG:Dist>:KBR_DIST>
)

if(KBR_ENABLE_ASAN)
    target_compile_options(KerberosBuildSettings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=address>
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fno-omit-frame-pointer>
    )
    target_link_options(KerberosBuildSettings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=address>
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fno-omit-frame-pointer>
    )
endif()

if(KBR_ENABLE_FUZZING)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(KerberosBuildSettings INTERFACE -fsanitize=fuzzer-no-link)
        target_link_options(KerberosBuildSettings INTERFACE -fsanitize=fuzzer-no-link)
    else()
        message(WARNING "KBR_ENABLE_FUZZING is ON, but fuzzing instrumentation is only supported with Clang.")
    endif()
endif()
