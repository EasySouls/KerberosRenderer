#pragma once

#include "Core/Core.hpp"
#include "Core/UUID.hpp"

#include <string_view>

namespace Kerberos
{
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
		Animation
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

	static constexpr std::string_view AssetTypeToString(const AssetType type)
	{
		switch (type)
		{
			case AssetType::Texture2D:
				return "Texture2D";
			case AssetType::TextureCube:
				return "TextureCube";
			case AssetType::Material:
				return "Material";
			case AssetType::Mesh:
				return "Mesh";
			case AssetType::Scene:
				return "Scene";
			case AssetType::Sound:
				return "Sound";
			case AssetType::Model:
				return "Model";
			case AssetType::Prefab:
				return "Prefab";
			case AssetType::Animation:
				return "Animation";
		}

		KBR_CORE_ASSERT(false, "Unknown Asset Type!");
		return "";
	}

	static constexpr AssetType AssetTypeFromString(const std::string_view str)
	{
		if (str == "Texture2D")
			return AssetType::Texture2D;
		if (str == "TextureCube")
			return AssetType::TextureCube;
		if (str == "Material")
			return AssetType::Material;
		if (str == "Mesh")
			return AssetType::Mesh;
		if (str == "Scene")
			return AssetType::Scene;
		if (str == "Sound")
			return AssetType::Sound;
		if (str == "Model")
			return AssetType::Model;
		if (str == "Prefab")
			return AssetType::Prefab;
		if (str == "Animation")
			return AssetType::Animation;

		KBR_CORE_ASSERT(false, "Unknown Asset Type as string!");
		return AssetType::Texture2D;
	}
}
