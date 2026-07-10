#include <DirectXPackedVector.h>
#include "MOC.h"

#include "Globals.h"

#include <MaskedOcclusionCulling/CullingThreadpool.h>
#include <MaskedOcclusionCulling/MaskedOcclusionCulling.h>
#include <meshoptimizer.h>

#include <d3d11.h>
#include <winrt/base.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <RE/B/BSGeometry.h>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/B/BSSceneGraph.h>
#include <RE/B/BSMultiBound.h>
#include <RE/B/BSMultiBoundAABB.h>
#include <RE/B/BSMultiBoundNode.h>
#include <RE/B/BSMultiBoundShape.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/B/BSTriShape.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiMatrix3.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiPoint3.h>
#include <RE/N/NiRTTI.h>
#include <RE/N/NiTransform.h>
#include <RE/R/Renderer.h>
#include <RE/R/RendererShadowState.h>
#include <RE/L/LoadingMenu.h>
#include <RE/S/State.h>
#include <RE/U/UI.h>
#include <RE/V/VertexDesc.h>

using namespace DirectX;

namespace MOC
{
	// ---- Settings (defaults mirror Nukem's) ----
	bool  EnableOcclusionTesting = true;
	bool  EnableOccluderRendering = true;
	float OccluderMaxDistance = 20000.0f;
	float OccluderFirstLevelMinSize = 200.0f;
	// Cap on occluders rasterized per frame (closest-first after the sort). NOT a MOC
	// library limit -- a budget for the per-frame build. With the threaded raster +
	// simplified meshes the default covers everything a typical scene gathers.
	std::uint32_t MaxOccludersPerFrame = 256;
	// Worker threads for the CullingThreadpool raster (applied at boot).
	std::int32_t RasterThreads = 2;
	// LOD-simplify occluder meshes to ~half the indices at cache time (meshopt_simplify,
	// Nukem's parameters). Cuts raster cost ~2-3x; conservativeness is preserved within
	// the simplifier's error bound (1e-3 of mesh extents).
	bool SimplifyOccluders = true;
	// Only objects/subtrees with at least this world-bound radius are occlusion-tested.
	// Lower = more tested objects (more draws saved) at more test cost per frame.
	float OccluderTestMinRadius = 50.0f;

	namespace
	{
		// Lower res = far cheaper raster (coverage cost scales with pixels) at coarser but
		// still-conservative occlusion. 400 full-detail meshes @1280x720 cost ~65ms/frame.
		constexpr unsigned int MOC_WIDTH = 512;
		constexpr unsigned int MOC_HEIGHT = 288;

		MaskedOcclusionCulling* g_moc = nullptr;
		CullingThreadpool*      g_pool = nullptr;
		bool                    g_init = false;

		// Camera matrices for the current build (camera-relative, row-vector layout).
		XMMATRIX     g_view = XMMatrixIdentity();
		XMMATRIX     g_proj = XMMatrixIdentity();
		XMMATRIX     g_viewProj = XMMatrixIdentity();
		RE::NiPoint3 g_posAdjust{ 0.0f, 0.0f, 0.0f };
		XMVECTOR     g_posAdjustV = _mm_setzero_ps();  // (x, y, z, 0)
		fplanes      g_frustum{};

		// Once-per-frame build coordination. Main-scene culls run CONCURRENTLY on several
		// job threads (BuildSceneLists cell culls) plus the render thread, all reaching
		// BuildOccluders -- a plain frame compare let multiple threads build at once and
		// race on g_geoList (crash: null geometry in the raster loop). One thread CLAIMS
		// the frame (CAS), builds, then PUBLISHES via g_buildDone; concurrent losers skip
		// testing for their pass (conservative: everything stays visible) until published.
		std::atomic<std::uint32_t> g_buildClaim{ 0xFFFFFFFFu };
		std::atomic<std::uint32_t> g_buildDone{ 0xFFFFFFFFu };

		struct GeoEntry
		{
			RE::BSGeometry* geometry;
			float           distanceSquared;
			float           coverageScore;  // worldBound.radius / distance -- projected-size proxy
		};
		std::vector<GeoEntry> g_geoList;

		// Occluder-list BUILDER thread: the scene-graph walk + sort + vertex-conversion
		// pre-warm run here, OFF the engine's cull critical path. The claim thread only
		// enqueues the previously prepared list (fresh matrices, warm caches). The list is
		// one frame stale -- occluders are static meshes, and a newly streamed cell's
		// occluders appearing a frame late is conservative (less culling, never wrong).
		std::thread             g_builder;
		std::mutex              g_builderMtx;
		std::condition_variable g_builderCV;
		bool                    g_builderKick = false;
		bool                    g_builderQuit = false;
		std::vector<GeoEntry>   g_readyList;   // published by the builder, consumed by the claim thread
		bool                    g_readyValid = false;
		double                  g_lastGatherMs = 0.0;
		std::uint32_t              g_lastOccluderCount = 0;

		// Walk-shape tallies (written by the single build thread, read by its diag print).
		std::uint32_t g_cellsSeen = 0;
		std::uint32_t g_cellsCulled = 0;



		// Lightweight diagnostic tallies (logged from BuildOccluders, not per-test).
		std::atomic<std::uint64_t> g_tested{ 0 };
		std::atomic<std::uint64_t> g_culled{ 0 };
		std::atomic<std::uint64_t> g_testedAABB{ 0 };
		std::atomic<std::uint64_t> g_culledAABB{ 0 };
		std::atomic<std::uint64_t> g_testedSphere{ 0 };
		std::atomic<std::uint64_t> g_culledSphere{ 0 };
		std::chrono::high_resolution_clock::time_point g_buildStart;

		// Debug depth-buffer view: MOC's software depth rasterized to an RGBA8 texture
		// for display in the feature settings (ComputePixelDepthBuffer -> grayscale).
		winrt::com_ptr<ID3D11Texture2D>          g_debugTex;
		winrt::com_ptr<ID3D11ShaderResourceView> g_debugSRV;
		std::vector<float>                       g_debugDepth;

		// --- Per-mesh converted vertex/index cache, keyed by rendererData (stable GPU mesh ptr) ---
		struct IndexPair
		{
			std::uint32_t* Data = nullptr;
			std::uint32_t  Count = 0;  // total indices (== triCount * 3)
		};

		std::unordered_map<void*, float*>    g_vertMap;
		std::unordered_map<void*, IndexPair> g_indexMap;
		std::mutex                           g_cacheMutex;


		// WorldScenegraph global (holds a NiNode*). Nukem 0x2F4CE30 -> SE REL::ID(517006).
		REL::Relocation<RE::NiNode**> g_worldScenegraph{ REL::ID(517006) };

		// The engine's frozen CULL camera (0x1432333F0 in 1.5.97): the NiCamera object that
		// DrawWorld hands to every MAIN-scene cull (BuildSceneLists cell culls, MainAccum
		// subtree culls, RenderDepth, RenderWorld). Distinct from the render camera -- its
		// NiFrustum is normalized to (-1,1,1,-1), so it identifies the pass but can NOT
		// provide the projection.
		REL::Relocation<RE::NiCamera**> g_cullCamera{ REL::ID(528062) };

		// ---------------------------------------------------------------------
		// Vertex / index conversion (solved raw-geometry code, FULLPREC-aware).
		// ---------------------------------------------------------------------

		// Vertex-buffer SLOT 0 stride, straight from the desc bits -- the game's own
		// BSGeometry::CalculateVertexSize: (desc << 2) & 0x3C (lowest nibble * 4). This is
		// NUKEM-EXACT and NOT CommonLib's VertexDesc::GetSize() (which describes a
		// different, GPU-interleaved layout; deriving the raw stride from it produced
		// garbage triangle soup for most meshes).
		std::uint32_t GetVertexStride(const RE::BSGraphics::VertexDesc& a_desc)
		{
			const auto raw = reinterpret_cast<const std::uint64_t&>(a_desc);
			return static_cast<std::uint32_t>((raw << 2) & 0x3C);
		}

