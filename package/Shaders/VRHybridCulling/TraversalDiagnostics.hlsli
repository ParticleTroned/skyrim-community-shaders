#ifndef CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI
#define CSX_HYBRID_TRAVERSAL_DIAGNOSTICS_HLSLI

#define HIZ_CLIP_CROSSING 2
#define HIZ_VIEWPORT_GUARD 3
#define HIZ_INVALID_INPUT 4
#define HIZ_DEPTH_BUDGET 5
#define HIZ_FINEST_UNRESOLVED 6
#define HIZ_STACK_CAPACITY 7
#define HIZ_NEAREST_UNRESOLVED 8
#define HIZ_VIEWPORT_OFFSCREEN 9
#define HIZ_VIEWPORT_PARTIAL 10

// The diagnostic permutation is compiled and selected only by the DevBench bridge.
#ifdef CSX_HIZ_DIAGNOSTICS
struct HiZTraversalDiagnostic
{
	uint4 traversal;
	// Keep the readback layout stable; unused proof lanes remain zero.
	uint4 proofs;
	uint4 planeWork;
	uint4 refinement;
};

#	define HIZ_DIAGNOSTIC_PARAMETERS , inout HiZTraversalDiagnostic diagnostic
#	define HIZ_DIAGNOSTIC_ARGUMENT , diagnostic
#	define HIZ_COUNT_DEPTH ++diagnostic.traversal.y
#	define HIZ_COUNT_REGION ++diagnostic.traversal.z
#	define HIZ_COUNT_TRIANGLE ++diagnostic.traversal.w
#	define HIZ_COUNT_PLANE_PROOF ++diagnostic.proofs.x
#	define HIZ_COUNT_POLYGON_CLIP ++diagnostic.proofs.y
#	define HIZ_COUNT_CLIP_PLANE ++diagnostic.planeWork.x
#	define HIZ_COUNT_CLIP_SKIP ++diagnostic.planeWork.y
#	define HIZ_COUNT_PLANE_BUILD ++diagnostic.planeWork.z
#	define HIZ_COUNT_PLANE_REUSE ++diagnostic.planeWork.w
#	define HIZ_COUNT_REFINED_CELL ++diagnostic.refinement.x
#	define HIZ_COUNT_SOURCE_PIXEL ++diagnostic.refinement.y
#	define HIZ_COUNT_RESOLVED_CELL ++diagnostic.refinement.z
#	define HIZ_COUNT_SOURCE_WITNESS ++diagnostic.refinement.w
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
#	define HIZ_COUNT_CLIP_PLANE
#	define HIZ_COUNT_CLIP_SKIP
#	define HIZ_COUNT_PLANE_BUILD
#	define HIZ_COUNT_PLANE_REUSE
#	define HIZ_COUNT_REFINED_CELL
#	define HIZ_COUNT_SOURCE_PIXEL
#	define HIZ_COUNT_RESOLVED_CELL
#	define HIZ_COUNT_SOURCE_WITNESS
#	define HIZ_VISIBLE(reason) return false
#	define HIZ_OCCLUDED return true
#endif

#endif
