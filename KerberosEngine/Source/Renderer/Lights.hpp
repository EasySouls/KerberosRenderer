#pragma once

#include <glm/glm.hpp>

namespace Kerberos
{
    /// Default physical intensity of a directional light, in lux.
    inline constexpr float DefaultSunIntensityLux = 100000.0f;

    /// Relative HDR environments (such as the bundled Pisa cube) are not
    /// calibrated in physical luminance. This factor maps their linear radiance
    /// into a plausible cd/m^2 range. Calibrated HDR environments should use 1.0.
    inline constexpr float DefaultEnvironmentIntensity = 50000.0f;

    struct DirectionalLight
    {
        alignas(16) glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        alignas(4) float Intensity = DefaultSunIntensityLux; // In lux
        alignas(16) glm::vec3 Color = glm::vec3(1.0f);
        alignas(4) bool IsEnabled = true;
        uint8_t _pad[3];
    };

    struct alignas(16) PointLight
    {
        alignas(16) glm::vec3 Color = glm::vec3(1.0f);
        alignas(4) float Intensity = 500.0f; // In candelas
        alignas(4) float Radius = 50.0f;
    };

    struct alignas(16) SpotLight : PointLight
    {
        alignas(16) glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        alignas(4) float CutOffAngleRadians = glm::radians(12.5f);
        alignas(4) float OuterCutOffAngleRadians = glm::radians(17.5f);
    };

    enum class LightType : uint32_t
    {
        Point = 0,
        Spot = 1,
		Area = 2
	};

	struct alignas(16) GPULight
    {
        alignas(16) glm::vec3 Color;
        alignas(4)  float Intensity;

        alignas(16) glm::vec3 Position;
        alignas(4)  float Range; // Max distance for attenuation/culling

        alignas(16) glm::vec3 Direction;
        alignas(4)  uint32_t Type; 

        // Spot Light specifics
        alignas(4)  float InnerConeCos;
        alignas(4)  float OuterConeCos;

        // Area Light specifics
        alignas(4)  float Width;
        alignas(4)  float Height;
	    
	    alignas(4)  int32_t ShadowSlot = -1;
	    alignas(4)  float ShadowNear = 0.05f;
	    alignas(4)  float ShadowFar = 0.0f;
	    alignas(4)  uint32_t ShadowCasterEntityID = 0;
    };
}