#pragma once

#if defined(KBR_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#define KBR_TRACY_FRAME_MARK() FrameMark
#define KBR_TRACY_FUNCTION() ZoneScoped
#define KBR_TRACY_SCOPE(name) ZoneScopedN(name)
#define KBR_TRACY_PLOT(name, value) TracyPlot(name, value)

#define KBR_TRACY_MESSAGE_LITERAL(text) TracyMessageL(text)
#define KBR_TRACY_MESSAGE(text)                                                 \
    do {                                                                        \
        const std::string_view kbrTracyMsgView{ (text) };                       \
        TracyMessage(kbrTracyMsgView.data(), kbrTracyMsgView.size());           \
    } while (false)
#define KBR_TRACY_MESSAGE_COLOR(text, color)                                    \
    do {                                                                        \
        const std::string_view kbrTracyMsgView{ (text) };                       \
        TracyMessageC(kbrTracyMsgView.data(), kbrTracyMsgView.size(), (color)); \
    } while (false)

#define KBR_TRACY_GPU_ZONE(ctx, cmd, name) TracyVkZone(ctx, cmd, name)
#define KBR_TRACY_GPU_COLLECT(ctx, cmd)    TracyVkCollect(ctx, cmd)

#else

#define KBR_TRACY_FRAME_MARK() do { } while (false)
#define KBR_TRACY_FUNCTION() do { } while (false)
#define KBR_TRACY_SCOPE(name) do { } while (false)
#define KBR_TRACY_PLOT(name, value) do { } while (false)

#define KBR_TRACY_MESSAGE_LITERAL(text) do { } while (false)
#define KBR_TRACY_MESSAGE(text) do { } while (false)
#define KBR_TRACY_MESSAGE_COLOR(text, color) do { } while (false)

#define KBR_TRACY_GPU_ZONE(ctx, cmd, name) do { } while (false)
#define KBR_TRACY_GPU_COLLECT(ctx, cmd)    do { } while (false)

#endif
