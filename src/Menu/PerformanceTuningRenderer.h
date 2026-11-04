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
	/** Draws the reversible feature switch; custom help never replaces a blocking reason. */
	static void RenderFeatureEnabledControl(Feature* a_feature, const char* a_tooltip = nullptr);
	/** Restores temporary profiling when no tuning panel or comparison is active. */
	static void NotifyOverviewInactive();
	/** Draws live counters, measurement controls and results; null compares all features. */
	static void RenderMeasurementSuite(Feature* a_feature = nullptr);
	/** Invalidates measured costs when a feature's settings change. */
	static void NotifyFeatureSettingsChanged(Feature* a_feature);
	/** Advances active scans and cost tests while the main settings window is closed. */
	static void UpdateClosedMenuMeasurement();
	/** Draws the non-interactive progress widget used by a closed-menu cost test. */
	static void RenderClosedMenuMeasurementOverlay();
	/** Cancels UI-owned scans or comparisons and restores their owned runtime state. */
	static bool CancelUserMeasurements();
	/** Invalidates saved toggle configurations and costs before loading settings. */
	static void NotifyConfigurationChanging();
	/** Cancels every scan and cost test and restores transient measurement state. */
	static void CancelActiveMeasurements();
	/** Starts the closed-menu phase after the settings window has closed. */
	static void NotifyMenuClosed();
	static bool HasActiveMeasurements();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Captures existing feature settings to invalidate externally changed comparisons. */
	static nlohmann::json CaptureComparisonSettings();
	/** @brief Shared gameplay/editor admission check for captured-setting comparisons. */
	static const char* GetComparisonEnvironmentError();
	/** @brief Shared scene/weather ownership check for reversible feature controls. */
	static const char* GetComparisonFeatureBlockReason(Feature* feature);
	/** Starts one surfaced feature's closed-menu cost comparison. */
	static nlohmann::json StartDevBenchFeatureCostMeasurement(
		std::string_view a_featureShortName);
	/** Captures all instrumented features together without changing feature settings. */
	static nlohmann::json StartDevBenchQuickScan();
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
#endif
};
