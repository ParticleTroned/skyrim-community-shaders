#include "Streamline.h"

#include "DxvkInterop.h"

#include "../../DxvkLoader.h"
#include "../../Globals.h"
#include "../../State.h"
#include "../../Utils/Game.h"

#include <filesystem>

// Streamline SDK headers (header-only in the repo; the plugin DLLs ship separately
// into the CS folder for NVIDIA users). NV_WINDOWS selects the Win32 surface.
#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471 5103)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_device_wrappers.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_xess.h>
#include <sl_helpers_vk.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

namespace
{
	// All Streamline state lives here so Streamline.h need not expose sl.h.
	struct SLState
	{
		HMODULE interposer = nullptr;

		// Core interposer entry points (resolved via GetProcAddress).
		PFun_slInit* slInit = nullptr;
		PFun_slShutdown* slShutdown = nullptr;
		PFun_slIsFeatureSupported* slIsFeatureSupported = nullptr;
		PFun_slGetFeatureRequirements* slGetFeatureRequirements = nullptr;
		PFun_slGetNewFrameToken* slGetNewFrameToken = nullptr;
		PFun_slSetTagForFrame* slSetTagForFrame = nullptr;
		PFun_slSetConstants* slSetConstants = nullptr;
		PFun_slEvaluateFeature* slEvaluateFeature = nullptr;
		PFun_slGetFeatureFunction* slGetFeatureFunction = nullptr;
		PFun_slAllocateResources* slAllocateResources = nullptr;
		PFun_slFreeResources* slFreeResources = nullptr;
		PFun_slSetFeatureLoaded* slSetFeatureLoaded = nullptr;  // runtime DLSS-G (un)load, bracketed by swapchain recreate

		// Feature-specific entry points (resolved via slGetFeatureFunction).
		PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings = nullptr;
		PFun_slDLSSSetOptions* slDLSSSetOptions = nullptr;
		PFun_slReflexSetOptions* slReflexSetOptions = nullptr;
		PFun_slReflexSleep* slReflexSleep = nullptr;
		PFun_slPCLSetMarker* slPCLSetMarker = nullptr;
		PFun_slDLSSGSetOptions* slDLSSGSetOptions = nullptr;
		PFun_slDLSSGGetState* slDLSSGGetState = nullptr;
		PFun_slFSRSetOptions* slFSRSetOptions = nullptr;
		PFun_slFSRFrameGenerationSetOptions* slFSRFrameGenerationSetOptions = nullptr;
		PFun_slFSRGetFrameGenState* slFSRGetFrameGenState = nullptr;
		PFun_slXeSSSetOptions* slXeSSSetOptions = nullptr;

		sl::ViewportHandle viewport{ 0 };

		// ONE frame token per game frame, shared by every per-frame SL call (PCL markers, common
		// constants, DLSS-G resource tags, Reflex sleep) and matched to the present. DLSS-G requires
		// the constants/tags/markers and the present to all carry the SAME frame index, otherwise SL
		// logs "common constants cannot be found for frame N" and silently drops the generated frame.
		// Established at the SimulationStart marker (frame start) and reused for the rest of the frame.
		sl::FrameToken* frameToken = nullptr;
		uint32_t frameIndex = 0;

		// Latches off after a dispatch fault so a single SEH fault (e.g. a driver
		// mismatch on an untested NVIDIA path) can't crash the game every frame.
		bool dispatchFaulted = false;

		// Cached Reflex options to avoid redundant slReflexSetOptions calls.
		bool reflexCacheValid = false;
		sl::ReflexMode reflexCachedMode = sl::ReflexMode::eOff;

		// Cached DLSS-G interpolation mode. SetDLSSGMode is called every frame with the gameplay-state
		// gate (on in-world, off while loading/paused/in-menu per the DLSS-G guide §13), so cache the
		// last mode and only issue slDLSSGSetOptions on a real change.
		bool dlssgModeCached = false;
		bool dlssgModeOn = false;

		// Whether a VALID DLSS-G input tag was set this frame (in the render pass). Reset at the frame
		// start (SimulationStart); set by TagDLSSGResources. If still false at present, the present path
		// sets a null/passthrough tag — SL's present hook requires a tag every present or it stalls.
		bool dlssgTaggedThisFrame = false;

		// Cached VkImageViews for the DLSS-G tagged resources (0=depth, 1=motion, 2=hudless). SL's
		// Vulkan backend requires a view per tagged resource. These must PERSIST after the non-blocking
		// submit because DLSS-G consumes them at present time, so they are recreated only when the
		// underlying VkImage changes and freed at Shutdown.
		struct {
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
		} dlssgViewCache[3];

		struct {
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
		} fsrFgHudlessCache;
	} g_sl;

	// DLSS-G runtime load state (Streamline DLSS-G guide §18). DLSS-G is loaded at slInit, so current=true.
	// CS sets `desired` on a real select/deselect and requests a DXVK swapchain recreate; DXVK's torn-down
	// callback below applies slSetFeatureLoaded while no swapchain exists, so the next vkCreateSwapchainKHR
	// installs/omits DLSS-G's present proxy + extra queue. Unloading when off removes the queue + off-screen
	// copy overhead the guide warns about.
	std::atomic<bool> g_dlssgDesiredLoaded{ true };
	bool g_dlssgCurrentlyLoaded = true;

	// Invoked by DXVK inside recreateSwapChain() between destroy and create (registered via
	// dxvkSetSwapchainTornDownCallback). Toggles DLSS-G's loaded state to match `desired` in exactly the
	// window the guide requires. Runs on DXVK's present/acquire thread under its surface lock.
	void DxvkSwapchainTornDownCallback()
	{
		const bool desired = g_dlssgDesiredLoaded.load(std::memory_order_acquire);
		if (desired == g_dlssgCurrentlyLoaded || !g_sl.slSetFeatureLoaded || g_sl.dispatchFaulted)
			return;
		__try {
			if (g_sl.slSetFeatureLoaded(sl::kFeatureDLSS_G, desired) != sl::Result::eOk)
				return;
			g_dlssgCurrentlyLoaded = desired;
			// A reloaded plugin may sit at a new base — re-resolve its entry points; reset the mode cache so
			// the next SetDLSSGMode re-issues slDLSSGSetOptions (plugin option state is gone after an unload).
			g_sl.dlssgModeCached = false;
			g_sl.dlssgModeOn = false;
			if (desired) {
				g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
				g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_sl.dispatchFaulted = true;
		}
	}

	// Routes Streamline's own logging into the CS logger.
	void LogCallback(sl::LogType a_type, const char* a_msg)
	{
		switch (a_type) {
		case sl::LogType::eError:
			logger::warn("[Streamline/SL] {}", a_msg);
			break;
		case sl::LogType::eWarn:
			logger::debug("[Streamline/SL] {}", a_msg);
			break;
		default:
			logger::trace("[Streamline/SL] {}", a_msg);
			break;
		}
	}

	// .../SKSE/Plugins/CommunityShaders/streamline — sibling of the DXVK dir,
	// resolved module-relative (MO2 VFS safe) just like DxvkLoader::GetDxvkDir().
	std::filesystem::path GetStreamlineDir()
	{
		const auto dxvkDir = DxvkLoader::GetDxvkDir();
		if (dxvkDir.empty())
			return {};
		return dxvkDir.parent_path() / L"streamline";
	}

	template <typename T>
	bool Resolve(T*& a_fn, const char* a_name)
	{
		a_fn = reinterpret_cast<T*>(GetProcAddress(g_sl.interposer, a_name));
		if (!a_fn)
			logger::warn("[Streamline] missing interposer export '{}'", a_name);
		return a_fn != nullptr;
	}
}

Streamline* Streamline::GetSingleton()
{
	static Streamline singleton;
	return &singleton;
}

void Streamline::PreloadInterposer()
{
	// Map sl.interposer.dll EARLY — before DXVK creates its VkInstance — so DXVK's
	// loadVulkanLibrary("sl.interposer.dll") (which we added to its loader) aliases this already-mapped
	// module and routes DXVK's ENTIRE Vulkan surface through Streamline (full interposition). A bare
	// runtime LoadLibraryA from inside dxvk_d3d11.dll does NOT search the CS dxvk/ subfolder, so without
	// this preload DXVK falls through to the real vulkan-1.dll and SL never sees the device/present.
	// LOAD_WITH_ALTERED_SEARCH_PATH lets the interposer resolve its sibling sl.*.dll from the CS folder.
	if (g_sl.interposer)
		return;
	const auto slDir = GetStreamlineDir();
	if (slDir.empty())
		return;
	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	g_sl.interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	logger::info("[Streamline] interposer preload for DXVK Vulkan interposition: {}",
		g_sl.interposer ? "mapped" : "FAILED (DXVK uses real driver)");
	if (!g_sl.interposer)
		return;
	// slInit MUST run before DXVK creates its VkInstance (the interposer's vkCreateInstance/Device proxies
	// add DLSS-G's device requirements + capture the handles). The later SetupResources Initialize() call
	// no-ops (triedInit guard).
	Initialize();
}

bool Streamline::Initialize()
{
	if (triedInit)
		return initialized;
	triedInit = true;

	// No GPU-vendor gate: Streamline performs its own per-feature compatibility check
	// (slIsFeatureSupported for DLSS / Reflex / DLSS-G). On hardware or a driver that
	// lacks a feature it simply reports unsupported and that path stays on FSR3-on-DXVK.
	// If the plugin DLLs are absent (the usual case — they are NVIDIA redistributables
	// fetched at build time) the interposer load below fails and we degrade to FSR.
	const auto slDir = GetStreamlineDir();
	if (slDir.empty()) {
		logger::warn("[Streamline] could not resolve plugin directory");
		return false;
	}

	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	// LOAD_WITH_ALTERED_SEARCH_PATH so the interposer resolves its sibling sl.*.dll
	// plugins from the same CS folder rather than the game root / System32.
	// May already be mapped by PreloadInterposer() (so DXVK could alias it as its vulkan-1.dll).
	if (!g_sl.interposer)
		g_sl.interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!g_sl.interposer) {
		// Expected on a normal install: the SL plugin DLLs are not bundled (NVIDIA
		// redistributables). Degrade cleanly to FSR.
		logger::info("[Streamline] sl.interposer.dll not present in '{}' — DLSS/Reflex disabled", slDir.string());
		return false;
	}

