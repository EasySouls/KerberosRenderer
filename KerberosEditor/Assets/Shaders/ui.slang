// Game UI shader.
//
// One pipeline draws every UI element: solid/gradient rounded rectangles, image
// quads and MSDF text. Authored colors arrive already in display-referred space
// (the UI pass renders into the post-tonemapping FinalImage) and are written
// without conversion. Image textures are sampled through sRGB views, so their
// linear texels are re-encoded before they are blended.
//
// Text uses the MTSDF atlas: the RGB channels carry the multi-channel distance
// field and A carries a true SDF. Median-of-three with a screen-space derivative
// ramp gives resolution-independent antialiasing.

import tonemapping;

struct UIPushConstants {
    float2 screenSize;
};

[[vk::push_constant]]
UIPushConstants pc;

[[vk::binding(0, 0)]]
Texture2D<float4> globalTextures[];
[[vk::binding(1, 0)]]
SamplerState globalSamplers[];

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float2 localUV : TEXCOORD1;
    float4 style : TEXCOORD2;   // xy = half size, z = corner radius, w = border width
    uint textureIndex : TEXCOORD3;
    uint mode : TEXCOORD4;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float2 localUV : TEXCOORD1;
    float4 style : TEXCOORD2;
    nointerpolation uint textureIndex : TEXCOORD3;
    nointerpolation uint mode : TEXCOORD4;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float2 ndc = input.position / pc.screenSize * 2.0 - 1.0;

    output.position = float4(ndc.x, -ndc.y, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.localUV = input.localUV;
    output.style = input.style;
    output.textureIndex = input.textureIndex;
    output.mode = input.mode;

    return output;
}

float4 SampleBindless(uint packedIndex, float2 uv)
{
    uint texIdx = packedIndex & 0x00FFFFFF;
    uint sampIdx = packedIndex >> 24;

    if (texIdx == 0)
        return float4(1.0, 0.0, 1.0, 1.0);

    return globalTextures[texIdx].Sample(globalSamplers[sampIdx], uv);
}

float Median3(float a, float b, float c)
{
    return max(min(a, b), min(max(a, b), c));
}

float RoundedBoxDistance(float2 p, float2 halfSize, float radius)
{
    float2 q = abs(p) - halfSize + radius;
    return length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    if (input.mode == 2) // image
    {
        // Color textures decode to linear in hardware, but FinalImage is
        // display-referred, so re-encode. Authored colors are already display
        // space and are never converted.
        const float4 sampled = SampleBindless(input.textureIndex, input.uv);
        return float4(LinearToSrgb(sampled.rgb), sampled.a) * input.color;
    }

    if (input.mode == 1) // MSDF/MTSDF text
    {
        float4 distanceSample = SampleBindless(input.textureIndex, input.uv);
        float sd = Median3(distanceSample.r, distanceSample.g, distanceSample.b);
        float width = max(fwidth(sd) * 0.5, 1e-4);
        float alpha = smoothstep(0.5 - width, 0.5 + width, sd);
        return float4(input.color.rgb, input.color.a * alpha);
    }

    // Rounded rectangle or its border ring. The distance field is in pixels and
    // the radius must not exceed the shorter half extent, otherwise the shape
    // folds inside out.
    const float2 halfSize = input.style.xy;
    const float radius = clamp(input.style.z, 0.0, min(halfSize.x, halfSize.y));
    float2 p = (input.localUV - 0.5) * 2.0 * halfSize;
    float sd = RoundedBoxDistance(p, halfSize, radius);
    float aa = max(fwidth(sd) * 0.5, 1e-4);

    // smoothstep requires edge0 < edge1, so we use the decreasing form explicitly.
    float outer = 1.0 - smoothstep(-aa, aa, sd);
    float alpha = outer;

    if (input.mode == 3 && input.style.w > 0.0) // border ring
    {
        const float borderWidth = min(input.style.w, min(halfSize.x, halfSize.y));
        float inner = 1.0 - smoothstep(-aa, aa, sd + borderWidth);
        alpha = max(outer - inner, 0.0);
    }

    return float4(input.color.rgb, input.color.a * alpha);
}
