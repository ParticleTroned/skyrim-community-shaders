#pragma once

#include "EngineFix.h"

/** Protects native VR grass traversal before it borrows shapes and instance groups. */
struct VRGrassLifetimeFix : EngineFix
{
	std::string GetName() override { return "VR Grass Lifetime Fix"; }
	bool TryInstall() override;
};
