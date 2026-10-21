import common;

[[vk::binding(3, 0)]] 
RWStructuredBuffer<uint> counters; // [1] = activeCount, [2] = next active count

[[vk::binding(2, 1)]] 
RWStructuredBuffer<uint> indirectBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computePrepareSimulate() {
    // The active count includes particles emitted earlier this frame.
    counters[2] = 0;
    indirectBuffer[0] = max(1u, (counters[1] + 255u) / 256u);
    indirectBuffer[1] = 1;
    indirectBuffer[2] = 1;
}