	const bool resolved =
		Resolve(g_sl.slInit, "slInit") &&
		Resolve(g_sl.slShutdown, "slShutdown") &&
		Resolve(g_sl.slIsFeatureSupported, "slIsFeatureSupported") &&
		Resolve(g_sl.slGetFeatureRequirements, "slGetFeatureRequirements") &&
		Resolve(g_sl.slGetNewFrameToken, "slGetNewFrameToken") &&
		Resolve(g_sl.slSetTagForFrame, "slSetTagForFrame") &&
		Resolve(g_sl.slSetConstants, "slSetConstants") &&
		Resolve(g_sl.slEvaluateFeature, "slEvaluateFeature") &&
		Resolve(g_sl.slGetFeatureFunction, "slGetFeatureFunction") &&
		Resolve(g_sl.slAllocateResources, "slAllocateResources") &&
		Resolve(g_sl.slFreeResources, "slFreeResources");

	// Non-fatal: runtime DLSS-G (un)load (Streamline DLSS-G guide §18 — avoids the extra present queue +
	// off-screen-copy overhead when DLSS-G is off). Absent on older SL builds -> the unload is skipped.
	Resolve(g_sl.slSetFeatureLoaded, "slSetFeatureLoaded");
	if (!resolved) {
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
		return false;
	}

	const auto slDirWide = slDir.wstring();
	const wchar_t* pluginPaths[] = { slDirWide.c_str() };
	// Co-load BOTH FG plugins so the user can switch FG method in-game with NO restart. They no longer fight
	// over the swapchain: the interposer's WSI dispatch suppresses sl.dlss_g's swapchain hooks whenever the
	// sl.fsr FFX FrameInterpolationSwapChain owns present ("sl.fsr.fgActive" param). And they no longer fight
	// over the shared per-frame constants/tags: the FSR FG-prepare runs on a DEDICATED viewport (see
	// EvaluateFSRFrameGen) so its depth/MV tags + camera constants never collide with the upscaler's (which
	// dlss_g also reads on viewport 0).
	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL, sl::kFeatureDLSS_G,
		sl::kFeatureFSR, sl::kFeatureXeSS };

	sl::Preferences pref{};
	pref.renderAPI = sl::RenderAPI::eVulkan;
	// Frame-based tagging is required by slEvaluateFeature / slSetTagForFrame.
	pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	// Full interposition: sl.interposer.dll IS DXVK's vulkan-1.dll, so SL's own
	// vkCreateInstance/Device/present proxies run. eUseManualHooking must be OFF.
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(featuresToLoad));
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0";
	// projectId is expected to be a GUID string for the eCustom engine path.
	pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
	pref.logLevel = sl::LogLevel::eDefault;
	pref.logMessageCallback = &LogCallback;

	const sl::Result res = g_sl.slInit(pref, sl::kSDKVersion);
	if (res != sl::Result::eOk) {
		logger::warn("[Streamline] slInit failed (result {}) — DLSS/Reflex disabled", static_cast<int>(res));
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
		return false;
	}

	initialized = true;
	logger::info("[Streamline] initialized on Vulkan (SDK {}.{}.{})",
		SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
	return true;
}

