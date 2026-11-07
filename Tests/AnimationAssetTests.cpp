#include "Assets/Formats/AnimationFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Pipeline/AssetMetaService.hpp"
#include "Renderer/Animation/AnimationClip.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

import Kerberos;

namespace
{
	class AnimationAssetTest : public testing::Test
	{
	protected:
		static void SetUpTestSuite()
		{
			Kerberos::Log::Init();
		}

		static void TearDownTestSuite()
		{
			Kerberos::Log::Shutdown();
		}

		void SetUp() override
		{
			m_TempDir = std::filesystem::temp_directory_path() / "kbr_animation_asset_test";
			std::error_code error;
			std::filesystem::remove_all(m_TempDir, error);
			std::filesystem::create_directories(m_TempDir, error);
		}

		void TearDown() override
		{
			std::error_code error;
			std::filesystem::remove_all(m_TempDir, error);
		}

		std::filesystem::path m_TempDir;
	};

	TEST_F(AnimationAssetTest, MetaRoundTripsMarkersAndEvents)
	{
		const std::filesystem::path source = m_TempDir / "model.gltf";
		{
			std::ofstream stream(source);
			stream << "{}";
		}

		Kerberos::AssetMetaService service(m_TempDir);
		Kerberos::AssetMetaFile meta = service.EnsureMetaForSource(source);
		ASSERT_EQ(meta.SchemaVersion, Kerberos::AssetMetaFile::CurrentSchemaVersion);

		Kerberos::AnimationClipMetaEntry clipMeta{};
		clipMeta.Markers.push_back({ .Time = 0.5f, .Name = "Footstep" });
		clipMeta.Events.push_back({
			.Time = 0.25f,
			.Duration = 0.4f,
			.Name = "WeaponTrail",
			.Payload = "right",
			.FireInEditor = true,
			.MinTriggerWeight = 0.01f,
			.Flags = 7u,
			.Parameters = {
				{
					.Name = "Damage",
					.Type = Kerberos::AnimationEventParameterTypeFile::Float,
					.FloatValue = 10.0f,
				},
				{
					.Name = "Socket",
					.Type = Kerberos::AnimationEventParameterTypeFile::String,
					.StringValue = "Weapon",
				},
				{
					.Name = "Offset",
					.Type = Kerberos::AnimationEventParameterTypeFile::Vector3,
					.VectorValue = glm::vec3(0.0f, 0.2f, 0.0f),
				},
				{
					.Name = "HitOnce",
					.Type = Kerberos::AnimationEventParameterTypeFile::Bool,
					.BoolValue = true,
				},
				{
					.Name = "Sound",
					.Type = Kerberos::AnimationEventParameterTypeFile::Asset,
					.AssetValue = 42,
				},
			},
		});
		meta.AnimationClips["animation:0"] = clipMeta;

		ASSERT_TRUE(service.SaveMeta(source, meta));

		const std::optional<Kerberos::AssetMetaFile> loaded = service.LoadMeta(source);
		ASSERT_TRUE(loaded.has_value());
		ASSERT_EQ(loaded->AnimationClips.size(), 1u);

		const Kerberos::AnimationClipMetaEntry& loadedClip = loaded->AnimationClips.at("animation:0");
		ASSERT_EQ(loadedClip.Markers.size(), 1u);
		EXPECT_FLOAT_EQ(loadedClip.Markers[0].Time, 0.5f);
		EXPECT_EQ(loadedClip.Markers[0].Name, "Footstep");

		ASSERT_EQ(loadedClip.Events.size(), 1u);
		const Kerberos::AnimationEventFile& event = loadedClip.Events[0];
		EXPECT_FLOAT_EQ(event.Time, 0.25f);
		EXPECT_FLOAT_EQ(event.Duration, 0.4f);
		EXPECT_EQ(event.Name, "WeaponTrail");
		EXPECT_EQ(event.Payload, "right");
		EXPECT_TRUE(event.FireInEditor);
		EXPECT_FLOAT_EQ(event.MinTriggerWeight, 0.01f);
		EXPECT_EQ(event.Flags, 7u);

		ASSERT_EQ(event.Parameters.size(), 5u);
		EXPECT_EQ(event.Parameters[0].Name, "Damage");
		EXPECT_EQ(event.Parameters[0].Type, Kerberos::AnimationEventParameterTypeFile::Float);
		EXPECT_FLOAT_EQ(event.Parameters[0].FloatValue, 10.0f);
		EXPECT_EQ(event.Parameters[1].StringValue, "Weapon");
		EXPECT_EQ(event.Parameters[2].Type, Kerberos::AnimationEventParameterTypeFile::Vector3);
		EXPECT_FLOAT_EQ(event.Parameters[2].VectorValue.y, 0.2f);
		EXPECT_TRUE(event.Parameters[3].BoolValue);
		EXPECT_EQ(event.Parameters[4].AssetValue, 42u);
	}

