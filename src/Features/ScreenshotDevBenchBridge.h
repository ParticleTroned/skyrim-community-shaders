#pragma once

/** Registers bounded screenshot/stereo-sequence capture controls with DevBench. */
namespace ScreenshotDevBenchBridge
{
	void Install();
	bool IsBuilt();
	bool IsRegistered();
}