void Streamline::SetVulkanDevice()
{
	if (!initialized || vulkanDeviceSet)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk || !dxvk->IsAvailable()) {
		logger::warn("[Streamline] DXVK interop unavailable — cannot hand Vulkan device to SL");
		return;
	}

	// SL already owns instance/device/queues (it created them via its vkCreateInstance/Device proxies
	// as DXVK's loader under full interposition), so no slSetVulkanInfo handoff is needed.
	vulkanDeviceSet = true;

	// Per-adapter feature probe. AdapterInfo with vkPhysicalDevice set keys SL off
	// the actual GPU (LUID ignored). Only eOk means "enable". On AMD this whole
	// method is unreachable (Initialize() returned false), but the per-feature
	// check is the authoritative second gate on NVIDIA (old GPUs lack DLSS-G, etc.).
	sl::AdapterInfo adapter{};
	adapter.vkPhysicalDevice = dxvk->GetPhysicalDevice();
	const auto supported = [&](sl::Feature f) {
		const sl::Result r = g_sl.slIsFeatureSupported(f, adapter);
		if (r != sl::Result::eOk)
			logger::info("[Streamline] feature {} unsupported (result {})", f, static_cast<int>(r));
		return r == sl::Result::eOk;
	};

	featureDLSS = supported(sl::kFeatureDLSS);
	featureReflex = supported(sl::kFeatureReflex);
	featureDLSSG = supported(sl::kFeatureDLSS_G);
	featureXeSS = supported(sl::kFeatureXeSS);  // Community Shaders sl.xess plugin (any GPU)
	featureFSR = supported(sl::kFeatureFSR);    // Community Shaders sl.fsr plugin (any GPU)

	// Bind the feature-specific functions (valid only after the device is set).
	if (featureDLSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", reinterpret_cast<void*&>(g_sl.slDLSSGetOptimalSettings));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSSetOptions));
		featureDLSS = g_sl.slDLSSSetOptions != nullptr;
	}
	if (featureReflex) {
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void*&>(g_sl.slReflexSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void*&>(g_sl.slReflexSleep));
		featureReflex = g_sl.slReflexSetOptions != nullptr && g_sl.slReflexSleep != nullptr;
	}
	// PCL latency markers (separate feature, loaded by default in slInit). Used to bracket the
	// frame pipeline so Reflex can place its sleep optimally and report PCL stats. GPU-agnostic.
	g_sl.slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", reinterpret_cast<void*&>(g_sl.slPCLSetMarker));
	logger::info("[Streamline] PCL latency markers {}", g_sl.slPCLSetMarker ? "available" : "unavailable");
	if (featureDLSSG) {
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(g_sl.slDLSSGSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(g_sl.slDLSSGGetState));
		featureDLSSG = g_sl.slDLSSGSetOptions != nullptr && g_sl.slDLSSGGetState != nullptr;
	}
	if (featureFSR) {
		g_sl.slGetFeatureFunction(sl::kFeatureFSR, "slFSRSetOptions", reinterpret_cast<void*&>(g_sl.slFSRSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(g_sl.slFSRFrameGenerationSetOptions));
		g_sl.slGetFeatureFunction(sl::kFeatureFSR, "slFSRGetFrameGenState", reinterpret_cast<void*&>(g_sl.slFSRGetFrameGenState));
		featureFSR = g_sl.slFSRSetOptions != nullptr;
	}
	if (featureXeSS) {
		g_sl.slGetFeatureFunction(sl::kFeatureXeSS, "slXeSSSetOptions", reinterpret_cast<void*&>(g_sl.slXeSSSetOptions));
		featureXeSS = g_sl.slXeSSSetOptions != nullptr;
	}

	logger::info("[Streamline] feature support: DLSS={} Reflex={} DLSS-G={} FSR={} XeSS={} (FSR-FG fns {})",
		featureDLSS, featureReflex, featureDLSSG, featureFSR, featureXeSS,
		g_sl.slFSRFrameGenerationSetOptions ? "ok" : "missing");

	// Identify the GPU generation from the Vulkan physical device for DLSS render-preset selection.
	// (vkGetPhysicalDeviceProperties carries the real PCI vendor/device IDs even under DXVK, unlike
	// the DXGI adapter desc which the create hook may not capture.)
	if (auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
			dxvk->GetInstanceProcAddr()(dxvk->GetInstance(), "vkGetPhysicalDeviceProperties"))) {
		VkPhysicalDeviceProperties props{};
		getProps(dxvk->GetPhysicalDevice(), &props);
		isNvidiaGPU = props.vendorID == 0x10DE;
		isRTXBelow40Series = isNvidiaGPU &&
		                     ((props.deviceID >= 0x2200 && props.deviceID <= 0x2600) ||   // RTX 30 (Ampere)
								(props.deviceID >= 0x1E00 && props.deviceID <= 0x1FFF));   // RTX 20 (Turing w/ RT)
		logger::info("[Streamline] GPU vendor=0x{:04X} device=0x{:04X} -> DLSS preset group: {}",
			props.vendorID, props.deviceID,
			isNvidiaGPU ? (isRTXBelow40Series ? "RTX 20/30 (J)" : "RTX 40+ (M)") : "non-NVIDIA (default)");
	}

}

void Streamline::Shutdown()
{
	// Free the cached DLSS-G resource views before tearing down the device.
	if (auto* dxvk = DxvkInterop::GetSingleton(); dxvk && dxvk->IsAvailable()) {
		VkDevice vkDevice = dxvk->GetDevice();
		if (auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
				dxvk->GetDeviceProcAddr()(vkDevice, "vkDestroyImageView"))) {
			for (auto& c : g_sl.dlssgViewCache) {
				if (c.view != VK_NULL_HANDLE)
					vkDestroyImageView(vkDevice, c.view, nullptr);
				c.view = VK_NULL_HANDLE;
				c.image = VK_NULL_HANDLE;
			}
			if (g_sl.fsrFgHudlessCache.view != VK_NULL_HANDLE)
				vkDestroyImageView(vkDevice, g_sl.fsrFgHudlessCache.view, nullptr);
			g_sl.fsrFgHudlessCache.view = VK_NULL_HANDLE;
			g_sl.fsrFgHudlessCache.image = VK_NULL_HANDLE;
		}
	}

	if (g_sl.interposer) {
		if (initialized && g_sl.slShutdown)
			g_sl.slShutdown();
		FreeLibrary(g_sl.interposer);
		g_sl.interposer = nullptr;
	}
	initialized = false;
	vulkanDeviceSet = false;
	featureDLSS = featureReflex = featureDLSSG = false;
}

// Returns the shared per-frame SL frame token. Pass a_beginFrame=true at the FIRST SL touch of the
// frame (the SimulationStart marker) to advance the frame index and mint a fresh token; every other
// per-frame SL call passes false to reuse it, so constants/tags/markers/present all carry one frame
// index (the DLSS-G "constants must match the presented frame" contract). Falls back to minting on
// the current index if none has been established yet.
static sl::FrameToken* SharedFrameToken(bool a_beginFrame)
{
	if (a_beginFrame || g_sl.frameToken == nullptr) {
		if (a_beginFrame) {
			++g_sl.frameIndex;
			g_sl.dlssgTaggedThisFrame = false;  // new frame: no valid DLSS-G tag set yet
		}
		sl::FrameToken* token = nullptr;
		if (g_sl.slGetNewFrameToken(token, &g_sl.frameIndex) == sl::Result::eOk && token)
			g_sl.frameToken = token;
	}
	return g_sl.frameToken;
}

void Streamline::UpdateReflex(bool a_enable, bool a_boost)
{
	// Tractable on the existing DXVK device (markers + sleep only, no extra queues).
	if (!initialized || !featureReflex || g_sl.dispatchFaulted)
		return;

	const sl::ReflexMode mode = !a_enable ? sl::ReflexMode::eOff :
	                            a_boost   ? sl::ReflexMode::eLowLatencyWithBoost :
	                                        sl::ReflexMode::eLowLatency;

	__try {
		if (!g_sl.reflexCacheValid || g_sl.reflexCachedMode != mode) {
			sl::ReflexOptions options{};
			options.mode = mode;
			if (g_sl.slReflexSetOptions(options) == sl::Result::eOk) {
				g_sl.reflexCachedMode = mode;
				g_sl.reflexCacheValid = true;
			}
		}
		if (mode != sl::ReflexMode::eOff) {
			// Reflex sleep runs just before the SimulationStart marker establishes this frame's token,
			// so it reuses the current (prior frame's) shared token — acceptable for pacing and keeps a
			// single frame-begin point (SimulationStart) for the DLSS-G constants/present contract.
			if (sl::FrameToken* token = SharedFrameToken(false))
				g_sl.slReflexSleep(*token);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] Reflex dispatch faulted — disabling for this session");
	}
}

void Streamline::SetPCLMarker(PclMarker a_marker)
{
	if (!initialized || !g_sl.slPCLSetMarker || g_sl.dispatchFaulted)
		return;
	__try {
		// SimulationStart is the first SL marker of the frame — establish the shared frame token here.
		sl::FrameToken* token = SharedFrameToken(a_marker == PclMarker::SimulationStart);
		if (token)
			g_sl.slPCLSetMarker(static_cast<sl::PCLMarker>(a_marker), *token);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] PCL marker faulted — disabling for this session");
	}
}

// ---- Shared upscale / frame-generation evaluate plumbing ---------------------------------------
// EvaluateDLSS / EvaluateXeSS / EvaluateFSR / EvaluateFSRFrameGen differ only in the feature-specific
// options they set up front; the per-frame SL constants, the DXVK->SL interop resource wrapping, the
// resource tagging, and the command-buffer evaluate+submit are identical. Those shared steps live in the
// helpers below so each public Evaluate* is just "set options -> call the core". They are free
// (internal-linkage) functions so this stays out of Streamline.h (which deliberately keeps SL types
// opaque). They are called from inside each Evaluate*'s __try; having no __try of their own, the C++
// objects they use are fine — only the SEH-wrapped callers must keep POD locals.

