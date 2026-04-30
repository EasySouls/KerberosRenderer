[vk::binding(0, 0)] Texture2D texResolvedHDR;
[vk::binding(1, 0)] SamplerState pointSampler;

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID: SV_VertexID)
{
    VSOutput output;

    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);

    return output;
}

// ACES filmic tonemapping curve
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

float3 LinearToSRGB(float3 color) {
    return pow(color, float3(1.0f / 2.2f));
}

[shader("fragment")]
float4 fragmentMain(float2 uv : TEXCOORD) : SV_Target 
{
    float3 hdrColor = texResolvedHDR.Sample(pointSampler, uv).rgb;
    
    // Tonemap: Compress [0 -> Infinity] into [0 -> 1]
    float3 ldrColor = ACESFilm(hdrColor);
    
    // Gamma Correction: Convert Linear to sRGB
    float3 finalColor = LinearToSRGB(ldrColor);
    
    return float4(finalColor, 1.0f);
}