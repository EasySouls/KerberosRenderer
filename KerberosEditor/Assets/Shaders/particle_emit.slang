import particles;
import common;

[vk::binding(0, 0)] RWStructuredBuffer<Particle> particlePool;
[vk::binding(1, 0)] RWStructuredBuffer<uint> deadList;
[vk::binding(2, 0)] RWStructuredBuffer<uint> aliveList;
[vk::binding(3, 0)] RWStructuredBuffer<uint> counters; // [0] = deadCount, [1] = aliveCount

[vk::binding(0, 1)] StructuredBuffer<SpawnRequest> spawnRequests;
[vk::binding(1, 1)] ConstantBuffer<ParticleFrameData> scene;
[vk::binding(2, 1)] RWStructuredBuffer<DrawIndirectCommand> indirectBuffer;

struct PushConstants {
    uint spawnRequestCount;
};
[[vk::push_constant]] 
PushConstants pushConstants;

uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(inout uint seed) {
    seed = pcg_hash(seed);
    // Convert to float between 0.0 and 1.0
    return asfloat(0x3f800000u | (seed >> 9)) - 1.0;
}

float3 randomFloat3(inout uint seed, float3 minVal, float3 maxVal) {
    float3 r = float3(randomFloat(seed), randomFloat(seed), randomFloat(seed));
    return lerp(minVal, maxVal, r);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadId: SV_DispatchThreadID)
{
    if (threadId.x >= pushConstants.spawnRequestCount)
        return;

    SpawnRequest req = spawnRequests[threadId.x];

    // Base seed unique to this emitter and current time
    uint seed = pcg_hash(threadId.x ^ asuint(scene.time));

    for (uint i = 0; i < req.spawnCount; i++) {
        // Get a dead particle index
        uint originalDeadCount;
        InterlockedAdd(counters[0], 0xFFFFFFFF /* -1 */, originalDeadCount);

        // If we ran out of dead particles, revert and abort
        if (originalDeadCount <= 0) {
            InterlockedAdd(counters[0], 1);
            break;
        }

        // Fetch the index from the dead list
        uint poolIndex = deadList[originalDeadCount - 1];

        // Initialize the particle using random interpolation
        Particle p;
        p.position = req.emitterPosition;
        p.life = lerp(req.minLife, req.maxLife, randomFloat(seed));
        p.maxLife = p.life;
        p.velocity = randomFloat3(seed, req.minVelocity, req.maxVelocity);
        p.acceleration = randomFloat3(seed, req.minAcceleration, req.maxAcceleration);

        p.startColor = req.startColor;
        p.endColor = req.endColor;
        p.color = req.startColor;

        p.startSize = req.startSize;
        p.endSize = req.endSize;
        p.size = req.startSize;

        // Write back to pool
        particlePool[poolIndex] = p;
    }
}
