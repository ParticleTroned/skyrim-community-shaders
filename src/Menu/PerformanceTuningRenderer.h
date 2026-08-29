#pragma once

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
};
