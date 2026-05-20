#pragma once

#include "Audio/AudioManager.hpp"
#include "Audio/Sound.hpp"

#include <miniaudio/miniaudio.h>

#include <unordered_map>
#include <filesystem>


namespace Kerberos
{
	class MiniaudioAudioManager : public AudioManager
	{
	public:
		MiniaudioAudioManager() = default;
		~MiniaudioAudioManager() override;

		MiniaudioAudioManager(const MiniaudioAudioManager& other) = delete;
		MiniaudioAudioManager(MiniaudioAudioManager&& other) noexcept = default;
		MiniaudioAudioManager& operator=(const MiniaudioAudioManager& other) = delete;
		MiniaudioAudioManager& operator=(MiniaudioAudioManager&& other) noexcept = default;

		void Init() override;
		void Update() override;
		void Shutdown() override;

		Ref<Sound> Load(const std::filesystem::path& filepath) override;
		void Play(const std::filesystem::path& filepath) override;
		void Play(const UUID& soundID) override;
		void Stop(const UUID& soundID) override;

		void IncreaseVolume(const UUID& soundID, float delta) override;
		void DecreaseVolume(const UUID& soundID, float delta) override;
		void SetVolume(const UUID& soundID, float volume) override;
		void ResetVolume(const UUID& soundID) override;
		void Mute(const UUID& soundID) override;

	private:
		ma_engine m_Engine;

		// Maps a loaded filepath to the sound's UUID
		std::unordered_map<std::filesystem::path, UUID> m_FilepathToUUID;

		// Active playing sounds mapped by UUID
		std::unordered_map<UUID, ma_sound*> m_PlayingSounds;
	};
}
