#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"

#include <string>

namespace Kerberos
{
	class Sound : public Asset
	{
	public:
		explicit Sound(std::string name)
			: m_Name(std::move(name))
		{
		}

		~Sound() override = default;

		Sound(const Sound& other) = default;
		Sound(Sound&& other) noexcept = default;
		Sound& operator=(const Sound& other) = default;
		Sound& operator=(Sound&& other) noexcept = default;

		void Play() const;
		void Stop() const;

		void IncreaseVolume(float delta) const;
		void DecreaseVolume(float delta) const;
		void SetVolume(float volume) const;
		void ResetVolume() const;
		void Mute() const;

		const std::string& GetName() const { return m_Name; }
		UUID GetSoundID() const { return m_SoundID; }

		static constexpr AssetType Type = AssetType::Sound;
        AssetType GetType() override
        {
            return Type;
        }

	private:
		std::string m_Name;

		/**
		* The UUID of the sound asset in the Audio Manager.
		*/
		UUID m_SoundID;
	};
}