// Fill SL common constants for the frame: real camera matrices from the frame-buffer cache fed through
// recalculateCameraMatrices, NEGATED jitter (SL/DLSS/FFX sign convention), non-inverted depth, and unit
// mvec scale (Skyrim MVs are already in SL space). Identical inputs for every upscaler and the FG-prepare.
static void cs_BuildConstants(sl::Constants& a_consts, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	a_consts = {};
	a_consts.cameraAspectRatio = static_cast<float>(a_outputWidth) / static_cast<float>(a_outputHeight);
	a_consts.cameraFOV = Util::GetVerticalFOVRad();
	a_consts.cameraNear = *globals::game::cameraNear;
	a_consts.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

	a_consts.cameraMotionIncluded = sl::Boolean::eTrue;
	a_consts.cameraPinholeOffset = { 0.f, 0.f };
	a_consts.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	a_consts.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	a_consts.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	a_consts.cameraPos = *reinterpret_cast<const sl::float3*>(&globals::game::frameBufferCached.GetCameraPosAdjust());
	a_consts.cameraViewToClip = *reinterpret_cast<const sl::float4x4*>(&cameraViewToClip);
	a_consts.depthInverted = sl::Boolean::eFalse;

	sl::recalculateCameraMatrices(a_consts);  // keep: fills clipToCameraView from cameraViewToClip

	// Override clipToPrevClip/prevClipToClip with a deterministic jitter-free reprojection from the game's OWN
	// current + previous unjittered view-proj, so SL's depth->MV reconstruction reproduces MotionBlur::GetSSMotionVector.
	// recalculateCameraMatrices derives these from a STATIC previous-frame cache (its own header: "DO NOT USE THIS IN
	// ANYTHING PROPER") that aliases across call sites — the upscale evaluate and the FG-prepare both build constants
	// every frame, each clobbering the other's "previous".
	//
	// CAMERA-RELATIVE BRIDGE (critical): Skyrim renders camera-relative. curVP.Invert() reconstructs world relative to
	// the CURRENT camera origin (CameraPosAdjust), but CameraPreviousViewProjUnjittered expects world relative to the
	// PREVIOUS camera origin (CameraPreviousPosAdjust) — the engine tracks the two adjusts separately and authors the
	// per-object previous-world positions in the previous frame's relative space (Lighting.hlsl:176 etc.). We must
	// translate the reconstructed world by the camera displacement (cur - prev adjust) to move it into the previous
	// frame's space before applying prevVP. Without this the reprojection assumes the camera only ROTATED, so motion
	// vectors are wrong under camera TRANSLATION (the sky still looks right because it is at infinity / translation-
	// invariant — which is exactly why the engine's own DeferredCompositeCS sky path can feed the current-relative
	// position to both legs). The translation scales with the homogeneous w, so it composes correctly pre-divide.
	//
	// Jitter: the game unprojects with the JITTERED inverse and reprojects UNJITTERED; that factors into a jitter-free
	// reprojection ∘ a current de-jitter, and SL applies the de-jitter itself via jitterOffset — so both legs here use
	// the UNJITTERED VP. .Transpose() maps the game's mul(M,v) column-vector matrices to SL's row-vector convention.
	Matrix curVP = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	Matrix prevVP = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered().Transpose();
	const auto& posAdj = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto& prevPosAdj = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
	Matrix camDelta = Matrix::CreateTranslation(posAdj.x - prevPosAdj.x, posAdj.y - prevPosAdj.y, posAdj.z - prevPosAdj.z);
	Matrix clipToPrevClip = curVP.Invert() * camDelta * prevVP;  // clip -> cur-rel world -> prev-rel world -> prev clip
	Matrix prevClipToClip = clipToPrevClip.Invert();
	a_consts.clipToPrevClip = *reinterpret_cast<const sl::float4x4*>(&clipToPrevClip);
	a_consts.prevClipToClip = *reinterpret_cast<const sl::float4x4*>(&prevClipToClip);

	a_consts.jitterOffset = { -a_jitterX, -a_jitterY };
	a_consts.reset = sl::Boolean::eFalse;
	a_consts.mvecScale = { 1.0f, 1.0f };
	a_consts.motionVectors3D = sl::Boolean::eFalse;
	a_consts.motionVectorsInvalidValue = FLT_MIN;
	a_consts.orthographicProjection = sl::Boolean::eFalse;
	a_consts.motionVectorsDilated = sl::Boolean::eFalse;
	a_consts.motionVectorsJittered = sl::Boolean::eFalse;
}

// Wrap one DXVK interop D3D11 resource as an SL Vulkan resource. SL's VK backend documents the
// sl::Resource view (VkImageView) as MANDATORY (null faults slEvaluateFeature), so create a transient 2D
// view (depth aspect for depth formats); the caller collects a_outView for destruction after the submit.
// Returns false if the backing VkImage can't be obtained.
static bool cs_WrapInteropImage(DxvkInterop* a_dxvk, VkDevice a_device, PFN_vkCreateImageView a_createView,
	ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h, VkImageView& a_outView)
{
	a_outView = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	if (!a_dxvk->GetVkImage(a_res, &image, &layout, &info) || image == VK_NULL_HANDLE)
		return false;
	VkImageView view = VK_NULL_HANDLE;
	if (a_createView) {
		VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		ci.image = image;
		ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ci.format = info.format;
		ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		if (info.format == VK_FORMAT_D32_SFLOAT || info.format == VK_FORMAT_D24_UNORM_S8_UINT ||
			info.format == VK_FORMAT_D16_UNORM || info.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
			ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		ci.subresourceRange.levelCount = 1;
		ci.subresourceRange.layerCount = 1;
		a_createView(a_device, &ci, nullptr, &view);
		a_outView = view;
	}
	a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, view, static_cast<uint32_t>(layout) };
	a_out.width = a_w;
	a_out.height = a_h;
	a_out.nativeFormat = static_cast<uint32_t>(info.format);
	a_out.mipLevels = info.mipLevels;
	a_out.arrayLayers = info.arrayLayers;
	a_out.usage = static_cast<uint32_t>(info.usage);
	a_out.flags = static_cast<uint32_t>(info.flags);
	return true;
}

// Destroy the transient views created by cs_WrapInteropImage. Called on every exit path (success, wrap
// failure, and the no-command-buffer bail) so views never leak — EvaluateFSRFrameGen runs every frame.
static void cs_DestroyViews(DxvkInterop* a_dxvk, VkDevice a_device, const VkImageView* a_views, int a_count)
{
	if (auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
			a_dxvk->GetDeviceProcAddr()(a_device, "vkDestroyImageView"))) {
		for (int i = 0; i < a_count; ++i)
			if (a_views[i] != VK_NULL_HANDLE)
				vkDestroyImageView(a_device, a_views[i], nullptr);
	}
}

