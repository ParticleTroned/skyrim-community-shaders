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
	static void CancelActiveMeasurements(CancelMode mode = CancelMode::ClearSession);
	static bool HasActiveMeasurements();
	static bool PrepareForSceneUpdate();
	static bool PrepareForSceneSettingsTransition();
};
