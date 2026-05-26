import common;

// ============================================================================
// FXAA 3.11 Shader
// ============================================================================

// The input texture MUST have Pre-tonemapped LDR RGB color, and Luma in the Alpha channel.
// The Sampler MUST be VK_FILTER_LINEAR with VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE.
[[vk::binding(0, 0)]] Texture2D<float4> sceneColorTex;
[[vk::binding(1, 0)]] SamplerState linearSampler;

struct PushConstants {
    float2 inverseScreenSize;
};
[[vk::push_constant]] 
PushConstants pc;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    
    return output;
}

// Helper function to get pre-calculated Luma from alpha channel
float SampleLuma(float2 uv)
{
    return sceneColorTex.SampleLevel(linearSampler, uv, 0.0).a;
}

static const float FXAA_EDGE_THRESHOLD_MIN = 0.0833;
static const float FXAA_EDGE_THRESHOLD = 0.166;
static const float FXAA_SUBPIX_CAP = 0.75;
static const int FXAA_ITERATIONS = 12;

// Edge search step sizes
static const float SEARCH_STEPS[12] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };

float3 ApplyFXAA(float2 uv)
{
    float2 texelSize = pc.inverseScreenSize;
    // Sample center and 4 direct neighbors
    float4 colorCenter = sceneColorTex.SampleLevel(linearSampler, uv, 0.0);
    float lumaM = SampleLuma(uv);

    float lumaS = SampleLuma(uv + float2(0.0, 1.0) * texelSize);
    float lumaN = SampleLuma(uv + float2(0.0, -1.0) * texelSize);
    float lumaE = SampleLuma(uv + float2(1.0, 0.0) * texelSize);
    float lumaW = SampleLuma(uv + float2(-1.0, 0.0) * texelSize);
    // Find contrast (max - min luma)
    float maxLuma = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float minLuma = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float contrast = maxLuma - minLuma;
    // Early Exit: If contrast is too low, it's not an edge. Just return the pixel.
    float threshold = max(FXAA_EDGE_THRESHOLD_MIN, maxLuma * FXAA_EDGE_THRESHOLD);
    if (contrast < threshold) {
        return colorCenter.rgb;
    }

    // Sample diagonals to determine edge direction
    float lumaNW = SampleLuma(uv + float2(-1.0, -1.0) * texelSize);
    float lumaNE = SampleLuma(uv + float2(1.0, -1.0) * texelSize);
    float lumaSW = SampleLuma(uv + float2(-1.0, 1.0) * texelSize);
    float lumaSE = SampleLuma(uv + float2(1.0, 1.0) * texelSize);

    // Calculate vertical and horizontal edge presence
    float edgeHorz = 
        abs((0.25 * lumaNW) + (-0.5 * lumaN) + (0.25 * lumaNE)) +
                     abs((0.50 * lumaW) + (-1.0 * lumaM) + (0.50 * lumaE)) +
                     abs((0.25 * lumaSW) + (-0.5 * lumaS) + (0.25 * lumaSE));

    float edgeVert = 
        abs((0.25 * lumaNW) + (-0.5 * lumaW) + (0.25 * lumaSW)) +
                     abs((0.50 * lumaN) + (-1.0 * lumaM) + (0.50 * lumaS)) +
                     abs((0.25 * lumaNE) + (-0.5 * lumaE) + (0.25 * lumaSE));

    bool isHorizontal = (edgeHorz >= edgeVert);

    // Determine step size and direction
    float stepLength = isHorizontal ? texelSize.y : texelSize.x;
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;

    // Find the steepest gradient
    float grad1 = luma1 - lumaM;
    float grad2 = luma2 - lumaM;
    bool isDirection1Steepest = abs(grad1) >= abs(grad2);

    float gradientScaled = 0.25 * max(abs(grad1), abs(grad2));

    // Calculate local average Luma in the correct direction
    float lumaLocalAverage = 0.0;
    if (isDirection1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }

    // Shift UV half a pixel in the direction of the edge
    float2 currentUv = uv;
    if (isHorizontal) currentUv.y += stepLength * 0.5;
    else currentUv.x += stepLength * 0.5;

    // Edge Search - Walk along the edge until the luma changes significantly
    float2 offset = isHorizontal ? float2(texelSize.x, 0.0) : float2(0.0, texelSize.y);
    float2 uv1 = currentUv - offset;
    float2 uv2 = currentUv + offset;

    float lumaEnd1 = SampleLuma(uv1) - lumaLocalAverage;
    float lumaEnd2 = SampleLuma(uv2) - lumaLocalAverage;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    // Search loop
    [unroll]
    for (int i = 2; i < FXAA_ITERATIONS; i++) {
        if (!reached1) {
            lumaEnd1 = SampleLuma(uv1) - lumaLocalAverage;
            if (abs(lumaEnd1) >= gradientScaled) reached1 = true;
            else uv1 -= offset * SEARCH_STEPS[i];
        }
        if (!reached2) {
            lumaEnd2 = SampleLuma(uv2) - lumaLocalAverage;
            if (abs(lumaEnd2) >= gradientScaled) reached2 = true;
            else uv2 += offset * SEARCH_STEPS[i];
        }
        if (reached1 && reached2) break;
    }

    // Distance and Blending
    float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool isDirection1Closest = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeThickness = (distance1 + distance2);

    float pixelOffset = -distanceFinal / edgeThickness + 0.5;

    bool isLumaCenterSmaller = lumaM < lumaLocalAverage;
    bool correctVariation = ((isDirection1Closest ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Sub-pixel aliasing mitigation (shimmering)
    float lumaAverage = (1.0 / 9.0) * (lumaN + lumaS + lumaE + lumaW + lumaNE + lumaNW + lumaSE + lumaSW + lumaM);
    float subPixelOffset1 = clamp(abs(lumaAverage - lumaM) / contrast, 0.0, 1.0);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * FXAA_SUBPIX_CAP;

    finalOffset = max(finalOffset, subPixelOffsetFinal);

    // Final texture sample
    float2 finalUv = uv;
    if (isHorizontal) finalUv.y += finalOffset * stepLength;
    else finalUv.x += finalOffset * stepLength;

    float4 finalColor = sceneColorTex.SampleLevel(linearSampler, finalUv, 0.0);
    return finalColor.rgb;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;
    float3 ldrColor = ApplyFXAA(uv);
    
    return float4(ldrColor, 1.0);
}