// Shared evaluate core: sets the frame constants on a_viewport, wraps depth + MV (+ color when BOTH
// colorIn and colorOut are given), tags them, evaluates a_feature into a fresh DXVK frame command buffer,
// submits (waitIdle), and frees the transient views on every path. colorIn/colorOut == null => the
// depth+MV-only FG-prepare. Returns the slEvaluateFeature result (or a pre-eval error code on bail).
// No __try here — each caller wraps the call in its own SEH guard.
static sl::Result cs_EvaluateFeatureCore(sl::Feature a_feature, const sl::ViewportHandle& a_viewport,
	ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY, ID3D11Resource* a_hudlessColor = nullptr)
{
	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk)
		return sl::Result::eErrorNotInitialized;

	sl::FrameToken* token = SharedFrameToken(false);
	if (!token)
		return sl::Result::eErrorMissingInputParameter;

	sl::Constants consts;
	cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);
	g_sl.slSetConstants(consts, *token, a_viewport);

	VkDevice vkDevice = dxvk->GetDevice();
	auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
		dxvk->GetDeviceProcAddr()(vkDevice, "vkCreateImageView"));
	VkImageView views[5] = {};
	int nv = 0;
	const auto wrap = [&](ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h) -> bool {
		VkImageView v = VK_NULL_HANDLE;
		if (!cs_WrapInteropImage(dxvk, vkDevice, vkCreateImageView, a_res, a_out, a_w, a_h, v))
			return false;
		if (v != VK_NULL_HANDLE && nv < 5)
			views[nv++] = v;
		return true;
	};

	const bool haveColor = (a_colorIn && a_colorOut);
	const bool haveHudless = (a_hudlessColor != nullptr);
	sl::Resource colorInRes{}, colorOutRes{}, depthRes{}, mvecRes{}, hudlessRes{};
	bool ok = wrap(a_depth, depthRes, a_renderWidth, a_renderHeight) &&
	          wrap(a_motionVectors, mvecRes, a_renderWidth, a_renderHeight);
	if (ok && haveColor)
		ok = wrap(a_colorIn, colorInRes, a_renderWidth, a_renderHeight) &&
		     wrap(a_colorOut, colorOutRes, a_outputWidth, a_outputHeight);
	// HUDLessColor is the post-upscale, pre-UI scene at DISPLAY (output) resolution — tag it at output dims.
	if (ok && haveHudless)
		ok = wrap(a_hudlessColor, hudlessRes, a_outputWidth, a_outputHeight);
	if (!ok) {
		cs_DestroyViews(dxvk, vkDevice, views, nv);
		return sl::Result::eErrorMissingInputParameter;
	}

	// Motion vectors must carry the SAME dimensions + subrect as depth (SL/FFX reconstruct MV on the depth
	// grid). Force MV's reported dimensions to depth's and tag both with the one renderExtent below.
	mvecRes.width = depthRes.width;
	mvecRes.height = depthRes.height;

	sl::Extent renderExtent{ 0, 0, a_renderWidth, a_renderHeight };
	sl::Extent outputExtent{ 0, 0, a_outputWidth, a_outputHeight };
	sl::ResourceTag tags[5];
	uint32_t nt = 0;
	if (haveColor) {
		tags[nt++] = sl::ResourceTag{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
		tags[nt++] = sl::ResourceTag{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent };
	}
	tags[nt++] = sl::ResourceTag{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
	tags[nt++] = sl::ResourceTag{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
	if (haveHudless)
		tags[nt++] = sl::ResourceTag{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent };

	sl::Result evalRes = sl::Result::eErrorNotInitialized;
	VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
	if (cmd != VK_NULL_HANDLE) {
		g_sl.slSetTagForFrame(*token, a_viewport, tags, nt, cmd);
		const sl::BaseStructure* inputs[] = { &a_viewport };
		evalRes = g_sl.slEvaluateFeature(a_feature, *token, inputs, static_cast<uint32_t>(std::size(inputs)), cmd);
		dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/true);
	}
	cs_DestroyViews(dxvk, vkDevice, views, nv);
	return evalRes;
}

void Streamline::EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	// Best-effort DLSS super-resolution dispatch on DXVK's Vulkan device.
	//
	// NOTE: this path is NVIDIA-only and cannot be exercised on the current AMD hardware (and ships
	// without the SL plugin DLLs), so it is structurally implemented from the SL VK spec but not
	// runtime-validated. Fully gated (featureDLSS) and SEH-guarded so any fault latches the feature off.
	if (!initialized || !featureDLSS || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;
	// Flush DXVK's pending D3D11 rendering so the interop VkImages are submitted/consistent before SL
	// records its compute work; SL applies the layout barriers itself from the tagged per-resource state.
	dxvk->FlushRenderingCommands();

	(void)a_sharpness;  // sharpness is deprecated in DLSSOptions; RCAS handles sharpening.

	__try {
		// Map quality preset -> DLSS mode (shared convention with getUpscaleRatio: 0=Native,1=Quality,
		// 2=Balanced,3=Performance,4=Ultra Performance) so the render res stays in the mode's [min,max].
		sl::DLSSMode dlssMode = sl::DLSSMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			dlssMode = sl::DLSSMode::eDLAA;  // Native AA, no upscale (render ratio 1.0)
			break;
		case 1:
			dlssMode = sl::DLSSMode::eMaxQuality;  // DLSS Quality (0.667x)
			break;
		case 2:
			dlssMode = sl::DLSSMode::eBalanced;  // DLSS Balanced (0.58x)
			break;
		case 3:
			dlssMode = sl::DLSSMode::eMaxPerformance;  // DLSS Performance (0.5x)
			break;
		case 4:
			dlssMode = sl::DLSSMode::eUltraPerformance;  // DLSS Ultra Performance (0.33x)
			break;
		default:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		}

		sl::DLSSOptions options{};
		options.mode = dlssMode;
		options.outputWidth = a_outputWidth;
		options.outputHeight = a_outputHeight;
		options.colorBuffersHDR = sl::Boolean::eFalse;  // CS upscales the SDR scene; HDR composite happens later.
		options.useAutoExposure = sl::Boolean::eTrue;   // matches the proven dev-branch DLSS integration.

		// Per-GPU DLSS render-preset selection (ported from the proven dev-branch integration). RTX 40+
		// (Ada) uses preset M for the upscaling modes; RTX 20/30 (Turing/Ampere) use preset J. Resolved
		// once at device-set time from the Vulkan physical device (isRTXBelow40Series / isNvidiaGPU).
		if (isRTXBelow40Series) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetJ;
			options.balancedPreset = sl::DLSSPreset::ePresetJ;
			options.performancePreset = sl::DLSSPreset::ePresetJ;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
		} else if (isNvidiaGPU) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetM;
			options.balancedPreset = sl::DLSSPreset::ePresetM;
			options.performancePreset = sl::DLSSPreset::ePresetM;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
		}

		static bool s_loggedPreset = false;
		if (!s_loggedPreset) {
			s_loggedPreset = true;
			logger::info("[Streamline] DLSS presets set (mode {}): quality={} (RTX40+={} below40={})",
				static_cast<int>(dlssMode), static_cast<int>(options.qualityPreset),
				isNvidiaGPU && !isRTXBelow40Series, isRTXBelow40Series);
		}

		if (g_sl.slDLSSSetOptions(g_sl.viewport, options) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureDLSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] DLSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	if (!initialized || !featureXeSS || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;
	dxvk->FlushRenderingCommands();

	__try {
		sl::XeSSMode xessMode = sl::XeSSMode::eQuality;
		switch (a_qualityMode) {
		case 0:
			xessMode = sl::XeSSMode::eNativeAA;
			break;
		case 1:
			xessMode = sl::XeSSMode::eQuality;
			break;
		case 2:
			xessMode = sl::XeSSMode::eBalanced;
			break;
		case 3:
			xessMode = sl::XeSSMode::ePerformance;
			break;
		case 4:
			xessMode = sl::XeSSMode::eUltraPerformance;
			break;
		}

		sl::XeSSOptions xessOpts{};
		xessOpts.mode = xessMode;
		xessOpts.outputWidth = a_outputWidth;
		xessOpts.outputHeight = a_outputHeight;
		xessOpts.sharpness = a_sharpness;
		xessOpts.colorBuffersHDR = sl::Boolean::eFalse;
		if (g_sl.slXeSSSetOptions(g_sl.viewport, xessOpts) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureXeSS, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] XeSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] XeSS dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	if (!initialized || !featureFSR || g_sl.dispatchFaulted)
		return;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;
	dxvk->FlushRenderingCommands();

	__try {
		sl::FSRMode fsrMode = sl::FSRMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			fsrMode = sl::FSRMode::eNativeAA;
			break;
		case 1:
			fsrMode = sl::FSRMode::eMaxQuality;
			break;
		case 2:
			fsrMode = sl::FSRMode::eBalanced;
			break;
		case 3:
			fsrMode = sl::FSRMode::eMaxPerformance;
			break;
		case 4:
			fsrMode = sl::FSRMode::eUltraPerformance;
			break;
		}

		sl::FSROptions fsrOpts{};
		fsrOpts.mode = fsrMode;
		fsrOpts.outputWidth = a_outputWidth;
		fsrOpts.outputHeight = a_outputHeight;
		fsrOpts.sharpness = a_sharpness;
		fsrOpts.colorBuffersHDR = sl::Boolean::eFalse;
		if (g_sl.slFSRSetOptions(g_sl.viewport, fsrOpts) != sl::Result::eOk)
			return;

		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR, g_sl.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] FSR evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR dispatch faulted — disabling for this session");
	}
}

