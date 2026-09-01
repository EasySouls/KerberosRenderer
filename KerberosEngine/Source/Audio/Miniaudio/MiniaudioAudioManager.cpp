#include "MiniaudioAudioManager.hpp"
#include "Application.hpp"

#include <memory>
#include <ranges>

import Kerberos;

namespace Kerberos
{
	MiniaudioAudioManager::~MiniaudioAudioManager()
	{
		MiniaudioAudioManager::Shutdown();
	}

	void MiniaudioAudioManager::Init()
	{
		if (ma_engine_init(nullptr, &m_Engine) != MA_SUCCESS)
		{
			Log::CoreError("Failed to initialize miniaudio engine");
			throw std::runtime_error("Failed to initialize miniaudio engine");
		}
		Log::CoreInfo("MiniaudioAudioManager initialized successfully.");
	}

	void MiniaudioAudioManager::Update()
	{
		// Cleanup finished sounds
		for (auto it = m_PlayingSounds.begin(); it != m_PlayingSounds.end(); )
		{
			ma_sound* sound = it->second;
			if (!ma_sound_is_playing(sound))
			{
				ma_sound_stop(sound);
				ma_sound_uninit(sound);
				Log::CoreTrace("Finished playing sound (miniaudio)");
				it = m_PlayingSounds.erase(it);
				continue;
			}
			++it;
		}
	}

	void MiniaudioAudioManager::Shutdown()
	{
		for (const auto& sound : m_PlayingSounds | std::views::values)
		{
			ma_sound_stop(sound);
			ma_sound_uninit(sound);
			delete sound;
		}
		m_PlayingSounds.clear();

		ma_engine_uninit(&m_Engine);
	}

	Ref<Sound> MiniaudioAudioManager::Load(const std::filesystem::path& filepath)
	{
		// Create Sound asset and remember mapping to filepath
		const std::string soundName = filepath.stem().string();
		Sound sound{ soundName };
		const UUID soundUUID = sound.GetSoundID();

		m_FilepathToUUID[filepath] = soundUUID;
		// Also keep reverse mapping for Stop()/Play(UUID)
		m_PlayingSounds.emplace(soundUUID, nullptr);

		return CreateRef<Sound>(sound);
	}

	void MiniaudioAudioManager::Play(const std::filesystem::path& filepath)
	{
		const auto it = m_FilepathToUUID.find(filepath);
		if (it == m_FilepathToUUID.end())
		{
			Log::CoreError("Sound not loaded: {0}", filepath.string());
			return;
		}
		const UUID uuid = it->second;

		// If already playing, stop previous instance
		if (const auto existing = m_PlayingSounds.find(uuid); existing != m_PlayingSounds.end() && existing->second != nullptr)
		{
			ma_sound* s = existing->second;
			ma_sound_stop(s);
			ma_sound_uninit(s);
			delete s;
			existing->second = nullptr;
		}

		ma_sound* pSound = new ma_sound{};
		if (ma_sound_init_from_file(&m_Engine, filepath.string().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, pSound) != MA_SUCCESS)
		{
			delete pSound;
			Log::CoreError("Failed to init miniaudio sound for file: {0}", filepath.string());
			return;
		}

		if (ma_sound_start(pSound) != MA_SUCCESS)
		{
			ma_sound_uninit(pSound);
			delete pSound;
			Log::CoreError("Failed to start miniaudio sound for file: {0}", filepath.string());
			return;
		}

		m_PlayingSounds[uuid] = pSound;
		Log::CoreInfo("Playing (miniaudio): {0}", filepath.string());
	}

	void MiniaudioAudioManager::Play(const UUID& soundID)
	{
		auto it = m_FilepathToUUID.end();
		// find filepath by reverse lookup in m_FilepathToUUID
		for (const auto& [path, uuid] : m_FilepathToUUID)
		{
			if (uuid == soundID)
			{
				it = m_FilepathToUUID.find(path);
				break;
			}
		}
		if (it == m_FilepathToUUID.end())
		{
			Log::CoreError("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}
		Play(it->first);
	}

	void MiniaudioAudioManager::Stop(const UUID& soundID)
	{
		const auto it = m_PlayingSounds.find(soundID);
		if (it == m_PlayingSounds.end() || it->second == nullptr)
		{
			Log::CoreError("Sound is not currently playing");
			return;
		}
		ma_sound* s = it->second;
		ma_sound_stop(s);
		ma_sound_uninit(s);
		delete s;
		m_PlayingSounds.erase(it);
	}

	void MiniaudioAudioManager::IncreaseVolume(const UUID& soundID, const float delta)
	{
		const auto it = m_PlayingSounds.find(soundID);
		if (it == m_PlayingSounds.end() || it->second == nullptr)
		{
			Log::CoreError("You can only increase the volume of a sound currently playing");
			return;
		}
		const float current = ma_sound_get_volume(it->second);
		ma_sound_set_volume(it->second, current + delta);
	}

	void MiniaudioAudioManager::DecreaseVolume(const UUID& soundID, const float delta)
	{
		const auto it = m_PlayingSounds.find(soundID);
		if (it == m_PlayingSounds.end() || it->second == nullptr)
		{
			Log::CoreError("You can only decrease the volume of a sound currently playing");
			return;
		}
		const float current = ma_sound_get_volume(it->second);
		ma_sound_set_volume(it->second, current - delta);
	}

	void MiniaudioAudioManager::SetVolume(const UUID& soundID, const float volume)
	{
		const auto it = m_PlayingSounds.find(soundID);
		if (it == m_PlayingSounds.end() || it->second == nullptr)
		{
			Log::CoreError("You can only set the volume of a sound currently playing");
			return;
		}
		ma_sound_set_volume(it->second, volume);
	}

	void MiniaudioAudioManager::ResetVolume(const UUID& soundID)
	{
		SetVolume(soundID, 1.0f);
	}

	void MiniaudioAudioManager::Mute(const UUID& soundID)
	{
		SetVolume(soundID, 0.0f);
	}

} // namespace Kerberos