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
	/** @brief Advances a running cost measurement while the settings menu is closed. */
	static void UpdateActiveMeasurements();
	/** @brief Draws the compact, non-interactive progress widget for a running measurement. */
	static void RenderMeasurementOverlay();
	static void CancelActiveMeasurements(CancelMode mode = CancelMode::ClearSession);
	static bool HasActiveMeasurements();
	static bool PrepareForSceneUpdate();
	static bool PrepareForSceneSettingsTransition();
};
