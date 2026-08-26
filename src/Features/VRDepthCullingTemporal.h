#pragma once

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
		bool installed = false;
		bool cullingEnabled = false;
		Mode mode = Mode::Balanced;
		std::uint64_t envelopeMisses = 0;
		std::uint64_t totalPromoted = 0;
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
}
