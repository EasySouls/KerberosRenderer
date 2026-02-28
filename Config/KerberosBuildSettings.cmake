add_library(KerberosBuildSettings INTERFACE)

# C++ standard
target_compile_features(KerberosBuildSettings INTERFACE cxx_std_23)

# Warnings
target_compile_options(KerberosBuildSettings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /MP /permissive->
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)

# Runtime selection (MSVC)
if(MSVC)
    target_compile_options(KerberosBuildSettings INTERFACE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
    )
endif()

# Definitions per config
target_compile_definitions(KerberosBuildSettings INTERFACE
    $<$<CONFIG:Debug>:KBR_DEBUG>
    $<$<CONFIG:Release>:KBR_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:KBR_RELEASE>
)