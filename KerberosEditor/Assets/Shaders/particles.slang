module particles;

public struct Particle {
    public float3 position;
    public float size;
    public float3 velocity;
    public float life;

    public float4 color;

    public float3 acceleration;
    public float maxLife;
    public float4 startColor;
    public float4 endColor;
    public float startSize;
    public float endSize;
}

public struct Counters {
    public Atomic<uint> aliveCount;
    public Atomic<uint> deadCount;
    public Atomic<uint> spawnRequestCount;
};

public struct SpawnRequest {
    public float3 emitterPosition;
    public uint spawnCount;

    public float minLife;
    public float maxLife;
    public float3 minVelocity;
    public float3 maxVelocity;

    public float3 minAcceleration;
    public float3 maxAcceleration;
    public float4 startColor;
    public float4 endColor;

    public float startSize;
    public float endSize;
}