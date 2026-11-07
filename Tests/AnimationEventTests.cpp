#include "Renderer/Animation/AnimationClip.hpp"
#include "Renderer/Animation/AnimationEvaluator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

import Kerberos;

namespace
{
	Kerberos::AnimationEvent MakeEvent(const float time,
	                                  const float duration = 0.0f,
	                                  const float minTriggerWeight = 0.0f,
	                                  std::string name = "Event")
	{
		return Kerberos::AnimationEvent{
			.Time = time,
			.Duration = duration,
			.Name = std::move(name),
			.MinTriggerWeight = minTriggerWeight,
		};
	}

	Kerberos::AnimationClip MakeClip(std::vector<Kerberos::AnimationEvent> events)
	{
		return Kerberos::AnimationClip("Events", {}, 1.0f, {}, std::move(events));
	}

	TEST(AnimationEventTest, PlaybackSpansHandleNormalAndWrappedPlayback)
	{
		const std::vector<Kerberos::PlaybackSpan> normal =
			Kerberos::BuildPlaybackSpans(0.2f, 0.5f, 1.0f, false);
		ASSERT_EQ(normal.size(), 1u);
		EXPECT_FLOAT_EQ(normal[0].Start, 0.2f);
		EXPECT_FLOAT_EQ(normal[0].End, 0.5f);
		EXPECT_FALSE(normal[0].IncludeStart);

		const std::vector<Kerberos::PlaybackSpan> wrapped =
			Kerberos::BuildPlaybackSpans(0.8f, 0.2f, 1.0f, true);
		ASSERT_EQ(wrapped.size(), 2u);
		EXPECT_FLOAT_EQ(wrapped[0].Start, 0.8f);
		EXPECT_FLOAT_EQ(wrapped[0].End, 1.0f);
		EXPECT_FALSE(wrapped[0].IncludeStart);
		EXPECT_FLOAT_EQ(wrapped[1].Start, 0.0f);
		EXPECT_FLOAT_EQ(wrapped[1].End, 0.2f);
		EXPECT_TRUE(wrapped[1].IncludeStart);
	}

