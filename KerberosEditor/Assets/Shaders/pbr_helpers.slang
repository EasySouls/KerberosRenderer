module pbr_helpers;

static const float PI = 3.14159265359;

float D_GGX(float dotNH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return (alpha2) / (PI * denom * denom);
}

float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

float3 F_Schlick(float cosTheta, float3 F0) { 
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

public float3 F_SchlickR(float cosTheta, float3 F0, float roughness) { 
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(1.0 - cosTheta, 5.0); 
}

public struct LightResult {
    public float3 diffuse;
    public float3 specular;
};

public LightResult GetDirectLight(float3 L, float3 V, float3 N, float3 F0, float3 albedo, float metallic, float roughness) {
    LightResult result;
    result.diffuse = float3(0.0);
    result.specular = float3(0.0);

    float3 H = normalize(V + L);
    float dotNH = clamp(dot(N, H), 0.0, 1.0);
    float dotNV = clamp(dot(N, V), 0.0, 1.0);
    float dotNL = clamp(dot(N, L), 0.0, 1.0);
    float dotHV = clamp(dot(H, V), 0.0, 1.0);

    if (dotNL > 0.0) {
        float D = D_GGX(dotNH, roughness);
        float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
        float3 F = F_Schlick(dotHV, F0);

        float3 spec = D * F * G / (4.0 * dotNL * dotNV + 0.001);
        float3 kD = (float3(1.0) - F) * (1.0 - metallic);

        result.diffuse = (kD * albedo / PI) * dotNL;
        result.specular = spec * dotNL;
    }
    return result;
}