void Streamline::EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	// Drives the sl.fsr plugin's standalone FG-prepare. Tags depth + MV but NO color, so the plugin's
	// shared kFeatureFSR evaluate runs only the FrameGenerationPrepare (not an upscale) — making FSR FG
	// independent of the active upscaler. The present-time FFX swapchain consumes what this prepares.
	//
	// Runs on a DEDICATED viewport (1), separate from the upscaler's viewport (0). SL keys both common
	// constants AND tagged resources by (frameToken, viewport): on viewport 0 this would (a) collide with
	// the upscaler's constants (eErrorDuplicatedConstants + clobber) and (b) see the upscaler's leftover
	// COLOR tags, making the plugin's shared kFeatureFSR evaluate take the UPSCALE branch instead of
	// FG-prepare. dlss_g also reads viewport-0 constants at present, so an isolated viewport keeps all
	// three apart. The plugin's FG-prepare uses GLOBAL ctx (fgContext/fgWrappedSwapchain/fgEnabled), not
	// per-viewport options, so viewport 1 is safe; it only needs its own constants + depth/MV tags.
	if (!initialized || !featureFSR || g_sl.dispatchFaulted)
		return;
	if (!a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;
	dxvk->FlushRenderingCommands();

	__try {
		const sl::ViewportHandle fgViewport{ 1 };
		// Tag depth + MV (FG-prepare inputs) AND the hudless scene (present-time UI extraction). No color, so
		// the plugin's shared kFeatureFSR evaluate runs FG-prepare, not an upscale.
		const sl::Result evalRes = cs_EvaluateFeatureCore(sl::kFeatureFSR, fgViewport,
			nullptr, nullptr, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY, a_hudlessColor);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		if (evalRes != s_loggedRes) {
			s_loggedRes = evalRes;
			logger::info("[Streamline] FSR FG-prepare result={} render={}x{}", static_cast<int>(evalRes), a_renderWidth, a_renderHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR FG-prepare faulted — disabling for this session");
	}
}

void Streamline::SetDLSSGMode(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_numFramesToGenerate)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;

	// DLSS-G may be runtime-unloaded when not the selected FG method (guide §18). Don't call its options
	// entry point while unloaded — wait for the load (driven by the swapchain recreate) to land.
	if (!g_dlssgCurrentlyLoaded)
		return;

	// Called every frame with the gameplay gate — skip the SL call when the mode hasn't changed.
	if (g_sl.dlssgModeCached && g_sl.dlssgModeOn == a_enable)
		return;

	__try {
		sl::DLSSGOptions options{};
		options.mode = a_enable ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
		options.numFramesToGenerate = a_numFramesToGenerate;
		// CS upscales via dynamic resolution, so the depth/MV inputs are at the (smaller) render
		// size while the back buffer is at display size. DLSS-G must be told this — without the
		// eDynamicResolutionEnabled flag + dimensions it assumes full-res inputs, mismatches the
		// DRS-downscaled depth/MV, and its eBlockPresentingClientQueue blocks the present queue
		// waiting on a workload that can't complete (the reported toggle freeze).
		// eRetainResourcesWhenOff: DLSS-G is toggled off every loading screen / menu and back on for
		// gameplay; retaining its resources across those off periods avoids realloc stutter on re-enable.
		options.flags = sl::DLSSGFlags::eDynamicResolutionEnabled | sl::DLSSGFlags::eRetainResourcesWhenOff;
		options.dynamicResWidth = a_renderWidth;
		options.dynamicResHeight = a_renderHeight;
		options.mvecDepthWidth = a_displayWidth;  // texture dims, not render size (extent gives the sub-rect)
		options.mvecDepthHeight = a_displayHeight;  // texture dims, not render size (extent gives the sub-rect)
		options.colorWidth = a_displayWidth;
		options.colorHeight = a_displayHeight;
		// eBlockNoClientQueues (Vulkan-only): SL does NOT block the client's presenting queue while it
		// runs frame generation. eBlockPresentingClientQueue (the default) blocks that queue until the FG
		// workload completes — which DEADLOCKS on a paused/menu/frozen scene where no new frame arrives to
		// unblock it (the reported hang). With NoClientQueues the present returns immediately and SL paces
		// asynchronously, so a frozen scene simply presents 1:1 with no hang. (Tradeoff: tagged depth/MV
		// must not be overwritten before SL consumes them; acceptable here since we only interpolate
		// during gameplay and the inputs are stable frame-to-frame.)
		options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockNoClientQueues;
		const sl::Result res = g_sl.slDLSSGSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			logger::warn("[Streamline] slDLSSGSetOptions failed (result {})", static_cast<int>(res));
		} else {
			g_sl.dlssgModeCached = true;
			g_sl.dlssgModeOn = a_enable;
			logger::info("[Streamline] DLSS-G mode={} frames={}", a_enable, a_numFramesToGenerate);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G SetOptions faulted — disabling for this session");
	}
}

bool Streamline::SetFSRFrameGen(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight, bool a_hdr,
	bool a_debugView, bool a_debugTearLines, bool a_debugPacingLines, bool a_onlyPresentGenerated)
{
	// Returns true only when the option was actually delivered to the plugin. The caller retries until
	// it does, because featureFSR (and the FG entry points) come up a few frames AFTER the first
	// CheckResources — a one-shot transition would silently miss that window.
	if (!initialized || !featureFSR || !g_sl.slFSRFrameGenerationSetOptions || g_sl.dispatchFaulted)
		return false;

	bool ok = false;
	__try {
		sl::FSRFrameGenOptions options{};
		options.enabled = a_enable ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.renderWidth = a_renderWidth;
		options.renderHeight = a_renderHeight;
		options.displayWidth = a_displayWidth;
		options.displayHeight = a_displayHeight;
		options.colorBuffersHDR = a_hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugView = a_debugView ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugTearLines = a_debugTearLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugPacingLines = a_debugPacingLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.onlyPresentGenerated = a_onlyPresentGenerated ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		const sl::Result res = g_sl.slFSRFrameGenerationSetOptions(g_sl.viewport, options);
		if (res != sl::Result::eOk) {
			logger::warn("[Streamline] slFSRFrameGenerationSetOptions failed (result {})", static_cast<int>(res));
		} else {
			ok = true;
			logger::info("[Streamline] FSR frame generation {} (render {}x{} display {}x{})",
				a_enable ? "ENABLED" : "disabled", a_renderWidth, a_renderHeight, a_displayWidth, a_displayHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR SetFrameGen faulted — disabling for this session");
	}
	return ok;
}

bool Streamline::IsFSRFrameGenActive() const
{
	if (!initialized || !featureFSR || !g_sl.slFSRGetFrameGenState || g_sl.dispatchFaulted)
		return false;
	sl::FSRFrameGenState state{};
	if (g_sl.slFSRGetFrameGenState(g_sl.viewport, state) != sl::Result::eOk)
		return false;
	return state.numFramesActuallyPresented >= 2;
}

bool Streamline::GetDLSSGState(uint64_t& a_vramUsage, uint32_t& a_maxFrames) const
{
	if (!initialized || !featureDLSSG)
		return false;

	sl::DLSSGState state{};
	const sl::Result res = g_sl.slDLSSGGetState(g_sl.viewport, state, nullptr);
	if (res != sl::Result::eOk)
		return false;
	a_vramUsage = state.estimatedVRAMUsageInBytes;
	a_maxFrames = state.numFramesToGenerateMax;
	return state.status == sl::DLSSGStatus::eOk;
}

void Streamline::LogDLSSGFrameStats()
{
	// SL owns present under interposition. Query slDLSSGGetState on the render thread purely for
	// doubling telemetry. numFramesActuallyPresented is a cumulative counter that reads 2 while
	// doubling. Throttled to ~once/2s so any SL warning can't spam the log.
	if (g_sl.dispatchFaulted || !g_sl.slDLSSGGetState || !g_sl.dlssgModeOn)
		return;
	static uint32_t s_n = 0;
	if ((s_n++ % 120) != 0)
		return;
	__try {
		sl::DLSSGState state{};
		if (g_sl.slDLSSGGetState(g_sl.viewport, state, nullptr) == sl::Result::eOk)
			logger::info("[Streamline] DLSS-G status={} framesPresented={} (2 = doubling active)",
				static_cast<int>(state.status), state.numFramesActuallyPresented);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
	}
}

void Streamline::TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight)
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;
	if (!a_depth || !a_motionVectors)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;

	__try {
		// §16.1 inputs back-pressure is now a GPU-side wait queued on the present thread (OnDLSSGPostPresent),
		// matching the SL sample's QueueGPUWaitOnSyncObjectSet — it gates the graphics queue so the rendering
		// that overwrites these depth/MV targets waits for the OFA to finish reading them. The old host-side
		// vkWaitSemaphores here blocked the CPU but did NOT gate the GPU's overwriting submissions, so the
		// OFA read being-overwritten memory -> device loss. Removed.

		// Tag DLSS-G inputs for the SAME frame the constants/markers/present use, else SL cannot match
		// them to the presented frame and drops interpolation.
		sl::FrameToken* token = SharedFrameToken(false);
		if (!token)
			return;

		// SL's Vulkan backend needs a VkImageView per tagged resource (the DLSS-SR path proved a
		// null view faults SL). DLSS-G consumes these at present time after a non-blocking submit,
		// so views are cached per VkImage (recreated only on change) rather than created+destroyed
		// per frame — see g_sl.dlssgViewCache. slot: 0=depth, 1=motion, 2=hudless.
		VkDevice vkDevice = dxvk->GetDevice();
		auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
			dxvk->GetDeviceProcAddr()(vkDevice, "vkCreateImageView"));
		auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
			dxvk->GetDeviceProcAddr()(vkDevice, "vkDestroyImageView"));

		const auto cachedView = [&](int a_slot, VkImage a_image, VkFormat a_format) -> VkImageView {
			auto& c = g_sl.dlssgViewCache[a_slot];
			if (c.image == a_image && c.view != VK_NULL_HANDLE)
				return c.view;
			if (c.view != VK_NULL_HANDLE && vkDestroyImageView)
				vkDestroyImageView(vkDevice, c.view, nullptr);
			c.view = VK_NULL_HANDLE;
			c.image = a_image;
			if (vkCreateImageView) {
				VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				ci.image = a_image;
				ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				ci.format = a_format;
				ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				if (a_format == VK_FORMAT_D32_SFLOAT || a_format == VK_FORMAT_D24_UNORM_S8_UINT ||
					a_format == VK_FORMAT_D16_UNORM || a_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
					ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				ci.subresourceRange.levelCount = 1;
				ci.subresourceRange.layerCount = 1;
				vkCreateImageView(vkDevice, &ci, nullptr, &c.view);
			}
			return c.view;
		};

		const auto makeResource = [&](ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h, int a_slot) {
			VkImage image = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			if (!dxvk->GetVkImage(a_res, &image, &layout, &info) || image == VK_NULL_HANDLE)
				return false;
			a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, cachedView(a_slot, image, info.format), static_cast<uint32_t>(layout) };
			// Resource dimensions = FULL texture size (the VkImage); the valid render sub-rect is the tag Extent.
			// Reporting the render size (a_w/a_h) told SL/DLSS-G the buffer was render-sized while the VkImage is
			// full-sized -> mis-scaled inputs. info.extent is the real texture size.
			(void)a_w; (void)a_h;
			a_out.width = info.extent.width;
			a_out.height = info.extent.height;
			a_out.nativeFormat = static_cast<uint32_t>(info.format);
			a_out.mipLevels = info.mipLevels;
			a_out.arrayLayers = info.arrayLayers;
			a_out.usage = static_cast<uint32_t>(info.usage);
			a_out.flags = static_cast<uint32_t>(info.flags);
			return true;
		};

		sl::Resource depthRes{}, mvecRes{};
		if (!makeResource(a_depth, depthRes, a_renderWidth, a_renderHeight, 0) ||
			!makeResource(a_motionVectors, mvecRes, a_renderWidth, a_renderHeight, 1))
			return;

		// Motion vectors must carry the SAME dimensions + subrect as depth (DLSS-G dilates MV on the depth
		// grid — DLSSG.MVecsSubrect must equal DLSSG.DepthSubrect). Force MV's dimensions to depth's; both are
		// tagged with the one `extent` below.
		mvecRes.width = depthRes.width;
		mvecRes.height = depthRes.height;

		sl::Extent extent{};
		extent.width = a_renderWidth;
		extent.height = a_renderHeight;

		// Depth: eOnlyValidNow — SL snapshots the depth into its own copy at tag time. This replaces a CS-side
		// CopyResource: the tagged depth is kMAIN_COPY, the render-res depth the engine saved before the in-place
		// depth upscale rewrote kMAIN; eValidUntilPresent would have SL read it back at present, after that upscale
		// and the next frame have reused kMAIN_COPY. eOnlyValidNow captures it now, while it is still correct.
		// MV stays eValidUntilPresent (referenced, no copy): copying BOTH depth+MV every frame is what previously
		// accumulated into a sustained-gameplay hang; the MV target is not disturbed before present.
		sl::ResourceTag tags[3];
		uint32_t tagCount = 0;
		tags[tagCount++] = { &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &extent };
		tags[tagCount++] = { &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extent };

		// HUDLessColor is the post-upscale, pre-UI scene at DISPLAY (back-buffer) resolution — NOT render
		// resolution like depth/MV. Tag it with the display extent so DLSS-G's UI extraction samples the
		// right region (tagging it render-res left it sampling only the top-left render sub-rect).
		sl::Extent displayExtent{};
		displayExtent.width = a_displayWidth;
		displayExtent.height = a_displayHeight;
		sl::Resource hudlessRes{};
		if (a_hudlessColor && makeResource(a_hudlessColor, hudlessRes, a_displayWidth, a_displayHeight, 2))
			tags[tagCount++] = { &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &displayExtent };

		VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return;

		g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, tagCount, cmd);
		dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
		g_sl.dlssgTaggedThisFrame = true;  // valid inputs tagged: present will interpolate this frame
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G tag faulted — disabling for this session");
	}
}

