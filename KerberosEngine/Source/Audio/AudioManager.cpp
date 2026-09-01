#include "AudioManager.hpp"

#ifdef KBR_USE_MINIAUDIO
#include "Audio/Miniaudio/MiniaudioAudioManager.hpp"
#elif defined(KBR_PLATFORM_WINDOWS)
#include "Audio/Windows/XAudio2AudioManager.hpp"
#endif

import Kerberos;

namespace Kerberos
{
	AudioManager* AudioManager::Create()
	{
#ifdef KBR_USE_MINIAUDIO
		return new MiniaudioAudioManager();
#elif defined(KBR_PLATFORM_WINDOWS)
		return new XAudio2AudioManager();
#endif

		KBRAssert(false, "No AudioManager implementation for this platform!");
		return nullptr;
	}
}