#pragma once

#include "Assets/Asset.hpp"

#include <glm/mat4.hpp>

#include <vector>
#include <string>

namespace Kerberos
{

struct Skin : public Asset
{
    std::vector<glm::mat4> InverseBindMatrices;
    std::vector<std::string> JointNames;

    AssetType GetType() override
    {
        return AssetType::Skin;
    }
};

}