	TEST(AnimationEventTest, PointEventsUseHalfOpenForwardIntervals)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.0f, 0.0f, 0.0f, "Start"),
			MakeEvent(0.5f, 0.0f, 0.0f, "Middle"),
			MakeEvent(1.0f, 0.0f, 0.0f, "End"),
		});

		std::vector<Kerberos::AnimationEventHit> hits;
		Kerberos::CollectPointEvents(clip, 0.0f, 0.6f, 1.0f, false, 1.0f, hits);

		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Event->Name, "Middle");
	}

	TEST(AnimationEventTest, PointEventsCrossLoopWraps)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.1f, 0.0f, 0.0f, "AfterWrap"),
			MakeEvent(0.9f, 0.0f, 0.0f, "BeforeWrap"),
		});

		std::vector<Kerberos::AnimationEventHit> hits;
		Kerberos::CollectPointEvents(clip, 0.8f, 0.2f, 1.0f, true, 1.0f, hits);

		std::ranges::sort(hits, {}, &Kerberos::AnimationEventHit::Time);
		ASSERT_EQ(hits.size(), 2u);
		EXPECT_EQ(hits[0].Event->Name, "AfterWrap");
		EXPECT_EQ(hits[1].Event->Name, "BeforeWrap");
	}

	TEST(AnimationEventTest, PointEventsRespectWeightThreshold)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.5f, 0.0f, 0.5f, "Weighted"),
		});

		std::vector<Kerberos::AnimationEventHit> hits;
		Kerberos::CollectPointEvents(clip, 0.0f, 1.0f, 1.0f, false, 0.25f, hits);
		EXPECT_TRUE(hits.empty());

		Kerberos::CollectPointEvents(clip, 0.0f, 1.0f, 1.0f, false, 0.75f, hits);
		ASSERT_EQ(hits.size(), 1u);
	}

	TEST(AnimationEventTest, PointEventsAreEmittedOnceForLargeSteps)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.25f, 0.0f, 0.0f, "A"),
			MakeEvent(0.75f, 0.0f, 0.0f, "B"),
		});

		std::vector<Kerberos::AnimationEventHit> hits;
		Kerberos::CollectPointEvents(clip, 0.5f, 0.5f, 1.0f, true, 1.0f, hits);

		ASSERT_EQ(hits.size(), 2u);
		EXPECT_EQ(hits[0].Event->Name, "A");
		EXPECT_EQ(hits[1].Event->Name, "B");
	}

	TEST(AnimationEventTest, DurationEventsEmitBeginTickAndEnd)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.2f, 0.4f, 0.0f, "Window"),
		});

		std::vector<Kerberos::ActiveAnimationEvent> active;
		std::vector<Kerberos::AnimationEventHit> hits;

		Kerberos::UpdateDurationEvents(clip, 0.1f, 0.3f, 1.0f, false, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 2u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::Begin);
		EXPECT_EQ(hits[1].Phase, Kerberos::AnimationEventPhase::Tick);
		ASSERT_EQ(active.size(), 1u);

		hits.clear();
		Kerberos::UpdateDurationEvents(clip, 0.3f, 0.5f, 1.0f, false, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::Tick);

		hits.clear();
		Kerberos::UpdateDurationEvents(clip, 0.5f, 0.7f, 1.0f, false, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::End);
		EXPECT_TRUE(active.empty());
	}

	TEST(AnimationEventTest, DurationEventsSpanLoopWraps)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.8f, 0.4f, 0.0f, "Wrap"),
		});

		std::vector<Kerberos::ActiveAnimationEvent> active;
		std::vector<Kerberos::AnimationEventHit> hits;

		Kerberos::UpdateDurationEvents(clip, 0.7f, 0.9f, 1.0f, true, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 2u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::Begin);
		EXPECT_EQ(hits[1].Phase, Kerberos::AnimationEventPhase::Tick);

		hits.clear();
		Kerberos::UpdateDurationEvents(clip, 0.9f, 0.1f, 1.0f, true, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::Tick);

		hits.clear();
		Kerberos::UpdateDurationEvents(clip, 0.1f, 0.3f, 1.0f, true, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::End);
	}

	TEST(AnimationEventTest, SkippedDurationWindowEmitsBeginAndEnd)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.2f, 0.2f, 0.0f, "Skipped"),
		});

		std::vector<Kerberos::ActiveAnimationEvent> active;
		std::vector<Kerberos::AnimationEventHit> hits;

		Kerberos::UpdateDurationEvents(clip, 0.0f, 0.7f, 1.0f, false, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 2u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::Begin);
		EXPECT_EQ(hits[1].Phase, Kerberos::AnimationEventPhase::End);
		EXPECT_TRUE(active.empty());
	}

	TEST(AnimationEventTest, WeightDropEndsActiveDurationEvent)
	{
		const Kerberos::AnimationClip clip = MakeClip({
			MakeEvent(0.2f, 0.6f, 0.5f, "Weighted"),
		});

		std::vector<Kerberos::ActiveAnimationEvent> active;
		std::vector<Kerberos::AnimationEventHit> hits;

		Kerberos::UpdateDurationEvents(clip, 0.1f, 0.3f, 1.0f, false, 1.0f, active, hits);
		ASSERT_EQ(hits.size(), 2u);

		hits.clear();
		Kerberos::UpdateDurationEvents(clip, 0.3f, 0.4f, 1.0f, false, 0.1f, active, hits);
		ASSERT_EQ(hits.size(), 1u);
		EXPECT_EQ(hits[0].Phase, Kerberos::AnimationEventPhase::End);
		EXPECT_TRUE(active.empty());
	}

} // namespace
