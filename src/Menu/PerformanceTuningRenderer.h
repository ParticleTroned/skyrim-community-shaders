#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

struct Feature;

class PerformanceTuningRenderer
{
public:
	static void Render();
	/** Preserve the saved baseline and reversible history; reject unsafe runtime states. */
	static bool SetFeatureEnabled(Feature* a_feature, bool a_enabled);
	/** Shared admission reason for a runtime feature switch. */
	static const char* GetFeatureControlBlockReason(Feature* a_feature);
	/** Resolve legacy comparison/scan links to the corresponding radio selection. */
	static void SelectView(std::string_view a_section);
	/** Scene ownership also applies to selections that do not switch a feature off. */
	static const char* GetFeatureSceneControlBlockReason(Feature* a_feature);
	/** Apply settings while preserving reversible toggle history on failure. */
	static bool ApplyFeatureSettingsTransaction(const std::vector<Feature*>& a_features, const std::function<bool()>& a_apply, bool a_replaceToggleHistory = false);
	/** Preserve reversible On/Off configurations across an explicit editor Restore. */
	static nlohmann::json CaptureFeatureToggleHistory();
	static void RestoreFeatureToggleHistory(const nlohmann::json& a_history);
	/** Draws the reversible feature switch; custom help never replaces a blocking reason. */
	static void RenderFeatureEnabledControl(Feature* a_feature, const char* a_tooltip = nullptr);
	/** Persistent toggle failure for the header or the feature's profiling row. */
	static const char* GetFeatureToggleFailureReason(Feature* a_feature);
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
#endif
	/** Starts one surfaced feature's closed-menu cost comparison. */
	static nlohmann::json StartApiFeatureCostMeasurement(
		std::string_view a_featureShortName);
	/** Captures all instrumented features together without changing feature settings. */
	static nlohmann::json StartApiQuickScan(bool batch = false, bool recordFpsVR = false);
	/** Measures each enabled runtime feature using the shared on/off protocol. */
	static nlohmann::json StartApiFeatureCostBatch();
	/** Changes a runtime toggle, retaining its prior enabled configuration. */
	static nlohmann::json SetApiFeatureEnabled(
		std::string_view a_featureShortName, bool a_enabled);
	/** Starts a hardware-specific Upscaling cost sweep relative to None. */
	static nlohmann::json StartApiUpscalingCostSweep(
		std::string_view a_matrix = "auto",
		std::string_view a_dlssPreset = {});
	/** Returns sweep results and a bounded page of raw timing diagnostics. */
	static nlohmann::json GetApiMeasurementStatus(
		std::uint64_t a_traceAfterSequence = 0,
		std::size_t a_maximumTraceSamples = 128);
	/** Returns the current saved run identity on the runtime main thread. */
	static std::string GetApiMeasurementRunId();
	/** Cancels DevBench-owned measurement work and restores its original state. */
	static nlohmann::json CancelApiMeasurements();
};
