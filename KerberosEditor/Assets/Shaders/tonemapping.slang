module tonemapping;

// From http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 Uncharted2Tonemap(float3 color)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float3 ACESFilmicTonemap(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 ReinhardTonemap(float3 color)
{
    return color / (color + 1.0f);
}

public float3 ApplyGammaCorrection(float3 color, float gamma)
{
    return pow(color, (1.0f / gamma).xxx);
}

const static float TonemapUncharted = 0;
const static float TonemapReinhard = 1;
const static float TonemapACES = 2;

public float3 ApplyToneMapping(float3 color, float exposure, float tonemapOperator)
{
    float3 tonemapped = color;
    if (tonemapOperator == TonemapUncharted) {
        tonemapped = Uncharted2Tonemap(color * exposure);
        tonemapped = tonemapped * (1.0f / Uncharted2Tonemap((11.2f).xxx));
    }
    else if (tonemapOperator == TonemapReinhard) {
        tonemapped = ReinhardTonemap(color * exposure);
    }
    else if (tonemapOperator == TonemapACES) {
        tonemapped = ACESFilmicTonemap(color * exposure);
    }
    return tonemapped;
}