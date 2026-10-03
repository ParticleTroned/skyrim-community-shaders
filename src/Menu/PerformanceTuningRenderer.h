#pragma once

class PerformanceTuningRenderer
{
public:
	enum class CancelMode
	{
		RunningOnly,
		ClearSession
	};

	static void Render();
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
};
