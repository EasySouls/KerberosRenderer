#pragma once

#include "Core/Core.hpp"
#include "Core/UUID.hpp"

#include <string_view>

namespace Kerberos {

using AssetHandle = UUID;

enum class AssetType : uint8_t
{
	Texture2D = 0,
	TextureCube,
	Material,
	Mesh,
	Scene,
	Sound,
	Model,
	Prefab,
	Animation,
	Skin
};

class Asset
{
public:
	virtual ~Asset() = default;

	virtual AssetType GetType() = 0;

	AssetHandle GetHandle() const { return m_Handle; }
	AssetHandle& GetHandle() { return m_Handle; }

protected:
	/**
	* Automatically generated UUID
	*/
	AssetHandle m_Handle{};
};

AssetType AssetTypeFromString(std::string_view typeStr);
std::string_view AssetTypeToString(AssetType type);

}
