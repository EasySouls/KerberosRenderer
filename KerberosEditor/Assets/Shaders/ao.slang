module ao;

public float3 GTAOMultiBounce(float visibility, float3 albedo)
{
    float3 a = 2.0 * albedo - 0.33;
    float3 b = -4.8 * albedo + 0.64;
    float3 c = 2.75 * albedo + 0.69;
    return max(visibility.xxx, ((visibility * a + b) * visibility + c) * visibility);
}

public float GetSpecularOcclusion(float NoV, float ao, float roughness)
{
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}