		std::uint32_t* ConvertIndices(const std::uint16_t* a_in, std::uint32_t a_count)
		{
			auto* out = new std::uint32_t[a_count];
			for (std::uint32_t i = 0; i < a_count; ++i)
				out[i] = a_in[i];
			return out;
		}

		// MOC float4 layout: (x, y, 1.0, z) -- W carries z, per Nukem. NUKEM-EXACT decode:
		// the slot-0 raw CPU data stores positions as PLAIN FLOATS at byte offset 0 of each
		// vertex (his working mod reads floats unconditionally -- no half-precision decode,
		// regardless of VF_FULLPREC, which describes the GPU-side packing only).
		float* ConvertVerts(const std::uint8_t* a_raw, std::uint32_t a_count, std::uint32_t a_stride)
		{
			auto* out = new float[static_cast<std::size_t>(a_count) * 4];
			for (std::uint32_t i = 0; i < a_count; ++i) {
				const auto* f = reinterpret_cast<const float*>(a_raw + static_cast<std::size_t>(i) * a_stride);
				out[i * 4 + 0] = f[0];
				out[i * 4 + 1] = f[1];
				out[i * 4 + 2] = 1.0f;
				out[i * 4 + 3] = f[2];
			}
			return out;
		}

		// BSShaderUtil::GetXMFromNiPosAdjust equivalent: local -> (world - posAdjust),
		// row-vector convention (uses R transposed since NiTransform is column-vector).
		XMMATRIX GetXMFromNiPosAdjust(const RE::NiTransform& t, const RE::NiPoint3& posAdjust)
		{
			const RE::NiMatrix3& R = t.rotate;
			const float          s = t.scale;

			XMMATRIX m;
			m.r[0] = XMVectorSet(s * R.entry[0][0], s * R.entry[1][0], s * R.entry[2][0], 0.0f);
			m.r[1] = XMVectorSet(s * R.entry[0][1], s * R.entry[1][1], s * R.entry[2][1], 0.0f);
			m.r[2] = XMVectorSet(s * R.entry[0][2], s * R.entry[1][2], s * R.entry[2][2], 0.0f);
			m.r[3] = XMVectorSet(t.translate.x - posAdjust.x,
				t.translate.y - posAdjust.y,
				t.translate.z - posAdjust.z, 1.0f);
			return m;
		}

		// Fetch (and lazily convert + cache) the MOC verts/indices for a static kTriShape.
		bool GetCachedGeometry(RE::BSGeometry* a_geom, IndexPair& a_outIndices, float*& a_outVerts)
		{
			if (a_geom->GetType().get() != RE::BSGeometry::Type::kTriShape)
				return false;

			auto&                     geomRT = a_geom->GetGeometryRuntimeData();
			RE::BSGraphics::TriShape* rendererData = geomRT.rendererData;
			if (!rendererData || !rendererData->rawVertexData || !rendererData->rawIndexData)
				return false;

			void* key = rendererData;

			std::scoped_lock lock(g_cacheMutex);

			bool haveIdx = false;
			bool haveVtx = false;
			if (auto it = g_indexMap.find(key); it != g_indexMap.end()) {
				a_outIndices = it->second;
				haveIdx = true;
			}
			if (auto it = g_vertMap.find(key); it != g_vertMap.end()) {
				a_outVerts = it->second;
				haveVtx = true;
			}
			if (haveIdx && haveVtx)
				return true;

			auto*               triShape = static_cast<RE::BSTriShape*>(a_geom);
			auto&               triRT = triShape->GetTrishapeRuntimeData();
			const std::uint32_t vertCount = triRT.vertexCount;
			const std::uint32_t triCount = triRT.triangleCount;
			if (triCount < 2 || vertCount == 0)
				return false;

			// The renderer's vertexDesc copy (identical to geomRT.vertexDesc).
			const std::uint32_t stride = GetVertexStride(rendererData->vertexDesc);
			if (stride == 0)
				return false;  // no position stream in slot 0

			if (!haveIdx) {
				IndexPair p;
				p.Count = triCount * 3;
				p.Data = ConvertIndices(reinterpret_cast<const std::uint16_t*>(rendererData->rawIndexData), p.Count);

				// LOD-simplify big occluder meshes at cache time (Nukem's parameters:
				// target half the indices, 1e-3 relative error, only above 300 indices).
				// The raw slot-0 buffer IS the float3-position stream meshopt expects.
				if (SimplifyOccluders && p.Count > 300) {
					const std::size_t simplified = meshopt_simplify(
						p.Data, p.Data, p.Count,
						reinterpret_cast<const float*>(rendererData->rawVertexData), vertCount, stride,
						static_cast<std::size_t>(p.Count * 0.5f), 1e-3f, 0, nullptr);
					p.Count = static_cast<std::uint32_t>(simplified);
				}

				g_indexMap.insert_or_assign(key, p);
				a_outIndices = p;
			}
			if (!haveVtx) {
				a_outVerts = ConvertVerts(reinterpret_cast<const std::uint8_t*>(rendererData->rawVertexData), vertCount, stride);
				g_vertMap.insert_or_assign(key, a_outVerts);

				// One-shot sanity: model-space positions of the first converted mesh should be
				// modest local coordinates (|v| within a few thousand units), not garbage.
				static std::atomic<bool> s_vertsLogged{ false };
				if (!s_vertsLogged.exchange(true)) {
					logger::info("[MOC][vert] first mesh: stride={} verts={} tris={} v0=({:.1f},{:.1f},{:.1f}) v1=({:.1f},{:.1f},{:.1f})",
						stride, vertCount, triCount,
						a_outVerts[0], a_outVerts[1], a_outVerts[3],
						a_outVerts[4], a_outVerts[5], a_outVerts[7]);
				}
			}
			return true;
		}

		// ---------------------------------------------------------------------
		// Matrix / camera resolution helpers.
		// ---------------------------------------------------------------------
		// Faithful port of Nukem's NiCamera::CalculateViewProjection (itself ported from
		// game code): the view matrix from the camera's world basis (his naming: dir =
		// rotate column 0, up = column 1, right = column 2; translation handled separately
		// via posAdjust), the projection from the camera's NiFrustum. Only valid on a
		// camera with a REAL (aspect-correct) frustum -- i.e. the RENDER camera. The
		// frozen cull camera's frustum is normalized to (-1,1,1,-1) and yields garbage.
		void CalculateViewProjection(RE::NiCamera* a_camera, XMMATRIX& a_view, XMMATRIX& a_proj, XMMATRIX& a_viewProj)
		{
			const RE::NiMatrix3& R = a_camera->world.rotate;
			const RE::NiPoint3   dir{ R.entry[0][0], R.entry[1][0], R.entry[2][0] };    // col 0
			const RE::NiPoint3   up{ R.entry[0][1], R.entry[1][1], R.entry[2][1] };     // col 1
			const RE::NiPoint3   right{ R.entry[0][2], R.entry[1][2], R.entry[2][2] };  // col 2

			a_view.r[0] = XMVectorSet(right.x, up.x, dir.x, 0.0f);
			a_view.r[1] = XMVectorSet(right.y, up.y, dir.y, 0.0f);
			a_view.r[2] = XMVectorSet(right.z, up.z, dir.z, 0.0f);
			a_view.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

			const RE::NiFrustum& fr = a_camera->GetRuntimeData2().viewFrustum;
			const float          rightLeftDiff = fr.fRight - fr.fLeft;
			const float          rightLeftRatio = -((1.0f / rightLeftDiff) * (fr.fRight + fr.fLeft));
			const float          topBottomDiff = fr.fTop - fr.fBottom;
			const float          topBottomRatio = -((1.0f / topBottomDiff) * (fr.fTop + fr.fBottom));
			const float          invNearFarDiff = 1.0f / (fr.fFar - fr.fNear);

			a_proj.r[0] = _mm_setzero_ps();
			a_proj.r[1] = _mm_setzero_ps();
			a_proj.r[2] = _mm_setzero_ps();
			a_proj.r[3] = _mm_setzero_ps();
			a_proj.r[0].m128_f32[0] = (1.0f / rightLeftDiff) * 2.0f;
			a_proj.r[1].m128_f32[1] = (1.0f / topBottomDiff) * 2.0f;
			if (!fr.bOrtho) {
				a_proj.r[2].m128_f32[0] = rightLeftRatio;
				a_proj.r[2].m128_f32[1] = topBottomRatio;
				a_proj.r[2].m128_f32[2] = invNearFarDiff * fr.fFar;
				a_proj.r[2].m128_f32[3] = 1.0f;
				a_proj.r[3].m128_f32[2] = -((fr.fNear * fr.fFar) * invNearFarDiff);
			} else {
				a_proj.r[2].m128_f32[2] = invNearFarDiff;
				a_proj.r[3].m128_f32[0] = rightLeftRatio;
				a_proj.r[3].m128_f32[1] = topBottomRatio;
				a_proj.r[3].m128_f32[2] = -(invNearFarDiff * fr.fNear);
				a_proj.r[3].m128_f32[3] = 1.0f;
			}

			a_viewProj = XMMatrixMultiply(a_view, a_proj);
		}

