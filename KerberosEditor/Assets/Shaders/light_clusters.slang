module light_clusters;

import common;
import lights;

public static const uint kClusterTileSize      = 16;
public static const uint kClusterTileShift     = 4; /// log2(kClusterTileSize)

/// Per-cluster light cap. Must match LightClusters::MaxLightsPerCluster in
/// Renderer.cpp. Lights beyond the cap are dropped from that cluster.
public static const uint kMaxLightsPerCluster  = 256;

public struct ClusterParams
{
    public uint tilesX;
    public uint tilesY;
    public uint slices;
    public uint lightCount;
    public float nearZ;
    public float farZ;
    public float sliceScale;
    public float maxPerCluster;
};

public ClusterParams MakeClusterParams(SceneData scene)
{
    ClusterParams p;
    p.tilesX        = scene.clusterGridSize.x;
    p.tilesY        = scene.clusterGridSize.y;
    p.slices        = scene.clusterGridSize.z;
    p.lightCount    = scene.clusterGridSize.w;
    p.nearZ         = scene.clusterDepthParams.x;
    p.farZ          = scene.clusterDepthParams.y;
    p.sliceScale    = scene.clusterDepthParams.z;   // slices / log(far/near)
    p.maxPerCluster = scene.clusterDepthParams.w;
    return p;
}

/// slices == 0 is the "clustered lighting disabled" flag written by the CPU.
/// Shaders must fall back to the full light loop in that case.
public bool ClusterEnabled(ClusterParams p)
{
    return p.slices > 0 && p.tilesX > 0 && p.tilesY > 0;
}

/// View-space depth (positive in front of the camera) to depth slice index.
/// Must stay identical to the mapping used by light_culling.slang. 
/// The CPU computes sliceScale from the same sanitized near/far values.
public uint ClusterSliceFromViewDepth(ClusterParams p, float viewDepth)
{
    if (p.slices == 0)
        return 0;

    float nearZ = max(p.nearZ, 1e-4);
    float farZ  = max(p.farZ, nearZ * 1.001);
    float z     = clamp(viewDepth, nearZ, farZ);
    float s     = log(z / nearZ) * p.sliceScale;
    return min((uint)max(s, 0.0), p.slices - 1);
}

/// Screen-tile index (tile-major, no slice component) from a render-target
/// pixel coordinate. SV_Position.xy is already in render-target pixels, so no
/// Y flip is applied here: the projection's Y flip affects world -> NDC only,
/// while NDC -> framebuffer is fixed by Vulkan.
public uint ClusterTileFromPixel(ClusterParams p, float2 pixelPos)
{
    if (p.tilesX == 0 || p.tilesY == 0)
        return 0;

    const float2 maxPixel = float2(p.tilesX * kClusterTileSize - 1.0, p.tilesY * kClusterTileSize - 1.0);
    const uint2 tile = uint2(clamp(pixelPos, float2(0.0, 0.0), maxPixel)) >> kClusterTileShift;
    return tile.x + tile.y * p.tilesX;
}

/// Cluster index for a fragment. Must match the layout written by
/// light_culling.slang: tileIndex + slice * (tilesX * tilesY).
public uint ClusterIndexFromViewPos(ClusterParams p, float3 viewPos, float2 pixelPos)
{
    const uint tile = ClusterTileFromPixel(p, pixelPos);
    return tile + ClusterSliceFromViewDepth(p, -viewPos.z) * (p.tilesX * p.tilesY);
}

public uint ClusterIndex(ClusterParams p, float4x4 viewMatrix, float3 worldPos, float2 pixelPos)
{
    const float3 viewPos = mul(viewMatrix, float4(worldPos, 1.0)).xyz;
    return ClusterIndexFromViewPos(p, viewPos, pixelPos);
}

/// {offset, count} into the light index list for a cluster.
public uint2 ClusterLightRange(ClusterParams p, StructuredBuffer<uint2> grid, uint cluster)
{
    if (cluster >= p.tilesX * p.tilesY * p.slices)
        return uint2(0, 0);

    return grid[cluster];
}
