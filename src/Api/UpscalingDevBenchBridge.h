#pragma once

namespace CSX::Api::UpscalingDevBenchBridge
{
	/** Registers the DevBench adapter for the public csx.upscaling ABI. */
	void Install();

	bool IsBuilt();
	bool IsRegistered();
}
