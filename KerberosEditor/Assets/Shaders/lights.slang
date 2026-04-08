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
};

static uint LightType_Point = 0;
static uint LightType_Spot = 1;
static uint LightType_Area = 2;

float CalculateLightAttenuation(GPULight light, float3 worldPos) {
    float distance = length(light.position - worldPos);
    if (distance > light.range) {
        return 0.0;
    }
    return 1.0 - (distance / light.range);
}

float CalculateSpotlightAttenuation(GPULight light, float3 worldPos) {
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

float CalculateLightContribution(GPULight light, float3 worldPos) {
    float attenuation = CalculateLightAttenuation(light, worldPos);
    if (attenuation <= 0.0) {
        return 0.0;
    }
    if (light.type == LightType_Spot) {
        attenuation *= CalculateSpotlightAttenuation(light, worldPos);
    }
    return attenuation * light.intensity;
}

public float3 CalculateLightColor(GPULight light, float3 worldPos) {
    float contribution = CalculateLightContribution(light, worldPos);
    return light.color * contribution;
}