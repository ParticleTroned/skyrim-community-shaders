#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "VRDepthCullingTelemetry.h"
#	include "VRDepthCullingTelemetryPolicy.h"
#	include "VRNativeVisibilityTelemetry.h"

#	include <array>
#	include <cstddef>
#	include <cstdint>
#	include <optional>
#endif

namespace VRDepthCullingTemporal
{
	enum class Mode
	{
		Balanced = 0,
		Legacy = 2,
		Hybrid = 3
	};

	/** Resolve the persisted legacy preference independently of logging mode. */
	constexpr Mode SelectMode(bool a_legacyMode)
	{
		return a_legacyMode ? Mode::Legacy : Mode::Balanced;
	}

	/** Preserve supported mode identities; retired or unknown values use the default. */
	constexpr Mode NormalizeMode(Mode a_mode)
	{
		return a_mode == Mode::Hybrid ? Mode::Hybrid : SelectMode(a_mode == Mode::Legacy);
	}

	/** Return the stable DevBench name for an effective temporal policy. */
	constexpr const char* GetModeName(Mode a_mode)
	{
		switch (a_mode) {
		case Mode::Balanced:
			return "balanced";
		case Mode::Legacy:
			return "legacy";
		case Mode::Hybrid:
			return "hybrid";
		}
		return "unknown";
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	struct Status
	{
		static constexpr std::size_t DurationBinCount = VRDepthCullingTelemetryPolicy::DurationBinCount;

		bool installed = false;
		bool hybridInstalled = false;
		bool cullingEnabled = false;
		std::optional<bool> engineCullingEnabled;
		std::optional<float> engineMinimumExtent;
		bool telemetryEnabled = true;
		bool telemetryFrozen = false;
		Mode mode = Mode::Balanced;
		std::uint64_t cullingEpoch = 0;
		std::uint64_t measurementWindowId = 0;
		std::uint64_t measurementStartEpoch = 0;
		std::uint32_t measurementStartFrame = 0;
		bool measurementWindowCurrent = false;
		VRDepthCullingTelemetry::StageTiming nativeReadback, outerDownscale, replayDownscale, nativeProducer;
		VRNativeVisibilityTelemetry::Status nativeVisibility;
		std::uint64_t envelopeMisses = 0;
		std::uint64_t recoveryAttempts = 0;
		std::uint64_t objectsInspected = 0;
		std::uint64_t invalidTransforms = 0;
		std::uint64_t invalidMotionEnvelopes = 0;
		std::uint64_t frustumTests = 0;
		std::uint64_t totalEligible = 0;
		std::uint64_t totalPromoted = 0;
		std::uint64_t totalDurationNanoseconds = 0;
		std::uint64_t maximumDurationNanoseconds = 0;
		std::array<std::uint64_t, DurationBinCount> durationHistogram{};
		std::uint32_t lastObjectCount = 0;
		std::uint32_t lastEligibleCount = 0;
		std::uint32_t lastPromotedCount = 0;
	};
#endif

	/** Install the Skyrim VR 1.4.15 producer and readback hooks. */
	void Install();
	/** Enable temporal work only while native depth culling is active. */
	void SetCullingEnabled(bool a_enabled);
	/** Publish one temporal policy from the main-thread settings path. */
	void SetMode(Mode a_mode);
	/** Return the mode currently observed by the render thread. */
	[[nodiscard]] Mode GetMode();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Return thread-safe diagnostics for DevBench inspection. */
	[[nodiscard]] Status GetStatus();
	/** Share admission between Advanced and Hybrid samples and their combined reset. */
	[[nodiscard]] VRDepthCullingTelemetryPolicy::WriterGate& GetTelemetryGate() noexcept;
	/** Enable or disable Advanced and Hybrid telemetry without changing culling behavior. */
	void SetTelemetryEnabled(bool a_enabled);
	/** Reset on the main thread only when no writer is active; start one explicit measurement window. */
	[[nodiscard]] bool TryResetStatus();
#endif
}
