import particles;
import common;

[[vk::binding(0, 0)]] 
ConstantBuffer<SceneData> scene;

[[vk::binding(0, 1)]] RWStructuredBuffer<Particle> particlePool;
[[vk::binding(1, 1)]] RWStructuredBuffer<uint> deadList;
[[vk::binding(2, 1)]] RWStructuredBuffer<uint> aliveList;
[[vk::binding(3, 1)]] RWStructuredBuffer<uint> counters; // [0] = deadCount, [1] = aliveCount
[[vk::binding(4, 1)]] RWStructuredBuffer<DrawIndirectCommand> indirectBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadId: SV_DispatchThreadID)
{
    // We dispatch over the MAXIMUM particle pool budget
    uint poolIndex = threadId.x;

    // Read particle
    Particle p = particlePool[poolIndex];

    // If it was already dead before this frame, skip entirely
    if (p.life <= 0.0) return;

    // Integrate
    p.life -= scene.deltaTime;
    p.velocity += p.acceleration *  scene.deltaTime;
    p.position += p.velocity * scene.deltaTime;

    // Check if it just died
    if (p.life <= 0.0) {
        // Add its index back to the dead list
        uint newDeadIndex;
        InterlockedAdd(counters[0], 1, newDeadIndex);
        deadList[newDeadIndex] = poolIndex;

        // Write the dead state back to memory
        particlePool[poolIndex].life = -1.0;
    }
    else {
        // It's still alive! Interpolate color and size over its lifetime
        float t = 1.0 - (p.life / p.maxLife);
        p.color = lerp(p.startColor, p.endColor, t);
        p.size = lerp(p.startSize, p.endSize, t);

        // Write the updated particle back to memory
        particlePool[poolIndex] = p;

        // Add to the alive list for rendering
        uint newAliveIndex;
        InterlockedAdd(counters[1], 1, newAliveIndex);
        aliveList[newAliveIndex] = poolIndex;
    }
}