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

float3 ACESFittedTonemap(float3 color, float exposure, float3 white)
{
    const float A = 0.0245786f;
    const float B = 0.000090537f;
    const float C = 0.983729f;
    const float D = 0.432951f;
    const float E = 0.238081f;

    const float3x3 rgb_to_rrt = float3x3(
		float3(0.59719f, 0.35458f, 0.04823f),
		float3(0.07600f, 0.90834f, 0.01566f),
        float3(0.02840f, 0.13383f, 0.83777f)
	);

    const float3x3 odt_to_rgb = float3x3(
		float3(1.60475f, -0.53108f, -0.07367f),
		float3(-0.10208f, 1.10813f, -0.00605f),
        float3(-0.00327f, -0.07276f, 1.07602f)
	);

    // Apply exposure
    color *= exposure;
    white *= exposure;

    // ACES input transform
    color = mul(rgb_to_rrt, color);

    // RRT + ODT fit
    float3 color_tonemapped =
		(color * (color + A) - B) /
		(color * (C * color + D) + E);

    // Output transform
    color_tonemapped = mul(odt_to_rgb, color_tonemapped);

    // White normalization

    float3 white_tonemapped =
		(white * (white + A) - B) /
		(white * (C * white + D) + E);

    return color_tonemapped / white_tonemapped;
}

public float3 ApplyGammaCorrection(float3 color, float gamma)
{
    return pow(color, (1.0f / gamma).xxx);
}

const static float TonemapUncharted = 0;
const static float TonemapReinhard = 1;
const static float TonemapACES = 2;
const static float TonemapACESFitted = 3;

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
    else if (tonemapOperator == TonemapACESFitted) {
        tonemapped = ACESFittedTonemap(color, exposure, float3(1.0f, 1.0f, 1.0f));
    }
    return tonemapped;
}