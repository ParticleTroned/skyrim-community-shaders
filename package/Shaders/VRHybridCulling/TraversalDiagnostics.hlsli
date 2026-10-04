#ifndef CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI
#define CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI

// The diagnostic permutation is compiled and selected only by the DevBench bridge.
#ifdef CSX_HIZ_DIAGNOSTICS
struct HiZTraversalDiagnostic
{
	uint4 traversal;
	uint4 proofs;
};

#	define HIZ_CLIP_CROSSING 2
#	define HIZ_VIEWPORT_GUARD 3
#	define HIZ_INVALID_INPUT 4
#	define HIZ_DEPTH_BUDGET 5
#	define HIZ_FINEST_UNRESOLVED 6
#	define HIZ_STACK_CAPACITY 7
#	define HIZ_NEAREST_UNRESOLVED 8
#	define HIZ_VIEWPORT_OFFSCREEN 9
#	define HIZ_VIEWPORT_PARTIAL 10
#	define HIZ_DIAGNOSTIC_PARAMETERS , inout HiZTraversalDiagnostic diagnostic
#	define HIZ_DIAGNOSTIC_ARGUMENT , diagnostic
#	define HIZ_COUNT_DEPTH ++diagnostic.traversal.y
#	define HIZ_COUNT_REGION ++diagnostic.traversal.z
#	define HIZ_COUNT_TRIANGLE ++diagnostic.traversal.w
#	define HIZ_COUNT_PLANE_PROOF ++diagnostic.proofs.x
#	define HIZ_COUNT_POLYGON_CLIP ++diagnostic.proofs.y
#	define HIZ_COUNT_FACE_BIAS_PROOF ++diagnostic.proofs.z
#	define HIZ_COUNT_TRIANGLE_BIAS_PROOF ++diagnostic.proofs.w
#	define HIZ_VISIBLE(reason)              \
		{                                    \
			diagnostic.traversal.x = reason; \
			return false;                    \
		}
#	define HIZ_OCCLUDED                \
		{                               \
			diagnostic.traversal.x = 1; \
			return true;                \
		}
#else
#	define HIZ_DIAGNOSTIC_PARAMETERS
#	define HIZ_DIAGNOSTIC_ARGUMENT
#	define HIZ_COUNT_DEPTH
#	define HIZ_COUNT_REGION
#	define HIZ_COUNT_TRIANGLE
#	define HIZ_COUNT_PLANE_PROOF
#	define HIZ_COUNT_POLYGON_CLIP
#	define HIZ_COUNT_FACE_BIAS_PROOF
#	define HIZ_COUNT_TRIANGLE_BIAS_PROOF
#	define HIZ_VISIBLE(reason) return false
#	define HIZ_OCCLUDED return true
#endif

#endif
