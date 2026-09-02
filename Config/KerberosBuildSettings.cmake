include_guard(GLOBAL)

add_library(KerberosBuildSettings INTERFACE)

target_compile_features(KerberosBuildSettings INTERFACE cxx_std_23)

target_compile_options(KerberosBuildSettings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<CXX_COMPILER_ID:MSVC>:/MP>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->

    $<$<CXX_COMPILER_ID:MSVC>:/w15038> # C5038: member initialization order
    $<$<CXX_COMPILER_ID:MSVC>:/w14265> # C4265: missing virtual destructor
    $<$<CXX_COMPILER_ID:MSVC>:/w14266> # C4266: hidden virtual functions
    $<$<CXX_COMPILER_ID:MSVC>:/w14062> # C4062: unhandled enum in switch
#    $<$<CXX_COMPILER_ID:MSVC>:/w14625> # C4625: copy ctor was implicitly defined as deleted
#    $<$<CXX_COMPILER_ID:MSVC>:/w14626> # C4626: assignment operator was implicitly defined as deleted
    $<$<CXX_COMPILER_ID:MSVC>:/w15266> # C5266: 'const' qualifier on return type has no effect

    $<$<CXX_COMPILER_ID:MSVC>:/wd4068> # C4068: unknown pragma

    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:GNU>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Werror>
    $<$<CXX_COMPILER_ID:GNU>:-Wpedantic>

    $<$<CXX_COMPILER_ID:Clang>:-Wall>
    $<$<CXX_COMPILER_ID:Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Werror>
)

target_compile_definitions(KerberosBuildSettings INTERFACE
    # Keep MSVC's debug preprocessor environment identical for module producers
    # and consumers. Some third-party CMake code adds _DEBUG to engine flags.
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:_DEBUG>
    $<$<CONFIG:Debug>:KBR_DEBUG>
    $<$<CONFIG:Release>:KBR_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:KBR_RELEASE>
    $<$<CONFIG:Dist>:KBR_DIST>
)

if(MSVC)
    add_compile_options(/FC)
    add_compile_options(/EHsc)
endif()

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
