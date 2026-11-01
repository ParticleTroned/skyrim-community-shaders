#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#	include "EngineFixes/SunShadowReceiverFix.h"

namespace Diagnostics::SunReceiverTrace
{
	/** Install verified receiver hooks; mask repairs require an explicit bounded capture. */
	void Install();
	/** Whether an explicit trace currently owns receiver policy. */
	bool Active() noexcept;
	/** Override ordinary repair during a capture, otherwise use the shared production path. */
	RE::BSShaderProperty::RenderPassArray* ProcessPasses(RE::BSLightingShaderProperty* property,
		RE::BSGeometry* geometry, std::uint32_t mode, RE::BSShaderAccumulator* accumulator, SunShadowReceiver::PassFunction original);
	/** Main-thread controls for bounded capture and terminal record retrieval. */
	nlohmann::json Handle(const nlohmann::json& args);
}
#endif
