#include "Features/Upscaling/StreamlineFrameTokenPublication.h"
#include "Features/Upscaling/VRSubmitTemporalSnapshot.h"

#include <array>
#include <iostream>
#include <limits>
#include <optional>

namespace
{
	namespace Policy = VRSubmitTemporalSnapshot;

	struct EyeCamera
	{
		std::array<float, 16> view{};
		std::array<float, 16> projection{};
		std::array<float, 16> previousViewProjection{};
		std::array<float, 3> position{};

		bool operator==(const EyeCamera&) const = default;
	};

	using Snapshot = Policy::Snapshot<EyeCamera>;

	Policy::Key ValidKey()
	{
		return { 42, 7, 3, 1200, 1300, 1800, 1950 };
	}

	Policy::Scalars ValidScalars()
	{
		return { 0.25f, -0.375f, 5.0f, 100000.0f, 1.5f, 11.1f, true };
	}

	bool CapturedInputsSurviveLaterCameraAndJitterChanges()
	{
		Snapshot snapshot;
		auto key = ValidKey();
		auto scalars = ValidScalars();
		std::array<EyeCamera, 2> cameras{};
		cameras[0].view.fill(1.0f);
		cameras[1].view.fill(2.0f);
		cameras[0].projection.fill(3.0f);
		cameras[1].projection.fill(4.0f);
		cameras[0].previousViewProjection.fill(5.0f);
		cameras[1].previousViewProjection.fill(6.0f);
		cameras[0].position = { -1.0f, 2.0f, 3.0f };
		cameras[1].position = { 1.0f, 2.0f, 3.0f };
		const auto producerCameras = cameras;
		if (!snapshot.Publish(key, scalars, cameras))
			return false;

		scalars = { -0.5f, 0.5f, 1.0f, 200000.0f, 2.0f, 100.0f, false };
		for (auto& camera : cameras) {
			camera.view.fill(10.0f);
			camera.projection.fill(20.0f);
			camera.previousViewProjection.fill(30.0f);
			camera.position.fill(40.0f);
		}
		// A second eye or a retry must consume the same producer tuple.
		if (!snapshot.Publish(key, scalars, cameras) || !snapshot.Matches(key) ||
			snapshot.eyes != producerCameras ||
			snapshot.scalars.jitterX != 0.25f || snapshot.scalars.jitterY != -0.375f ||
			snapshot.scalars.cameraNear != 5.0f || snapshot.scalars.cameraFar != 100000.0f ||
			snapshot.scalars.verticalFov != 1.5f || snapshot.scalars.frameTimeMilliseconds != 11.1f ||
			!snapshot.scalars.historyReset) {
			return false;
		}

		++key.frame;
		return snapshot.Publish(key, scalars, cameras) && snapshot.Matches(key) &&
		       snapshot.eyes == cameras && snapshot.scalars.jitterX == scalars.jitterX &&
		       !snapshot.scalars.historyReset;
	}

	bool ExactContractAndSameFrameInvalidation()
	{
		const auto key = ValidKey();
		const auto scalars = ValidScalars();
		constexpr std::array fields{
			&Policy::Key::generation, &Policy::Key::method,
			&Policy::Key::inputWidth, &Policy::Key::inputHeight,
			&Policy::Key::outputWidth, &Policy::Key::outputHeight
		};
		for (auto field : fields) {
			Snapshot snapshot;
			if (!snapshot.Publish(key, scalars, {}))
				return false;
			auto changed = key;
			++(changed.*field);
			if (snapshot.Matches(changed) || snapshot.Publish(changed, scalars, {}) ||
				snapshot.Matches(key) || snapshot.Publish(key, scalars, {}))
				return false;
			++changed.frame;
			if (!snapshot.Publish(changed, scalars, {}) || !snapshot.Matches(changed))
				return false;
		}

		Snapshot snapshot;
		snapshot.Invalidate();
		if (!snapshot.Publish(key, scalars, {}))
			return false;
		snapshot.Invalidate();
		return !snapshot.Matches(key) && !snapshot.Publish(key, scalars, {});
	}

