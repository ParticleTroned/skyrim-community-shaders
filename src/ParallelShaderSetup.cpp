#include "ParallelShaderSetup.h"

#include "Globals.h"
#include "State.h"
#include "Util.h"

#include <RE/B/BSBatchRenderer.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSRenderPass.h>
#include <RE/B/BSShaderAccumulator.h>
#include <RE/B/BSUtilityShader.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <thread>

namespace
{
	// --- Diagnostic: compare our fill vs the engine's actual geometry-CB write ---
	// After the engine's SetupGeometry writes the VS geometry CB, copy it to a staging
	// buffer and read it back (rate-limited to one sample per ~300 frames so the
	// readback stall is negligible), then diff against what our fill would produce. No
	// bind -> rendering stays the engine's correct output.
	bool g_diag = false;                     // CS_PARALLEL_DIAG=1
	bool g_bindEnabled = false;              // CS_PARALLEL_BIND=1
	bool g_skipEngineGeomFill = false;       // CS_PARALLEL_SKIP_FILL=1 (the perf win)

	// Dev-only within-session A/B toggle (CS_PARALLEL_TOGGLE=1): read an integer mode from
	// a control file once per frame so shipping vs. our path can be compared on the SAME
	// warm session (cross-session FPS is confounded by the rig warming up). Modes:
	//   0 = pipeline off  (shipping behavior: no fill, no bind, no redirect)
	//   1 = our path      (parallel fill + slice bind + engine-map redirected -> the win)
	//   2 = our path, no redirect (fill + bind, but engine still maps -> isolates our overhead)
	bool g_toggleMode = false;
	int g_runtimeMode = 1;
	bool g_pipelineActive = true;            // derived per frame from the mode

	// The BSUtility geometry-CB flag word is F = passEnum - 0x2B (verified: passEnum 0x14046
	// -> F 0x1401B, 0x4045 -> 0x401A). So world-only routing is decided inline from the
	// pass with NO per-technique map + no cross-frame harvest. The world matrix always lands
	// at float offset base[80] (constant 4 for BSUtility); cached once from the engine.
	constexpr uint32_t kFlagBias = 0x2B;
	uint32_t g_worldFloatOffset = 4;
	inline uint32_t FlagsForPass(const RE::BSRenderPass* p) { return p->passEnum - kFlagBias; }
	ID3D11Buffer* g_vsGeomCB = nullptr;      // *(ConstantBuffers+0x38)
	uint32_t g_geomCBSize = 0;
	bool g_inUtilitySetup = false;           // render thread only
	void* g_lastGeomMapPtr = nullptr;
	bool g_engineCaptured = false;
	alignas(16) uint8_t g_engineScratch[1024] = {};

	// Set true (render thread, per pass) when this pass will bind our pre-filled slice,
	// which authorizes redirecting the engine's redundant geometry-CB DISCARD map to a
	// throwaway so no DXVK per-pass allocation happens. Cleared after the engine call.
	bool g_redirectThisPass = false;
	bool g_geomMapRedirected = false;
	// The engine writes its (now-redundant) geometry constants here instead of into a
	// freshly DISCARD-renamed GPU buffer. Never read by the GPU -- our slice is bound.
	alignas(16) uint8_t g_geomRedirectScratch[1024] = {};