		// ---------------------------------------------------------------------
		// Scene-graph helpers (safe indexed child access).
		// ---------------------------------------------------------------------
		RE::NiAVObject* ChildAt(RE::NiNode* a_node, std::uint32_t a_index)
		{
			if (!a_node)
				return nullptr;
			auto& children = a_node->GetChildren();
			if (a_index >= children.capacity())
				return nullptr;
			return children[static_cast<std::uint16_t>(a_index)].get();
		}

		RE::NiNode* ChildNodeAt(RE::NiNode* a_node, std::uint32_t a_index)
		{
			auto* obj = ChildAt(a_node, a_index);
			return obj ? obj->AsNode() : nullptr;
		}

		bool IsLeafAnimNode(RE::NiNode* a_node)
		{
			if (auto* rtti = a_node->GetRTTI())
				return std::string_view(rtti->name) == std::string_view("BSLeafAnimNode");
			return false;
		}

		RE::BSMultiBoundAABB* GetAABBNode(RE::NiAVObject* a_object)
		{
			auto* mbn = netimmerse_cast<RE::BSMultiBoundNode*>(a_object);
			if (!mbn)
				return nullptr;
			auto* mb = mbn->GetRuntimeData().multiBound.get();
			if (!mb || !mb->data)
				return nullptr;
			return netimmerse_cast<RE::BSMultiBoundAABB*>(mb->data.get());
		}

		// ---------------------------------------------------------------------
		// Occluder gather (RenderRecursive / RegisterGeometry, adapted to RE::).
		// ---------------------------------------------------------------------
		void RegisterGeometry(RE::BSGeometry* a_geom)
		{
			// Strictly BSLightingShaderProperty occluders only.
			if (!a_geom->lightingShaderProp_cast())
				return;

			if (a_geom->GetType().get() != RE::BSGeometry::Type::kTriShape)
				return;

			auto& geomRT = a_geom->GetGeometryRuntimeData();
			auto* rendererData = geomRT.rendererData;
			if (!rendererData || !rendererData->rawIndexData)
				return;

			auto* triShape = static_cast<RE::BSTriShape*>(a_geom);
			if (triShape->GetTrishapeRuntimeData().triangleCount <= 1)
				return;

			const RE::NiPoint3& c = a_geom->worldBound.center;
			const float         dx = c.x - g_posAdjust.x;
			const float         dy = c.y - g_posAdjust.y;
			const float         dz = c.z - g_posAdjust.z;

			GeoEntry entry;
			entry.geometry = a_geom;
			entry.distanceSquared = dx * dx + dy * dy + dz * dz;
			// Projected-size proxy: radius over distance. Selecting the raster budget by
			// CENTER DISTANCE starved huge occluders -- terrain cells have far-away centers
			// even when they fill the near view, so nearby pebbles consumed the budget and
			// the terrain went MISSING from the buffer. Coverage picks what blocks pixels.
			entry.coverageScore = a_geom->worldBound.radius / (sqrtf(entry.distanceSquared) + 1.0f);
			g_geoList.push_back(entry);
		}

		// Coarse frustum + flag pre-cull for the occluder gather. true = skip.
		bool CullObject(RE::NiAVObject* a_object)
		{
			if (!a_object)
				return true;

			const auto flags = a_object->GetFlags();
			const bool alwaysDraw = flags.any(RE::NiAVObject::Flag::kAlwaysDraw);
			if (a_object->GetAppCulled() && !alwaysDraw)
				return true;

			if (auto* aabb = GetAABBNode(a_object)) {
				// Not all objects have valid boundaries (certain global cells).
				if (aabb->size.z > 1.0f) {
					const XMVECTOR center = _mm_sub_ps(_mm_setr_ps(aabb->center.x, aabb->center.y, aabb->center.z, 0.0f), g_posAdjustV);
					const XMVECTOR halfExtents = _mm_setr_ps(aabb->size.x, aabb->size.y, aabb->size.z, 0.0f);
					if (!g_frustum.AABBInFrustum(center, halfExtents))
						return true;
				}
			} else if (a_object->worldBound.radius > 10.0f) {
				const auto&    b = a_object->worldBound;
				const XMVECTOR center = _mm_sub_ps(_mm_setr_ps(b.center.x, b.center.y, b.center.z, b.radius), g_posAdjustV);
				if (!g_frustum.SphereInFrustum(center))
					return true;
			}

			return false;
		}

		void RenderRecursive(RE::NiAVObject* a_object, bool a_firstLevel)
		{
			if (CullObject(a_object))
				return;

			const bool validBounds = a_object->worldBound.radius > 1.0f;
			if (a_firstLevel && validBounds && a_object->worldBound.radius < OccluderFirstLevelMinSize)
				return;

			if (RE::NiNode* node = a_object->AsNode()) {
				// Don't descend into leaf anim nodes (trees, bushes, alpha plants).
				if (!IsLeafAnimNode(node)) {
					auto& children = node->GetChildren();
					for (std::uint16_t i = 0; i < children.capacity(); ++i) {
						if (auto* child = children[i].get())
							RenderRecursive(child, false);
					}
				}
			} else if (RE::BSGeometry* geometry = a_object->AsGeometry()) {
				if (geometry->worldBound.radius > 100.0f) {
					const float d1 = geometry->world.translate.x - g_posAdjust.x;
					const float d2 = geometry->world.translate.y - g_posAdjust.y;
					if (((d1 * d1) + (d2 * d2)) < (OccluderMaxDistance * OccluderMaxDistance))
						RegisterGeometry(geometry);
				}
			}
		}

