#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <nlohmann/json_fwd.hpp>

struct Feature;

class PerformanceTuningRenderer
{
public:
	enum class CancelMode
	{
		RunningOnly,
		ClearSession
	};

	static void Render();
	static void RenderFeatureEnabledControl(Feature* feature);
	static void NotifyOverviewInactive();
	static void RenderMeasurementSuite(Feature* feature = nullptr);
	static void NotifyFeatureSettingsChanged(Feature* feature);
	static bool CancelUserMeasurements();
	/** Restore and release measurement ownership before applying a configuration. False defers the change. */
	static bool NotifyConfigurationChanging();
	/** Advances the cost test once per frame while the settings menu is closed. */
	static void UpdateClosedMenuMeasurement();
	/** Draws the non-interactive, top-centre cost-test countdown. */
	static void RenderClosedMenuMeasurementOverlay();
	/** Starts an armed test after menu closure, or cancels an ordinary session. */
	static void NotifyMenuClosed();
	static void CancelActiveMeasurements(CancelMode mode = CancelMode::ClearSession);
	static bool HasActiveMeasurements();
	static bool PrepareForSceneUpdate();
	static bool PrepareForSceneSettingsTransition();
#ifdef DEVBENCH_BRIDGE_ENABLED
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
