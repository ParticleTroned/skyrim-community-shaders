#pragma once

#include "EngineFix.h"

/** Keeps SKSE VR task callbacks outside the queue's producer lock. */
struct VRTaskQueueFix : EngineFix
{
	std::string GetName() override { return "VR Task Queue Callback Fix"; }
	/** Installs on a supported, unmodified task pump before world-task execution. */
	bool TryInstall() override;
};
