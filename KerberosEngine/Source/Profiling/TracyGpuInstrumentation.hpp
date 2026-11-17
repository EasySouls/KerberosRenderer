#pragma once

// Vulkan-specific Tracy instrumentation. TracyVulkan.hpp requires the Vulkan
// headers to be included first, so keep this header out of the CPU-only
// instrumentation path used by non-renderer code.

#if defined(KBR_ENABLE_TRACY)
#include <vulkan/vulkan.h>
#include <tracy/TracyVulkan.hpp>

namespace Kerberos {

// Vulkan Tracy context owned by VulkanContext (one per graphics queue for now).
// Only valid while the Vulkan context is alive.
TracyVkCtx GetTracyContext();

}

#define KBR_TRACY_CONCAT_IMPL(a, b) a##b
#define KBR_TRACY_CONCAT(a, b) KBR_TRACY_CONCAT_IMPL(a, b)

// GPU zones are scope based: the scope object's destructor writes the closing
// timestamp. 
// Declare one in the same C++ scope as the matching render-pass
// label pair so the zone covers the recorded work instead of collapsing to
// zero duration. 
// KBR_TRACY_GPU_ZONE requires a string literal (Tracy stores a
// static source location), use the TRANSIENT variant for runtime-built names
// such as render-graph pass names or std::format results.
#define KBR_TRACY_GPU_ZONE(ctx, cmd, name) TracyVkZone(ctx, cmd, name)
#define KBR_TRACY_GPU_ZONE_TRANSIENT(ctx, cmd, name) \
    TracyVkZoneTransient(ctx, KBR_TRACY_CONCAT(kbrTracyGpuZone, __LINE__), cmd, name, true)
#define KBR_TRACY_GPU_COLLECT(ctx, cmd) TracyVkCollect(ctx, cmd)

#else

#define KBR_TRACY_GPU_ZONE(ctx, cmd, name) do { } while (false)
#define KBR_TRACY_GPU_ZONE_TRANSIENT(ctx, cmd, name) do { } while (false)
#define KBR_TRACY_GPU_COLLECT(ctx, cmd) do { } while (false)

#endif
