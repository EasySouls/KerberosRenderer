#include "Material.hpp"

import Kerberos;

namespace Kerberos {

void Material::ResolveIndices(TextureManager& textureManager)
{
    const uint32_t albedoTex = AlbedoTexture ? textureManager.GetTextureIndex(AlbedoTexture) : textureManager.GetWhiteTexture();
    KBRAssert(albedoTex != 0, "Albedo texture index is 0!");
    Params.AlbedoIndex = TextureManager::Pack(albedoTex, DefaultSampler::AnisoWrap);
    KBRAssert(Params.AlbedoIndex != 0, "Albedo texture index is 0!");

    const uint32_t normalTex = NormalTexture ? textureManager.GetTextureIndex(NormalTexture) : textureManager.GetDefaultNormalTexture();
    KBRAssert(normalTex != 0, "Normal texture index is 0!");
    Params.NormalIndex = TextureManager::Pack(normalTex, DefaultSampler::LinearWrap);
    KBRAssert(Params.NormalIndex != 0, "Normal texture index is 0!");

    const uint32_t roughnessTex = RoughnessTexture ? textureManager.GetTextureIndex(RoughnessTexture) : textureManager.GetDefaultRoughnessTexture();
    KBRAssert(roughnessTex != 0, "Roughness texture index is 0!");
    Params.RoughnessIndex = TextureManager::Pack(roughnessTex, DefaultSampler::LinearWrap);
    KBRAssert(Params.RoughnessIndex != 0, "Roughness texture index is 0!");

    const uint32_t metallicTex = MetallicTexture ? textureManager.GetTextureIndex(MetallicTexture) : textureManager.GetDefaultMetallicTexture();
    KBRAssert(metallicTex != 0, "Metallic texture index is 0!");
    Params.MetallicIndex = TextureManager::Pack(metallicTex, DefaultSampler::LinearWrap);
    KBRAssert(Params.MetallicIndex != 0, "Metallic texture index is 0!");

    const uint32_t aoTex = AOTexture ? textureManager.GetTextureIndex(AOTexture) : textureManager.GetDefaultAOTexture();
    KBRAssert(aoTex != 0, "AO texture index is 0!");
    Params.AOIndex = TextureManager::Pack(aoTex, DefaultSampler::LinearWrap);
    KBRAssert(Params.AOIndex != 0, "AO texture index is 0!");

    const uint32_t emissiveTex = EmissiveTexture ? textureManager.GetTextureIndex(EmissiveTexture) : textureManager.GetDefaultEmissiveTexture();
    KBRAssert(emissiveTex != 0, "Emissive texture index is 0!");
    Params.EmissiveIndex = TextureManager::Pack(emissiveTex, DefaultSampler::LinearWrap);
    KBRAssert(Params.EmissiveIndex != 0, "Emissive texture index is 0!");

    Params.Emissive = EmissiveColor * EmissiveIntensity;
}

Material::Material(std::string n, const glm::vec4 c, const float r, const float m): Name(std::move(n))
{
    Params.RoughnessFactor = r;
    Params.MetallicFactor = m;
    Params.AlbedoFactor = c;
    Params.Emissive = glm::vec3(0.0f);
}

Material::Material(std::string name,
    const glm::vec4 c,
    const float r,
    const float m,
    const Ref<Texture2D>& albedoTex,
    const Ref<Texture2D>& normalTex): Name(std::move(name)), AlbedoTexture(albedoTex), NormalTexture(normalTex)
{
    Params.RoughnessFactor = r;
    Params.MetallicFactor = m;
    Params.AlbedoFactor = c;
    Params.Emissive = glm::vec3(0.0f);
}

Material::Material(const Material& other): Params(other.Params)
                                           , Name(other.Name)
                                           , EmissiveColor(other.EmissiveColor)
                                           , EmissiveIntensity(other.EmissiveIntensity)
                                           , AlbedoTexture(other.AlbedoTexture)
                                           , NormalTexture(other.NormalTexture)
                                           , RoughnessTexture(other.RoughnessTexture)
                                           , MetallicTexture(other.MetallicTexture)
                                           , AOTexture(other.AOTexture)
                                           , EmissiveTexture(other.EmissiveTexture)
{
}

Material& Material::operator=(const Material& other)
{
    if (this != &other)
    {
        Params = other.Params;
        Name = other.Name;
        EmissiveColor = other.EmissiveColor;
        EmissiveIntensity = other.EmissiveIntensity;
        AlbedoTexture = other.AlbedoTexture;
        NormalTexture = other.NormalTexture;
        MetallicTexture = other.MetallicTexture;
        RoughnessTexture = other.RoughnessTexture;
        AOTexture = other.AOTexture;
        EmissiveTexture = other.EmissiveTexture;
    }
    return *this;
}

bool Material::operator==(const Material& other) const
{
    return Params.AlbedoFactor == other.Params.AlbedoFactor &&
           Params.Emissive == other.Params.Emissive &&
           Params.RoughnessFactor == other.Params.RoughnessFactor &&
           Params.MetallicFactor == other.Params.MetallicFactor &&
           EmissiveColor == other.EmissiveColor &&
           EmissiveIntensity == other.EmissiveIntensity &&
           AlbedoTexture == other.AlbedoTexture &&
           NormalTexture == other.NormalTexture &&
           RoughnessTexture == other.RoughnessTexture &&
           MetallicTexture == other.MetallicTexture &&
           AOTexture == other.AOTexture &&
           EmissiveTexture == other.EmissiveTexture;
}

}