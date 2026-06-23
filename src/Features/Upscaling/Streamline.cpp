#include "Streamline.h"

#include "DxvkInterop.h"

#include "../../DxvkLoader.h"
#include "../../Globals.h"
#include "../../State.h"

#include <filesystem>

// Streamline SDK headers (header-only in the repo; the plugin DLLs ship separately
// into the CS folder for NVIDIA users). NV_WINDOWS selects the Win32 surface.
#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4471 5103)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_device_wrappers.h>
#include <sl_dlss.h>
#include <sl_helpers_vk.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

namespace SIE
{
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
			PFun_slSetVulkanInfo* slSetVulkanInfo = nullptr;
			PFun_slGetNewFrameToken* slGetNewFrameToken = nullptr;
			PFun_slSetTagForFrame* slSetTagForFrame = nullptr;
			PFun_slSetConstants* slSetConstants = nullptr;
			PFun_slEvaluateFeature* slEvaluateFeature = nullptr;
			PFun_slGetFeatureFunction* slGetFeatureFunction = nullptr;
			PFun_slAllocateResources* slAllocateResources = nullptr;
			PFun_slFreeResources* slFreeResources = nullptr;

			// Feature-specific entry points (resolved via slGetFeatureFunction).
			PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings = nullptr;
			PFun_slDLSSSetOptions* slDLSSSetOptions = nullptr;
			PFun_slReflexSetOptions* slReflexSetOptions = nullptr;
			PFun_slReflexSleep* slReflexSleep = nullptr;

			sl::ViewportHandle viewport{ 0 };

			// Latches off after a dispatch fault so a single SEH fault (e.g. a driver
			// mismatch on an untested NVIDIA path) can't crash the game every frame.
			bool dispatchFaulted = false;

