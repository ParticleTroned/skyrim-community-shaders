#pragma once

/** @brief Registers the render-scale iteration tool with the external devbench host. */
namespace VRRenderScaleDevBenchBridge
{
	/**
	 * @brief Installs the optional MCP/REST bridge after SKSE data loading.
	 *
	 * This is idempotent and becomes a no-op when the bridge was disabled at
	 * build time or the external devbench host is not installed.
	 */
	void Install();

	/** @brief Returns whether this binary contains devbench API support. */
	bool IsBuilt();

	/** @brief Returns whether the render-scale tool registered with a live host. */
	bool IsRegistered();
}