		// Walk the ObjectLODRoot -> cell -> {LandNode, StaticNode} hierarchy (Nukem's
		// layout). Heavily null-guarded so a layout mismatch degrades to "no occluders"
		// (safe: an empty depth buffer occludes nothing, so everything stays visible).
		void GatherOccluders()
		{
			RE::NiNode* scene = *g_worldScenegraph;  // "WorldRoot Node" (SceneGraph)

			// DIAG (env-gated CS_MOC_DUMP=1, every ~600 frames): dump the scene-graph
			// structure so the walk can be verified against the live layout (menu,
			// interior and exterior all differ). The debug buffer is black iff this walk
			// misses the cell nodes.
			static const bool s_dumpStructure = [] {
				char buf[8] = {};
				return GetEnvironmentVariableA("CS_MOC_DUMP", buf, sizeof(buf)) && buf[0] == '1';
			}();
			if (scene && s_dumpStructure) {
				auto* ssn = ChildNodeAt(scene, 1);
				auto* gs = RE::BSGraphics::State::GetSingleton();
				if (ssn && gs && (gs->frameCount % 600u) == 0u) {
					const std::size_t count = ssn->GetChildren().size();
					logger::info("[MOC][diag] structure: '{}' childCount={}", ssn->name.c_str(), count);
					for (std::uint16_t i = 0; i < ssn->GetChildren().capacity() && i < 16; ++i) {
						auto* c = ChildAt(ssn, i);
						if (!c)
							continue;
						auto* cn = c->AsNode();
						logger::info("[MOC][diag]   ssn[{}]='{}' node={} kids={}", i, c->name.c_str(),
							cn != nullptr, cn ? cn->GetChildren().size() : 0);
						// One level deeper for candidate cell containers.
						if (cn) {
							for (std::uint16_t j = 0; j < cn->GetChildren().capacity() && j < 12; ++j)
								if (auto* g = ChildAt(cn, j))
									logger::info("[MOC][diag]     ssn[{}][{}]='{}' node={}", i, j, g->name.c_str(), g->AsNode() != nullptr);
						}
					}
				}
			}

			if (!scene)
				return;

			RE::NiNode* shadowScene = ChildNodeAt(scene, 1);          // "shadow scene node"
			RE::NiNode* objectLODRoot = ChildNodeAt(shadowScene, 3);  // "ObjectLODRoot"
			if (!objectLODRoot)
				return;

			// Walk-shape tallies for the rate-limited diag line (no logging inside the
			// walk -- it runs on job threads).
			g_cellsSeen = 0;
			g_cellsCulled = 0;


			auto& cells = objectLODRoot->GetChildren();
			for (std::uint16_t i = 2; i < cells.capacity(); ++i) {
				RE::NiNode* cellNode = ChildNodeAt(objectLODRoot, i);
				if (!cellNode)
					continue;
				++g_cellsSeen;
				if (CullObject(cellNode)) {
					++g_cellsCulled;
					continue;
				}

				// One-shot diag: the first unculled cell's child names, to verify layout.
				static std::atomic<bool> s_cellNamesDumped{ false };
				if (s_dumpStructure && !s_cellNamesDumped.exchange(true)) {
					logger::info("[MOC][cell] first cell '{}' kids={}", cellNode->name.c_str(), cellNode->GetChildren().size());
					for (std::uint16_t j = 0; j < cellNode->GetChildren().capacity() && j < 12; ++j)
						if (auto* c = ChildAt(cellNode, j))
							logger::info("[MOC][cell]   [{}]='{}' node={}", j, c->name.c_str(), c->AsNode() != nullptr);
				}

				// Walk every child container from index 2 (skipping [0]=ActorNode and
				// [1]=MarkerNode, which are positional in both layouts) EXCEPT DynamicNode
				// (movables must never be occluders -- they'd leave stale depth when they
				// move). Exteriors thus cover LandNode[2]+StaticNode[3] as before; interiors
				// (children unnamed, statics parented under room BSMultiBoundNodes) get
				// their geometry via the room containers. Non-renderable helpers (occlusion
				// planes, portals, collision) are rejected by RenderRecursive's geometry
				// filters and the raster's rendererData/shader-property checks.
				for (std::uint16_t j = 2; j < cellNode->GetChildren().capacity(); ++j) {
					auto* container = ChildNodeAt(cellNode, j);
					if (!container)
						continue;
					if (std::string_view{ container->name.c_str() } == "DynamicNode")
						continue;
					if (CullObject(container))
						continue;
					auto& kids = container->GetChildren();
					for (std::uint16_t k = 0; k < kids.capacity(); ++k) {
						if (auto* child = kids[k].get())
							RenderRecursive(child, true);
					}
				}
			}
		}

		bool GetCachedGeometry(RE::BSGeometry* a_geom, IndexPair& a_outIndices, float*& a_outVerts);

		// Builder-thread main: each kick runs one gather + coverage selection + front-to-back
		// sort + vertex-conversion pre-warm, then publishes the list for the NEXT frame's
		// enqueue. Scene-graph reads race the game by design (Nukem's model) -- the walk is
		// null-guarded throughout and only ever READS.
		void BuilderLoop()
		{
			for (;;) {
				{
					std::unique_lock lk(g_builderMtx);
					g_builderCV.wait(lk, [] { return g_builderKick || g_builderQuit; });
					if (g_builderQuit)
						return;
					g_builderKick = false;
				}

				const auto t0 = std::chrono::high_resolution_clock::now();

				g_geoList.clear();
				GatherOccluders();

				const std::size_t budget = std::min<std::size_t>(g_geoList.size(), MaxOccludersPerFrame);
				if (budget < g_geoList.size())
					std::nth_element(g_geoList.begin(), g_geoList.begin() + budget, g_geoList.end(),
						[](const GeoEntry& a, const GeoEntry& b) { return a.coverageScore > b.coverageScore; });
				std::sort(g_geoList.begin(), g_geoList.begin() + budget,
					[](const GeoEntry& a, const GeoEntry& b) { return a.distanceSquared < b.distanceSquared; });
				g_geoList.resize(budget);

				// Pre-warm the conversion cache so the claim thread's enqueue never pays
				// for vertex/index conversion or meshopt simplification.
				for (auto& e : g_geoList) {
					IndexPair idx;
					float*    verts = nullptr;
					if (e.geometry)
						GetCachedGeometry(e.geometry, idx, verts);
				}

				g_lastGatherMs = std::chrono::duration<double, std::milli>(
					std::chrono::high_resolution_clock::now() - t0)
				                     .count();

				{
					std::scoped_lock lk(g_builderMtx);
					g_readyList.swap(g_geoList);
					g_readyValid = true;
				}
			}
		}

		void RasterizeOccluder(RE::BSGeometry* a_geom)
		{
			IndexPair indices;
			float*    verts = nullptr;
			if (!a_geom || !GetCachedGeometry(a_geom, indices, verts) || !verts || !indices.Data)
				return;

			auto*      sp = a_geom->lightingShaderProp_cast();
			const bool twoSided = sp && sp->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided);
			const MaskedOcclusionCulling::BackfaceWinding winding =
				twoSided ? MaskedOcclusionCulling::BACKFACE_NONE : MaskedOcclusionCulling::BACKFACE_CW;

			const XMMATRIX worldRel = GetXMFromNiPosAdjust(a_geom->world, g_posAdjust);
			const XMMATRIX worldViewProj = XMMatrixMultiply(worldRel, g_viewProj);

