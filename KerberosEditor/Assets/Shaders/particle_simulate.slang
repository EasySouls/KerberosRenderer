import particles;
import common;

[vk::binding(0, 0)] RWStructuredBuffer<Particle> particlePool;
[vk::binding(1, 0)] RWStructuredBuffer<uint> deadList;
[vk::binding(2, 0)] RWStructuredBuffer<uint> activeListA;
[vk::binding(3, 0)] RWStructuredBuffer<uint> counters; // [0] = dead, [1] = active, [2] = next active, [3] = list index
[vk::binding(4, 0)] RWStructuredBuffer<uint> activeListB;

[vk::binding(1, 1)] ConstantBuffer<ParticleFrameData> scene;

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 threadId: SV_DispatchThreadID)
{
    uint activeCount = counters[1];
    if (threadId.x >= activeCount)
        return;

    uint poolIndex = counters[3] == 0 ? activeListA[threadId.x] : activeListB[threadId.x];
    Particle p = particlePool[poolIndex];

    // Integrate
    p.life -= scene.deltaTime;
    p.velocity += p.acceleration * scene.deltaTime;
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

        // Add to the next active list for rendering and the next frame.
        uint newActiveIndex;
        InterlockedAdd(counters[2], 1, newActiveIndex);
        if (counters[3] == 0)
            activeListB[newActiveIndex] = poolIndex;
        else
            activeListA[newActiveIndex] = poolIndex;
    }
}