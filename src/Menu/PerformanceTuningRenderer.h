#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

struct Feature;

class PerformanceTuningRenderer
{
public:
	static void Render();
	/** Draws a shared feature switch through the existing reversible toggle path. */
	static void RenderFeatureEnabledControl(Feature* a_feature);
	/** Restores overview profiling when another panel is selected and no comparison is active. */
	static void NotifyOverviewInactive();
	/** Draws the shared cost test and results for a feature's current settings. */
	static void RenderFeatureMeasurement(Feature* a_feature, bool a_inlineButton = false);
	/** Invalidates measured costs when a feature's settings change. */
	static void NotifyFeatureSettingsChanged(Feature* a_feature);
	/** Advances an active cost test while the main settings window is closed. */
	static void UpdateClosedMenuMeasurement();
	/** Draws the non-interactive progress widget used by a closed-menu cost test. */
	static void RenderClosedMenuMeasurementOverlay();
	/** Cancels UI-owned comparisons, restoring runtime state before reopening the menu. */
	static bool CancelUserMeasurements();
	/** Invalidates saved toggle configurations and costs before loading settings. */
	static void NotifyConfigurationChanging();
	/** Cancels every cost test and restores any transient comparison state. */
	static void CancelActiveMeasurements();
	/** Starts the closed-menu phase after the settings window has closed. */
	static void NotifyMenuClosed();
	static bool HasActiveMeasurements();
	/** Starts one surfaced feature's closed-menu cost comparison. */
	static nlohmann::json StartDevBenchFeatureCostMeasurement(
		std::string_view a_featureShortName);
	/** Measures each enabled runtime feature using the shared on/off protocol. */
	static nlohmann::json StartDevBenchFeatureCostBatch();
	/** Changes a runtime toggle, retaining its prior enabled configuration. */
	static nlohmann::json SetDevBenchFeatureEnabled(
		std::string_view a_featureShortName, bool a_enabled);
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
