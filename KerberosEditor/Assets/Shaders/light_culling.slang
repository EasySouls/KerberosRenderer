import common;
import lights;
import light_clusters;

[[vk::binding(0, 0)]] ConstantBuffer<SceneData>   scene;
[[vk::binding(1, 0)]] StructuredBuffer<GPULight>  lights;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint2>   clusterGrid;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>    clusterIndices;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint>    counters;      // counters[0] = bump offset

// Candidates a single tile can hold before lights are dropped for that tile.
// Sized to the full light buffer so tile screening never loses a light; the
// per-cluster cap from ClusterParams is what bounds each cluster's list.
static const uint kMaxCandidatesPerTile = MaxLights;
static const uint kGroupThreads         = 16 * 16;
static const uint kMaxSlices            = 64;   // must match gCountPerSlice

// Workgroup scratch budget: kMaxCandidatesPerTile * 4 B + kMaxSlices * 4 B
// (currently 2 KiB + 256 B). If MaxLights is ever raised far above 512, verify
// this still fits the device's workgroup memory limit.

groupshared uint gCandidateIndices[kMaxCandidatesPerTile];
groupshared uint gCandidateCount;
groupshared uint gCountPerSlice[kMaxSlices];

// Broadcast from thread 0 to the whole workgroup.
groupshared uint gBaseOffset;
groupshared uint gClusterCount;
groupshared uint gWriteCursor;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    ClusterParams clusterParams = MakeClusterParams(scene);

    if (!ClusterEnabled(clusterParams))
        return;

    // Bounded index-list capacity, taken from the binding itself so the C++
    // allocation (LightClusters::ClusterIndexCapacity) stays the only source
    // of truth.
    uint indexCapacity = 0;
    uint indexStride = 0;
    clusterIndices.GetDimensions(indexCapacity, indexStride);

    if (groupThreadID.x == 0 && groupThreadID.y == 0)
    {
        gCandidateCount = 0;
        for (uint s = 0; s < min(clusterParams.slices, kMaxSlices); ++s)
            gCountPerSlice[s] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint flatThread = groupThreadID.y * 16 + groupThreadID.x; // 0..255
    const float2 screen = scene.viewportSize;
    const float2 tileMin = float2(groupID.xy) * kClusterTileSize;
    const float2 tileMax = tileMin + kClusterTileSize;

    // Screen-space candidates for this tile
    for (uint li = flatThread; li < clusterParams.lightCount; li += kGroupThreads)
    {
        GPULight light = lights[li];

        const float3 vc = mul(scene.view, float4(light.position, 1.0)).xyz;
        const float  centerZ = -vc.z;
        const float  r = max(light.range, 1e-3);

        // Entirely behind the near plane: cannot touch any cluster.
        if (centerZ + r <= clusterParams.nearZ)
            continue;

        // The sphere crosses the near plane, so its projection is unbounded.
        // Treat it as covering the whole screen. 
        // The depth-band test below still limits which slices it enters.
        bool  fullScreen = (centerZ - r <= clusterParams.nearZ);
        float2 discMin = float2(0.0, 0.0);
        float2 discMax = screen;
        if (!fullScreen)
        {
            const float2 scale = float2(scene.projection[0][0] * 0.5 * screen.x,
                                        scene.projection[1][1] * 0.5 * screen.y);
            const float  rPx   = r * max(abs(scale.x), abs(scale.y)) / centerZ;
            const float4 clip  = mul(scene.projection, float4(vc, 1.0));
            const float2 ndc   = clip.xy / clip.w;
            const float2 ctrPx = (ndc * 0.5 + 0.5) * screen;
            discMin = ctrPx - rPx;
            discMax = ctrPx + rPx;
        }

        // A light is kept whenever its screen disc touches the tile
        if (discMin.x > tileMax.x || discMax.x < tileMin.x ||
            discMin.y > tileMax.y || discMax.y < tileMin.y)
            continue;

        uint slot;
        InterlockedAdd(gCandidateCount, 1, slot);
        if (slot < kMaxCandidatesPerTile)
            gCandidateIndices[slot] = li;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint candidateCount = min(gCandidateCount, kMaxCandidatesPerTile);
    const uint tileIndex = groupID.y * clusterParams.tilesX + groupID.x;
    const uint sliceStride = clusterParams.tilesX * clusterParams.tilesY;
    const uint maxPerCluster = (uint)clusterParams.maxPerCluster;

    // Per depth slice: count, reserve, fill
    for (uint slice = 0; slice < min(clusterParams.slices, kMaxSlices); ++slice)
    {
        if (flatThread == 0)
        {
            gCountPerSlice[slice] = 0;
            gWriteCursor = 0;
        }
        GroupMemoryBarrierWithGroupSync();

        // Count the candidates whose depth extent covers this slice.
        for (uint c = flatThread; c < candidateCount; c += kGroupThreads)
        {
            const GPULight light = lights[gCandidateIndices[c]];
            const float zc = -mul(scene.view, float4(light.position, 1.0)).z;
            const float r  = max(light.range, 1e-3);
            const uint  k0 = ClusterSliceFromViewDepth(clusterParams, zc - r);
            const uint  k1 = ClusterSliceFromViewDepth(clusterParams, zc + r);
            if (slice >= k0 && slice <= k1)
                InterlockedAdd(gCountPerSlice[slice], 1);
        }
        GroupMemoryBarrierWithGroupSync();

        // Thread 0 reserves a contiguous range in the global index list and
        // publishes the cluster entry. Every cluster gets an entry, including
        // empty ones, so the fragment side never reads stale data.
        if (flatThread == 0)
        {
            uint count = min(gCountPerSlice[slice], maxPerCluster);

            uint start;
            InterlockedAdd(counters[0], count, start);

            if (start + count > indexCapacity)
                count = (start < indexCapacity) ? (indexCapacity - start) : 0;

            gBaseOffset  = (start < indexCapacity) ? start : 0;
            gClusterCount = count;

            clusterGrid[tileIndex + slice * sliceStride] = uint2(gBaseOffset, gClusterCount);
        }
        GroupMemoryBarrierWithGroupSync();

        if (gClusterCount == 0)
            continue;

        // Fill the reserved range. The write cursor must be workgroup-wide,
        // otherwise every thread would write at the same slots.
        for (uint c = flatThread; c < candidateCount; c += kGroupThreads)
        {
            const GPULight light = lights[gCandidateIndices[c]];
            const float zc = -mul(scene.view, float4(light.position, 1.0)).z;
            const float r  = max(light.range, 1e-3);
            const uint  k0 = ClusterSliceFromViewDepth(clusterParams, zc - r);
            const uint  k1 = ClusterSliceFromViewDepth(clusterParams, zc + r);
            if (slice >= k0 && slice <= k1)
            {
                uint at;
                InterlockedAdd(gWriteCursor, 1, at);
                if (at < gClusterCount)
                    clusterIndices[gBaseOffset + at] = gCandidateIndices[c];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
