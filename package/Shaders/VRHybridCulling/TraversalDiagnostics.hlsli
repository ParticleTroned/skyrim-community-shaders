#ifndef CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI
#define CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI

// The diagnostic permutation is compiled and selected only by the DevBench bridge.
#ifdef CSX_HIZ_DIAGNOSTICS
#	define HIZ_CLIP_CROSSING 2
#	define HIZ_VIEWPORT_GUARD 3
#	define HIZ_INVALID_INPUT 4
#	define HIZ_DEPTH_BUDGET 5
#	define HIZ_FINEST_UNRESOLVED 6
#	define HIZ_STACK_CAPACITY 7
#	define HIZ_DIAGNOSTIC_PARAMETERS , inout uint4 diagnostic
#	define HIZ_DIAGNOSTIC_ARGUMENT , diagnostic
#	define HIZ_COUNT_DEPTH ++diagnostic.y
#	define HIZ_COUNT_REGION ++diagnostic.z
#	define HIZ_COUNT_TRIANGLE ++diagnostic.w
#	define HIZ_VISIBLE(reason)    \
		{                          \
			diagnostic.x = reason; \
			return false;          \
		}
#	define HIZ_OCCLUDED      \
		{                     \
			diagnostic.x = 1; \
			return true;      \
		}
#else
#	define HIZ_DIAGNOSTIC_PARAMETERS
#	define HIZ_DIAGNOSTIC_ARGUMENT
#	define HIZ_COUNT_DEPTH
#	define HIZ_COUNT_REGION
#	define HIZ_COUNT_TRIANGLE
#	define HIZ_VISIBLE(reason) return false
#	define HIZ_OCCLUDED return true
#endif

#endif
