#include "OcclusionCulling.h"

#include "MOC.h"

#include "Globals.h"

#include <RE/B/BSCullingProcess.h>
#include <RE/B/BSMultiBound.h>
#include <RE/B/BSParabolicCullingProcess.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiCamera.h>
#include <RE/S/State.h>

#include <atomic>

#include <imgui.h>

namespace
{
	// The culling-process instance currently running a MAIN-camera cull (nullptr when
	// none). Per-object Process1 calls are occlusion-tested only when their process
	// matches. Instance-keyed rather than a bool because culls run CONCURRENTLY (the
	// main scene cull executes inside a BuildSceneLists job while water/shadow culls
	// run on other threads, each on a different process object) -- a shared flag would
	// leak main-pass testing into unrelated passes.
	std::atomic<RE::NiCullingProcess*> g_activeCullProcess{ nullptr };

	// BSCullingProcess::Process1 (NiCullingProcess vtable index 0x16): per-object
	// processing / recursion driver. If the object is provably occluded during the
	// main cull pass, skip the original call entirely so neither the object nor its
	// subtree is accumulated.
	struct Process1_Hook
	{
		static void thunk(RE::NiCullingProcess* a_self, RE::NiAVObject* a_object, std::int32_t a_arg2)
		{
			if (a_self == g_activeCullProcess.load(std::memory_order_relaxed) && a_object && !MOC::TestObject(a_object))
				return;  // occluded -> do not accumulate / recurse
			func(a_self, a_object, a_arg2);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Same hook for BSParabolicCullingProcess, which OVERRIDES Process1/Process2 with its
	// own bodies. The main-scene subtree culls run on the global parabolic process --
	// hooking only BSCullingProcess never sees them. Separate hook structs keep each
	// body's original function pointer.
	struct PProcess1_Hook
	{
		static void thunk(RE::NiCullingProcess* a_self, RE::NiAVObject* a_object, std::int32_t a_arg2)
		{
			if (a_self == g_activeCullProcess.load(std::memory_order_relaxed) && a_object && !MOC::TestObject(a_object))
				return;  // occluded -> do not accumulate / recurse
			func(a_self, a_object, a_arg2);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Shared Process2 (top-level cull entry, vtable index 0x17) logic: decide whether the
	// nested Process1 calls of THIS cull should be occlusion-tested. BuildOccluders gates
	// on pointer identity with the engine's main world-render camera and returns true only
	// for main-scene culls with a freshly built (or reused same-frame) buffer. Synchronous
	// in V1: the buffer is ready before the nested Process1 calls.
	bool Process2_Begin(const RE::NiCamera* a_camera, RE::NiAVObject* a_scene)
	{
		auto* feature = OcclusionCulling::GetSingleton();

		bool active = false;
		if (feature->IsActive() && MOC::IsInitialized() && a_camera) {
			if (MOC::BuildOccluders(const_cast<RE::NiCamera*>(a_camera)))
				active = MOC::EnableOcclusionTesting;
		}
		(void)a_scene;
		return active;
	}

	// Mark a_self as the active main-cull process for the duration of the original call
	// (only when this cull is the main camera's). Non-main culls don't touch the marker,
	// so concurrent auxiliary passes can neither enable themselves nor disable a running
	// main cull. Restores the previous marker (not nullptr) because both Process2 bodies
	// are detoured and the parabolic override may chain into the base implementation --
	// a nested bracket must not strip the outer one.
	template <class HookT>
	void Process2_Bracketed(RE::NiCullingProcess* a_self, const RE::NiCamera* a_camera, RE::NiAVObject* a_scene, RE::NiVisibleArray* a_visibleSet)
	{
		const bool active = Process2_Begin(a_camera, a_scene);
		RE::NiCullingProcess* const prev = g_activeCullProcess.load(std::memory_order_relaxed);
		if (active)
			g_activeCullProcess.store(a_self, std::memory_order_relaxed);
		HookT::func(a_self, a_camera, a_scene, a_visibleSet);
		if (active)
			g_activeCullProcess.store(prev, std::memory_order_relaxed);
	}

	struct Process2_Hook
	{
		static void thunk(RE::NiCullingProcess* a_self, const RE::NiCamera* a_camera, RE::NiAVObject* a_scene, RE::NiVisibleArray* a_visibleSet)
		{
			Process2_Bracketed<Process2_Hook>(a_self, a_camera, a_scene, a_visibleSet);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct PProcess2_Hook
	{
		static void thunk(RE::NiCullingProcess* a_self, const RE::NiCamera* a_camera, RE::NiAVObject* a_scene, RE::NiVisibleArray* a_visibleSet)
		{
			Process2_Bracketed<PProcess2_Hook>(a_self, a_camera, a_scene, a_visibleSet);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// TestBaseVisibility1(BSMultiBound&) -- the engine's CONTAINER visibility path (rooms,
	// cells, building shells). Multibound nodes never reach Process1, so this is where the
	// high-value tight-AABB occlusion tests belong: one occluded container prunes all its
	// contents. Engine verdict first; we only downgrade visible -> occluded.
	struct TestBaseVis1_Hook
	{
		static bool thunk(RE::BSCullingProcess* a_self, RE::BSMultiBound* a_bound)
		{
			const bool visible = func(a_self, a_bound);
			if (visible && a_bound &&
				static_cast<RE::NiCullingProcess*>(a_self) == g_activeCullProcess.load(std::memory_order_relaxed) &&
				!MOC::TestMultiBound(a_bound))
				return false;
			return visible;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct PTestBaseVis1_Hook
	{
		static bool thunk(RE::BSCullingProcess* a_self, RE::BSMultiBound* a_bound)
		{
			const bool visible = func(a_self, a_bound);
			if (visible && a_bound &&
				static_cast<RE::NiCullingProcess*>(a_self) == g_activeCullProcess.load(std::memory_order_relaxed) &&
				!MOC::TestMultiBound(a_bound))
				return false;
			return visible;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void OcclusionCulling::PostPostLoad()
{
	// Idempotent: this runs from XSEPlugin's direct call AND (once we set loaded=true)
	// from Feature::ForEachLoadedFeature, so guard against double vtable-hook install.
	static bool s_installed = false;
	if (s_installed)
		return;

	// CS_OCCLUSION=1 just flips the master toggle ON at boot; the menu checkbox
	// (settings.EnableOcclusionTesting) is the real gate and works without the env var.
	char buf[16] = {};
	if (GetEnvironmentVariableA("CS_OCCLUSION", buf, sizeof(buf)) && buf[0] == '1') {
		envEnabled = true;
		settings.EnableOcclusionTesting = true;
	}
	// CS_MOC_MAX_OCCLUDERS=<n>: boot-time raster-budget override for automated A/B runs.
	if (GetEnvironmentVariableA("CS_MOC_MAX_OCCLUDERS", buf, sizeof(buf)) && buf[0]) {
		const int v = atoi(buf);
		if (v > 0)
			settings.MaxOccludersPerFrame = v;
	}
	// CS_MOC_NO_SIMPLIFY=1: disable occluder mesh simplification (A/B of cull-rate impact).
	if (GetEnvironmentVariableA("CS_MOC_NO_SIMPLIFY", buf, sizeof(buf)) && buf[0] == '1')
		settings.SimplifyOccluders = false;
	// CS_MOC_MIN_TEST_RADIUS=<n>: per-object test gate override for A/B runs.
	if (GetEnvironmentVariableA("CS_MOC_MIN_TEST_RADIUS", buf, sizeof(buf)) && buf[0]) {
		const int v = atoi(buf);
		if (v >= 0)
			settings.OccluderTestMinRadius = static_cast<float>(v);
	}

	// SE 1.5.97 only: the address-library id and struct offsets used by the port are SE.
	if (!REL::Module::IsSE()) {
		logger::info("[OcclusionCulling] SE-only for now; not installing on this runtime");
		return;
	}

	// Sync BEFORE Init so boot-time settings (env overrides, defaults) reach the pool
	// creation (RasterThreads is consumed inside MOC::Init).
	SyncSettingsToMOC();
	MOC::Init();

	// Detour the Process1/Process2 FUNCTION BODIES of both culling-process classes
	// (base BSCullingProcess and the BSParabolicCullingProcess overrides the main world
	// cull runs on). Body detours are essential: the engine's main cull walk calls
	// Process1 DIRECTLY (devirtualized), so vtable patches never see it -- with vtable
	// hooks the buffer built correctly but tested stayed 0. This mirrors Nukem, who
	// detoured the Process function body (1.5.23 0xD50310). Virtual dispatch lands in
	// the same bodies, so these four detours cover every call path. Installed
	// unconditionally; runtime behavior is gated by IsActive() inside the thunks.
	const auto p1b = REL::ID(74804).address();
	const auto p2b = REL::ID(74805).address();
	const auto p1p = REL::ID(101597).address();
	const auto p2p = REL::ID(101598).address();
	stl::detour_thunk<Process1_Hook>(REL::RelocationID(74804, 74804));     // BSCullingProcess::Process1
	stl::detour_thunk<Process2_Hook>(REL::RelocationID(74805, 74805));     // BSCullingProcess::Process2
	stl::detour_thunk<PProcess1_Hook>(REL::RelocationID(101597, 101597));  // BSParabolicCullingProcess::Process1
	stl::detour_thunk<PProcess2_Hook>(REL::RelocationID(101598, 101598));  // BSParabolicCullingProcess::Process2
	stl::detour_thunk<TestBaseVis1_Hook>(REL::RelocationID(74816, 74816));      // BSCullingProcess::TestBaseVisibility1
	stl::detour_thunk<PTestBaseVis1_Hook>(REL::RelocationID(101605, 101605));   // BSParabolicCullingProcess::TestBaseVisibility1
	// On success DetourAttach rewrites T::func to the trampoline (!= original address);
	// equal means the attach silently failed.
	logger::info("[OcclusionCulling] detours attached: P1base={} P2base={} P1para={} P2para={}",
		Process1_Hook::func.address() != p1b, Process2_Hook::func.address() != p2b,
		PProcess1_Hook::func.address() != p1p, PProcess2_Hook::func.address() != p2p);

	s_installed = true;
	// Mark loaded (+ a nominal version) so the feature appears as a normal entry in the
	// CS menu; it has no shader .ini so Feature::Load leaves it unloaded otherwise. The
	// disk-cache overrides above keep this safe.
	version = "1-0-0";
	loaded = true;

	logger::info("[OcclusionCulling] hooks installed (master={})", settings.EnableOcclusionTesting ? "on" : "off");
}

bool OcclusionCulling::IsActive() const
{
	// Menu-driven master gate (also flipped on at boot by CS_OCCLUSION=1).
	return settings.EnableOcclusionTesting;
}

void OcclusionCulling::SyncSettingsToMOC()
{
	MOC::EnableOcclusionTesting = settings.EnableOcclusionTesting;
	MOC::EnableOccluderRendering = settings.EnableOccluderRendering;
	MOC::OccluderMaxDistance = settings.OccluderMaxDistance;
	MOC::OccluderFirstLevelMinSize = settings.OccluderFirstLevelMinSize;
	MOC::MaxOccludersPerFrame = static_cast<std::uint32_t>(std::max(settings.MaxOccludersPerFrame, 1));
	MOC::RasterThreads = settings.RasterThreads;      // applied at boot (pool created once)
	MOC::SimplifyOccluders = settings.SimplifyOccluders;  // affects newly cached meshes
	MOC::OccluderTestMinRadius = settings.OccluderTestMinRadius;
}

void OcclusionCulling::Prepass()
{
	MOC::DumpDebugImages();  // env-gated (CS_MOC_DUMP=1) + rate-limited; no-op otherwise
}

void OcclusionCulling::DrawSettings()
{
	if (!REL::Module::IsSE()) {
		ImGui::TextWrapped("%s", T("feature.occlusion_culling.se_only", "Occlusion Culling currently supports Skyrim SE 1.5.97 only."));
		return;
	}

	ImGui::TextWrapped("%s", T("feature.occlusion_culling.desc",
		"Software (CPU) occlusion culling: skips drawing scene objects fully hidden behind large static meshes. Experimental."));
	ImGui::Separator();

	bool changed = false;

	// Master on/off — this is the "toggle culling entirely" switch.
	changed |= ImGui::Checkbox(T("feature.occlusion_culling.enable_testing", "Enable Occlusion Culling"), &settings.EnableOcclusionTesting);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.enable_testing_tooltip",
			"Master toggle. When on, large static occluders are rasterized each frame and objects hidden behind them are skipped."));

	ImGui::BeginDisabled(!settings.EnableOcclusionTesting);

	changed |= ImGui::Checkbox(T("feature.occlusion_culling.enable_rendering", "Render Occluders"), &settings.EnableOccluderRendering);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.enable_rendering_tooltip",
			"Rasterize large static meshes into the CPU occlusion buffer each frame. Turn OFF to A/B the cost: the buffer stays empty so nothing is culled, but the traversal still runs."));

	changed |= ImGui::SliderFloat(T("feature.occlusion_culling.max_distance", "Occluder Max Distance"), &settings.OccluderMaxDistance, 1000.0f, 100000.0f, "%.0f");
	changed |= ImGui::SliderFloat(T("feature.occlusion_culling.first_level_min_size", "Occluder Min Size"), &settings.OccluderFirstLevelMinSize, 0.0f, 2000.0f, "%.0f");

	changed |= ImGui::SliderInt(T("feature.occlusion_culling.max_occluders", "Max Occluders / Frame"), &settings.MaxOccludersPerFrame, 16, 4096, "%d", ImGuiSliderFlags_Logarithmic);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.max_occluders_tooltip",
			"CPU raster budget per frame, selected by estimated screen coverage (large occluders like terrain always make the cut). Typical scenes offer 700-1600 candidates, so high values mean 'rasterize everything'."));

	changed |= ImGui::SliderInt(T("feature.occlusion_culling.raster_threads", "Raster Threads"), &settings.RasterThreads, 1, 8);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.raster_threads_tooltip",
			"Worker threads for occluder rasterization (Intel CullingThreadpool). Applied at next game start."));

	changed |= ImGui::SliderFloat(T("feature.occlusion_culling.min_test_radius", "Min Tested Object Size"), &settings.OccluderTestMinRadius, 0.0f, 200.0f, "%.0f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.min_test_radius_tooltip",
			"Objects smaller than this (world-bound radius) are never occlusion-tested. Lower = more draw calls saved but more CPU per frame."));

	changed |= ImGui::Checkbox(T("feature.occlusion_culling.simplify", "Simplify Occluder Meshes"), &settings.SimplifyOccluders);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", T("feature.occlusion_culling.simplify_tooltip",
			"Reduce occluder meshes to ~half the triangles at cache time (meshoptimizer). Big raster speedup; affects newly loaded meshes."));

	ImGui::EndDisabled();

	if (changed)
		SyncSettingsToMOC();

	// Debug: the software occlusion depth buffer (occluder silhouettes, near = bright).
	if (ImGui::CollapsingHeader(T("feature.occlusion_culling.debug_buffer", "Occlusion Depth Buffer"))) {
		MOC::UpdateDebugView();
		if (void* srv = MOC::GetDebugSRV()) {
			const float w = ImGui::GetContentRegionAvail().x;
			ImGui::Image(srv, ImVec2(w, w * (9.0f / 16.0f)));  // buffer is 16:9 (512x288)
			ImGui::TextWrapped("%s", T("feature.occlusion_culling.debug_buffer_hint",
				"Grayscale = rasterized static occluders (brighter = closer). Empty until CS_OCCLUSION=1 and occluders are rendered."));
		} else {
			ImGui::TextWrapped("%s", T("feature.occlusion_culling.debug_buffer_unavailable", "Depth buffer texture not available."));
		}
	}
}

void OcclusionCulling::LoadSettings(json& o_json)
{
	if (o_json["EnableOcclusionTesting"].is_boolean())
		settings.EnableOcclusionTesting = o_json["EnableOcclusionTesting"];
	if (o_json["EnableOccluderRendering"].is_boolean())
		settings.EnableOccluderRendering = o_json["EnableOccluderRendering"];
	if (o_json["OccluderMaxDistance"].is_number())
		settings.OccluderMaxDistance = o_json["OccluderMaxDistance"];
	if (o_json["OccluderFirstLevelMinSize"].is_number())
		settings.OccluderFirstLevelMinSize = o_json["OccluderFirstLevelMinSize"];
	if (o_json["MaxOccludersPerFrame"].is_number_integer())
		settings.MaxOccludersPerFrame = o_json["MaxOccludersPerFrame"];
	if (o_json["RasterThreads"].is_number_integer())
		settings.RasterThreads = o_json["RasterThreads"];
	if (o_json["SimplifyOccluders"].is_boolean())
		settings.SimplifyOccluders = o_json["SimplifyOccluders"];
	if (o_json["OccluderTestMinRadius"].is_number())
		settings.OccluderTestMinRadius = o_json["OccluderTestMinRadius"];

	SyncSettingsToMOC();
}

void OcclusionCulling::SaveSettings(json& o_json)
{
	o_json["EnableOcclusionTesting"] = settings.EnableOcclusionTesting;
	o_json["EnableOccluderRendering"] = settings.EnableOccluderRendering;
	o_json["OccluderMaxDistance"] = settings.OccluderMaxDistance;
	o_json["OccluderFirstLevelMinSize"] = settings.OccluderFirstLevelMinSize;
	o_json["MaxOccludersPerFrame"] = settings.MaxOccludersPerFrame;
	o_json["RasterThreads"] = settings.RasterThreads;
	o_json["SimplifyOccluders"] = settings.SimplifyOccluders;
	o_json["OccluderTestMinRadius"] = settings.OccluderTestMinRadius;
}

void OcclusionCulling::RestoreDefaultSettings()
{
	settings = {};
	SyncSettingsToMOC();
}
