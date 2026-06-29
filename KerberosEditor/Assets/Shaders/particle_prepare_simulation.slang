import common;

[[vk::binding(3, 0)]] 
RWStructuredBuffer<uint> counters; // [0] = deadCount, [1] = aliveCount

[[vk::binding(4, 0)]] 
RWStructuredBuffer<DrawIndirectCommand> indirectBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computePrepareSimulate() {
    // Write last frame's alive count to the indirect draw buffer
    indirectBuffer[0].vertexCount   = 6; // Drawing 1 quad (2 triangles) per instance
    indirectBuffer[0].instanceCount = 1000;
    //indirectBuffer[0].instanceCount = counters[1];
    indirectBuffer[0].firstInstance = 0;
    indirectBuffer[0].firstVertex   = 0;

    // Reset alive count to 0 so the upcoming simulate pass can populate it
    counters[1] = 0;
}