#pragma once

#include "Core/Core.hpp"
#include "Sound.hpp"

#include <filesystem>


namespace Kerberos
{
	class UUID;

	class AudioManager
	{
	public:
		AudioManager() = default;
		virtual ~AudioManager() = default;

		AudioManager(const AudioManager& other) = default;
		AudioManager(AudioManager&& other) noexcept = default;
		AudioManager& operator=(const AudioManager& other) = default;
		AudioManager& operator=(AudioManager&& other) noexcept = default;

		virtual void Init() = 0;
		virtual void Update() = 0;
		virtual void Shutdown() = 0;

		virtual Ref<Sound> Load(const std::filesystem::path& filepath) = 0;
		virtual void Play(const std::filesystem::path& filepath) = 0;
		virtual void Play(const UUID& soundID) = 0;
		virtual void Stop(const UUID& soundID) = 0;

		virtual void IncreaseVolume(const UUID& soundID, float delta) = 0;
		virtual void DecreaseVolume(const UUID& soundID, float delta) = 0;
		virtual void SetVolume(const UUID& soundID, float volume) = 0;
		virtual void ResetVolume(const UUID& soundID) = 0;
		virtual void Mute(const UUID& soundID) = 0;

		static AudioManager* Create();
	};
}