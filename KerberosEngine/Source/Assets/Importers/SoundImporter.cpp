#include "kbrpch.hpp"
#include "SoundImporter.hpp"

#include "Application.hpp"
#include "Audio/AudioManager.hpp"

namespace Kerberos
{
	Ref<Sound> SoundImporter::ImportSound(AssetHandle, const AssetMetadata& metadata)
	{
		return ImportSound(metadata.Filepath);
	}

	Ref<Sound> SoundImporter::ImportSound(const std::filesystem::path& filepath)
	{
		return Application::Get().GetAudioManager()->Load(filepath);
	}
}