			// Cached Reflex options to avoid redundant slReflexSetOptions calls.
			bool reflexCacheValid = false;
			sl::ReflexMode reflexCachedMode = sl::ReflexMode::eOff;
		} g_sl;

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

	bool Streamline::Initialize()
	{
		if (triedInit)
			return initialized;
		triedInit = true;

		// PRIMARY GATE: Streamline/DLSS/Reflex are NVIDIA-only. On any non-NVIDIA GPU
		// (this AMD machine) we do not even load the interposer — slInit would later
		// inject NVIDIA-only VkDevice extensions that the AMD driver rejects. The
		// upscaler stays on the FSR3-on-DXVK path.
		if (!globals::state->IsNVIDIA()) {
			logger::info("[Streamline] non-NVIDIA GPU (vendor 0x{:04X}) — DLSS/Reflex disabled, using FSR",
				globals::state->adapterVendorId);
			return false;
		}

		const auto slDir = GetStreamlineDir();
		if (slDir.empty()) {
			logger::warn("[Streamline] could not resolve plugin directory");
			return false;
		}

		const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
		// LOAD_WITH_ALTERED_SEARCH_PATH so the interposer resolves its sibling sl.*.dll
		// plugins from the same CS folder rather than the game root / System32.
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
			Resolve(g_sl.slSetVulkanInfo, "slSetVulkanInfo") &&
			Resolve(g_sl.slGetNewFrameToken, "slGetNewFrameToken") &&
			Resolve(g_sl.slSetTagForFrame, "slSetTagForFrame") &&
			Resolve(g_sl.slSetConstants, "slSetConstants") &&
			Resolve(g_sl.slEvaluateFeature, "slEvaluateFeature") &&
			Resolve(g_sl.slGetFeatureFunction, "slGetFeatureFunction") &&
			Resolve(g_sl.slAllocateResources, "slAllocateResources") &&
			Resolve(g_sl.slFreeResources, "slFreeResources");
		if (!resolved) {
			FreeLibrary(g_sl.interposer);
			g_sl.interposer = nullptr;
			return false;
		}

		const auto slDirWide = slDir.wstring();
		const wchar_t* pluginPaths[] = { slDirWide.c_str() };
		sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };

		sl::Preferences pref{};
		pref.renderAPI = sl::RenderAPI::eVulkan;
		// Manual hooking: DXVK owns the Vk device/instance; we hand them to SL via
		// slSetVulkanInfo rather than letting SL proxy vkCreateDevice. Frame-based
		// tagging is required by slEvaluateFeature / slSetTagForFrame.
		pref.flags |= sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		pref.featuresToLoad = featuresToLoad;
		pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(featuresToLoad));
		pref.pathsToPlugins = pluginPaths;
		pref.numPathsToPlugins = 1;
		pref.engine = sl::EngineType::eCustom;
		pref.engineVersion = "1.0";
		// projectId is expected to be a GUID string for the eCustom engine path.
		pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
		pref.logLevel = sl::LogLevel::eOff;
		pref.logMessageCallback = &LogCallback;
		pref.pathToLogsAndData = nullptr;  // no file logging

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

		// Hand DXVK's own Vulkan device/instance/queue to Streamline. DXVK exposes a
		// single graphics+compute queue; SL is told that family for both. The
		// optical-flow queue is left null — DLSS-G is not wired on DXVK (the queue +
		// VK_NV_optical_flow extension are vkCreateDevice-time properties DXVK owns).
		sl::VulkanInfo info{};
		info.device = dxvk->GetDevice();
		info.instance = dxvk->GetInstance();
		info.physicalDevice = dxvk->GetPhysicalDevice();
		info.graphicsQueueFamily = dxvk->GetQueueFamilyIndex();
		info.graphicsQueueIndex = 0;
		info.computeQueueFamily = dxvk->GetQueueFamilyIndex();
		info.computeQueueIndex = 0;

		const sl::Result res = g_sl.slSetVulkanInfo(info);
		if (res != sl::Result::eOk) {
			logger::warn("[Streamline] slSetVulkanInfo failed (result {})", static_cast<int>(res));
			return;
		}
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
		// DLSS-G is expected to be unsupported here: it needs an optical-flow queue
		// created at vkCreateDevice time, which DXVK does not provide. Probed for
		// completeness/logging only; it is never wired into the present path.
		featureDLSSG = supported(sl::kFeatureDLSS_G);

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

		logger::info("[Streamline] feature support: DLSS={} Reflex={} DLSS-G={} (DLSS-G unwired on DXVK)",
			featureDLSS, featureReflex, featureDLSSG);
	}

	void Streamline::Shutdown()
	{
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
				sl::FrameToken* token = nullptr;
				if (g_sl.slGetNewFrameToken(token, nullptr) == sl::Result::eOk && token)
					g_sl.slReflexSleep(*token);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_sl.dispatchFaulted = true;
			logger::error("[Streamline] Reflex dispatch faulted — disabling for this session");
		}
	}

	void Streamline::EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness)
	{
		// Best-effort DLSS super-resolution dispatch on DXVK's Vulkan device.
		//
		// NOTE: this path is NVIDIA-only and cannot be exercised on the current AMD
		// hardware (and ships without the SL plugin DLLs), so it is structurally
		// implemented from the SL VK spec but not runtime-validated. It is fully gated
		// (featureDLSS is false unless slIsFeatureSupported(kFeatureDLSS) returned eOk
		// on an NVIDIA adapter) and SEH-guarded so any fault latches the feature off
		// rather than crashing.
		if (!initialized || !featureDLSS || g_sl.dispatchFaulted)
			return;
		if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
			return;

		auto* dxvk = DxvkInterop::GetSingleton();
		if (!dxvk || !dxvk->IsAvailable() || !dxvk->CommandResourcesReady())
			return;

		(void)a_sharpness;  // sharpness is deprecated in DLSSOptions; RCAS handles sharpening.

		__try {
			// Map quality preset -> DLSS mode.
			sl::DLSSMode dlssMode = sl::DLSSMode::eMaxQuality;
			switch (a_qualityMode) {
			case 0: dlssMode = sl::DLSSMode::eMaxPerformance; break;
			case 1: dlssMode = sl::DLSSMode::eBalanced; break;
			case 2: dlssMode = sl::DLSSMode::eMaxQuality; break;
			case 3: dlssMode = sl::DLSSMode::eUltraPerformance; break;
			default: dlssMode = sl::DLSSMode::eMaxQuality; break;
			}

			sl::DLSSOptions options{};
			options.mode = dlssMode;
			options.outputWidth = a_outputWidth;
			options.outputHeight = a_outputHeight;
			options.colorBuffersHDR = sl::Boolean::eFalse;  // CS upscales the SDR scene; HDR composite happens later.
			if (g_sl.slDLSSSetOptions(g_sl.viewport, options) != sl::Result::eOk)
				return;

			sl::FrameToken* token = nullptr;
			if (g_sl.slGetNewFrameToken(token, nullptr) != sl::Result::eOk || !token)
				return;

			// Wrap each interop image as an SL Vulkan resource. The backing VkImages
			// belong to DXVK; SL records its compute work into the command buffer we
			// provide and we submit it under DXVK's queue lock (mirroring FidelityFX).
			const auto makeResource = [&](ID3D11Resource* a_res, sl::Resource& a_out, uint32_t a_w, uint32_t a_h) {
				VkImage image = VK_NULL_HANDLE;
				VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
				if (!dxvk->GetVkImage(a_res, &image, &layout, &info) || image == VK_NULL_HANDLE)
					return false;
				a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, nullptr, static_cast<uint32_t>(layout) };
				a_out.width = a_w;
				a_out.height = a_h;
				a_out.nativeFormat = static_cast<uint32_t>(info.format);
				a_out.mipLevels = info.mipLevels;
				a_out.arrayLayers = info.arrayLayers;
				a_out.usage = static_cast<uint32_t>(info.usage);
				a_out.flags = static_cast<uint32_t>(info.flags);
				return true;
			};

			sl::Resource colorInRes{}, colorOutRes{}, depthRes{}, mvecRes{};
			if (!makeResource(a_colorIn, colorInRes, a_renderWidth, a_renderHeight) ||
				!makeResource(a_colorOut, colorOutRes, a_outputWidth, a_outputHeight) ||
				!makeResource(a_depth, depthRes, a_renderWidth, a_renderHeight) ||
				!makeResource(a_motionVectors, mvecRes, a_renderWidth, a_renderHeight))
				return;

			sl::Extent renderExtent{};
			renderExtent.top = 0;
			renderExtent.left = 0;
			renderExtent.width = a_renderWidth;
			renderExtent.height = a_renderHeight;
			sl::Extent outputExtent{};
			outputExtent.top = 0;
			outputExtent.left = 0;
			outputExtent.width = a_outputWidth;
			outputExtent.height = a_outputHeight;
			sl::ResourceTag tags[] = {
				sl::ResourceTag{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
				sl::ResourceTag{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent },
				sl::ResourceTag{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
				sl::ResourceTag{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
			};

			VkCommandBuffer cmd = dxvk->BeginFrameCommandBuffer();
			if (cmd == VK_NULL_HANDLE)
				return;

			g_sl.slSetTagForFrame(*token, g_sl.viewport, tags, static_cast<uint32_t>(std::size(tags)), cmd);

			const sl::BaseStructure* inputs[] = { &g_sl.viewport };
			g_sl.slEvaluateFeature(sl::kFeatureDLSS, *token, inputs, static_cast<uint32_t>(std::size(inputs)), cmd);

			dxvk->SubmitFrameCommandBuffer(cmd, /*waitIdle=*/true);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_sl.dispatchFaulted = true;
			logger::error("[Streamline] DLSS dispatch faulted — disabling for this session");
		}
	}
}
