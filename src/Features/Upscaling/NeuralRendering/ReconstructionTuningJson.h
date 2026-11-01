#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ReconstructionTuning.h"
#	include <nlohmann/json.hpp>

namespace NeuralRendering
{
	/** Record the exact transient controls alongside requested and frozen conditions. */
	inline nlohmann::json ReconstructionTuningJson(const ReconstructionTuning& tuning)
	{
		return { { "sessionOnly", true }, { "shadowTransferPercent", tuning.shadowTransferPercent },
			{ "footprintSubdivisions", tuning.footprintSubdivisions }, { "perChannelClamping", tuning.perChannelClamping },
			{ "depthSampleMotion", tuning.depthSampleMotion } };
	}
}
#endif