	// Capture the engine's geometry-CB write via its own Map/Unmap: grab the CPU
	// mapped pointer (+ size from RowPitch) at Map, copy the bytes at Unmap BEFORE they
	// go to the GPU. No staging, no GPU sync (unlike GetDesc/readback, which crashed).
	//
	// When g_redirectThisPass is set, the geometry CB's Map is short-circuited: instead
	// of the engine's expensive per-pass Map(WRITE_DISCARD) (a fresh DXVK constant-buffer
	// allocation -- the render thread's hottest cluster), we hand back a persistent
	// scratch pointer with NO real map. The engine's write into it is dead (our
	// pre-filled arena slice is bound over the engine's stale bind), and the matching
	// Unmap is swallowed so the map/unmap stays balanced. This is the whole point: the
	// per-pass write/discard the user asked to eliminate never reaches DXVK.
	struct Context_Map_Hook
	{
		static HRESULT __stdcall thunk(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub, D3D11_MAP mapType, UINT flags, D3D11_MAPPED_SUBRESOURCE* pMapped)
		{
			if (g_redirectThisPass && res == reinterpret_cast<ID3D11Resource*>(g_vsGeomCB) && pMapped) {
				pMapped->pData = g_geomRedirectScratch;
				pMapped->RowPitch = sizeof(g_geomRedirectScratch);
				pMapped->DepthPitch = sizeof(g_geomRedirectScratch);
				g_lastGeomMapPtr = g_geomRedirectScratch;  // DIAG still captures the engine's write
				g_geomMapRedirected = true;
				return S_OK;
			}
			const HRESULT hr = func(ctx, res, sub, mapType, flags, pMapped);
			if (g_inUtilitySetup && res == reinterpret_cast<ID3D11Resource*>(g_vsGeomCB) && SUCCEEDED(hr) && pMapped) {
				g_lastGeomMapPtr = pMapped->pData;
				if (pMapped->RowPitch)
					g_geomCBSize = pMapped->RowPitch;
			}
			return hr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct Context_Unmap_Hook
	{
		static void __stdcall thunk(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub)
		{
			if (g_geomMapRedirected && res == reinterpret_cast<ID3D11Resource*>(g_vsGeomCB)) {
				// Swallow the unmap of the buffer we never really mapped; capture first.
				if (g_diag && g_lastGeomMapPtr) {
					std::memcpy(g_engineScratch, g_lastGeomMapPtr, std::min<uint32_t>(g_geomCBSize ? g_geomCBSize : 256, sizeof(g_engineScratch)));
					g_engineCaptured = true;
				}
				g_lastGeomMapPtr = nullptr;
				g_geomMapRedirected = false;
				return;  // do NOT forward to DXVK -- the resource was never mapped
			}
			if (g_diag && g_inUtilitySetup && res == reinterpret_cast<ID3D11Resource*>(g_vsGeomCB) && g_lastGeomMapPtr) {
				std::memcpy(g_engineScratch, g_lastGeomMapPtr, std::min<uint32_t>(g_geomCBSize ? g_geomCBSize : 256, sizeof(g_engineScratch)));
				g_engineCaptured = true;
				g_lastGeomMapPtr = nullptr;
			}
			func(ctx, res, sub);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Engine helpers used by the parallel fill (both pure math -> thread-safe):
	//   FUN_1412c3440: build a 4x4 matrix from an NiTransform (world) into out[16].
	//   D3DXMatrixTranspose: transpose src[16] into dst[16] (matches SetupGeometry).
	REL::Relocation<void(float*, const void*)> BuildMatrixFromTransform{ REL::ID(99814) };
	REL::Relocation<void(float*, const float*)> MatrixTranspose{ REL::ID(102356) };

	// Toggle-mode: read the A/B mode integer from the control file (once per frame). Sets
	// g_runtimeMode + derived g_pipelineActive / g_skipEngineGeomFill. No-op unless
	// CS_PARALLEL_TOGGLE=1. Path is fixed to the dev scratchpad.
	void ReadRuntimeMode()
	{
		std::ifstream f("F:\\claudetmp\\rtprof\\psmode.txt");
		int mode = 1;
		if (f >> mode) {
			if (mode < 0 || mode > 2)
				mode = 1;
		} else {
			mode = 1;
		}
		g_runtimeMode = mode;
		g_pipelineActive = (mode >= 1);
		g_skipEngineGeomFill = (mode == 1);
	}

	// A BSUtility geometry-CB write is safe to replace with our world-matrix-only fill
	// iff, for its flag word F (= technique - 0x2B, at shader+0x90), SetupGeometry writes
	// ONLY the World matrix and skips every other constant (ShadowFadeParam / EyePos /
	// WaterParams / TreeParams) -- so the untouched floats are stale-and-unread. Gates
	// verified byte-exact against the engine (see F:\claudetmp\rtprof\bsutility_geomcb_map.txt):
	//   world written:  (F & 4) == 0
	//   shadow skipped:  (F & 0x1E00000) == 0
	//   eye+water skipped: (F & 0x20004000) == 0x4000
	//   tree skipped:  (F & 0x4000000) == 0
	inline bool IsWorldOnlyTechnique(uint32_t F)
	{
		return (F & 0x4u) == 0 && (F & 0x1E00000u) == 0 && (F & 0x20004000u) == 0x4000u && (F & 0x4000000u) == 0;
	}

	// BSShaderAccumulator::RenderGeometryGroup (SE 0x1412CCE40, id 99963). Renders
	// one geometry group's accumulated passes; hooked to collect BSUtility passes
	// before the serial draw walk. Return preserved (rax) via uint64_t.
	struct RenderGeometryGroup_Hook
	{
		static uint64_t thunk(RE::BSShaderAccumulator* a_acc, int32_t a2, int32_t a3, uint32_t a4, int32_t a5)
		{
			ParallelShaderSetup::GetSingleton()->OnRenderGeometryGroup(a_acc);
			return func(a_acc, a2, a3, a4, a5);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// BSUtilityShader::SetupGeometry (vtbl idx 6). Ground-truth on BSUtility pass
	// volume, and the point where the pre-filled slice will be bound (skipping the
	// engine Map/write). For now: count + pass through unchanged.
	struct BSUtilityShader_SetupGeometry_Hook
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
		{
			auto* self = ParallelShaderSetup::GetSingleton();
			self->OnUtilitySetupGeometry(a_pass);

			// A pass uses our slice iff binding is enabled and its slice was pre-filled
			// this frame. That single condition also authorizes redirecting the engine's
			// geometry-CB DISCARD map to scratch (only ever skip the engine's fill when we
			// are certain our correct slice will be bound over it).
			const bool useSlice = self->WillBindSlice(a_pass);
			g_inUtilitySetup = true;
			g_engineCaptured = false;
			g_redirectThisPass = useSlice && g_skipEngineGeomFill && g_vsGeomCB != nullptr;

			// Elide the engine's redundant world-matrix compute+write: SetupGeometry skips
			// it when (F & 4) != 0 (disasm 0x14130ed62), and that bit gates ONLY the world
			// path -- the VS constant buffer (alpha ref/fade) + pipeline state + binds still
			// run. Our pre-filled slice supplies the world. Kept OFF under DIAG, which needs
			// the engine's own write as the byte-compare ground truth.
			uint32_t* const flagWord = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(a_shader) + 0x90);
			const bool skipEngineWorld = useSlice && g_skipEngineGeomFill && !g_diag;
			const uint32_t savedFlag = skipEngineWorld ? *flagWord : 0u;
			if (skipEngineWorld)
				*flagWord = savedFlag | 0x4u;

			func(a_shader, a_pass, a_flags);  // engine: VS CB + state + binds (geom map redirected, world elided)

			if (skipEngineWorld)
				*flagWord = savedFlag;  // restore before the draw reads it
			g_inUtilitySetup = false;
			g_redirectThisPass = false;
			g_geomMapRedirected = false;  // defensive: never let a swallow-flag leak past this pass
			if (g_diag)
				self->CompareFill(a_pass);
			if (useSlice)
				self->BindUtilitySlice(a_pass);  // bind our pre-filled slice at VS slot 2
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void ParallelShaderSetup::ApplyEnvOverride()
{
	char buf[8] = {};
	if (GetEnvironmentVariableA("CS_PARALLEL_SETUP", buf, sizeof(buf)) && buf[0] == '1')
		enabled = true;
	char db[8] = {};
	if (GetEnvironmentVariableA("CS_PARALLEL_DIAG", db, sizeof(db)) && db[0] == '1')
		g_diag = true;
	char bb[8] = {};
	if (GetEnvironmentVariableA("CS_PARALLEL_BIND", bb, sizeof(bb)) && bb[0] == '1')
		g_bindEnabled = true;
	char sf[8] = {};
	if (GetEnvironmentVariableA("CS_PARALLEL_SKIP_FILL", sf, sizeof(sf)) && sf[0] == '1')
		g_skipEngineGeomFill = true;
	char tg[8] = {};
	if (GetEnvironmentVariableA("CS_PARALLEL_TOGGLE", tg, sizeof(tg)) && tg[0] == '1') {
		// Dev A/B mode: bind + skip are driven by the per-frame control file, so enable the
		// machinery (hooks, bind) unconditionally and let the mode decide behavior.
		g_toggleMode = true;
		g_bindEnabled = true;
		g_skipEngineGeomFill = true;  // provisional; ReadRuntimeMode refreshes it per frame
	}
}

void ParallelShaderSetup::Setup()
{
	if (initialized)
		return;

	ApplyEnvOverride();
	if (!enabled) {
		// Inert by default: no arena, no threads, no behavior change.
		return;
	}

	auto* device = globals::d3d::device;
	if (!device) {
		logger::warn("[ParallelShaderSetup] D3D device not ready; deferring init");
		return;
	}

	// One big dynamic constant buffer holding every pass's geometry slice. Mapped
	// once per frame; workers write disjoint slices into the shared CPU pointer.
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = kMaxPasses * kSliceBytes;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	winrt::com_ptr<ID3D11Buffer> buffer;
	const HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.put());
	if (FAILED(hr) || !buffer) {
		logger::warn("[ParallelShaderSetup] CreateBuffer(arena {} MB) failed (0x{:08X}); disabling",
			desc.ByteWidth / (1024 * 1024), static_cast<uint32_t>(hr));
		enabled = false;
		return;
	}
	arena = std::move(buffer);
	Util::SetResourceName(arena.get(), "ParallelShaderSetup::GeometryCBArena");

	// ID3D11DeviceContext1 for offset constant-buffer binding (device is FL 11.1).
	if (auto* ctx = globals::d3d::context)
		ctx->QueryInterface(IID_PPV_ARGS(context1.put()));
	if (!context1) {
		logger::warn("[ParallelShaderSetup] no ID3D11DeviceContext1; disabling");
		arena = nullptr;
		enabled = false;
		return;
	}

	// One worker per hardware thread minus the render + DXVK CS threads, capped.
	const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
	const size_t workerCount = std::clamp<size_t>(static_cast<size_t>(hw) - 2, 1, 8);
	pool = std::make_unique<BS::thread_pool<>>(workerCount);

	initialized = true;
	logger::info("[ParallelShaderSetup] initialized: {} MB geometry CB arena, {} fill worker(s) (P0; fill/bind not yet routed)",
		desc.ByteWidth / (1024 * 1024), workerCount);

	InstallHooks();
}

void ParallelShaderSetup::InstallHooks()
{
	if (!enabled)
		return;
	// SE-only for now: the collection hook uses the SE address-library id. AE ids
	// differ; the AE pair is added when the path is validated on SE.
	if (!REL::Module::IsSE()) {
		logger::info("[ParallelShaderSetup] collection hook is SE-only for now; not installing on this runtime");
		return;
	}
	// AE id is a placeholder; install is gated to SE above so it is never resolved.
	stl::detour_thunk<RenderGeometryGroup_Hook>(REL::RelocationID(99963, 99963));
	stl::write_vfunc<0x6, BSUtilityShader_SetupGeometry_Hook>(RE::VTABLE_BSUtilityShader[0]);

	// The context Map/Unmap hooks serve two roles: the DIAG byte-capture and the
	// map-redirect that skips the engine's per-pass DISCARD alloc. Install if either is on.
	if ((g_diag || g_skipEngineGeomFill || g_toggleMode) && globals::d3d::context) {
		stl::detour_vfunc<14, Context_Map_Hook>(globals::d3d::context);
		stl::detour_vfunc<15, Context_Unmap_Hook>(globals::d3d::context);
		logger::info("[ParallelShaderSetup] installed context Map/Unmap hooks (diag={} skipFill={} toggle={})", g_diag, g_skipEngineGeomFill, g_toggleMode);
	}
	logger::info("[ParallelShaderSetup] installed RenderGeometryGroup + BSUtilityShader::SetupGeometry hooks");
}

void ParallelShaderSetup::OnRenderGeometryGroup(RE::BSShaderAccumulator* a_accumulator)
{
	if (!initialized || !a_accumulator)
		return;

	// Fill each accumulator's BSUtility passes ONCE, on its first RenderGeometryGroup
	// call this frame: at that point accumulation is done but none of its passes have
	// drawn yet, so all slices can be filled before any bind. Subsequent calls for the
	// same accumulator skip. This is the parallel-fill dispatch point; for now the
	// per-pass work is a safe read-only op to validate threaded collection + dispatch
	// without touching rendering.
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;

	static uint32_t s_frame = 0xFFFFFFFFu;
	static std::vector<RE::BSShaderAccumulator*> s_seen;
	if (frame != s_frame) {
		s_frame = frame;
		s_seen.clear();
		arenaOffset.store(0, std::memory_order_relaxed);
		arenaFreshFrame = true;   // next fill DISCARDs (renames) the arena for the new frame
		passToSlice.clear();
		if (g_toggleMode && (frame % 20u) == 0u)
			ReadRuntimeMode();    // A/B: refresh mode ~3x/sec (off/ours/ours-no-redirect)
	}
	// Toggle-mode 0 = pipeline off: leave passToSlice empty so nothing binds and nothing
	// redirects (bit-identical to shipping). The frame bookkeeping above still runs.
	if (g_toggleMode && !g_pipelineActive)
		return;
	if (std::find(s_seen.begin(), s_seen.end(), a_accumulator) != s_seen.end())
		return;  // already handled this accumulator this frame
	s_seen.push_back(a_accumulator);

	auto* runtimeData = a_accumulator->GetRuntimeData();
	auto* batchRenderer = runtimeData ? runtimeData->batchRenderer : nullptr;
	auto* utilityShader = RE::BSUtilityShader::GetSingleton();
	if (!batchRenderer || !utilityShader)
		return;

	// Collect this accumulator's BSUtility passes. renderPass is BSTArray<PassGroup>
	// BY VALUE (0x30 stride), not BSTArray<PassGroup*> as CommonLibSSE-NG annotates.
	auto& arr = batchRenderer->renderPass;
	const uint32_t groupCount = arr.size();
	if (groupCount > 4096)
		return;  // sanity guard against a misread array
	auto* groups = reinterpret_cast<RE::BSBatchRenderer::PassGroup*>(arr.data());
	if (!groups)
		return;

	collectBuffer.clear();
	for (uint32_t g = 0; g < groupCount; ++g) {
		RE::BSBatchRenderer::PassGroup& group = groups[g];
		for (int i = 0; i < 5; ++i) {
			if (!(group.validPassBits & (1u << i)))
				continue;
			for (RE::BSRenderPass* pass = group.passes[i]; pass; pass = pass->passGroupNext) {
				if (pass->shader != utilityShader || !pass->geometry)
					continue;
				// Route ONLY techniques that write just the World matrix (flag word derived
				// inline as passEnum - 0x2B). Every other technique writes constants we do
				// not reproduce, so it falls back to the engine (correct). Particle geoms
				// use a re-centered transform, so they are excluded.
				if (!IsWorldOnlyTechnique(FlagsForPass(pass)))
					continue;
				if (pass->geometry->GetType().get() == RE::BSGeometry::Type::kParticles)
					continue;
				collectBuffer.push_back(pass);
			}
		}
	}
	if (collectBuffer.empty())
		return;

	// SERIAL FILL. The A/B proved the thread pool was pure overhead here: passes are
	// collected immediately before they draw, so there is no overlap window -- dispatch +
	// pool->wait() only added a render-thread stall. The world-matrix build is a handful
	// of SIMD ops; doing it serially (once) is cheaper than orchestrating threads, and the
	// engine's own compute is elided via the 0x4 "skip world" flag bit set around func().
	// Map the arena ONCE per frame (DISCARD on the first fill, NO_OVERWRITE after so slices
	// already queued for their draws are untouched) instead of the engine's per-pass DISCARD.
	auto* context = globals::d3d::context;
	if (!context || !arena)
		return;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	const D3D11_MAP mapType = arenaFreshFrame ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
	if (FAILED(context->Map(arena.get(), 0, mapType, 0, &mapped)) || !mapped.pData)
		return;
	arenaFreshFrame = false;
	arenaMapped = static_cast<uint8_t*>(mapped.pData);

	const size_t n = collectBuffer.size();
	auto* passes = collectBuffer.data();
	const uint32_t worldByteOff = 4u * g_worldFloatOffset;
	uint32_t filled = 0;
	for (size_t idx = 0; idx < n; ++idx) {
		RE::BSRenderPass* pass = passes[idx];
		if (!pass || !pass->geometry)
			continue;
		uint8_t* slice = nullptr;
		const uint32_t off = AllocateSlice(slice);
		if (off == UINT32_MAX || !slice)
			break;  // arena full: remaining passes fall back to the engine (no slice -> no bind)
		// Write only the world matrix (transposed) at the geometry CB's world offset. For
		// world-only techniques the other floats are unread (WRITE_DISCARD stale-and-unread,
		// workflow-verified), so no per-slice memset is needed.
		alignas(16) float m[16];
		BuildMatrixFromTransform(m, &pass->geometry->world);
		MatrixTranspose(reinterpret_cast<float*>(slice + worldByteOff), m);
		passToSlice[pass] = off;
		++filled;
	}

	context->Unmap(arena.get(), 0);
	arenaMapped = nullptr;

	if ((frame % 300u) == 0u)
		logger::info("[ParallelShaderSetup] accumulator BSUtility={} serial-filled={}", collectBuffer.size(), filled);
}

void ParallelShaderSetup::OnUtilitySetupGeometry(RE::BSRenderPass* a_pass)
{
	if (!initialized || !a_pass)
		return;

	// ConstantBuffers_1430281F8 is a POINTER global (disasm: `mov rdx, cs:ConstantBuffers`)
	// to the current shader's CB struct: base[+0x38]=geom CB resource, [+0x40]=mapped ptr,
	// byte[80]=world-matrix float offset. Cache the world offset (constant per shader) so
	// the fill needs no per-pass harvest, and resolve the geom CB once for the redirect.
	static REL::Relocation<uint8_t*> constantBuffersAddr{ REL::Offset(0x30281F8) };
	const uint8_t* base = *reinterpret_cast<uint8_t* const*>(constantBuffersAddr.get());
	if (!base)
		return;
	g_worldFloatOffset = base[80];

	// Lazily resolve the shared VS geometry CB resource (base[0x38]) once in-world; used
	// by both the DIAG snoop and the map-redirect. Settled after boot to avoid a
	// transient early-frame resource.
	if ((g_diag || g_skipEngineGeomFill || g_toggleMode) && !g_vsGeomCB && globals::state && globals::state->frameCount > 600) {
		g_vsGeomCB = *reinterpret_cast<ID3D11Buffer* const*>(base + 0x38);
		g_geomCBSize = 512;
		logger::info("[ParallelShaderSetup] resolved geomCB={} base={} base[80]={} mapped={}",
			reinterpret_cast<void*>(g_vsGeomCB), reinterpret_cast<const void*>(base), base[80],
			reinterpret_cast<void*>(*reinterpret_cast<const uintptr_t*>(base + 0x40)));
	}
}

void ParallelShaderSetup::CompareFill(RE::BSRenderPass* a_pass)
{
	if (!g_engineCaptured || !a_pass || !a_pass->geometry)
		return;

	static REL::Relocation<uint8_t*> cbAddr{ REL::Offset(0x30281F8) };
	const uint8_t* base = *reinterpret_cast<uint8_t* const*>(cbAddr.get());

	// Broad verification (every pass): for a world-only technique, confirm our world
	// matrix byte-matches the engine's write at base[80]. Tally match/mismatch so we
	// know the routing is universally correct, not just on rate-limited samples.
	static std::atomic<uint64_t> s_woMatch{ 0 }, s_woMiss{ 0 }, s_woParticleMiss{ 0 };
	if (base && IsWorldOnlyTechnique(FlagsForPass(a_pass))) {
		alignas(16) float m[16] = {}, t[16] = {};
		BuildMatrixFromTransform(m, &a_pass->geometry->world);
		MatrixTranspose(t, m);
		const uint32_t woff = base[80];
		const bool inRange = (4u * woff + 64u) <= std::min<uint32_t>(g_geomCBSize, sizeof(g_engineScratch));
		const bool match = inRange && std::memcmp(g_engineScratch + 4u * woff, t, 64) == 0;
		if (match)
			s_woMatch.fetch_add(1, std::memory_order_relaxed);
		else if (a_pass->geometry->GetType().get() == RE::BSGeometry::Type::kParticles)
			s_woParticleMiss.fetch_add(1, std::memory_order_relaxed);
		else
			s_woMiss.fetch_add(1, std::memory_order_relaxed);
	}

	// Rate-limit the (verbose) dump to one sample per ~200 frames.
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	static uint32_t s_logFrame = 0xFFFFFFFFu;
	if (frame == s_logFrame || (frame % 200u) != 0u)
		return;
	s_logFrame = frame;
	logger::info("[ParallelShaderSetup][DIAG] worldOnly tally: match={} MISMATCH={} particleMiss={}",
		s_woMatch.load(), s_woMiss.load(), s_woParticleMiss.load());

	const uint32_t cap = std::min<uint32_t>(g_geomCBSize ? g_geomCBSize : 256u, sizeof(g_engineScratch));

	// Helper: build+transpose a NiTransform and scan the engine's captured bytes for
	// the first 12 floats (a 3x4 world-style matrix) on 16-byte boundaries.
	auto findMatrix = [&](const RE::NiTransform& xf) -> int {
		alignas(16) float m[16] = {}, t[16] = {};
		BuildMatrixFromTransform(m, &xf);
		MatrixTranspose(t, m);
		for (uint32_t off = 0; off + 48 <= cap; off += 16)
			if (std::memcmp(g_engineScratch + off, t, 48) == 0)
				return static_cast<int>(off / 4);
		return -1;
	};

	const int worldOff = findMatrix(a_pass->geometry->world);
	const int prevWorldOff = findMatrix(a_pass->geometry->previousWorld);

	// The per-shader constant-table offsets the engine uses (float units). (base is
	// resolved at the top of this function.)
	const int o80 = base ? base[80] : -1, o82 = base ? base[82] : -1, o85 = base ? base[85] : -1,
			  o86 = base ? base[86] : -1, o87 = base ? base[87] : -1;

	// Dump the engine's write as floats so the full layout is visible.
	const float* ef = reinterpret_cast<const float*>(g_engineScratch);
	const uint32_t nf = std::min<uint32_t>(cap / 4, 32);
	std::string dump;
	for (uint32_t i = 0; i < nf; ++i)
		dump += std::format("{}{:.3g}", i ? " " : "", ef[i]);

	logger::info("[ParallelShaderSetup][DIAG] pe=0x{:X} accHint={} cbSize={} off[80/82/85/86/87]={}/{}/{}/{}/{} worldAt={} prevWorldAt={} | eng=[{}]",
		a_pass->passEnum, a_pass->accumulationHint, g_geomCBSize, o80, o82, o85, o86, o87, worldOff, prevWorldOff, dump);
}

bool ParallelShaderSetup::WillBindSlice(RE::BSRenderPass* a_pass) const
{
	// A pass is served from our arena iff binding is enabled (CS_PARALLEL_BIND=1) and its
	// slice was pre-filled this frame (collected as world-only in OnRenderGeometryGroup).
	return g_bindEnabled && initialized && context1 && passToSlice.find(a_pass) != passToSlice.end();
}

void ParallelShaderSetup::BindUtilitySlice(RE::BSRenderPass* a_pass)
{
	// Gated (CS_PARALLEL_BIND=1): the world-matrix-only fill is correct for the world-only
	// techniques (verified byte-exact); every other technique falls back to the engine.
	if (!WillBindSlice(a_pass))
		return;
	auto it = passToSlice.find(a_pass);

	// Rebind VS constant slot 2 (the geometry CB) to this pass's pre-filled arena
	// slice. VSSetConstantBuffers1 offsets are in 16-byte (float4) units and must be
	// multiples of 16; slices are 256-byte aligned so both hold.
	ID3D11Buffer* buffer = arena.get();
	const UINT firstConstant = it->second / 16u;        // byte offset -> float4 units
	const UINT numConstants = kSliceBytes / 16u;         // 64 (multiple of 16)
	context1->VSSetConstantBuffers1(2, 1, &buffer, &firstConstant, &numConstants);
}

uint32_t ParallelShaderSetup::AllocateSlice(uint8_t*& o_cpuPtr)
{
	o_cpuPtr = nullptr;
	if (!arenaMapped)
		return UINT32_MAX;

	// 256-byte-aligned bump allocation (D3D11.1 VSSetConstantBuffers1 offset rule).
	const uint32_t offset = arenaOffset.fetch_add(kSliceBytes, std::memory_order_relaxed);
	static_assert(kSliceBytes % kSliceAlignment == 0, "slice size must keep offsets 256-byte aligned");
	if (offset + kSliceBytes > kMaxPasses * kSliceBytes)
		return UINT32_MAX;

	o_cpuPtr = arenaMapped + offset;
	return offset;
}

void ParallelShaderSetup::Shutdown()
{
	pool.reset();
	arena = nullptr;
	arenaMapped = nullptr;
	initialized = false;
}
