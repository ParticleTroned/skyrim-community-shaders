#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

class PerformanceTuningRenderer
{
public:
	static void Render();
	/** Advances an active cost test while the main settings window is closed. */
	static void UpdateClosedMenuMeasurement();
	/** Draws the non-interactive progress widget used by a closed-menu cost test. */
	static void RenderClosedMenuMeasurementOverlay();
	/** Cancels every cost test and restores any transient comparison state. */
	static void CancelActiveMeasurements();
	/** Starts the closed-menu phase after the settings window has closed. */
	static void NotifyMenuClosed();
	static bool HasActiveMeasurements();
	/** Starts a hardware-specific Upscaling cost sweep relative to None. */
	static nlohmann::json StartDevBenchUpscalingCostSweep(
		std::string_view a_matrix = "auto",
		std::string_view a_dlssPreset = {});
	/** Returns sweep results and a bounded page of raw timing diagnostics. */
	static nlohmann::json GetDevBenchMeasurementStatus(
		std::uint64_t a_traceAfterSequence = 0,
		std::size_t a_maximumTraceSamples = 128);
	/** Cancels DevBench-owned measurement work and restores its original state. */
	static nlohmann::json CancelDevBenchMeasurements();
};
