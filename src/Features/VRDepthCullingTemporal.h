#pragma once

#include "VRDepthCullingTelemetryPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VRDepthCullingTemporal
{
	enum class Mode
	{
		Balanced,
		Performance,
		Legacy
	};

	/** Resolve persisted toggles to one policy; malformed conflicts fall back to Balanced. */
	constexpr Mode SelectMode(bool a_performanceMode, bool a_legacyMode)
	{
		if (a_performanceMode == a_legacyMode)
			return Mode::Balanced;
		return a_performanceMode ? Mode::Performance : Mode::Legacy;
	}

	/** Return the stable DevBench name for an effective temporal policy. */
	constexpr const char* GetModeName(Mode a_mode)
	{
		switch (a_mode) {
		case Mode::Balanced:
			return "balanced";
		case Mode::Performance:
			return "performance";
		case Mode::Legacy:
			return "legacy";
		}
		return "unknown";
	}

	struct Status
	{
		static constexpr std::size_t DurationBinCount = VRDepthCullingTelemetryPolicy::DurationBinCount;

		bool installed = false;
		bool cullingEnabled = false;
		bool telemetryEnabled = true;
		Mode mode = Mode::Balanced;
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

	/** Install the Skyrim VR 1.4.15 producer and readback hooks. */
	void Install();
	/** Enable temporal work only while native depth culling is active. */
	void SetCullingEnabled(bool a_enabled);
	/** Publish one temporal policy from the main-thread settings path. */
	void SetMode(Mode a_mode);
	/** Return the mode currently observed by the render thread. */
	[[nodiscard]] Mode GetMode();
	/** Return thread-safe diagnostics for DevBench inspection. */
	[[nodiscard]] Status GetStatus();
	/** Enable or disable recovery-path telemetry without changing culling behavior. */
	void SetTelemetryEnabled(bool a_enabled);
	/** Reset recovery telemetry when no render-depth writer is active. */
	[[nodiscard]] bool TryResetStatus();
}