void Streamline::TagFSRFGHudless(ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight)
{
	if (!initialized || !featureFSR || g_sl.dispatchFaulted)
		return;
	if (!a_hudlessColor)
		return;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->CommandResourcesReady())  // DXVK is a hard requirement (load-time enforced); only the command-pool timing gate remains
		return;

	__try {
		sl::FrameToken* token = SharedFrameToken(false);
		if (!token)
			return;

		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		if (!dxvk->GetVkImage(a_hudlessColor, &image, &layout, &info) || image == VK_NULL_HANDLE)
			return;

		VkDevice vkDevice = dxvk->GetDevice();
		auto& c = g_sl.fsrFgHudlessCache;
		if (c.image != image || c.view == VK_NULL_HANDLE) {
			auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
				dxvk->GetDeviceProcAddr()(vkDevice, "vkDestroyImageView"));
			if (c.view != VK_NULL_HANDLE && vkDestroyImageView)
				vkDestroyImageView(vkDevice, c.view, nullptr);
			c.view = VK_NULL_HANDLE;
			c.image = image;
			auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
				dxvk->GetDeviceProcAddr()(vkDevice, "vkCreateImageView"));
			if (vkCreateImageView) {
				VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				ci.image = image;
				ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				ci.format = info.format;
				ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ci.subresourceRange.levelCount = 1;
				ci.subresourceRange.layerCount = 1;
				vkCreateImageView(vkDevice, &ci, nullptr, &c.view);
			}
		}

		sl::Resource hudlessRes{ sl::ResourceType::eTex2d, image, nullptr, c.view, static_cast<uint32_t>(layout) };
		hudlessRes.width = a_renderWidth;
		hudlessRes.height = a_renderHeight;
		hudlessRes.nativeFormat = static_cast<uint32_t>(info.format);
		hudlessRes.mipLevels = info.mipLevels;
		hudlessRes.arrayLayers = info.arrayLayers;
		hudlessRes.usage = static_cast<uint32_t>(info.usage);

		sl::Extent extent{ 0, 0, a_renderWidth, a_renderHeight };
		sl::ResourceTag tags[] = {
			{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &extent },
		};

		VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return;

		g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, static_cast<uint32_t>(std::size(tags)), cmd);
		dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] FSR FG hudless tag faulted — disabling for this session");
	}
}

