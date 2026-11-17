#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
namespace CSX::UpscalingAPI
{
	struct Snapshot001;
}
#endif

namespace CSX::Api::UpscalingDevBenchBridge
{
	/** Registers the DevBench adapter for the public csx.upscaling ABI. */
	void Install();

	bool IsBuilt();
	bool IsRegistered();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Serialize the native API snapshot without changing revision authority. */
	nlohmann::json BuildSnapshotJson(const UpscalingAPI::Snapshot001& a_snapshot);
#endif
}
