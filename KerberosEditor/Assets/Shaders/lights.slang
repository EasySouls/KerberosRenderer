module lights;

public struct GPULight {
    public float3 color;
    public float intensity;

    public float3 position;
    public float range;

    public float3 direction;
    public uint type;

    public float innerConeCos;
    public float outerConeCos;

    public float width;
    public float height;
    
    public int shadowSlot;
    public float shadowNear;
    public float shadowFar;
    public float _shadowPadding;
};

static uint LightType_Point = 0;
static uint LightType_Spot = 1;
static uint LightType_Area = 2;

public static const uint MaxLights = 512;

float CalculateLightAttenuation(GPULight light, float3 worldPos) 
{
    //float distance = length(light.position - worldPos);
    //if (distance > light.range) {
    //    return 0.0;
    //}
    //return 1.0 - (distance / light.range);

    float distanceSqr = dot(light.position - worldPos, light.position - worldPos);

    // Inverse square falloff
    float attenuation = 1.0 / (distanceSqr + 0.0001);

    // Windowing function to smoothly fade out at light.range
    float distance = sqrt(distanceSqr);
    float range = max(light.range, 0.001);
    float falloff = clamp(1.0 - pow(distance / range, 4.0), 0.0, 1.0);

    // Apply windowing to the inverse-square attenuation
    return attenuation * (falloff * falloff);
}

float CalculateSpotlightAttenuation(GPULight light, float3 worldPos) 
{
    float3 lightToPoint = normalize(worldPos - light.position);
    float cosTheta = dot(light.direction, lightToPoint);
    if (cosTheta < light.outerConeCos) {
        return 0.0;
    } else if (cosTheta > light.innerConeCos) {
        return 1.0;
    } else {
        float epsilon = light.innerConeCos - light.outerConeCos;
        return clamp((cosTheta - light.outerConeCos) / epsilon, 0.0, 1.0);
    }
}

float CalculateLightContribution(GPULight light, float3 worldPos)
{
    float attenuation = 0.0;
    if (light.type == LightType_Spot) {
        attenuation = CalculateSpotlightAttenuation(light, worldPos) *
                      CalculateLightAttenuation(light, worldPos);
    } else {
        attenuation = CalculateLightAttenuation(light, worldPos);
    }

    if (attenuation <= 0.0) {
        return 0.0;
    }
    
    return attenuation * light.intensity;
}

public float3 CalculateLightColor(GPULight light, float3 worldPos) 
{
    float contribution = CalculateLightContribution(light, worldPos);
    return light.color * contribution;
}

public float CalculatePointShadowVisibility(
    SamplerCubeArrayShadow shadowMap,
    GPULight light,
    float3 worldPos,
    float3 N,
    bool widePcf)
{
    if (light.shadowSlot < 0 || light.shadowFar <= light.shadowNear)
        return 1.0;

    const float3 toFrag = worldPos - light.position;
    const float axisDistance = max(max(abs(toFrag.x), abs(toFrag.y)), abs(toFrag.z));

    if (axisDistance >= light.shadowFar)
        return 1.0;

    // TODO: Might be able to hardcode the widht and height, since we use only one size currently
    uint width, height, elements;
    shadowMap.GetDimensions(width, height, elements);
    const float resolution = max((float)width, 1.0);

    // At 90 degrees, a face spans 2 * distance world units.
    const float worldPerTexel = 2.0 * max(axisDistance, light.shadowNear) / resolution;

    const float3 biasedWorldPos = worldPos + N * (worldPerTexel * 1.5);
    const float3 dir = biasedWorldPos - light.position;
    const float maxAxis = max(max(abs(dir.x), abs(dir.y)), abs(dir.z));

    if (maxAxis <= light.shadowNear)
        return 1.0;

    // Matches the GLM zero-to-one perspective used for the CPU cube-face matrix.
    float depth =
        (light.shadowFar * (maxAxis - light.shadowNear)) /
        ((light.shadowFar - light.shadowNear) * maxAxis);
    const float reference = saturate(depth - 0.00002);

    const float arrayIndex = (float)light.shadowSlot;
    float shadow;

    if (widePcf)
    {
        const float3 up = abs(dir.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        const float3 tangent = normalize(cross(up, dir));
        const float3 bitangent = cross(dir, tangent);
        const float texelAngle = 1.57079632679 / resolution;
        const float offsetScale = texelAngle * maxAxis;

        shadow = 0.0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                const float3 offsetDir = dir + tangent * (x * offsetScale) + bitangent * (y * offsetScale);
                shadow += shadowMap.SampleCmp(float4(offsetDir, arrayIndex), reference);
            }
        }
        shadow /= 9.0;
    }
    else
    {
        shadow = shadowMap.SampleCmp(float4(dir, arrayIndex), reference);
    }

    // Sampler uses GreaterOrEqual + white border, matching the CSM path,
    // a comparison result of 1 means the fragment is occluded.
    return 1.0 - shadow;
}