	bool InvalidInputsCannotBecomePublished()
	{
		const auto key = ValidKey();
		const auto scalars = ValidScalars();
		constexpr std::array scalarFields{
			&Policy::Scalars::jitterX, &Policy::Scalars::jitterY,
			&Policy::Scalars::cameraNear, &Policy::Scalars::cameraFar,
			&Policy::Scalars::verticalFov, &Policy::Scalars::frameTimeMilliseconds
		};
		for (auto field : scalarFields) {
			for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
				Snapshot snapshot;
				auto invalidScalars = scalars;
				invalidScalars.*field = invalid;
				if (snapshot.Publish(key, invalidScalars, {}) || snapshot.valid ||
					snapshot.Publish(key, scalars, {}))
					return false;
			}
		}
		constexpr std::array dimensionFields{
			&Policy::Key::inputWidth, &Policy::Key::inputHeight,
			&Policy::Key::outputWidth, &Policy::Key::outputHeight
		};
		for (auto field : dimensionFields) {
			for (const auto invalid : { 0u, 16385u, std::numeric_limits<std::uint32_t>::max() }) {
				Snapshot snapshot;
				auto invalidKey = key;
				invalidKey.*field = invalid;
				if (snapshot.Publish(invalidKey, scalars, {}) || snapshot.valid)
					return false;
			}
		}
		auto invalidScalars = scalars;
		invalidScalars.cameraNear = 0.0f;
		if (Policy::IsValid(invalidScalars))
			return false;
		invalidScalars = scalars;
		invalidScalars.cameraFar = scalars.cameraNear;
		if (Policy::IsValid(invalidScalars))
			return false;
		invalidScalars = scalars;
		invalidScalars.verticalFov = 3.141592654f;
		if (Policy::IsValid(invalidScalars))
			return false;
		invalidScalars = scalars;
		invalidScalars.frameTimeMilliseconds = -1.0f;
		return !Policy::IsValid(invalidScalars);
	}

	bool FrameOrderingAndNativeGeneration()
	{
		Snapshot snapshot;
		auto key = ValidKey();
		auto scalars = ValidScalars();
		key.frame = std::numeric_limits<std::uint32_t>::max() - 1;
		if (!snapshot.Publish(key, scalars, {}))
			return false;
		auto stale = key;
		--stale.frame;
		if (snapshot.Publish(stale, scalars, {}) || !snapshot.Matches(key))
			return false;
		key.frame = 0;
		key.generation = 0;
		scalars.frameTimeMilliseconds = 0.0f;
		if (!snapshot.Publish(key, scalars, {}) || !snapshot.Matches(key))
			return false;
		stale.frame = std::numeric_limits<std::uint32_t>::max();
		return !Policy::IsValid(stale) && !snapshot.Publish(stale, scalars, {}) && snapshot.Matches(key);
	}

	bool RecoveryResetCanStrengthenCapturedDecision()
	{
		Snapshot snapshot;
		auto key = ValidKey();
		auto scalars = ValidScalars();
		scalars.historyReset = false;
		if (!snapshot.Publish(key, scalars, {}) ||
			Policy::ResolveHistoryReset(snapshot.scalars.historyReset, false))
			return false;
		const bool leftEyeReset = Policy::ResolveHistoryReset(snapshot.scalars.historyReset, true);
		const bool rightEyeReset = Policy::ResolveHistoryReset(snapshot.scalars.historyReset, true);
		if (!leftEyeReset || !rightEyeReset || snapshot.scalars.historyReset)
			return false;

		++key.frame;
		scalars.historyReset = true;
		return snapshot.Publish(key, scalars, {}) &&
		       Policy::ResolveHistoryReset(snapshot.scalars.historyReset, false) &&
		       Policy::ResolveHistoryReset(snapshot.scalars.historyReset, true);
	}

	bool CompositorCycleSurvivesDesktopPresent()
	{
		Snapshot snapshot;
		auto producer = ValidKey();
		producer.frame = 7;
		producer.compositorCycle = 10;
		if (!snapshot.Publish(producer, ValidScalars(), {}))
			return false;
		auto submission = producer;
		submission.frame = 8;
		if (!snapshot.MatchesForDispatch(submission) || snapshot.Matches(submission))
			return false;
		++submission.compositorCycle;
		if (snapshot.MatchesForDispatch(submission))
			return false;
		if (!snapshot.Publish(submission, ValidScalars(), {}) || !snapshot.MatchesForDispatch(submission))
			return false;
		if (snapshot.Publish(producer, ValidScalars(), {}) || !snapshot.MatchesForDispatch(submission))
			return false;

		auto incompatible = submission;
		++incompatible.generation;
		if (snapshot.MatchesForDispatch(incompatible))
			return false;
		incompatible = submission;
		++incompatible.frame;
		if (snapshot.Publish(incompatible, ValidScalars(), {}) || snapshot.valid)
			return false;
		++incompatible.compositorCycle;
		if (!snapshot.Publish(incompatible, ValidScalars(), {}))
			return false;

		Snapshot beforePoses;
		producer.compositorCycle = 0;
		if (!beforePoses.Publish(producer, ValidScalars(), {}))
			return false;
		submission = producer;
		++submission.frame;
		return !beforePoses.MatchesForDispatch(submission);
	}

	bool CycleWrapAndNewerFrameTokenRemainSafe()
	{
		Snapshot snapshot;
		auto key = ValidKey();
		key.compositorCycle = Policy::MaxCompositorCycle;
		if (!snapshot.Publish(key, ValidScalars(), {}))
			return false;
		key.compositorCycle = 1;
		++key.frame;
		if (!snapshot.Publish(key, ValidScalars(), {}) || !snapshot.MatchesForDispatch(key))
			return false;
		auto stale = key;
		stale.compositorCycle = Policy::MaxCompositorCycle;
		if (snapshot.Publish(stale, ValidScalars(), {}) || !snapshot.MatchesForDispatch(key))
			return false;

		StreamlineFrameTokenPublication::Coordinator<std::uint32_t> tokens;
		const auto acquire = [](std::uint32_t frame) -> std::optional<std::uint32_t> { return frame; };
		if (!tokens.Resolve(key.frame, acquire) || !tokens.Resolve(key.frame + 1u, acquire))
			return false;
		// A valid same-cycle snapshot does not authorize reuse of a retired SDK token.
		return !tokens.Resolve(snapshot.key.frame, acquire);
	}

	bool NewCycleRequiresNewLogicalFrame()
	{
		Snapshot snapshot;
		auto producer = ValidKey();
		producer.compositorCycle = 10;
		if (!snapshot.Publish(producer, ValidScalars(), {}))
			return false;

		auto repeated = producer;
		++repeated.compositorCycle;
		if (snapshot.Publish(repeated, ValidScalars(), {}) || snapshot.valid ||
			snapshot.MatchesForDispatch(producer) || snapshot.MatchesForDispatch(repeated) ||
			snapshot.Publish(repeated, ValidScalars(), {})) {
			return false;
		}

		// A failed cycle cannot recapture after Present advances the logical frame.
		auto changedFrame = repeated;
		++changedFrame.frame;
		if (snapshot.Publish(changedFrame, ValidScalars(), {}) || snapshot.valid)
			return false;
		++repeated.compositorCycle;
		if (snapshot.Publish(repeated, ValidScalars(), {}) || snapshot.valid)
			return false;
		++repeated.frame;
		++repeated.compositorCycle;
		if (!snapshot.Publish(repeated, ValidScalars(), {}) || !snapshot.MatchesForDispatch(repeated))
			return false;

		// Before pose tracking starts, the same logical sample remains ineligible.
		Snapshot beforePoses;
		producer.compositorCycle = 0;
		if (!beforePoses.Publish(producer, ValidScalars(), {}))
			return false;
		producer.compositorCycle = 1;
		if (beforePoses.Publish(producer, ValidScalars(), {}) || beforePoses.valid)
			return false;
		++producer.frame;
		++producer.compositorCycle;
		if (!beforePoses.Publish(producer, ValidScalars(), {}))
			return false;

		Snapshot wrapped;
		producer.frame = std::numeric_limits<std::uint32_t>::max() - 1u;
		producer.compositorCycle = Policy::MaxCompositorCycle;
		if (!wrapped.Publish(producer, ValidScalars(), {}))
			return false;
		producer.compositorCycle = 1;
		if (wrapped.Publish(producer, ValidScalars(), {}) || wrapped.valid)
			return false;
		producer.frame = 0;
		++producer.compositorCycle;
		return wrapped.Publish(producer, ValidScalars(), {});
	}

	bool SubmitCachesFollowTheProducerAcrossPresent()
	{
		std::uint32_t cachedFrame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t cachedCycle = 0;
		unsigned stereoBatchCount = 0;
		const auto submit = [&](std::uint32_t a_frame, std::uint64_t a_cycle) {
			if (!Policy::MatchesProducer(cachedFrame, cachedCycle, a_frame, a_cycle)) {
				cachedFrame = a_frame;
				cachedCycle = a_cycle;
				++stereoBatchCount;
			}
		};

		// The first eye prepares and evaluates both FSR eyes. Its peer reuses that batch,
		// even when desktop Present advances the observed engine frame between submits.
		submit(7, 10);
		if (stereoBatchCount != 1)
			return false;
		submit(8, 10);
		if (stereoBatchCount != 1)
			return false;
		submit(8, 11);
		if (stereoBatchCount != 2)
			return false;

		// Resource teardown invalidates the frame sentinel; an equal cycle cannot revive it.
		cachedFrame = std::numeric_limits<std::uint32_t>::max();
		submit(8, 11);
		if (stereoBatchCount != 3)
			return false;

		// Before compositor pose tracking starts, only an exact engine frame is reusable.
		submit(8, 0);
		submit(8, 0);
		if (stereoBatchCount != 4)
			return false;
		submit(9, 0);
		return stereoBatchCount == 5 &&
		       !Policy::MatchesProducer(cachedFrame, cachedCycle, std::numeric_limits<std::uint32_t>::max(), cachedCycle);
	}

	bool PendingRecoveryResetSurvivesDesktopPresent()
	{
		Snapshot snapshot;
		auto producer = ValidKey();
		producer.compositorCycle = 10;
		auto scalars = ValidScalars();
		scalars.historyReset = false;
		if (!snapshot.Publish(producer, scalars, {}))
			return false;
		auto submission = producer;
		++submission.frame;
		if (!snapshot.MatchesForDispatch(submission))
			return false;

		// A prepared-input cache hit after Present leaves the engine's new frame unlatched.
		// A recovery request must still reset both temporal consumers in the submit scope.
		const bool historyResetThisFrame = false;
		const bool historyResetRequested = true;
		const bool leftEyeReset = Policy::ResolveHistoryReset(snapshot.scalars.historyReset, historyResetThisFrame, historyResetRequested);
		const bool rightEyeReset = Policy::ResolveHistoryReset(snapshot.scalars.historyReset, historyResetThisFrame, historyResetRequested);
		return leftEyeReset && rightEyeReset && !snapshot.scalars.historyReset;
	}
}

int main()
{
	const std::array tests{
		CapturedInputsSurviveLaterCameraAndJitterChanges,
		ExactContractAndSameFrameInvalidation,
		InvalidInputsCannotBecomePublished,
		FrameOrderingAndNativeGeneration,
		RecoveryResetCanStrengthenCapturedDecision,
		CompositorCycleSurvivesDesktopPresent,
		CycleWrapAndNewerFrameTokenRemainSafe,
		NewCycleRequiresNewLogicalFrame,
		SubmitCachesFollowTheProducerAcrossPresent,
		PendingRecoveryResetSurvivesDesktopPresent
	};
	for (std::size_t index = 0; index < tests.size(); ++index) {
		if (!tests[index]()) {
			std::cerr << "Temporal snapshot case " << index << " failed\n";
			return 1;
		}
	}
	return 0;
}
