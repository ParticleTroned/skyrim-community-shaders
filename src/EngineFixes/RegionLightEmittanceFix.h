#pragma once

#include "EngineFix.h"

/** @brief Preserves resolved regional light colours during weather transitions. */
struct RegionLightEmittanceFix : EngineFix
{
	std::string GetName() override { return "Region Light Emittance Fix"; }
	/** @brief Skips unsupported or modified weather argument instructions. */
	bool TryInstall() override;

private:
	bool installed = false;
};