			// Queue the drawcall on the threadpool (async; workers bin + rasterize).
			// SetMatrix copies the matrix into per-job state, so the stack lifetime is
			// fine; the cached verts/indices are persistent, satisfying the pool's
			// keep-alive-until-flush requirement.
			g_pool->SetMatrix(reinterpret_cast<const float*>(&worldViewProj));
			g_pool->RenderTriangles(
				verts,
				indices.Data,
				static_cast<int>(indices.Count / 3),
				winding,
				MaskedOcclusionCulling::CLIP_PLANE_SIDES);
		}
	}  // namespace

	// -------------------------------------------------------------------------
	// Public API.
	// -------------------------------------------------------------------------
	void Init()
	{
		if (g_init)
			return;

		// Request AVX2 (the shipping build targets /arch:AVX2). MOC caps the request
		// to the best implementation the CPU actually supports.
		g_moc = MaskedOcclusionCulling::Create(MaskedOcclusionCulling::AVX2);
		if (!g_moc) {
			logger::warn("[MOC] MaskedOcclusionCulling::Create failed; occlusion culling disabled");
			return;
		}
		g_moc->SetResolution(MOC_WIDTH, MOC_HEIGHT);
		g_moc->ClearBuffer();

		// Intel's CullingThreadpool: occluder rasterization is queued by the (single)
		// build thread and executed by worker threads binning the screen. Bins must be
		// tile-aligned and >= thread count: 4x4 bins of 128x72 over 512x288. maxJobs=32
		// is Intel's recommended queue depth. Workers are woken per build and suspended
		// right after the flush, so the pool costs nothing outside the build window.
		const unsigned int threads = std::clamp(RasterThreads, 1, 8);
		g_pool = new CullingThreadpool(threads, 4, 4, 32);
		g_pool->SetBuffer(g_moc);
		g_pool->SetVertexLayout(MaskedOcclusionCulling::VertexLayout(16, 4, 12));
		g_pool->SuspendThreads();

		// Drop the raster workers below normal priority: in CPU-bound scenes they must
		// yield to the engine's own job threads or the raster wins cost more than the
		// culling saves. (The pool spawned `threads` workers followed by nothing else on
		// this process yet at PostPostLoad -- identify them by enumeration snapshot.)
		// CullingThreadpool offers no handle access; adjust via the builder-side knob
		// instead: fewer workers is the honest lever, so the default is now 2.

		g_builderQuit = false;
		g_builder = std::thread(BuilderLoop);

		g_init = true;
		logger::info("[MOC] initialized {}x{} rasterThreads={}", MOC_WIDTH, MOC_HEIGHT, threads);
	}

	// Create the debug texture lazily: MOC::Init runs at PostPostLoad, BEFORE the game
	// creates the D3D11 device, so globals::d3d::device is null there. This is called
	// from UpdateDebugView (menu open) by which point the device exists.
	void EnsureDebugTexture()
	{
		if (g_debugSRV)
			return;
		auto* device = globals::d3d::device;
		if (!device)
			return;
		D3D11_TEXTURE2D_DESC td{};
		td.Width = MOC_WIDTH;
		td.Height = MOC_HEIGHT;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DYNAMIC;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (SUCCEEDED(device->CreateTexture2D(&td, nullptr, g_debugTex.put())))
			device->CreateShaderResourceView(g_debugTex.get(), nullptr, g_debugSRV.put());
		g_debugDepth.resize(static_cast<std::size_t>(MOC_WIDTH) * MOC_HEIGHT);
	}

	// Rasterize the current MOC depth buffer into the debug texture as grayscale
	// (near = bright). Called from the settings UI; returns the SRV to ImGui::Image.
	void UpdateDebugView()
	{
		if (!g_init || !g_moc)
			return;
		EnsureDebugTexture();
		if (!g_debugTex || !g_debugSRV)
			return;
		auto* ctx = globals::d3d::context;
		if (!ctx)
			return;

		// Read the buffer the CULL HOOK built this frame (with the correct main-camera
		// matrices at cull time). Do NOT rebuild here: at menu-draw time the global
		// shadowState holds post-process/UI matrices, so a rebuild would rasterize the
		// occluders with the wrong transform (garbage view) -- and it doubles the per-frame
		// cost. The cull hook already produced this frame's buffer.
		g_moc->ComputePixelDepthBuffer(g_debugDepth.data(), false);  // flipY=false is empirically top-down (matches Nukem)

		// MOC returns FLT_MAX (or ~0) for uncovered pixels; normalize only over real occluders
		// (0 < d < kBackground) so their silhouettes are visible (see DumpDepthImage).
		constexpr float kBackground = 1e30f;
		float           mn = FLT_MAX, mx = 0.0f;
		for (float d : g_debugDepth) {
			if (d > 0.0f && d < kBackground) {
				mn = std::min(mn, d);
				mx = std::max(mx, d);
			}
		}
		const float range = (mx > mn) ? (mx - mn) : 1.0f;

		D3D11_MAPPED_SUBRESOURCE ms{};
		if (FAILED(ctx->Map(g_debugTex.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
			return;
		for (unsigned int y = 0; y < MOC_HEIGHT; ++y) {
			auto* row = static_cast<std::uint8_t*>(ms.pData) + static_cast<std::size_t>(y) * ms.RowPitch;
			for (unsigned int x = 0; x < MOC_WIDTH; ++x) {
				const float        d = g_debugDepth[static_cast<std::size_t>(y) * MOC_WIDTH + x];
				const bool         occluder = d > 0.0f && d < kBackground;
				const std::uint8_t g = occluder ? static_cast<std::uint8_t>(40.0f + 215.0f * ((d - mn) / range)) : 0;
				row[x * 4 + 0] = g;
				row[x * 4 + 1] = g;
				row[x * 4 + 2] = g;
				row[x * 4 + 3] = 255;
			}
		}
		ctx->Unmap(g_debugTex.get(), 0);
	}

	void* GetDebugSRV()
	{
		return g_debugSRV.get();
	}

	// DIAG: save the current depth buffer as a grayscale PNG so its content can be verified
	// directly (Read the image) without opening the ImGui menu. Same data + mapping the UI shows.
	void DumpDepthImage()
	{
		if (!g_init || !g_moc)
			return;
		const std::size_t n = static_cast<std::size_t>(MOC_WIDTH) * MOC_HEIGHT;
		if (g_debugDepth.size() != n)
			g_debugDepth.resize(n);
		g_moc->ComputePixelDepthBuffer(g_debugDepth.data(), false);  // flipY=false is empirically top-down (matches Nukem)

		// MOC returns FLT_MAX (or ~0) for uncovered pixels; only 0 < d < kBackground is a real
		// rasterized occluder. Normalize over those so the silhouettes are visible.
		constexpr float kBackground = 1e30f;
		float           mn = FLT_MAX, mx = 0.0f;
		std::size_t     nonzero = 0;
		for (float d : g_debugDepth) {
			if (d > 0.0f && d < kBackground) {
				mn = std::min(mn, d);
				mx = std::max(mx, d);
				++nonzero;
			}
		}
		const float range = (mx > mn) ? (mx - mn) : 1.0f;

		static std::vector<std::uint8_t> rgba;
		rgba.resize(n * 4);
		for (std::size_t i = 0; i < n; ++i) {
			const float        d = g_debugDepth[i];
			const bool         occluder = d > 0.0f && d < kBackground;
			// Occluders: bright→near via normalized depth (min gray 40 so they're always visible).
			const std::uint8_t g = occluder ? static_cast<std::uint8_t>(40.0f + 215.0f * ((d - mn) / range)) : 0;
			rgba[i * 4 + 0] = g;
			rgba[i * 4 + 1] = g;
			rgba[i * 4 + 2] = g;
			rgba[i * 4 + 3] = 255;
		}

		const DirectX::Image img{ MOC_WIDTH, MOC_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM,
			static_cast<std::size_t>(MOC_WIDTH) * 4, n * 4, rgba.data() };
		const HRESULT hr = DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"F:\\claudetmp\\moc_depth.png");
		logger::info("[MOC][depth] saved moc_depth.png nonzero={}/{} range=[{:.4g},{:.4g}] hr=0x{:08X}",
			nonzero, n, mn, mx, static_cast<std::uint32_t>(hr));

		// Raw float dump for offline numeric comparison against the game depth buffer:
		// header = uint32 W, uint32 H, then the 16 floats of g_proj (row-vector layout, to
		// recover near/far), then W*H floats of MOC depth (= 1/viewW; FLT_MAX where empty),
		// row-major top-down (matches the flipY'd PNG).
		if (FILE* f = nullptr; _wfopen_s(&f, L"F:\\claudetmp\\moc_depth.bin", L"wb") == 0 && f) {
			const std::uint32_t wh[2] = { MOC_WIDTH, MOC_HEIGHT };
			fwrite(wh, sizeof(wh), 1, f);
			XMFLOAT4X4 pm;
			XMStoreFloat4x4(&pm, g_proj);
			fwrite(&pm, sizeof(pm), 1, f);
			fwrite(g_debugDepth.data(), sizeof(float), n, f);
			fclose(f);
		}
	}

	// Read back the game's own main depth buffer (kMAIN) to a PNG, for side-by-side comparison
	// with the MOC occluder buffer. At cull time (when BuildOccluders runs) kMAIN still holds
	// the PREVIOUS frame's fully-rendered depth -- the per-frame depth clear happens later, in
	// the render passes -- so for a static view this is a complete, valid depth image. The game
	// depth contains ALL geometry (trees, actors, ...) while MOC has only the large statics, so
	// they won't match pixel-for-pixel; what matters is whether the big silhouettes (buildings,
	// terrain, walls) sit at the same screen positions + relative depths. If the MOC shapes are
	// rotated / mirrored / offset vs this, the matrices are still wrong. DEBUG-only: the
	// CopyResource + Map hard-stalls the render thread, so it is gated to the diag cadence.
	void DumpGameDepth()
	{
		auto* renderer = globals::game::renderer;
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!renderer || !device || !context)
			return;

		ID3D11Texture2D* srcTex =
			renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
		if (!srcTex)
			return;

		D3D11_TEXTURE2D_DESC desc{};
		srcTex->GetDesc(&desc);

		D3D11_TEXTURE2D_DESC sd = desc;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sd.MiscFlags = 0;
		winrt::com_ptr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&sd, nullptr, staging.put())))
			return;
		context->CopyResource(staging.get(), srcTex);

		D3D11_MAPPED_SUBRESOURCE map{};
		if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map)))
			return;

		const UINT W = desc.Width;
		const UINT H = desc.Height;

		// Decode one depth sample. Skyrim SE kMAIN is R24G8_TYPELESS (D24_UNORM_S8): the low 24
		// bits are the UNORM depth. Handle R32-float and R32G8X24 too, just in case. Skyrim uses
		// reversed-Z (near=1, far/sky=0), so mapping depth->brightness gives near=bright naturally.
		auto readDepth = [&](const std::uint8_t* row, UINT x) -> float {
			switch (desc.Format) {
			case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
				return static_cast<float>(reinterpret_cast<const std::uint32_t*>(row)[x] & 0x00FFFFFFu) / 16777215.0f;
			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32_FLOAT:
				return reinterpret_cast<const float*>(row)[x];
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
				return reinterpret_cast<const float*>(row)[x * 2];  // [float depth][uint stencil]
			default:
				return 0.0f;
			}
		};

		// Decode the whole buffer to floats once (also dumped raw for offline correlation).
		static std::vector<float> depths;
		depths.resize(static_cast<std::size_t>(W) * H);
		float mn = FLT_MAX, mx = -FLT_MAX;
		for (UINT y = 0; y < H; ++y) {
			const auto* row = static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch;
			for (UINT x = 0; x < W; ++x) {
				const float d = readDepth(row, x);
				depths[static_cast<std::size_t>(y) * W + x] = d;
				if (d > 0.0f && d < 1.0f) {
					mn = std::min(mn, d);
					mx = std::max(mx, d);
				}
			}
		}
		context->Unmap(staging.get(), 0);
		const float range = (mx > mn) ? (mx - mn) : 1.0f;

		// near = bright, matching the MOC PNG's convention (standard-Z: small d = near).
		static std::vector<std::uint8_t> rgba;
		rgba.resize(static_cast<std::size_t>(W) * H * 4);
		for (std::size_t i = 0; i < depths.size(); ++i) {
			const float        d = depths[i];
			const std::uint8_t g = (d > 0.0f) ? static_cast<std::uint8_t>(255.0f * std::clamp(1.0f - (d - mn) / range, 0.0f, 1.0f)) : 0;
			rgba[i * 4 + 0] = g;
			rgba[i * 4 + 1] = g;
			rgba[i * 4 + 2] = g;
			rgba[i * 4 + 3] = 255;
		}

		// Raw dump: uint32 W, uint32 H, then W*H floats of z/w (standard [0,1]), top-down.
		if (FILE* f = nullptr; _wfopen_s(&f, L"F:\\claudetmp\\moc_game_depth.bin", L"wb") == 0 && f) {
			const std::uint32_t wh[2] = { W, H };
			fwrite(wh, sizeof(wh), 1, f);
			fwrite(depths.data(), sizeof(float), depths.size(), f);
			fclose(f);
		}

		const DirectX::Image img{ W, H, DXGI_FORMAT_R8G8B8A8_UNORM,
			static_cast<std::size_t>(W) * 4, static_cast<std::size_t>(W) * H * 4, rgba.data() };
		const HRESULT hr = DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"F:\\claudetmp\\moc_game_depth.png");
		logger::info("[MOC][depth] saved moc_game_depth.png {}x{} fmt={} range=[{:.4g},{:.4g}] hr=0x{:08X}",
			W, H, static_cast<int>(desc.Format), mn, mx, static_cast<std::uint32_t>(hr));
	}

	void Shutdown()
	{
		std::scoped_lock lock(g_cacheMutex);
		for (auto& [key, verts] : g_vertMap)
			delete[] verts;
		for (auto& [key, idx] : g_indexMap)
			delete[] idx.Data;
		g_vertMap.clear();
		g_indexMap.clear();
		g_geoList.clear();

		// Stop the builder before tearing anything down (it walks the scene graph and
		// touches the conversion cache).
		if (g_builder.joinable()) {
			{
				std::scoped_lock lk(g_builderMtx);
				g_builderQuit = true;
			}
			g_builderCV.notify_one();
			g_builder.join();
		}

		// The pool references g_moc and joins its workers in the destructor -- destroy it first.
		delete g_pool;
		g_pool = nullptr;

		if (g_moc) {
			MaskedOcclusionCulling::Destroy(g_moc);
			g_moc = nullptr;
		}
		g_init = false;
	}

	bool IsInitialized()
	{
		return g_init && g_moc != nullptr;
	}

	RE::NiCamera* GetMainCamera()
	{
		// The engine's own main-render camera slot: Main::spWorldRoot (REL::ID 517006, ==
		// g_worldScenegraph) is a BSSceneGraph whose runtime camera (+0x128 SE) is the exact
		// pointer DrawWorld_PreRender (0x1405B1860 in 1.5.97) loads for CacheCameraData /
		// SetCameraData and every main-scene cull. NOT a scene-graph child walk, which can
		// find a different (stale) camera under CameraRoot.
		RE::NiNode* root = *g_worldScenegraph;
		if (!root)
			return nullptr;
		return static_cast<RE::BSSceneGraph*>(root)->GetRuntimeData().camera.get();
	}

	RE::NiCamera* GetCullCamera()
	{
		return *g_cullCamera;
	}

	void RemoveCachedGeometry(void* a_rendererData)
	{
		std::uint32_t* indices = nullptr;
		float*         verts = nullptr;
		{
			std::scoped_lock lock(g_cacheMutex);
			if (auto it = g_indexMap.find(a_rendererData); it != g_indexMap.end()) {
				indices = it->second.Data;
				g_indexMap.erase(it);
			}
			if (auto it = g_vertMap.find(a_rendererData); it != g_vertMap.end()) {
				verts = it->second;
				g_vertMap.erase(it);
			}
		}
		delete[] indices;
		delete[] verts;
	}

	// Returns true iff this cull pass is the MAIN world view and the occluder buffer is
	// current for the frame -- i.e. the caller should enable per-object occlusion testing.
	// All other passes (shadow cascades, water/cubemap reflections, first-person, sky)
	// return false and are left untouched.
	bool BuildOccluders(RE::NiCamera* a_camera)
	{
		if (!g_init || !g_moc)
			return false;

		// MAIN-pass gate by pointer identity with the engine's two main-scene cameras.
		// The main culls carry one of TWO NiCamera objects depending on phase (verified by
		// hook counters): the BuildSceneLists CELL culls get the RENDER camera directly
		// (DrawWorld_PreRender seeds the job cull processes with it), while the MainAccum /
		// depth-prepass / world subtree culls get the frozen CULL camera (REL::ID 528062,
		// normalized frustum). Auxiliary passes (water reflection, first-person, sky,
		// shadows, cubemap) each carry their own camera and match neither.
		RE::NiCamera* renderCam = GetMainCamera();
		if (!a_camera || !renderCam)
			return false;
		if (a_camera != renderCam && a_camera != *g_cullCamera)
			return false;

		// Quiesce during loads: the BUILDER thread walks the scene graph, and a scene
		// teardown (coc / fast travel / load) mid-walk is a use-after-free (crashed on a
		// worldspace transition). The loading screen covers the teardown window; skipping
		// builds there costs nothing (nothing worth culling renders) and starves the
		// builder before the graph is torn down.
		if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))
			return false;

		// The matrices always come from the RENDER camera: the cull camera's frustum is
		// normalized to (-1,1,1,-1), and the engine's per-pass cameraData is still stale
		// from the previous frame during the earliest main cull (BuildSceneLists runs as a
		// job BEFORE SetCameraData). The render camera's world transform + real frustum
		// are already updated for this frame -- DrawWorld itself reads them at the same
		// point. Same recipe as Nukem.

		// Once per frame: CAS-claim the frame so exactly ONE of the concurrent main-scene
		// culls builds. Losers report whether a completed build for this frame is already
		// published -- if the winner is still mid-build they return false and their pass
		// simply isn't occlusion-tested (conservative).
		auto*               gfxState = RE::BSGraphics::State::GetSingleton();
		const std::uint32_t frame = gfxState ? gfxState->frameCount : 0;
		std::uint32_t       claimed = g_buildClaim.load(std::memory_order_relaxed);
		if (claimed == frame || !g_buildClaim.compare_exchange_strong(claimed, frame, std::memory_order_relaxed))
			return g_buildDone.load(std::memory_order_acquire) == frame;
		g_buildStart = std::chrono::high_resolution_clock::now();

		CalculateViewProjection(renderCam, g_view, g_proj, g_viewProj);
		const RE::NiPoint3 posAdj = renderCam->world.translate;
		g_posAdjust = posAdj;
		g_posAdjustV = _mm_setr_ps(posAdj.x, posAdj.y, posAdj.z, 0.0f);

		// Coarse occluder-gather frustum. Sphere test uses the view-proj clip planes; AABB test
		// uses the reconstructed frustum. The scalar derivation below (X-based fov, proj-ratio
		// aspect, and the deliberately odd near/far extraction) is VERBATIM Nukem -- his values
		// are unusual but shipped and conservative; do not "clean up". Position is zero because
		// all our tests feed camera-relative centers (posAdjust already subtracted).
		g_frustum.CreateFromViewProjMatrix(g_viewProj);

		const float          fov = atanf(1.0f / g_proj.r[0].m128_f32[0]) * 2.0f * (180.0f / 3.14159265359f);
		const float          aspect = g_proj.r[1].m128_f32[1] / g_proj.r[0].m128_f32[0];
		const float          mynear = g_proj.r[2].m128_f32[3] / (g_proj.r[2].m128_f32[2] - 1.0f);
		const float          myfar = g_proj.r[2].m128_f32[3] / (g_proj.r[2].m128_f32[2] + 1.0f);
		const RE::NiMatrix3& camR = renderCam->world.rotate;
		const XMVECTOR       look = XMVectorSet(camR.entry[0][0], camR.entry[1][0], camR.entry[2][0], 0.0f);  // dir = col 0
		const XMVECTOR       up = XMVectorSet(camR.entry[0][1], camR.entry[1][1], camR.entry[2][1], 0.0f);    // up = col 1
		g_frustum.InitializeFrustumAABB(myfar, mynear, aspect, fov, _mm_setzero_ps(), look, up);

		if (!EnableOccluderRendering) {
			g_moc->ClearBuffer();  // workers idle when rendering is off; direct clear is safe
			g_buildDone.store(frame, std::memory_order_release);
			return true;  // buffer cleared -> TestObject keeps everything visible
		}

		// Wake the raster workers (~100us) before touching the buffer, then clear THROUGH
		// the pool: its ClearBuffer flushes any last-frame straggler first, so the clear
		// can never race an in-flight rasterization.
		g_pool->WakeThreads();
		g_pool->ClearBuffer();

		// Enqueue the list the BUILDER prepared last frame (gather + coverage selection +
		// front-to-back sort + conversion pre-warm all happened off this thread). The list
		// is one frame stale -- occluders are static, so at worst a newly streamed cell's
		// occluders join a frame late (conservative). First frame after a scene change has
		// no list yet: nothing occludes, everything stays visible.
		static std::vector<GeoEntry> s_enqueue;
		s_enqueue.clear();
		{
			std::scoped_lock lk(g_builderMtx);
			if (g_readyValid)
				s_enqueue.swap(g_readyList);
			g_readyValid = false;
		}
		g_lastOccluderCount = static_cast<std::uint32_t>(s_enqueue.size());
		for (auto& e : s_enqueue)
			RasterizeOccluder(e.geometry);

		// Kick the builder to prepare NEXT frame's list with the matrices/frustum we just
		// published above.
		{
			std::scoped_lock lk(g_builderMtx);
			g_builderKick = true;
		}
		g_builderCV.notify_one();

		// NO Flush: publish immediately and let the workers keep rasterizing while the
		// cull proceeds. Querying a partially built buffer is conservatively correct by
		// the library contract ("may only lead to objects being incorrectly classified
		// as visible"), and front-to-back submission puts the best occluders in first.
		// SuspendThreads is only a flag: workers drain the whole queue, THEN park, so no
		// work is lost and no busy-spin lingers. This removes the ~2ms rasterization wait
		// from the critical cull path entirely (the enqueue above costs ~0.2-0.5ms).
		g_pool->SuspendThreads();

		// DIAG (rate-limited): build time (gather + raster, on whichever cull thread won
		// the claim) + cull tally. Verification dumps are NOT done here -- BuildOccluders
		// runs on BuildSceneLists WORKER threads and must never touch the D3D context (a
		// worker-thread CopyResource/Map killed the process); see DumpDebugImages, called
		// from the render thread (Feature::Prepass).
		if ((frame % 120u) == 0u) {
			const double buildMs = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - g_buildStart)
									   .count();
			logger::info("[MOC][diag] frame={} enqueue={:.2f}ms gather={:.2f}ms occluders={} cells={}/culled={} | tested={} culled={} (aabb {}/{} sphere {}/{})",
				frame, buildMs, g_lastGatherMs, g_lastOccluderCount,
				g_cellsSeen, g_cellsCulled,
				g_tested.load(), g_culled.load(),
				g_culledAABB.load(), g_testedAABB.load(), g_culledSphere.load(), g_testedSphere.load());
		}
		g_buildDone.store(frame, std::memory_order_release);
		return true;
	}

	void DumpDebugImages()
	{
		// RENDER THREAD ONLY (DumpGameDepth uses the immediate context). Env-gated,
		// rate-limited verification dumps of the MOC buffer + the game's own depth.
		static const bool s_dump = [] {
			char buf[8] = {};
			return GetEnvironmentVariableA("CS_MOC_DUMP", buf, sizeof(buf)) && buf[0] == '1';
		}();
		if (!s_dump || !g_init || !g_moc)
			return;
		auto* gfxState = RE::BSGraphics::State::GetSingleton();
		if (!gfxState || (gfxState->frameCount % 120u) != 0u)
			return;
		DumpDepthImage();  // MOC occluder buffer -> moc_depth.png (+ raw .bin)
		DumpGameDepth();   // game kMAIN depth -> moc_game_depth.png (+ raw .bin)
	}

	// -------------------------------------------------------------------------
	// Occlusion queries.
	// -------------------------------------------------------------------------
	namespace
	{
		bool TestAABB(RE::BSMultiBoundAABB* a_object)
		{
			const __m128 vCenter = _mm_sub_ps(_mm_setr_ps(a_object->center.x, a_object->center.y, a_object->center.z, 0.0f), g_posAdjustV);
			const __m128 vHalf = _mm_setr_ps(a_object->size.x, a_object->size.y, a_object->size.z, 0.0f);

			const __m128 vMin = _mm_sub_ps(vCenter, vHalf);
			const __m128 vMax = _mm_add_ps(vCenter, vHalf);

			__m128 xRow[2], yRow[2], zRow[2];
			xRow[0] = _mm_mul_ps(_mm_shuffle_ps(vMin, vMin, 0x00), g_viewProj.r[0]);
			xRow[1] = _mm_mul_ps(_mm_shuffle_ps(vMax, vMax, 0x00), g_viewProj.r[0]);
			yRow[0] = _mm_mul_ps(_mm_shuffle_ps(vMin, vMin, 0x55), g_viewProj.r[1]);
			yRow[1] = _mm_mul_ps(_mm_shuffle_ps(vMax, vMax, 0x55), g_viewProj.r[1]);
			zRow[0] = _mm_mul_ps(_mm_shuffle_ps(vMin, vMin, 0xaa), g_viewProj.r[2]);
			zRow[1] = _mm_mul_ps(_mm_shuffle_ps(vMax, vMax, 0xaa), g_viewProj.r[2]);

			const __m128 minVert = _mm_add_ps(g_viewProj.r[3],
				_mm_add_ps(_mm_add_ps(_mm_min_ps(xRow[0], xRow[1]), _mm_min_ps(yRow[0], yRow[1])), _mm_min_ps(zRow[0], zRow[1])));
			const float minW = minVert.m128_f32[3];

			if (minW < 0.00000001f)
				return true;

			static const std::uint32_t sBBxInd[8] = { 1, 0, 0, 1, 1, 1, 0, 0 };
			static const std::uint32_t sBByInd[8] = { 1, 1, 1, 1, 0, 0, 0, 0 };
			static const std::uint32_t sBBzInd[8] = { 1, 1, 0, 0, 0, 1, 1, 0 };

			__m128       screenMin = _mm_set1_ps(FLT_MAX);
			__m128       screenMax = _mm_set1_ps(-FLT_MAX);
			const __m128 baseVert = g_viewProj.r[3];

			for (std::uint32_t i = 0; i < 8; i++) {
				__m128 vert = baseVert;
				vert = _mm_add_ps(vert, xRow[sBBxInd[i]]);
				vert = _mm_add_ps(vert, yRow[sBByInd[i]]);
				vert = _mm_add_ps(vert, zRow[sBBzInd[i]]);

				const __m128 vertW = _mm_shuffle_ps(vert, vert, 0xff);
				const __m128 xformedPos = _mm_div_ps(vert, vertW);

				screenMin = _mm_min_ps(screenMin, xformedPos);
				screenMax = _mm_max_ps(screenMax, xformedPos);
			}

			const auto r = g_moc->TestRect(screenMin.m128_f32[0], screenMin.m128_f32[1], screenMax.m128_f32[0], screenMax.m128_f32[1], minW);
			return r == MaskedOcclusionCulling::VISIBLE;
		}

		bool TestSphere(RE::NiAVObject* a_object)
		{
			const float sphereRadius = a_object->worldBound.radius;
			if (sphereRadius <= 5.0f)
				return true;

			const RE::NiPoint3& c = a_object->worldBound.center;

			// Camera-relative sphere center (w = 1).
			XMVECTOR bounds = _mm_sub_ps(_mm_setr_ps(c.x, c.y, c.z, 1.0f), g_posAdjustV);

			// Never cull a sphere the camera is inside of.
			if (XMVector3Length(bounds).m128_f32[0] <= sphereRadius)
				return true;

			// Early depth-rejection point: nearest point on the sphere toward the eye.
			XMVECTOR v = XMVectorSubtract(_mm_setzero_ps(), bounds);
			XMVECTOR closestPoint = XMVectorAdd(bounds, XMVectorScale(XMVector3Normalize(v), sphereRadius));
			closestPoint = XMVector4Transform(XMVectorSetW(closestPoint, 1.0f), g_viewProj);

			const float closestSpherePointW = closestPoint.m128_f32[3];
			if (closestSpherePointW < 0.000001f)
				return true;

			XMVECTOR viewEye = { g_view.r[0].m128_f32[3], g_view.r[1].m128_f32[3], g_view.r[2].m128_f32[3], 0.0f };
			viewEye = XMVectorNegate(viewEye);

			XMVECTOR    viewEyeSphereDirection = XMVectorSubtract(viewEye, bounds);
			const float cameraSphereDistance = XMVector3Length(viewEyeSphereDirection).m128_f32[0];

			XMVECTOR viewUp = { g_view.r[0].m128_f32[1], g_view.r[1].m128_f32[1], g_view.r[2].m128_f32[1], 0.0f };
			XMVECTOR viewRight = XMVector3Normalize(XMVector3Cross(viewEyeSphereDirection, viewUp));

			// Perspective-distortion compensation.
			const float fRadius = cameraSphereDistance * tanf(asinf(sphereRadius / cameraSphereDistance));

			XMVECTOR vUpRadius = XMVectorScale(viewUp, fRadius);
			XMVECTOR vRightRadius = XMVectorScale(viewRight, fRadius);

			XMVECTOR vCorner0WS = XMVectorSubtract(XMVectorAdd(bounds, vUpRadius), vRightRadius);
			XMVECTOR vCorner1WS = XMVectorAdd(XMVectorAdd(bounds, vUpRadius), vRightRadius);
			XMVECTOR vCorner2WS = XMVectorSubtract(XMVectorSubtract(bounds, vUpRadius), vRightRadius);
			XMVECTOR vCorner3WS = XMVectorAdd(XMVectorSubtract(bounds, vUpRadius), vRightRadius);

			XMVECTOR vCorner0CS = XMVector4Transform(vCorner0WS, g_viewProj);
			XMVECTOR vCorner1CS = XMVector4Transform(vCorner1WS, g_viewProj);
			XMVECTOR vCorner2CS = XMVector4Transform(vCorner2WS, g_viewProj);
			XMVECTOR vCorner3CS = XMVector4Transform(vCorner3WS, g_viewProj);

			XMVECTOR vCorner0NDC = XMVectorDivide(vCorner0CS, XMVectorSplatW(vCorner0CS));
			XMVECTOR vCorner1NDC = XMVectorDivide(vCorner1CS, XMVectorSplatW(vCorner1CS));
			XMVECTOR vCorner2NDC = XMVectorDivide(vCorner2CS, XMVectorSplatW(vCorner2CS));
			XMVECTOR vCorner3NDC = XMVectorDivide(vCorner3CS, XMVectorSplatW(vCorner3CS));

			XMVECTOR xyMins = _mm_min_ps(vCorner0NDC, _mm_min_ps(vCorner1NDC, _mm_min_ps(vCorner2NDC, vCorner3NDC)));
			XMVECTOR xyMaxs = _mm_max_ps(vCorner0NDC, _mm_max_ps(vCorner1NDC, _mm_max_ps(vCorner2NDC, vCorner3NDC)));

			const auto r = g_moc->TestRect(xyMins.m128_f32[0], xyMins.m128_f32[1], xyMaxs.m128_f32[0], xyMaxs.m128_f32[1], closestSpherePointW);
			return r == MaskedOcclusionCulling::VISIBLE;
		}
	}  // namespace

	bool TestObject(RE::NiAVObject* a_object)
	{
		if (!g_init || !EnableOcclusionTesting || !a_object)
			return true;

		if (a_object->GetAppCulled())
			return true;

		// Cheap size gate BEFORE any RTTI: skip small objects/subtrees entirely. This is a
		// plain float compare on a member, not the expensive netimmerse_cast in GetAABBNode.
		if (a_object->worldBound.radius < OccluderTestMinRadius)
			return true;

		auto*      aabb = GetAABBNode(a_object);  // single RTTI lookup (was called twice)
		const bool visible = aabb ? TestAABB(aabb) : TestSphere(a_object);

		g_tested.fetch_add(1, std::memory_order_relaxed);
		(aabb ? g_testedAABB : g_testedSphere).fetch_add(1, std::memory_order_relaxed);
		if (!visible) {
			g_culled.fetch_add(1, std::memory_order_relaxed);
			(aabb ? g_culledAABB : g_culledSphere).fetch_add(1, std::memory_order_relaxed);
		}
		return visible;
	}

	bool TestMultiBound(void* a_multiBound)
	{
		if (!g_init || !EnableOcclusionTesting || !a_multiBound)
			return true;

		auto* mb = static_cast<RE::BSMultiBound*>(a_multiBound);
		auto* aabb = netimmerse_cast<RE::BSMultiBoundAABB*>(mb->data.get());
		if (!aabb || aabb->size.z <= 1.0f)
			return true;  // no AABB shape (spheres etc.) or degenerate bounds -> keep

		const bool visible = TestAABB(aabb);
		g_tested.fetch_add(1, std::memory_order_relaxed);
		g_testedAABB.fetch_add(1, std::memory_order_relaxed);
		if (!visible) {
			g_culled.fetch_add(1, std::memory_order_relaxed);
			g_culledAABB.fetch_add(1, std::memory_order_relaxed);
		}
		return visible;
	}
}
