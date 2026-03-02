#include "kbrpch.hpp"
#include "Sound.hpp"

#include "Application.hpp"

namespace Kerberos
{
	void Sound::Play() const
	{
		Application::Get().GetAudioManager()->Play(m_SoundID);
	}

	void Sound::Stop() const
	{
		Application::Get().GetAudioManager()->Stop(m_SoundID);
	}

	void Sound::IncreaseVolume(const float delta) const
	{
		Application::Get().GetAudioManager()->IncreaseVolume(m_SoundID, delta);
	}

	void Sound::DecreaseVolume(const float delta) const
	{
		Application::Get().GetAudioManager()->DecreaseVolume(m_SoundID, delta);
	}

	void Sound::SetVolume(const float volume) const
	{
		Application::Get().GetAudioManager()->SetVolume(m_SoundID, volume);
	}

	void Sound::ResetVolume() const
	{
		Application::Get().GetAudioManager()->ResetVolume(m_SoundID);
	}

	void Sound::Mute() const
	{
		Application::Get().GetAudioManager()->Mute(m_SoundID);
	}
}