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
#include <RE/A/Actor.h>
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
	std::uint32_t MaxOccludersPerFrame = 4096;
	// Worker threads for the CullingThreadpool raster (applied at boot).
	std::int32_t RasterThreads = 2;
	// LOD-simplify occluder meshes to ~half the indices at cache time (meshopt_simplify,
	// Nukem's parameters). Cuts raster cost ~2-3x; conservativeness is preserved within
	// the simplifier's error bound (1e-3 of mesh extents).
	bool SimplifyOccluders = true;
	// Only objects/subtrees with at least this world-bound radius are occlusion-tested.
	// Lower = more tested objects (more draws saved) at more test cost per frame.
	float OccluderTestMinRadius = 0.0f;
	// MOC-exclusive mode: zero the compound frustum's operator count around the main
	// culls so the vanilla occlusion planes never process; the engine keeps its plain
	// view-frustum culling and MOC provides ALL occlusion. Off by default.
	bool ExclusiveOcclusion = false;
	// Leaf gate for the occluder GATHER: geometry below this world-bound radius is not
	// rasterized into the buffer ("render everything apart from small objects").
	float OccluderMinLeafSize = 100.0f;
	// DIAGNOSTIC: forcibly cull this percentage of objects that survive the real tests
	// (stable per-object hash). Answers "at what cull volume does perf improve" on a
	// given profile, independent of MOC's actual yield. BREAKS THE IMAGE -- probe only.
	std::int32_t DiagForceCullPercent = 0;
	bool CullTreeLODGroups = true;
	bool TreeOccluders = true;
	bool AlphaTestedOccluders = false;

	namespace
	{
		// Lower res = far cheaper raster (coverage cost scales with pixels) at coarser but
		// still-conservative occlusion. 400 full-detail meshes @1280x720 cost ~65ms/frame.
		// Buffer resolution (boot-time; CS_MOC_RES=WxH overrides, W mult of 8, H mult of 4).
		// Higher = tighter conservative tests = more culling yield; raster cost is on the
		// worker threads, so the on-path cost is resolution-independent.
		unsigned int MOC_WIDTH = 1280;
		unsigned int MOC_HEIGHT = 720;

		// PING-PONG buffers: the builder rasterizes into the BACK instance and
		// publishes it as FRONT on completion, so tests always read a COMPLETE
		// buffer ("complete z prepass is enough to test against prepass + main
		// lighting + alpha"). Testing the single shared instance mid-fill was the
		// distant-tree flicker; a wait-for-completion gate instead starved culling
		// entirely (first tests always precede raster completion). Cost: the front
		// buffer is one kick old, so verdicts project current-frame rects against
		// last-frame depth -- bounded by one frame of camera motion, absorbed by
		// the per-frame verdict cache + tree hysteresis.
		MaskedOcclusionCulling*              g_mocA = nullptr;
		MaskedOcclusionCulling*              g_mocB = nullptr;
		std::atomic<MaskedOcclusionCulling*> g_mocFront{ nullptr };
		MaskedOcclusionCulling*              g_mocBack = nullptr;  // builder thread only
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
		std::vector<GeoEntry>   g_readyList;   // builder-owned prepared list
		std::atomic<bool>       g_builderBusy{ false };  // true while a kick is being processed
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
		std::atomic<std::uint64_t> g_treeTested{ 0 };
		std::atomic<std::uint64_t> g_treeCulled{ 0 };
		std::atomic<std::uint64_t> g_kickCounter{ 0 };
		std::atomic<std::uint64_t> g_settleUntilKick{ 0 };

		// Determine occlusion ONCE per object per frame, and only against a
		// COMPLETE buffer. The z-prepass-side walk (the first main-view cull to
		// reach an object) computes the verdict; the prepass draw, the main
		// lighting passes and the alpha passes all reuse it. Without this the
		// same object was re-tested per walk against the async builder's
		// progressively filling buffer, so passes could disagree within one
		// frame -- visible flicker on distant tree groups (save 338).
		// g_rasterFrame is the builder's "raster phase complete for frame N"
		// publication; verdicts computed before completion are forced VISIBLE
		// (and cached, so all walks still agree) and do not advance the
		// occluded streak. occludedStreak = temporal hysteresis: a
		// caller-chosen number of consecutive occluded frames is required
		// before the verdict culls; any visible frame resets it.
		std::atomic<std::uint32_t> g_kickFrame{ 0xFFFFFFFFu };    // claim -> builder
		std::atomic<std::uint32_t> g_rasterFrame{ 0xFFFFFFFFu };  // builder -> testers
		struct FrameVerdict
		{
			std::uint32_t frame = 0xFFFFFFFFu;
			std::uint8_t  occludedStreak = 0;
			bool          culled = false;
		};
		constexpr std::size_t kVerdictShards = 16;  // cull walks run on concurrent job threads
		std::mutex                                    g_verdictMtx[kVerdictShards];
		std::unordered_map<const void*, FrameVerdict> g_verdictMap[kVerdictShards];
		std::atomic<std::uint32_t>                    g_verdictFlips{ 0 };  // stability diag

		template <class TestFn>
		bool CachedVerdict(const void* a_key, std::uint8_t a_framesToCull, TestFn&& a_test)
		{
			auto*               gfx = RE::BSGraphics::State::GetSingleton();
			const std::uint32_t frame = gfx ? gfx->frameCount : 0;
			const std::size_t   shard = (reinterpret_cast<std::uintptr_t>(a_key) >> 4) & (kVerdictShards - 1);
			std::scoped_lock    lk(g_verdictMtx[shard]);
			auto&               map = g_verdictMap[shard];
			if (map.size() > 4096)
				map.clear();  // scene-change eviction; entries rebuild on demand
			auto& v = map[a_key];
			if (v.frame != frame) {
				v.frame = frame;
				// Front buffer is always a COMPLETE raster (ping-pong), so the
				// verdict is always real -- no forced-visible starvation.
				if (a_test()) {
					v.occludedStreak = 0;
					if (v.culled)
						g_verdictFlips.fetch_add(1, std::memory_order_relaxed);
					v.culled = false;
				} else {
					v.occludedStreak = static_cast<std::uint8_t>(std::min<int>(v.occludedStreak + 1, 250));
					const bool cull = v.occludedStreak >= a_framesToCull;
					if (cull != v.culled)
						g_verdictFlips.fetch_add(1, std::memory_order_relaxed);
					v.culled = cull;
				}
			}
			return !v.culled;  // true = keep (possibly visible)
		}
		std::chrono::high_resolution_clock::time_point g_buildStart;

		// Debug depth-buffer view: MOC's software depth rasterized to an RGBA8 texture
		// for display in the feature settings (ComputePixelDepthBuffer -> grayscale).
		winrt::com_ptr<ID3D11Texture2D>          g_debugTex;
		winrt::com_ptr<ID3D11ShaderResourceView> g_debugSRV;
		std::vector<float>                       g_debugDepth;
		// Flicker-free debug view: the BUILDER publishes a complete-buffer snapshot
		// right after rasterization finishes; the UI only ever uploads settled
		// snapshots (a live read from the UI thread races the async fill and shows
		// half-built buffers -- "objects flicker", far/late occluders like terrain
		// mostly absent). g_debugViewFrames > 0 keeps snapshotting for ~1s after the
		// last UI request so the view stays live while the menu is open.
		std::atomic<int>   g_debugViewFrames{ 0 };
		std::mutex         g_debugSnapMtx;
		std::vector<float> g_debugSnapshot;
		bool               g_debugSnapValid = false;

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
			// Accept every BSTriShape-derived type with the standard TriShape runtime
			// data. Critically this includes kSubIndexLandTriShape/kSubIndexTriShape --
			// ACTIVE-CELL TERRAIN -- which the kTriShape-only gate silently rejected,
			// leaving holes in the buffer floor ("seeing through the ground" = nothing
			// behind terrain ever culled). Dynamic/particle/instanced types stay out.
			switch (a_geom->GetType().get()) {
			case RE::BSGeometry::Type::kTriShape:
			case RE::BSGeometry::Type::kMeshLODTriShape:
			case RE::BSGeometry::Type::kLODMultiIndexTriShape:
			case RE::BSGeometry::Type::kMultiIndexTriShape:
			case RE::BSGeometry::Type::kSubIndexTriShape:
			case RE::BSGeometry::Type::kSubIndexLandTriShape:
				break;
			default:
				return false;
			}

			auto&                     geomRT = a_geom->GetGeometryRuntimeData();
			RE::BSGraphics::TriShape* rendererData = geomRT.rendererData;
			if (!rendererData || !rendererData->rawVertexData || !rendererData->rawIndexData) {
				// One-shot diag: LARGE geometry rejected here = holes in the buffer floor.
				static std::atomic<int> s_logBudget{ 8 };
				if (a_geom->worldBound.radius > 500.0f && s_logBudget.fetch_sub(1, std::memory_order_relaxed) > 0)
					logger::info("[MOC][reject] '{}' type={} radius={:.0f} rendererData={} rawVtx={} rawIdx={}",
						a_geom->name.c_str(), static_cast<int>(a_geom->GetType().get()), a_geom->worldBound.radius,
						rendererData != nullptr,
						rendererData && rendererData->rawVertexData, rendererData && rendererData->rawIndexData);
				return false;
			}

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
			if (stride == 0) {
				static std::atomic<int> s_strideBudget{ 8 };
				if (a_geom->worldBound.radius > 500.0f && s_strideBudget.fetch_sub(1, std::memory_order_relaxed) > 0)
					logger::info("[MOC][reject] '{}' type={} radius={:.0f} STRIDE0",
						a_geom->name.c_str(), static_cast<int>(a_geom->GetType().get()), a_geom->worldBound.radius);
				return false;  // no position stream in slot 0
			}

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
				// Trees/bushes (BSLeafAnimNode subtrees): descend when TreeOccluders is
				// on -- their OPAQUE parts (trunks, solid branches) are legitimate
				// occluders; every alpha-tested/blended leaf card is rejected by the
				// per-geometry alpha gate below. With the option off, skip the whole
				// subtree (the pre-tree-occluder behavior).
				if (TreeOccluders || !IsLeafAnimNode(node)) {
					auto& children = node->GetChildren();
					for (std::uint16_t i = 0; i < children.capacity(); ++i) {
						if (auto* child = children[i].get())
							RenderRecursive(child, false);
					}
				}
			} else if (RE::BSGeometry* geometry = a_object->AsGeometry()) {
				// One-shot diag: LAND geometry flowing through the walk (type 8/9). Suspect:
				// land world bounds are degenerate -> the size gate drops the ground plane.
				const auto geoType = geometry->GetType().get();
				if (geoType == RE::BSGeometry::Type::kSubIndexTriShape || geoType == RE::BSGeometry::Type::kSubIndexLandTriShape) {
					static std::atomic<int> s_landLog{ 6 };
					if (s_landLog.fetch_sub(1, std::memory_order_relaxed) > 0)
						logger::info("[MOC][land] '{}' type={} radius={:.1f} pos=({:.0f},{:.0f},{:.0f})",
							geometry->name.c_str(), static_cast<int>(geoType), geometry->worldBound.radius,
							geometry->world.translate.x, geometry->world.translate.y, geometry->world.translate.z);
				}
				// Alpha gate: alpha-BLENDED surfaces are translucent and never occlude.
				// Alpha-TESTED textures have holes, so they are excluded by default --
				// but AlphaTestedOccluders opts them in as solid occluders (aggressive:
				// dense foliage blocks almost everything behind it in practice, at the
				// risk of over-culling through sparse cutouts).
				if (auto* alpha = geometry->GetGeometryRuntimeData().alphaProperty.get();
					alpha && (alpha->GetAlphaBlending() || (!AlphaTestedOccluders && alpha->GetAlphaTesting())))
					return;
				if (geometry->worldBound.radius > OccluderMinLeafSize) {
					const float d1 = geometry->world.translate.x - g_posAdjust.x;
					const float d2 = geometry->world.translate.y - g_posAdjust.y;
					if (((d1 * d1) + (d2 * d2)) < (OccluderMaxDistance * OccluderMaxDistance))
						RegisterGeometry(geometry);
				}
			}
		}

		// Terrain occluders, built from the decoded heightmap. The engine's land
		// render meshes keep NO CPU-side raw index copy (rendererData->rawIndexData
		// is null for every land quad in the grid -- measured), so the normal
		// register/cache path can never rasterize terrain. Instead one mesh per
		// cell is generated from LoadedLandData::heights (4 quadrants of 17x17
		// samples, 128-unit grid step) and rasterized every kick. Verts are packed
		// (x, y, 1.0f, z) to match the pool's VertexLayout(16,4,12).
		struct LandOccluderMesh
		{
			std::vector<float>        verts;
			std::vector<unsigned int> indices;
		};
		std::unordered_map<std::uint64_t, LandOccluderMesh> g_landCache;  // builder thread only
		std::vector<const LandOccluderMesh*>                g_landReady;  // filled by gather, drawn next kick

		LandOccluderMesh BuildLandMesh(const RE::TESObjectLAND::LoadedLandData* a_data, float a_worldX, float a_worldY)
		{
			LandOccluderMesh m;
			m.verts.reserve(4u * 289u * 4u);
			m.indices.reserve(4u * 16u * 16u * 6u);
			// heights[] are CELL-LOCAL Z relative to the cell's height midpoint
			// ((min+max)/2, stored at LoadedLandData+0x49C0) -- the engine's own
			// consumers add it back (TESObjectLAND::GetNiPointHeight 0x14025B830:
			// addss [loadedData+49C0h]; the render-mesh builder bakes it into the
			// block node's local translation instead). IDA-verified 1.5.97.
			const float zBase = *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_data) + 0x49C0);
			for (int q = 0; q < 4; ++q) {
				const float qx = a_worldX + static_cast<float>(q & 1) * 2048.0f;
				const float qy = a_worldY + static_cast<float>(q >> 1) * 2048.0f;
				const unsigned int base = static_cast<unsigned int>(m.verts.size() / 4u);
				for (int row = 0; row < 17; ++row) {
					for (int col = 0; col < 17; ++col) {
						m.verts.push_back(qx + static_cast<float>(col) * 128.0f);
						m.verts.push_back(qy + static_cast<float>(row) * 128.0f);
						m.verts.push_back(1.0f);
						// Small downward bias: keeps the occluder plane strictly conservative
						// against LOD morphing / terrain-blending displacement.
						m.verts.push_back(a_data->heights[q][row * 17 + col] + zBase - 16.0f);
					}
				}
				for (unsigned int r = 0; r < 16; ++r) {
					for (unsigned int c = 0; c < 16; ++c) {
						const unsigned int i0 = base + r * 17u + c;
						const unsigned int i1 = i0 + 1u;
						const unsigned int i2 = i0 + 17u;
						const unsigned int i3 = i2 + 1u;
						m.indices.insert(m.indices.end(), { i0, i1, i2, i1, i3, i2 });
					}
				}
			}
			return m;
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
				static std::atomic<bool> s_ssnCensus{ false };
				if (ssn && gs && !s_ssnCensus.exchange(true)) {
					// Type census of EVERY shadow-scene child subtree: find where LAND
					// geometry (t8/t9) actually hangs -- the cells contain none.
					struct SsnCensus
					{
						int types[16] = {};
						int total = 0;
						void Walk(RE::NiAVObject* o, int depth)
						{
							if (!o || depth > 10)
								return;
							if (auto* g = o->AsGeometry()) {
								const int t = static_cast<int>(g->GetType().get());
								if (t >= 0 && t < 16)
									++types[t];
								++total;
							} else if (auto* n = o->AsNode()) {
								auto& kids = n->GetChildren();
								for (std::uint16_t k = 0; k < kids.capacity(); ++k)
									Walk(kids[k].get(), depth + 1);
							}
						}
					};
					logger::info("[MOC][diag] ssn census: '{}' childCount={}", ssn->name.c_str(), ssn->GetChildren().size());
					for (std::uint16_t i = 0; i < ssn->GetChildren().capacity() && i < 16; ++i) {
						auto* c = ChildAt(ssn, i);
						if (!c)
							continue;
						SsnCensus cen;
						cen.Walk(c, 0);
						std::string typeStr;
						for (int t = 0; t < 16; ++t)
							if (cen.types[t])
								typeStr += fmt::format("t{}={} ", t, cen.types[t]);
						logger::info("[MOC][diag]   ssn[{}]='{}' geoms={} {}", i, c->name.c_str(), cen.total, typeStr);
					}
					// Also the two ObjectLODRoot children the cell walk skips.
					if (auto* olr = ChildNodeAt(ssn, 3)) {
						for (std::uint16_t i = 0; i < 2; ++i)
							if (auto* c = ChildAt(olr, i)) {
								SsnCensus cen;
								cen.Walk(c, 0);
								std::string typeStr;
								for (int t = 0; t < 16; ++t)
									if (cen.types[t])
										typeStr += fmt::format("t{}={} ", t, cen.types[t]);
								logger::info("[MOC][diag]   olr[{}]='{}' geoms={} {}", i, c->name.c_str(), cen.total, typeStr);
							}
					}
				}
				// One-shot: trace the ACTUAL land quads (TES -> cell -> cellLand ->
				// loadedData->geom[4]) -- exact pointers, no name guessing. The parent
				// chain shows where they hang; radius/flags/desc show which filter
				// would drop them from the walk.
				static std::atomic<bool> s_landChain{ false };
				if (!s_landChain.exchange(true)) {
					if (auto* tes = RE::TES::GetSingleton()) {
						int logged = 0;
						auto traceCell = [&](RE::TESObjectCELL* cell) {
							if (!cell || logged >= 12)
								return;
							auto* land = cell->GetRuntimeData().cellLand;
							if (!land || !land->loadedData)
								return;
							for (int q = 0; q < 4 && logged < 12; ++q) {
								auto* g = land->loadedData->geom[q].get();
								if (!g)
									continue;
								std::string chain;
								for (RE::NiNode* pn = g->parent; pn; pn = pn->parent)
									chain += fmt::format(" <- '{}'", pn->name.c_str());
								const auto& rt = g->GetGeometryRuntimeData();
								const int stride = rt.rendererData ? static_cast<int>(GetVertexStride(rt.rendererData->vertexDesc)) : -1;
								logger::info("[MOC][landchain] quad{} '{}' type={} r={:.0f} pos=({:.0f},{:.0f},{:.0f}) stride={} flags={:X} chain:{}",
									q, g->name.c_str(), static_cast<int>(g->GetType().get()), g->worldBound.radius,
									g->world.translate.x, g->world.translate.y, g->world.translate.z,
									stride, g->GetFlags().underlying(), chain);
								++logged;
							}
						};
						if (auto* grid = tes->gridCells)
							for (std::uint32_t gx = 0; gx < grid->length; ++gx)
								for (std::uint32_t gy = 0; gy < grid->length; ++gy)
									traceCell(grid->GetCell(gx, gy));
						if (logged == 0)
							logger::info("[MOC][landchain] no land quads found in the loaded grid");
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

				// One-shot diag: EVERY cell's child census (finds where LAND geometry hangs).
				static std::atomic<int> s_cellCensus{ 30 };
				if (s_dumpStructure && s_cellCensus.fetch_sub(1, std::memory_order_relaxed) > 0) {
					logger::info("[MOC][cell] cell[{}] '{}' kids={}", i, cellNode->name.c_str(), cellNode->GetChildren().size());
					struct Census
					{
						int types[16] = {};
						int total = 0;
						void Walk(RE::NiAVObject* o, int depth)
						{
							if (!o || depth > 8)
								return;
							if (auto* g = o->AsGeometry()) {
								const int t = static_cast<int>(g->GetType().get());
								if (t >= 0 && t < 16)
									++types[t];
								++total;
							} else if (auto* n = o->AsNode()) {
								auto& kids = n->GetChildren();
								for (std::uint16_t k = 0; k < kids.capacity(); ++k)
									Walk(kids[k].get(), depth + 1);
							}
						}
					};
					Census cellCen;
					cellCen.Walk(cellNode, 0);
					std::string cellTypes;
					for (int t = 0; t < 16; ++t)
						if (cellCen.types[t])
							cellTypes += fmt::format("t{}={} ", t, cellCen.types[t]);
					logger::info("[MOC][cell]   TOTAL geoms={} {}", cellCen.total, cellTypes);
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

			// Land pass: rasterize terrain from heightmap-built meshes (see
			// LandOccluderMesh above). Cell selection = distance gate on the cell
			// center only; NO frustum gate (the cell the camera stands in has its
			// center behind the view and must still occlude the foreground).
			g_landReady.clear();
			if (g_landCache.size() > 512)
				g_landCache.clear();  // travel eviction; rebuilt on demand
			if (auto* tes = RE::TES::GetSingleton()) {
				// land3DAttached is the engine's own "all grid land is attached" flag --
				// the strongest available guard against reading half-built LoadedLandData.
				if (auto* grid = tes->gridCells; grid && grid->land3DAttached) {
					for (std::uint32_t gx = 0; gx < grid->length; ++gx) {
						for (std::uint32_t gy = 0; gy < grid->length; ++gy) {
							auto* cell = grid->GetCell(gx, gy);
							// Fully-attached cells only: during the post-load attach window
							// (after the LoadingMenu closes but before the scene settles)
							// half-built cells carry garbage land pointers -- reading
							// loadedData there crashed the builder (AV, crash-16-11-27).
							if (!cell || !cell->IsAttached())
								continue;
							auto* landForm = cell->GetRuntimeData().cellLand;
							if (!landForm || !landForm->loadedData)
								continue;
							auto* ext = cell->GetCoordinates();
							if (!ext)
								continue;
							const float d1 = (ext->worldX + 2048.0f) - g_posAdjust.x;
							const float d2 = (ext->worldY + 2048.0f) - g_posAdjust.y;
							if (((d1 * d1) + (d2 * d2)) >= (OccluderMaxDistance * OccluderMaxDistance))
								continue;
							const std::uint64_t key =
								(static_cast<std::uint64_t>(static_cast<std::uint32_t>(ext->cellX)) << 32) |
								static_cast<std::uint32_t>(ext->cellY);
							auto it = g_landCache.find(key);
							if (it == g_landCache.end()) {
								// One-shot decode diagnostic: height samples vs the camera and
								// the engine's own extents -- catches wrong scale/offset fast.
								static std::atomic<int> s_hdec{ 4 };
								if (s_hdec.fetch_sub(1, std::memory_order_relaxed) > 0)
									logger::info("[MOC][hdec] cell=({},{}) world=({:.0f},{:.0f}) h[0][0]={:.1f} h[0][144]={:.1f} h[3][288]={:.1f} extents=({:.1f},{:.1f}) camZ={:.1f}",
										ext->cellX, ext->cellY, ext->worldX, ext->worldY,
										landForm->loadedData->heights[0][0], landForm->loadedData->heights[0][144],
										landForm->loadedData->heights[3][288],
										landForm->loadedData->heightExtents.x, landForm->loadedData->heightExtents.y,
										g_posAdjust.z);
								it = g_landCache.emplace(key, BuildLandMesh(landForm->loadedData, ext->worldX, ext->worldY)).first;
							}
							g_landReady.push_back(&it->second);
						}
					}
				}
			}
		}

		bool GetCachedGeometry(RE::BSGeometry* a_geom, IndexPair& a_outIndices, float*& a_outVerts);
		std::uint32_t RasterizeOccluder(RE::BSGeometry* a_geom);

		// Builder-thread main: the pool's SINGLE PRODUCER. Each kick: (1) rasterize the
		// list prepared last kick using the CURRENT frame's matrices (published by the
		// claim thread before kicking), (2) gather + select + sort + pre-warm NEXT frame's
		// list. Scene-graph reads race the game by design (Nukem's model) -- the walk is
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
				g_builderBusy.store(true, std::memory_order_release);

				const auto t0 = std::chrono::high_resolution_clock::now();

				// Phase 1: rasterize last kick's prepared list. All pool calls live here.
				// Rasterize into the BACK buffer; tests keep reading FRONT.
				// SetBuffer AFTER WakeThreads: it Flush()es first, and Flush yield-
				// spins until the WORKERS drain the queue -- with workers suspended
				// the builder livelocks (28fps, gather frozen, front never updated).
				g_pool->WakeThreads();
				g_pool->SetBuffer(g_mocBack);
				g_pool->ClearBuffer();
				if (EnableOccluderRendering) {
					// No triangle budget: the producer is this async builder, so job-queue
					// backpressure only stretches this thread's schedule -- the game never
					// waits. A complete buffer (no missing floors/walls) culls strictly
					// better; partial fills mid-frame are conservative by contract.
					std::uint32_t enqueued = 0;
					for (auto& e : g_readyList) {
						RasterizeOccluder(e.geometry);
						++enqueued;
					}
					// Terrain heightmap meshes: world-space verts, so camera-relative
					// is a pure translation. BACKFACE_NONE = winding-proof; near clip
					// required (the camera stands ON these triangles).
					if (!g_landReady.empty()) {
						const XMMATRIX rel = XMMatrixTranslation(-g_posAdjust.x, -g_posAdjust.y, -g_posAdjust.z);
						const XMMATRIX mvp = XMMatrixMultiply(rel, g_viewProj);
						g_pool->SetMatrix(reinterpret_cast<const float*>(&mvp));
						for (const auto* lm : g_landReady) {
							g_pool->RenderTriangles(
								lm->verts.data(), lm->indices.data(),
								static_cast<int>(lm->indices.size() / 3u),
								MaskedOcclusionCulling::BACKFACE_NONE,
								MaskedOcclusionCulling::CLIP_PLANE_ALL);
							++enqueued;
						}
					}
					g_lastOccluderCount = enqueued;
				} else {
					g_lastOccluderCount = 0;
				}
				g_pool->SuspendThreads();
				// Raster complete: publish back as the new FRONT, recycle old front.
				g_mocBack = g_mocFront.exchange(g_mocBack, std::memory_order_acq_rel);
				g_rasterFrame.store(g_kickFrame.load(std::memory_order_acquire), std::memory_order_release);

				// Publish a debug-view snapshot of the now-complete buffer (menu open).
				if (g_debugViewFrames.load(std::memory_order_relaxed) > 0) {
					g_debugViewFrames.fetch_sub(1, std::memory_order_relaxed);
					std::scoped_lock lk2(g_debugSnapMtx);
					g_debugSnapshot.resize(static_cast<std::size_t>(MOC_WIDTH) * MOC_HEIGHT);
					g_mocFront.load(std::memory_order_acquire)->ComputePixelDepthBuffer(g_debugSnapshot.data(), false);
					g_debugSnapValid = true;
				}

				// Phase 2: gather NEXT frame's candidates.
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

				g_readyList.swap(g_geoList);  // builder-owned; no other consumer
				g_builderBusy.store(false, std::memory_order_release);
			}
		}

		std::uint32_t RasterizeOccluder(RE::BSGeometry* a_geom)
		{
			IndexPair indices;
			float*    verts = nullptr;
			if (!a_geom || !GetCachedGeometry(a_geom, indices, verts) || !verts || !indices.Data)
				return 0;

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
			// CLIP_PLANE_ALL, not SIDES: without near-plane clipping every triangle
			// crossing the near plane is dropped instead of clipped -- which is exactly
			// the terrain under/ahead of the camera (and interior walls you stand
			// against). That was the "ground is black at the bottom of the buffer"
			// hole: mid/far terrain rasterized, the near rows vanished.
			g_pool->RenderTriangles(
				verts,
				indices.Data,
				static_cast<int>(indices.Count / 3),
				winding,
				MaskedOcclusionCulling::CLIP_PLANE_ALL);
			return indices.Count / 3;
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
		g_mocA = MaskedOcclusionCulling::Create(MaskedOcclusionCulling::AVX2);
		g_mocB = MaskedOcclusionCulling::Create(MaskedOcclusionCulling::AVX2);
		auto* g_moc = g_mocA;  // local alias for the shared init below
		if (!g_moc || !g_mocB) {
			logger::warn("[MOC] MaskedOcclusionCulling::Create failed; occlusion culling disabled");
			return;
		}
		// CS_MOC_RES=WxH (e.g. 1280x720): buffer-resolution override for A/B sweeps.
		{
			char resBuf[24] = {};
			if (GetEnvironmentVariableA("CS_MOC_RES", resBuf, sizeof(resBuf)) && resBuf[0]) {
				unsigned int w = 0, h = 0;
				if (sscanf_s(resBuf, "%ux%u", &w, &h) == 2 && w >= 256 && h >= 144 && w <= 1920 && h <= 1080) {
					MOC_WIDTH = w & ~7u;   // multiple of 8
					MOC_HEIGHT = h & ~3u;  // multiple of 4
				}
			}
		}
		g_mocA->SetResolution(MOC_WIDTH, MOC_HEIGHT);
		g_mocA->ClearBuffer();
		g_mocB->SetResolution(MOC_WIDTH, MOC_HEIGHT);
		g_mocB->ClearBuffer();
		g_mocFront.store(g_mocA, std::memory_order_release);
		g_mocBack = g_mocB;

		// Intel's CullingThreadpool: occluder rasterization is queued by the (single)
		// build thread and executed by worker threads binning the screen. Bins must be
		// tile-aligned and >= thread count: 4x4 bins of 128x72 over 512x288. maxJobs=32
		// is Intel's recommended queue depth. Workers are woken per build and suspended
		// right after the flush, so the pool costs nothing outside the build window.
		const unsigned int threads = std::clamp(RasterThreads, 1, 16);  // 4x4 bins support up to 16 workers
		g_pool = new CullingThreadpool(threads, 4, 4, 32);
		g_pool->SetBuffer(g_mocBack);
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
		if (!g_init || !g_mocFront.load(std::memory_order_relaxed))
			return;
		EnsureDebugTexture();
		if (!g_debugTex || !g_debugSRV)
			return;
		auto* ctx = globals::d3d::context;
		if (!ctx)
			return;

		// Ask the builder to keep publishing complete-buffer snapshots (~1s worth per
		// request) and display the latest one. Never read the live buffer from this
		// thread: the async builder clears+refills it mid-frame, so a UI-time read
		// catches a random fill state (flicker; late-rasterized far occluders like
		// terrain mostly missing).
		g_debugViewFrames.store(60, std::memory_order_relaxed);
		{
			std::scoped_lock lk(g_debugSnapMtx);
			if (!g_debugSnapValid)
				return;  // no complete snapshot yet (first frames after menu open)
			g_debugDepth = g_debugSnapshot;
		}

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
		if (!g_init || !g_mocFront.load(std::memory_order_relaxed))
			return;
		const std::size_t n = static_cast<std::size_t>(MOC_WIDTH) * MOC_HEIGHT;
		if (g_debugDepth.size() != n)
			g_debugDepth.resize(n);
		g_mocFront.load(std::memory_order_acquire)->ComputePixelDepthBuffer(g_debugDepth.data(), false);  // flipY=false is empirically top-down (matches Nukem)

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

		// The pool references the MOC instances and joins its workers in the destructor -- destroy it first.
		delete g_pool;
		g_pool = nullptr;

		g_mocFront.store(nullptr, std::memory_order_release);
		g_mocBack = nullptr;
		if (g_mocA) {
			MaskedOcclusionCulling::Destroy(g_mocA);
			g_mocA = nullptr;
		}
		if (g_mocB) {
			MaskedOcclusionCulling::Destroy(g_mocB);
			g_mocB = nullptr;
		}
		g_init = false;
	}

	bool IsInitialized()
	{
		return g_init && g_mocFront.load(std::memory_order_relaxed) != nullptr;
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

	void QuiesceBuilder()
	{
		// Refuse-then-drain: the claim path already refuses kicks while the loading menu
		// is open; here we wait out any gather already in flight. Bounded (~200ms) so a
		// wedged walk can't hang the UI thread -- by then the walk has either finished or
		// crashed anyway.
		for (int i = 0; i < 2000 && g_builderBusy.load(std::memory_order_acquire); ++i)
			std::this_thread::sleep_for(std::chrono::microseconds(100));
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
		if (!g_init || !g_mocFront.load(std::memory_order_relaxed))
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
		if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
			g_settleUntilKick.store(g_kickCounter.load(std::memory_order_relaxed) + 240, std::memory_order_relaxed);
			return false;
		}
		// Post-load settle window: the LoadingMenu closes BEFORE the scene finishes
		// attaching (cells stream in for a couple of seconds); a gather in that window
		// walks half-built cells -- two load-time crashes (16:11 AV in the land pass,
		// 16:20 silent death mid cell-census). ~240 kicks =~ 1-2s of no culling after
		// every load, invisible to the player.
		if (g_kickCounter.fetch_add(1, std::memory_order_relaxed) < g_settleUntilKick.load(std::memory_order_relaxed))
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

		// Kick the BUILDER: it is the pool's single producer and does EVERYTHING -- wake,
		// clear, enqueue LAST frame's prepared list with THIS frame's matrices (published
		// above), suspend, then gather NEXT frame's candidates. The claim thread pays ~0;
		// tests may start immediately against the filling buffer (conservatively correct
		// per the library contract). When occluder rendering is off the builder clears.
		{
			std::scoped_lock lk(g_builderMtx);
			g_builderKick = true;
		}
		g_kickFrame.store(frame, std::memory_order_release);
		g_builderCV.notify_one();

		// DIAG (rate-limited): build time (gather + raster, on whichever cull thread won
		// the claim) + cull tally. Verification dumps are NOT done here -- BuildOccluders
		// runs on BuildSceneLists WORKER threads and must never touch the D3D context (a
		// worker-thread CopyResource/Map killed the process); see DumpDebugImages, called
		// from the render thread (Feature::Prepass).
		if ((frame % 120u) == 0u) {
			const double buildMs = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - g_buildStart)
									   .count();
			logger::info("[MOC][diag] frame={} enqueue={:.2f}ms gather={:.2f}ms occluders={} cells={}/culled={} | tested={} culled={} (aabb {}/{} sphere {}/{}) tree {}/{} flips={}",
				frame, buildMs, g_lastGatherMs, g_lastOccluderCount,
				g_cellsSeen, g_cellsCulled,
				g_tested.load(), g_culled.load(),
				g_culledAABB.load(), g_testedAABB.load(), g_culledSphere.load(), g_testedSphere.load(),
				g_treeCulled.load(), g_treeTested.load(), g_verdictFlips.exchange(0, std::memory_order_relaxed));
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
		if (!s_dump || !g_init || !g_mocFront.load(std::memory_order_relaxed))
			return;
		auto* gfxState = RE::BSGraphics::State::GetSingleton();
		if (!gfxState || (gfxState->frameCount % 120u) != 0u)
			return;
		// Deterministic dump: the buffer fills ASYNCHRONOUSLY (builder + workers, no
		// flush); reading mid-fill shows a partial buffer that varies run to run. Drain
		// the builder, then give the workers a moment to finish the queued tail.
		QuiesceBuilder();
		std::this_thread::sleep_for(std::chrono::milliseconds(8));
		DumpDepthImage();  // MOC occluder buffer -> moc_depth.png (+ raw .bin)
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

			if (minW < 0.00000001f) {
				// Behind/straddling the near plane: fall back to the frustum verdict so
				// MOC-exclusive culling reproduces vanilla frustum culls (see TestSphere).
				const XMVECTOR center = _mm_sub_ps(_mm_setr_ps(a_object->center.x, a_object->center.y, a_object->center.z, 0.0f), g_posAdjustV);
				const XMVECTOR halfExtents = _mm_setr_ps(a_object->size.x, a_object->size.y, a_object->size.z, 0.0f);
				return g_frustum.AABBInFrustum(center, halfExtents);
			}

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

			const auto r = g_mocFront.load(std::memory_order_acquire)->TestRect(screenMin.m128_f32[0], screenMin.m128_f32[1], screenMax.m128_f32[0], screenMax.m128_f32[1], minW);
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
			if (closestSpherePointW < 0.000001f) {
				// Behind/straddling the near plane: TestRect can't decide. For MOC-exclusive
				// culling this must match the ENGINE's frustum verdict, not blanket-keep:
				// a sphere fully outside the view frustum (incl. behind the camera) is
				// culled exactly like vanilla frustum culling would.
				const XMVECTOR sphere = _mm_setr_ps(
					c.x - g_posAdjust.x, c.y - g_posAdjust.y, c.z - g_posAdjust.z, sphereRadius);
				return g_frustum.SphereInFrustum(sphere);
			}

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

			const auto r = g_mocFront.load(std::memory_order_acquire)->TestRect(xyMins.m128_f32[0], xyMins.m128_f32[1], xyMaxs.m128_f32[0], xyMaxs.m128_f32[1], closestSpherePointW);
			return r == MaskedOcclusionCulling::VISIBLE;
		}
	}  // namespace

	bool TestObject(RE::NiAVObject* a_object)
	{
		if (!g_init || !EnableOcclusionTesting || !a_object)
			return true;

		if (a_object->GetAppCulled())
			return true;

		// DIAGNOSTIC forced culling: pseudo-randomly (stable per object) cull a share of
		// everything that would otherwise be kept. Perf probe only; image intentionally
		// breaks. Placed after the init/master gates but before the real work so the
		// probe measures pure cull-volume effect.
		if (DiagForceCullPercent > 0) {
			const std::uintptr_t h = reinterpret_cast<std::uintptr_t>(a_object);
			if (static_cast<std::int32_t>((h >> 4) * 2654435761u % 100u) < DiagForceCullPercent) {
				g_culled.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		// Never occlusion-test ACTORS: skinned world bounds lag animation, actors move
		// against our one-frame-stale occluder list, and they are few (cheap to keep).
		// Vanilla culling handles them with fresh state; wrongly culling an NPC is the
		// most visible artifact possible.
		if (auto* ref = a_object->GetUserData(); ref && ref->As<RE::Actor>())
			return true;

		// Cheap size gate BEFORE any RTTI: skip small objects/subtrees entirely. This is a
		// plain float compare on a member, not the expensive netimmerse_cast in GetAABBNode.
		if (a_object->worldBound.radius < OccluderTestMinRadius)
			return true;

		// Verdict computed on the first walk of the frame against the complete
		// buffer, reused by every later pass (tallies tick once per object per
		// FRAME, not per walk).
		const bool visible = CachedVerdict(a_object, 1, [&] {
			auto*      aabb = GetAABBNode(a_object);  // single RTTI lookup (was called twice)
			const bool vis = aabb ? TestAABB(aabb) : TestSphere(a_object);
			g_tested.fetch_add(1, std::memory_order_relaxed);
			(aabb ? g_testedAABB : g_testedSphere).fetch_add(1, std::memory_order_relaxed);
			if (!vis) {
				g_culled.fetch_add(1, std::memory_order_relaxed);
				(aabb ? g_culledAABB : g_culledSphere).fetch_add(1, std::memory_order_relaxed);
			}
			return vis;
		});
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

		const bool visible = CachedVerdict(a_multiBound, 1, [&] {
			const bool vis = TestAABB(aabb);
			g_tested.fetch_add(1, std::memory_order_relaxed);
			g_testedAABB.fetch_add(1, std::memory_order_relaxed);
			if (!vis) {
				g_culled.fetch_add(1, std::memory_order_relaxed);
				g_culledAABB.fetch_add(1, std::memory_order_relaxed);
			}
			return vis;
		});
		return visible;
	}

	bool IsMainViewCamera(const RE::NiCamera* a_camera)
	{
		return a_camera && (a_camera == GetMainCamera() || a_camera == *g_cullCamera);
	}

	bool TestInstanceGroup(RE::BSMultiBoundAABB* a_aabb)
	{
		if (!g_init || !EnableOcclusionTesting || !CullTreeLODGroups || !a_aabb)
			return true;

		// 3 consecutive occluded frames before a tree group culls: these large,
		// distant groups sit near the verdict boundary and flickered (save 338).
		return CachedVerdict(a_aabb, 3, [&] {
			const bool vis = TestAABB(a_aabb);
			g_treeTested.fetch_add(1, std::memory_order_relaxed);
			if (!vis)
				g_treeCulled.fetch_add(1, std::memory_order_relaxed);
			return vis;
		});
	}
}
