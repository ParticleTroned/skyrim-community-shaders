#ifndef CSX_LIGHTING_FLAGS_HLSLI
#define CSX_LIGHTING_FLAGS_HLSLI

#include "Common/Permutation.hlsli"

namespace Lighting
{
	/** Restores material flags omitted from the pixel shader's compile key. */
	uint GetFlags(uint pixelDescriptor)
	{
#if !defined(LIGHTING) || defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LODLANDSCAPE)
		uint flags = 0;
#elif defined(LANDSCAPE)
		// Other lighting maps alias landscape normal textures.
		uint flags = pixelDescriptor & Permutation::LightingFlags::Specular;
#else
		uint flags = pixelDescriptor & (Permutation::LightingFlags::Specular | Permutation::LightingFlags::SoftLighting | Permutation::LightingFlags::RimLighting | Permutation::LightingFlags::BackLighting);
#endif
#if defined(SPECULAR)
		flags |= Permutation::LightingFlags::Specular;
#endif
#if defined(SOFT_LIGHTING)
		flags |= Permutation::LightingFlags::SoftLighting;
#endif
#if defined(RIM_LIGHTING)
		flags |= Permutation::LightingFlags::RimLighting;
#endif
#if defined(BACK_LIGHTING)
		flags |= Permutation::LightingFlags::BackLighting;
#endif
		return flags;
	}

	/** Applies shading exclusions without changing shadow-visibility eligibility. */
	uint GetShadingFlags(uint pixelDescriptor)
	{
		uint flags = GetFlags(pixelDescriptor);
#if defined(SSS) && defined(SKIN) && defined(DEFERRED)
		flags &= ~Permutation::LightingFlags::SoftLighting;
#endif
		return flags;
	}

	bool HasSpecular()
	{
		return (GetShadingFlags(Permutation::PixelShaderDescriptor) & Permutation::LightingFlags::Specular) != 0;
	}

	bool HasSoftLighting()
	{
		return (GetShadingFlags(Permutation::PixelShaderDescriptor) & Permutation::LightingFlags::SoftLighting) != 0;
	}

	bool HasRimLighting()
	{
		return (GetShadingFlags(Permutation::PixelShaderDescriptor) & Permutation::LightingFlags::RimLighting) != 0;
	}

	bool HasBackLighting()
	{
		return (GetShadingFlags(Permutation::PixelShaderDescriptor) & Permutation::LightingFlags::BackLighting) != 0;
	}

	bool LoadRimSoftLighting()
	{
#if defined(LOAD_SOFT_LIGHTING)
		return true;
#else
		return HasRimLighting() || HasSoftLighting();
#endif
	}

	bool UseSkylightingShadowVisibility()
	{
		// Deferred skin still uses soft-shadow visibility when SSS replaces soft lighting.
		return (GetFlags(Permutation::PixelShaderDescriptor) & (Permutation::LightingFlags::SoftLighting | Permutation::LightingFlags::RimLighting | Permutation::LightingFlags::BackLighting)) != 0;
	}
}
#endif
