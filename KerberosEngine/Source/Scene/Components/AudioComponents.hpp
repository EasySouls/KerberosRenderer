#pragma once

#include "Core/Core.hpp"
#include "Audio/Sound.hpp"

namespace Kerberos
{
	struct AudioSource3DComponent
	{
		Ref<Sound> SoundAsset = nullptr;
		bool Loop = false;

		// Volume ranges from 0.0 (mute) to 1.0 (full volume)
		float Volume = 1.0f;
		bool IsPlaying = false;

		AudioSource3DComponent() = default;
		explicit AudioSource3DComponent(const Ref<Sound>& soundAsset, const bool loop = false, const float volume = 1.0f)
			: SoundAsset(soundAsset), Loop(loop), Volume(volume)
		{
		}
	};

	struct AudioSource2DComponent
	{
		Ref<Sound> SoundAsset = nullptr;
		bool Loop = false;

		// Volume ranges from 0.0 (mute) to 1.0 (full volume)
		float Volume = 1.0f;
		bool IsPlaying = false;

		AudioSource2DComponent() = default;
		explicit AudioSource2DComponent(const Ref<Sound>& soundAsset, const bool loop = false, const float volume = 1.0f)
			: SoundAsset(soundAsset), Loop(loop), Volume(volume)
		{
		}
	};

	struct AudioListenerComponent
	{
		// Volume ranges from 0.0 (mute) to 1.0 (full volume)
		float Volume = 1.0f;

		AudioListenerComponent() = default;
	};
}