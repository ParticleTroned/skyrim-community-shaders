#pragma once

#include "EngineFix.h"

/** @brief Polls native sun visibility without forcing GPU command submission. */
struct SunOcclusionPollFix : EngineFix
{
	std::string GetName() override { return "Sun Occlusion Poll Fix"; }
	/** @brief Retains native polling when addresses or instructions are unsupported. */
	bool TryInstall() override;

private:
	bool installed = false;
};
