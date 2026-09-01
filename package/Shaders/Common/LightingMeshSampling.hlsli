#ifndef LIGHTING_MESH_SAMPLING_HLSLI
#define LIGHTING_MESH_SAMPLING_HLSLI

#if defined(TERRAIN_VARIATION) && !(defined(LANDSCAPE) || defined(LODLANDSCAPE) || defined(LOD_LAND_BLEND) || defined(LOD) || defined(SKIN) || defined(SKINNED) || defined(HAIR) || defined(EYE) || defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS) || defined(DEPTH_WRITE_DECALS) || defined(DO_ALPHA_TEST) || defined(PROJECTED_UV) || defined(SPARKLE) || defined(MULTI_LAYER_PARALLAX))
#	define TERRAIN_VARIATION_MESH
#	include "TerrainVariation/TerrainVariation.hlsli"
#endif

// These macros share the pre-parallax lattice while retaining each map's sampling contract.
#if defined(TERRAIN_VARIATION_MESH)
#	define MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, STOCHASTIC, FALLBACK)                \
		{                                                                               \
			[branch] if (applyMeshTV) { DEST = STOCHASTIC(TEX, SAMP, UV, meshOffset); } \
			else                                                                        \
			{                                                                           \
				DEST = FALLBACK;                                                        \
			}                                                                           \
		}
#else
#	define MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, STOCHASTIC, FALLBACK) DEST = FALLBACK
#endif

#define MESH_TV_COLOR(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffect, TEX.Sample(SAMP, UV))
#define MESH_TV_COLOR_BIAS(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffect, TEX.SampleBias(SAMP, UV, SharedData::MipBias))
#define MESH_TV_NORMAL(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffectNormal, TEX.Sample(SAMP, UV))
#define MESH_TV_NORMAL_BIAS(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffectNormal, TEX.SampleBias(SAMP, UV, SharedData::MipBias))
#define MESH_TV_DATA(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffectMaterialData, TEX.Sample(SAMP, UV))
#define MESH_TV_DATA_BIAS(DEST, TEX, SAMP, UV) MESH_TV_SAMPLE_AS(DEST, TEX, SAMP, UV, StochasticEffectMaterialData, TEX.SampleBias(SAMP, UV, SharedData::MipBias))

#endif
