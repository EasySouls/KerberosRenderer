#include "kbrpch.hpp"
#include "AudioManager.hpp"


#ifdef KBR_PLATFORM_WINDOWS
#include "Audio/Windows/XAudio2AudioManager.hpp"
#endif

namespace Kerberos
{
	AudioManager* AudioManager::Create()
	{
#ifdef KBR_PLATFORM_WINDOWS
		return new XAudio2AudioManager();
#endif

		KBR_CORE_ASSERT(false, "No AudioManager implementation for this platform!");
		return nullptr;
	}
}