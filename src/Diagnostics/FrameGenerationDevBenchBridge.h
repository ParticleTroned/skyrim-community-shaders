#pragma once

namespace CSX::Diagnostics::FrameGenerationDevBenchBridge
{
	/** @brief Registers the bounded frame-generation diagnostic tool when DevBench is present. */
	void Install();

	/** @brief Returns whether the diagnostic tool is registered with DevBench. */
	[[nodiscard]] bool IsRegistered();
}