	TEST_F(AnimationAssetTest, OlderMetaWithoutAnimationClipsLoadsEmpty)
	{
		const std::filesystem::path source = m_TempDir / "legacy.gltf";
		{
			std::ofstream stream(source);
			stream << "{}";
		}

		const std::filesystem::path metaPath = Kerberos::AssetMetaService::MetaPathFor(source);
		{
			std::ofstream stream(metaPath);
			stream << "SchemaVersion: 2\n"
			          "SourceHandle: 1\n"
			          "ImporterType: 1\n"
			          "ImporterVersion: 1\n"
			          "SourceHash: abc\n"
			          "SubAssets: []\n";
		}

		Kerberos::AssetMetaService service(m_TempDir);
		const std::optional<Kerberos::AssetMetaFile> loaded = service.LoadMeta(source);
		ASSERT_TRUE(loaded.has_value());
		EXPECT_EQ(loaded->SchemaVersion, 2u);
		EXPECT_TRUE(loaded->AnimationClips.empty());
	}

	TEST_F(AnimationAssetTest, NativeClipRoundTripsMarkersAndEvents)
	{
		Kerberos::AnimationClipFile clip{};
		clip.Version = 3;
		clip.LocalKey = "animation:0";
		clip.Name = "TestClip";
		clip.DurationSeconds = 1.0f;
		clip.Markers = {
			{ .Time = 0.2f, .Name = "Start" },
			{ .Time = 0.8f, .Name = "End" },
		};
		clip.Events = {
			{
				.Time = 0.3f,
				.Duration = 0.0f,
				.Name = "Footstep",
				.Payload = "left",
				.FireInEditor = false,
				.MinTriggerWeight = 0.0f,
				.Flags = 0,
				.Parameters = {
					{
						.Name = "Damage",
						.Type = Kerberos::AnimationEventParameterTypeFile::Float,
						.FloatValue = 20.0f,
					},
					{
						.Name = "Socket",
						.Type = Kerberos::AnimationEventParameterTypeFile::String,
						.StringValue = "Weapon",
					},
					{
						.Name = "Offset",
						.Type = Kerberos::AnimationEventParameterTypeFile::Vector3,
						.VectorValue = glm::vec3(0.0f, 1.0f, 0.0f),
					},
					{
						.Name = "HitOnce",
						.Type = Kerberos::AnimationEventParameterTypeFile::Bool,
						.BoolValue = true,
					},
					{
						.Name = "Sound",
						.Type = Kerberos::AnimationEventParameterTypeFile::Asset,
						.AssetValue = 12345,
					},
				},
			},
			{
				.Time = 0.4f,
				.Duration = 0.2f,
				.Name = "Trail",
				.Payload = "right",
				.FireInEditor = true,
				.MinTriggerWeight = 0.25f,
				.Flags = 3,
			},
		};

		const std::filesystem::path path = m_TempDir / "clip.kbranimation";
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::SerializeAnimationClip(clip, path));

