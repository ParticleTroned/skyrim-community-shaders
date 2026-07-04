#pragma once

class PerformanceTuningRenderer
{
public:
	static void Render();
	static void CancelActiveMeasurements(bool includePending = false);
	static void NotifyMenuClosed();
	static bool HasActiveMeasurements();
};
