#include "kbrpch.hpp"
#include "XAudio2AudioManager.hpp"

#include "Audio/Sound.hpp"

namespace Kerberos
{
	XAudio2AudioManager::~XAudio2AudioManager()
	{
		XAudio2AudioManager::Shutdown();
	}

	void XAudio2AudioManager::Init()
	{
		HRESULT res = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(res))
		{
			KBR_CORE_ERROR("Failed to initialize COM library for XAudio2! HRESULT: {0}", res);
			KBR_CORE_ASSERT(false, "Failed to initialize COM library for XAudio2!");
			throw std::runtime_error("Failed to initialize COM library for XAudio2!");
		}

		res = XAudio2Create(&m_XAudio2, 0, XAUDIO2_USE_DEFAULT_PROCESSOR);
		if (FAILED(res))
		{
			KBR_CORE_ERROR("Failed to create XAudio2 instance! HRESULT: {0}", res);
			KBR_CORE_ASSERT(false, "Failed to create XAudio2 instance!");
			throw std::runtime_error("Failed to create XAudio2 instance!");
		}

		res = m_XAudio2->CreateMasteringVoice(&m_MasteringVoice);
		if (FAILED(res))
		{
			KBR_CORE_ERROR("Failed to create XAudio2 mastering voice! HRESULT: {0}", res);
			KBR_CORE_ASSERT(false, "Failed to create XAudio2 mastering voice!");
			throw std::runtime_error("Failed to create XAudio2 mastering voice!");
		}

#ifdef KBR_DEBUG
		XAUDIO2_DEBUG_CONFIGURATION debugConfig;
		debugConfig.TraceMask = XAUDIO2_LOG_ERRORS;
		debugConfig.BreakMask = XAUDIO2_LOG_ERRORS;
		debugConfig.LogThreadID = TRUE;
		debugConfig.LogFileline = TRUE;
		debugConfig.LogFunctionName = TRUE;
		debugConfig.LogTiming = TRUE;
		m_XAudio2->SetDebugConfiguration(&debugConfig);

#endif

