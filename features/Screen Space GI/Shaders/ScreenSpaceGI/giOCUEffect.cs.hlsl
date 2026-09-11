// Separate entry point: a stale loose GI shader must fail experimental
// compilation rather than silently ignore a new define and report active.
#define OCU_EFFECT_FOVEATION
#include "ScreenSpaceGI/gi.cs.hlsl"

#if !defined(OCU_EFFECT_FOVEATION_GI_VERSION) || OCU_EFFECT_FOVEATION_GI_VERSION != 2
#	error OCU effect sampling requires the matching GI shader implementation.
#endif
#if !defined(OCU_EFFECT_FOVEATION_CONSTANTS_VERSION) || OCU_EFFECT_FOVEATION_CONSTANTS_VERSION != 2
#	error OCU effect sampling requires the matching effect constant-buffer helper.
#endif
