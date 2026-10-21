#pragma once

#if defined(KBR_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define KBR_TRACY_FRAME_MARK() FrameMark
#define KBR_TRACY_FUNCTION() ZoneScoped
#define KBR_TRACY_SCOPE(name) ZoneScopedN(name)
#define KBR_TRACY_PLOT(name, value) TracyPlot(name, value)
#else
#define KBR_TRACY_FRAME_MARK() do { } while (false)
#define KBR_TRACY_FUNCTION() do { } while (false)
#define KBR_TRACY_SCOPE(name) do { } while (false)
#define KBR_TRACY_PLOT(name, value) do { } while (false)
#endif