		Kerberos::AnimationClipFile loaded{};
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(path, loaded));
		EXPECT_EQ(loaded.Version, 3u);
		ASSERT_EQ(loaded.Markers.size(), 2u);
		ASSERT_EQ(loaded.Events.size(), 2u);

		EXPECT_FLOAT_EQ(loaded.Markers[0].Time, 0.2f);
		EXPECT_EQ(loaded.Markers[0].Name, "Start");
		EXPECT_FLOAT_EQ(loaded.Markers[1].Time, 0.8f);

		EXPECT_FLOAT_EQ(loaded.Events[0].Time, 0.3f);
		EXPECT_EQ(loaded.Events[0].Name, "Footstep");
		EXPECT_EQ(loaded.Events[0].Payload, "left");

		EXPECT_FLOAT_EQ(loaded.Events[1].Duration, 0.2f);
		EXPECT_EQ(loaded.Events[1].Name, "Trail");
		EXPECT_TRUE(loaded.Events[1].FireInEditor);
		EXPECT_FLOAT_EQ(loaded.Events[1].MinTriggerWeight, 0.25f);
		EXPECT_EQ(loaded.Events[1].Flags, 3u);

		ASSERT_EQ(loaded.Events[0].Parameters.size(), 5u);
		EXPECT_EQ(loaded.Events[0].Parameters[0].Name, "Damage");
		EXPECT_EQ(loaded.Events[0].Parameters[0].Type, Kerberos::AnimationEventParameterTypeFile::Float);
		EXPECT_FLOAT_EQ(loaded.Events[0].Parameters[0].FloatValue, 20.0f);
		EXPECT_EQ(loaded.Events[0].Parameters[1].StringValue, "Weapon");
		EXPECT_EQ(loaded.Events[0].Parameters[2].Type, Kerberos::AnimationEventParameterTypeFile::Vector3);
		EXPECT_FLOAT_EQ(loaded.Events[0].Parameters[2].VectorValue.y, 1.0f);
		EXPECT_TRUE(loaded.Events[0].Parameters[3].BoolValue);
		EXPECT_EQ(loaded.Events[0].Parameters[4].AssetValue, 12345u);
		EXPECT_TRUE(loaded.Events[1].Parameters.empty());
	}

	TEST_F(AnimationAssetTest, VersionOneClipStillLoads)
	{
		Kerberos::AnimationClipFile clip{};
		clip.Version = 1;
		clip.LocalKey = "animation:0";
		clip.Name = "LegacyClip";
		clip.DurationSeconds = 1.0f;

		const std::filesystem::path path = m_TempDir / "legacy.kbranimation";
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::SerializeAnimationClip(clip, path));

		Kerberos::AnimationClipFile loaded{};
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(path, loaded));
		EXPECT_EQ(loaded.Version, 1u);
		EXPECT_TRUE(loaded.Markers.empty());
		EXPECT_TRUE(loaded.Events.empty());
	}

	TEST_F(AnimationAssetTest, VersionTwoClipStillLoadsWithoutParameters)
	{
		Kerberos::AnimationClipFile clip{};
		clip.Version = 2;
		clip.LocalKey = "animation:0";
		clip.Name = "LegacyEvents";
		clip.DurationSeconds = 1.0f;
		clip.Events = {
			{
				.Time = 0.5f,
				.Duration = 0.0f,
				.Name = "Legacy",
			},
		};

		const std::filesystem::path path = m_TempDir / "legacy_events.kbranimation";
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::SerializeAnimationClip(clip, path));

		Kerberos::AnimationClipFile loaded{};
		ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(path, loaded));
		EXPECT_EQ(loaded.Version, 2u);
		ASSERT_EQ(loaded.Events.size(), 1u);
		EXPECT_EQ(loaded.Events[0].Name, "Legacy");
		EXPECT_TRUE(loaded.Events[0].Parameters.empty());
	}

	TEST_F(AnimationAssetTest, RuntimeClipSanitizesAndSortsMetadata)
	{
		std::vector<Kerberos::AnimationMarker> markers{
			{ .Time = 0.8f, .Name = "B" },
			{ .Time = 0.2f, .Name = "A" },
			{ .Time = 2.0f, .Name = "Clamped" },
			{ .Time = std::nanf(""), .Name = "Invalid" },
		};
		std::vector<Kerberos::AnimationEvent> events{
			{ .Time = 0.7f, .Duration = -1.0f, .Name = "NegativeDuration" },
			{ .Time = 0.1f, .Duration = 0.2f, .Name = "First" },
			{ .Time = std::nanf(""), .Duration = 0.0f, .Name = "Invalid" },
		};

		const Kerberos::AnimationClip clip("Sanitized", {}, 1.0f,
		                                   std::move(markers), std::move(events));

		ASSERT_EQ(clip.GetMarkers().size(), 3u);
		EXPECT_EQ(clip.GetMarkers()[0].Name, "A");
		EXPECT_EQ(clip.GetMarkers()[1].Name, "B");
		EXPECT_FLOAT_EQ(clip.GetMarkers()[2].Time, 1.0f);

		ASSERT_EQ(clip.GetEvents().size(), 2u);
		EXPECT_EQ(clip.GetEvents()[0].Name, "First");
		EXPECT_EQ(clip.GetEvents()[1].Name, "NegativeDuration");
		EXPECT_FLOAT_EQ(clip.GetEvents()[1].Duration, 0.0f);
		EXPECT_TRUE(clip.GetEvents()[1].IsPointEvent());
	}

	TEST_F(AnimationAssetTest, ExistingThreeArgumentConstructionStillWorks)
	{
		const Kerberos::AnimationClip clip("Bare", {}, 0.5f);
		EXPECT_TRUE(clip.GetMarkers().empty());
		EXPECT_TRUE(clip.GetEvents().empty());
	}

}