		KBR_CORE_INFO("XAudio2AudioManager initialized successfully.");
	}


	void XAudio2AudioManager::Update()
	{
		for (auto it = m_PlayingAudios.begin(); it != m_PlayingAudios.end(); )
		{
			IXAudio2SourceVoice* sourceVoice = it->second;
			XAUDIO2_VOICE_STATE state;
			sourceVoice->GetState(&state);
			if (state.BuffersQueued == 0)
			{
				const HRESULT res = sourceVoice->Stop();
				if (FAILED(res))
				{
					KBR_CORE_ERROR("Failed to stop source voice for audio: {0}", it->first.string());
				}
				sourceVoice->DestroyVoice();
				KBR_CORE_TRACE("Finished playing audio: {0}", it->first.string());
				it = m_PlayingAudios.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void XAudio2AudioManager::Shutdown()
	{
		if (m_MasteringVoice)
		{
			m_MasteringVoice->DestroyVoice();
			m_MasteringVoice = nullptr;
		}
		if (m_XAudio2)
		{
			m_XAudio2->Release();
			m_XAudio2 = nullptr;
		}
		CoUninitialize();
	}

	Ref<Sound> XAudio2AudioManager::Load(const std::filesystem::path& filepath)
	{
		const AudioFormat format = DetectAudioFormat(filepath);
		if (format == AudioFormat::FormatUnknown)
		{
			KBR_CORE_ERROR("Unsupported audio format for file: {0}", filepath.string());
			return nullptr;
		}
		if (format == AudioFormat::FormatPcm)
		{
			LoadWavFile(filepath);

			const std::string soundName = filepath.stem().string();

			Sound sound{ soundName };
			const UUID soundUUID = sound.GetSoundID();

			m_SoundUUIDToFilepath[soundUUID] = filepath;

			return CreateRef<Sound>(sound);
		}

		KBR_CORE_ERROR("Audio format not implemented for file: {0}", filepath.string());
		return nullptr;
	}

	void XAudio2AudioManager::Play(const std::filesystem::path& filepath)
	{
		const auto it = m_LoadedWAVs.find(filepath);
		if (it == m_LoadedWAVs.end()) {
			KBR_CORE_ERROR("WAV file not loaded: {0}", filepath.string());
			return;
		}

		if (const auto existingIt = m_PlayingAudios.find(filepath); existingIt != m_PlayingAudios.end())
		{
			const auto& existingAudio = existingIt->second;
			existingAudio->Stop();
			existingAudio->DestroyVoice();
			m_PlayingAudios.erase(existingIt);
			KBR_CORE_WARN("Stopping sound before playing it: {0}", filepath.string());
		}

		const AudioData& soundData = it->second;
		IXAudio2SourceVoice* sourceVoice;

		if (soundData.buffer.empty()) {
			KBR_CORE_ERROR("WAV file has no audio data: {0}", filepath.string());
			return;
		}

		HRESULT res = m_XAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfx);
		if (FAILED(res)) {
			KBR_CORE_ERROR("Failed to create source voice for WAV file: {0}", filepath.string());
			return;
		}
		XAUDIO2_BUFFER buffer = {};
		buffer.AudioBytes = static_cast<uint32_t>(soundData.buffer.size());
		buffer.pAudioData = soundData.buffer.data();
		buffer.Flags = XAUDIO2_END_OF_STREAM;

		res = sourceVoice->SubmitSourceBuffer(&buffer);
		if (FAILED(res)) {
			KBR_CORE_ERROR("Failed to submit source buffer for WAV file: {0}", filepath.string());
			sourceVoice->DestroyVoice();
			return;
		}

		res = sourceVoice->Start();
		if (FAILED(res)) {
			KBR_CORE_ERROR("Failed to start source voice for WAV file: {0}", filepath.string());
			sourceVoice->DestroyVoice();
			return;
		}

		m_PlayingAudios[filepath] = sourceVoice;

		KBR_CORE_INFO("Playing WAV file: {0}", filepath.string());
	}

	void XAudio2AudioManager::Play(const UUID& soundID)
	{
		const auto filepath = m_SoundUUIDToFilepath.find(soundID);
		if (filepath == m_SoundUUIDToFilepath.end())
		{
			KBR_CORE_ERROR("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}

		Play(filepath->second);
	}

	void XAudio2AudioManager::Stop(const UUID& soundID)
	{
		const auto filepath = m_SoundUUIDToFilepath.find(soundID);
		if (filepath == m_SoundUUIDToFilepath.end())
		{
			KBR_CORE_ERROR("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}

		const auto it = m_PlayingAudios.find(filepath->second);
		if (it == m_PlayingAudios.end())
		{
			KBR_CORE_ERROR("Sound is not currently playing: {0}", filepath->second.string());
			return;
		}

		IXAudio2SourceVoice* sourceVoice = it->second;
		const HRESULT res = sourceVoice->Stop();
		if (FAILED(res))
		{
			KBR_CORE_ERROR("Failed to stop source voice for audio: {0}", filepath->second.string());
			return;
		}

		sourceVoice->DestroyVoice();
		m_PlayingAudios.erase(it);
	}

	void XAudio2AudioManager::IncreaseVolume(const UUID& soundID, const float delta)
	{
		const auto filepath = m_SoundUUIDToFilepath.find(soundID);
		if (filepath == m_SoundUUIDToFilepath.end())
		{
			KBR_CORE_ERROR("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}

		const auto audio = m_PlayingAudios.find(filepath->second);
		if (audio == m_PlayingAudios.end())
		{
			KBR_CORE_ERROR("You can only increase the volume of a sound currently playing: {0}", filepath->second.string());
			return;
		}

		float currentVolume = 0;
		audio->second->GetVolume(&currentVolume);

		if (const HRESULT res = audio->second->SetVolume(currentVolume + delta); FAILED(res))
		{
			KBR_CORE_ERROR("Failed to increase volume for sound: {0}", filepath->second.string());
		}
	}

	void XAudio2AudioManager::DecreaseVolume(const UUID& soundID, const float delta)
	{
		const auto filepath = m_SoundUUIDToFilepath.find(soundID);
		if (filepath == m_SoundUUIDToFilepath.end())
		{
			KBR_CORE_ERROR("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}

		const auto audio = m_PlayingAudios.find(filepath->second);
		if (audio == m_PlayingAudios.end())
		{
			KBR_CORE_ERROR("You can only decrease the volume of a sound currently playing: {0}", filepath->second.string());
			return;
		}

		float currentVolume = 0;
		audio->second->GetVolume(&currentVolume);

		if (const HRESULT res = audio->second->SetVolume(currentVolume - delta); FAILED(res))
		{
			KBR_CORE_ERROR("Failed to decrease volume for sound: {0}", filepath->second.string());
		}
	}

	void XAudio2AudioManager::SetVolume(const UUID& soundID, const float volume)
	{
		const auto filepath = m_SoundUUIDToFilepath.find(soundID);
		if (filepath == m_SoundUUIDToFilepath.end())
		{
			KBR_CORE_ERROR("Sound ID not found: {0}", static_cast<uint64_t>(soundID));
			return;
		}

		const auto audio = m_PlayingAudios.find(filepath->second);
		if (audio == m_PlayingAudios.end())
		{
			KBR_CORE_ERROR("You can only set the volume of a sound currently playing: {0}", filepath->second.string());
			return;
		}

		if (const HRESULT res = audio->second->SetVolume(volume); FAILED(res))
		{
			KBR_CORE_ERROR("Failed to set volume for sound: {0}", filepath->second.string());
		}
	}

	void XAudio2AudioManager::ResetVolume(const UUID& soundID)
	{
		SetVolume(soundID, 1.0f);
	}

	void XAudio2AudioManager::Mute(const UUID& soundID)
	{
		SetVolume(soundID, 0.0f);
	}


	AudioFormat XAudio2AudioManager::DetectAudioFormat(const std::filesystem::path& filepath)
	{
		const std::string extension = filepath.extension().string();
		if (extension == ".wav" || extension == ".WAV")
		{
			return AudioFormat::FormatPcm;
		}
		if (extension == ".adpcm" || extension == ".ADPCM")
		{
			return AudioFormat::FormatAdpcm;
		}
		if (extension == ".f32" || extension == ".F32")
		{
			return AudioFormat::FormatIeeeFloat;
		}
		return AudioFormat::FormatUnknown;
	}

	void XAudio2AudioManager::LoadWavFile(const std::filesystem::path& filepath)
	{
		std::ifstream file(filepath, std::ios::binary);
		if (!file) {
			KBR_CORE_ERROR("Failed to open WAV file: {0}", filepath.string());
			return;
		}

		char chunkId[4];
		file.read(chunkId, 4);
		if (strncmp(chunkId, "RIFF", 4) != 0) {
			KBR_CORE_ERROR("Invalid WAV file (missing RIFF): {0}", filepath.string());
			return;
		}

		DWORD chunkSize;

		file.read(reinterpret_cast<char*>(&chunkSize), 4);

		file.read(chunkId, 4);
		if (strncmp(chunkId, "WAVE", 4) != 0) {
			KBR_CORE_ERROR("Invalid WAV file (missing WAVE): {0}", filepath.string());
			return;
		}

		AudioData soundData;
		bool foundFmt = false;
		bool foundData = false;

		// Parse chunks
		while (file.read(chunkId, 4)) {
			file.read(reinterpret_cast<char*>(&chunkSize), 4);

			if (strncmp(chunkId, "fmt ", 4) == 0) {
				// Read format chunk
				file.read(reinterpret_cast<char*>(&soundData.wfx.wFormatTag), 2);
				file.read(reinterpret_cast<char*>(&soundData.wfx.nChannels), 2);
				file.read(reinterpret_cast<char*>(&soundData.wfx.nSamplesPerSec), 4);
				file.read(reinterpret_cast<char*>(&soundData.wfx.nAvgBytesPerSec), 4);
				file.read(reinterpret_cast<char*>(&soundData.wfx.nBlockAlign), 2);
				file.read(reinterpret_cast<char*>(&soundData.wfx.wBitsPerSample), 2);

				// Set cbSize to 0 for PCM format
				soundData.wfx.cbSize = 0;

				// Skip any extra format bytes
				if (chunkSize > 16) {
					file.seekg(chunkSize - 16, std::ios::cur);
				}

				foundFmt = true;
				KBR_CORE_TRACE("WAV Format - Channels: {0}, SampleRate: {1}, BitsPerSample: {2}",
							   soundData.wfx.nChannels, soundData.wfx.nSamplesPerSec, soundData.wfx.wBitsPerSample);
			}
			else if (strncmp(chunkId, "data", 4) == 0) {
				// Read audio data
				soundData.buffer.resize(chunkSize);
				file.read(reinterpret_cast<char*>(soundData.buffer.data()), chunkSize);
				foundData = true;
				KBR_CORE_TRACE("WAV Data - Size: {0} bytes", chunkSize);
				break; // Data chunk is typically the last one we need
			}
			else {
				// Skip unknown chunk
				file.seekg(chunkSize, std::ios::cur);
			}
		}

		if (!foundFmt) {
			KBR_CORE_ERROR("WAV file missing 'fmt ' chunk: {0}", filepath.string());
			return;
		}

		if (!foundData) {
			KBR_CORE_ERROR("WAV file missing 'data' chunk: {0}", filepath.string());
			return;
		}

		if (soundData.buffer.empty()) {
			KBR_CORE_ERROR("WAV file has empty audio data: {0}", filepath.string());
			return;
		}

		m_LoadedWAVs[filepath] = std::move(soundData);
	}
}