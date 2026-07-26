#pragma once

#include "EngineFix.h"

/**
 * Prevents decals from being applied to soft-effect geometry.
 *
 * Soft-effect particles and billboards should not receive projected decals
 * such as blood spatters or impact marks. The engine does not mark this
 * geometry as decal-ineligible, so set the persistent geometry flag after
 * effect-shader setup has completed.
 */
struct EffectShaderNoDecalsFix : EngineFix
{
	std::string GetName() override { return "Effect Shader No Decals Fix"; }

	void Install() override;

private:
	struct BSEffectShaderProperty_SetupGeometry
	{
		static bool thunk(RE::BSEffectShaderProperty* a_property, RE::BSGeometry* a_geometry);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
