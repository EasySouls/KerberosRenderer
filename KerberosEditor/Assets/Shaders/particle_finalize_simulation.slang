import common;

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> counters; // [1] = active, [2] = next active, [3] = list index

[[vk::binding(2, 1)]]
RWStructuredBuffer<uint> indirectBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeFinalizeSimulate()
{
    counters[1] = counters[2];
    counters[3] ^= 1;

    indirectBuffer[4] = 6;
    indirectBuffer[5] = counters[1];
    indirectBuffer[6] = 0;
    indirectBuffer[7] = 0;
}
