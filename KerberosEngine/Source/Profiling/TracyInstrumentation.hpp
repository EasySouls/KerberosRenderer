#pragma once

#if defined(KBR_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define KBR_TRACY_FRAME_MARK() FrameMark
#define KBR_TRACY_SCOPE(name) ZoneScopedN(name)
#else
#define KBR_TRACY_FRAME_MARK()
#define KBR_TRACY_SCOPE(name)
#endif
