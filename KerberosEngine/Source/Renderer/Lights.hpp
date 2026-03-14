#pragma once

#include <glm/glm.hpp>

namespace Kerberos
{
    struct DirectionalLight
    {
        alignas(4) bool IsEnabled = true;
        alignas(16) glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        alignas(16) glm::vec3 Color = glm::vec3(1.0f);
        alignas(4) float Intensity = 1.0f;
    };

    struct alignas(16) PointLight
    {
        /// World-space position
        alignas(16) glm::vec3 Position = glm::vec3(0.0f);

        alignas(16) glm::vec3 Color = glm::vec3(1.0f);
        alignas(4) float Intensity = 1.0f;

        /// Attenuation factors
        alignas(4) float Constant = 1.0f;
        alignas(4) float Linear = 0.09f;
        alignas(4) float Quadratic = 0.032f;
    };

    struct alignas(16) SpotLight : PointLight
    {
        alignas(16) glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        alignas(4) float CutOffAngleRadians = glm::radians(12.5f);
        alignas(4) float OuterCutOffAngleRadians = glm::radians(17.5f);
    };
}