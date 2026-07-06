#pragma once

class PerformanceTuningRenderer
{
public:
	static void Render();
	static void CancelActiveMeasurements(bool includePending = false);
	static bool HasActiveMeasurements();
};
