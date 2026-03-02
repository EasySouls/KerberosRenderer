#pragma once

#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Audio/Sound.hpp"

namespace Kerberos
{
	class SoundImporter
	{
	public:
		static Ref<Sound> ImportSound(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Sound> ImportSound(const std::filesystem::path& filepath);
	};
}