void Streamline::ClearDLSSGTags()
{
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;

	__try {
		sl::FrameToken* token = SharedFrameToken(false);
		if (!token)
			return;

		// Null-resource tags tell DLSS-G "no valid interpolation inputs this frame" so it presents the
		// real frame 1:1. Required on every present that did not set valid tags (the first present, and
		// loading/paused/menu frames) — otherwise SL's present hook waits on inputs that never arrive and
		// stalls. Null tags carry no GPU work, so they are set with a NULL command buffer: this works
		// even before DXVK's interop command ring is ready (the very first present at swapchain create).
		sl::ResourceTag tags[] = {
			sl::ResourceTag{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
		};
		// Use a real command buffer when the interop ring is ready (most frames); fall back to a null
		// command buffer at the very first present (before the ring exists) — null tags record no work.
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		auto* dxvk = DxvkInterop::GetSingleton();
		if (dxvk->CommandResourcesReady())  // DXVK hard-required; only the command-pool timing gate remains
			cmd = dxvk->BeginFrameCommandBuffer();
		g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, static_cast<uint32_t>(std::size(tags)), cmd);
		if (cmd != VK_NULL_HANDLE)
			dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/false);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G clear-tags faulted — disabling for this session");
	}
}

void Streamline::EnsureDLSSGPresentTag()
{
	// Called from the present path for the DLSS-G/SL-owned swapchain. If the render pass already set a
	// valid tag this frame (gameplay), interpolate; otherwise set a null/passthrough tag so the present
	// never waits on absent inputs. This is what makes the FIRST present (before any render pass) and
	// every loading/menu present safe.
	if (!initialized || !featureDLSSG || g_sl.dispatchFaulted)
		return;
	if (g_sl.dlssgTaggedThisFrame)
		return;
	ClearDLSSGTags();
}

void Streamline::SetConstants(const float4x4& a_clipToPrevClip, const float4x4& a_prevClipToClip,
	float2 a_jitter, float2 a_mvecScale, float a_cameraNear, float a_cameraFar,
	float a_cameraFOV, float a_cameraAspect, bool a_depthInverted, bool a_reset)
{
	if (!initialized || g_sl.dispatchFaulted)
		return;

	__try {
		sl::FrameToken* token = SharedFrameToken(false);
		if (!token)
			return;

		sl::Constants consts{};
		std::memcpy(&consts.clipToPrevClip, &a_clipToPrevClip, sizeof(float4x4));
		std::memcpy(&consts.prevClipToClip, &a_prevClipToClip, sizeof(float4x4));
		consts.jitterOffset = { a_jitter.x, a_jitter.y };
		consts.mvecScale = { a_mvecScale.x, a_mvecScale.y };
		consts.cameraNear = a_cameraNear;
		consts.cameraFar = a_cameraFar;
		consts.cameraFOV = a_cameraFOV;
		consts.cameraAspectRatio = a_cameraAspect;
		consts.depthInverted = a_depthInverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		consts.cameraMotionIncluded = sl::Boolean::eTrue;
		consts.motionVectors3D = sl::Boolean::eFalse;
		consts.reset = a_reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

		g_sl.slSetConstants(consts, *token, g_sl.viewport);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_sl.dispatchFaulted = true;
		logger::error("[Streamline] SetConstants faulted — disabling for this session");
	}
}

void Streamline::RegisterDxvkOwnershipPredicate()
{
	// Under full interposition SL's interposed present is unconditionally on the present path, so EVERY
	// DXVK swapchain is externally paced. Register the ownership predicate so DXVK omits its present-fence
	// pNext + present-wait worker (which SL never signals — without this the first post-present acquire
	// blocks forever and the game freezes on the intro logo).
	HMODULE dxvkModule = GetModuleHandleW(L"dxvk_d3d11.dll");
	if (!dxvkModule) {
		logger::warn("[Streamline] DXVK module not loaded — cannot register ownership predicate");
		return;
	}
	using SetQueryFn = void (*)(bool (*)(VkSwapchainKHR));
	auto setQuery = reinterpret_cast<SetQueryFn>(GetProcAddress(dxvkModule, "dxvkSetFrameGenOwnershipQuery"));
	if (!setQuery) {
		logger::warn("[Streamline] dxvkSetFrameGenOwnershipQuery not found in DXVK module");
		return;
	}
	setQuery([](VkSwapchainKHR) -> bool { return true; });
	logger::info("[Streamline] registered DXVK ownership predicate (all swapchains externally paced)");

	// Register the swapchain-torn-down callback used to (un)load DLSS-G in the no-swapchain window
	// (Streamline DLSS-G guide §18). Non-fatal if the DXVK build predates the export.
	using SetTornDownFn = void (*)(void (*)());
	if (auto setTornDown = reinterpret_cast<SetTornDownFn>(GetProcAddress(dxvkModule, "dxvkSetSwapchainTornDownCallback"))) {
		setTornDown(&DxvkSwapchainTornDownCallback);
		logger::info("[Streamline] registered DXVK swapchain-torn-down callback (DLSS-G load/unload)");
	} else {
		logger::warn("[Streamline] dxvkSetSwapchainTornDownCallback not found — DLSS-G stays resident when disabled");
	}
}

void Streamline::SetDLSSGDesiredLoaded(bool a_loaded)
{
	// Request DLSS-G to be (un)loaded on the next swapchain recreate (applied by DxvkSwapchainTornDownCallback
	// in the no-swapchain window). The caller must follow this with RequestDxvkSwapchainRecreate().
	g_dlssgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool Streamline::IsDLSSGLoaded() const
{
	return g_dlssgCurrentlyLoaded;
}

void Streamline::RequestDxvkSwapchainRecreate()
{
	// Force DXVK to recreate its Vulkan swapchain on the next acquire. Needed when switching FG method
	// from DLSS-G back to FSR: sl.dlss_g's present proxy is sticky and bypasses the Vulkan present hooks,
	// so FSR can never return VK_SUBOPTIMAL to drive the recreate that lets the FFX FG layer re-wrap the
	// swapchain. This goes through DXVK's own internal recreate (preserves the D3D11 back buffers), which
	// runs vkDestroy/CreateSwapchainKHR — dlss_g releases its proxy and FFX wraps the fresh swapchain.
	static auto requestRecreate = []() -> void (*)() {
		HMODULE dxvkModule = GetModuleHandleW(L"dxvk_d3d11.dll");
		if (!dxvkModule)
			return nullptr;
		return reinterpret_cast<void (*)()>(GetProcAddress(dxvkModule, "dxvkRequestSwapchainRecreate"));
	}();
	if (requestRecreate) {
		requestRecreate();
		logger::info("[Streamline] requested DXVK swapchain recreate (FG method switch)");
	} else {
		logger::warn("[Streamline] dxvkRequestSwapchainRecreate not found — DLSS-G->FSR switch may not re-wrap");
	}
}
