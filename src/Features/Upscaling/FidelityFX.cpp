#include "FidelityFX.h"

#include "../../DxvkLoader.h"
#include "../../State.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DxvkInterop.h"
#include "DxvkWsiHook.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace
{
	// Mirror of the FFX VK backend's getVKImageLayoutFromResourceState so we can
	// pre-transition DXVK interop images into the layout FFX assumes (FFX seeds its
	// first barrier oldLayout from the declared state).
	VkImageLayout FfxStateToLayout(uint32_t state)
	{
		switch (state) {
		case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:
		case FFX_API_RESOURCE_STATE_COMMON:
			return VK_IMAGE_LAYOUT_GENERAL;
		case FFX_API_RESOURCE_STATE_COPY_SRC:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case FFX_API_RESOURCE_STATE_COPY_DEST:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		default:
			// COMPUTE_READ / PIXEL_COMPUTE_READ / PIXEL_READ
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}

	// A DXVK interop image whose layout we moved for FFX and must restore afterwards
	// (DXVK requires interop surfaces be returned to their original layout before D3D11
	// uses them again). FFX restores resources to their declared-state layout at dispatch
	// end, so origLayout == FfxStateToLayout(state) is the layout FFX leaves it in.
	struct LayoutGuard
	{
		ID3D11Resource* resource;
		VkImageLayout original;
		VkImageLayout ffx;
		VkImageAspectFlags aspect;
	};

	// Wrap a D3D11 resource as an FFX VK resource (its backing VkImage) and queue the
	// layout transition needed before the FFX dispatch.
	FfxApiResource WrapAndPrepare(DxvkInterop* dxvk, ID3D11Resource* resource,
		uint32_t state, uint32_t usage, VkImageAspectFlags aspect, std::vector<LayoutGuard>& guards)
	{
		if (!resource)
			return ffxApiGetResourceVK(nullptr, {}, state);

		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		if (!dxvk->GetVkImage(resource, &image, &layout, &info) || image == VK_NULL_HANDLE)
			return ffxApiGetResourceVK(nullptr, {}, state);

		VkImageLayout ffxLayout = FfxStateToLayout(state);
		if (layout != ffxLayout) {
			dxvk->TransitionImageLayout(resource, layout, ffxLayout, aspect);
			guards.push_back({ resource, layout, ffxLayout, aspect });
		}

		FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(image, info, usage);
		return ffxApiGetResourceVK(reinterpret_cast<void*>(image), desc, state);
	}

	void RestoreLayouts(DxvkInterop* dxvk, std::vector<LayoutGuard>& guards)
	{
		// FFX leaves each resource in its declared-state (ffx) layout; move it back.
		for (auto it = guards.rbegin(); it != guards.rend(); ++it)
			dxvk->TransitionImageLayout(it->resource, it->ffx, it->original, it->aspect);
		guards.clear();
	}

	// --- DXVK ⇄ unmodified prebuilt-FFX VK backend shim -------------------------------------
	// The official amd_fidelityfx_vk.dll resolves every device function through the
	// vkGetDeviceProcAddr callback we hand it in ffxCreateBackendVKDesc. Two of those resolve
	// to NULL on DXVK, and the stock backend calls them without a null-check:
	//   • vkGetBufferMemoryRequirements2KHR — DXVK promoted VK_KHR_get_memory_requirements2 to
	//     core and dropped the KHR alias, so the KHR name is NULL. The breadcrumbs dedicated-
	//     allocation setup (enabled because DXVK enumerates VK_KHR_dedicated_allocation) calls
	//     it during ffxCreateContext → access violation. Route to the core entry point.
	//   • vkCmdWriteBufferMarkerAMD / 2AMD — DXVK enumerates VK_AMD_buffer_marker (so the
	//     backend turns on its breadcrumbs marker path) but leaves the entry points NULL. They
	//     are called at dispatch → access violation. Hand back a no-op stub so the debug-only
	//     breadcrumbs feature degrades to nothing instead of crashing.
	// The gating capability flags come from static vkEnumerateDeviceExtensionProperties calls
	// we cannot intercept, but the NULL calls themselves go through this callback — so shimming
	// it lets the UNMODIFIED prebuilt DLL run on DXVK with no patching.
	PFN_vkGetDeviceProcAddr g_realDeviceProcAddr = nullptr;

	// FFX FG-swapchain game-queue submit serialization.
	// In composeOnGameQueue mode the FG swapchain submits to the shared DXVK graphics queue from
	// TWO threads: interpolation from the calling (Present) thread, and UI composition from FFX's
	// internal present/pacing thread. Vulkan requires vkQueueSubmit on a single VkQueue to be
	// externally synchronized, so we route those submits through this leaf mutex. It is held only
	// across the submit call (no drain, no wait), so it can never deadlock.
	//
	// We deliberately do NOT serialize against DXVK's OWN renderer submits with DXVK's queue mutex
	// (m_mutexQueue). A DXVK fork exposing it (IDXGIVkInteropDevice2::LockDeviceQueue) was tried and
	// DEADLOCKS: DxvkSubmissionQueue::submitCmdLists holds m_mutexQueue across presentImage ->
	// vkQueuePresentKHR (dxvk_queue.cpp), which is OUR wrapped present -> FFX QueuePresent -> the
	// composition's game-queue submit -> LockDeviceQueue -> m_mutexQueue again (recursive on the
	// submit thread / cross-thread with FFX's pacing thread). DXVK calling our present while holding
	// its queue mutex makes that lock fundamentally unusable here. So we keep this leaf mutex: it
	// serializes FFX-vs-FFX, and FFX-vs-DXVK-internal relies on the driver's per-queue serialization
	// (validated stable across long runs + repeated FG toggles). The DXVK fork change was reverted.
	std::mutex g_ffxGameQueueSubmitMutex;
	PFN_vkQueueSubmit g_ffxRealQueueSubmit = nullptr;
	VkQueue g_ffxGameQueue = VK_NULL_HANDLE;

	VkResult FfxGameQueueSubmit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
	{
		if (!g_ffxRealQueueSubmit || g_ffxGameQueue == VK_NULL_HANDLE)
			return VK_ERROR_INITIALIZATION_FAILED;
		std::lock_guard<std::mutex> lock(g_ffxGameQueueSubmitMutex);
		return g_ffxRealQueueSubmit(g_ffxGameQueue, submitCount, pSubmits, fence);
	}

	VKAPI_ATTR void VKAPI_CALL Noop_vkCmdWriteBufferMarker(
		VkCommandBuffer, VkPipelineStageFlagBits, VkBuffer, VkDeviceSize, uint32_t) {}

	VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DxvkFfxGetDeviceProcAddr(VkDevice device, const char* pName)
	{
		if (!pName || !g_realDeviceProcAddr)
			return nullptr;
		if (std::strcmp(pName, "vkGetBufferMemoryRequirements2KHR") == 0) {
			if (auto p = g_realDeviceProcAddr(device, "vkGetBufferMemoryRequirements2KHR"))
				return p;
			return g_realDeviceProcAddr(device, "vkGetBufferMemoryRequirements2");
		}
		if (std::strcmp(pName, "vkCmdWriteBufferMarkerAMD") == 0 ||
			std::strcmp(pName, "vkCmdWriteBufferMarker2AMD") == 0)
			return reinterpret_cast<PFN_vkVoidFunction>(&Noop_vkCmdWriteBufferMarker);
		return g_realDeviceProcAddr(device, pName);
	}

	// SEH wrappers — kept free of any object that requires unwinding so __try is legal
	// (MSVC forbids __try in a function with C++ objects whose lifetime spans it). A
	// fault here is typically RenderDoc interfering with FFX; we degrade gracefully.
	static LONG LogFfxException(const char* where, _EXCEPTION_POINTERS* ep)
	{
		uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"CommunityShaders.dll"));
		uintptr_t addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
		// For a call/jump through a null pointer (addr==0), the return address sits at [Rsp].
		// Resolve its owning module so we see WHO made the null call (FFX DLL vs Vulkan vs us).
		uintptr_t caller = 0, callerRva = 0;
		char callerMod[64] = "?";
		if (ep->ContextRecord && ep->ContextRecord->Rsp) {
			caller = *reinterpret_cast<uintptr_t*>(ep->ContextRecord->Rsp);
			HMODULE m = nullptr;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(caller), &m) &&
				m) {
				wchar_t path[MAX_PATH]{};
				GetModuleFileNameW(m, path, MAX_PATH);
				const wchar_t* leaf = wcsrchr(path, L'\\');
				wcstombs_s(nullptr, callerMod, sizeof(callerMod), leaf ? leaf + 1 : path, _TRUNCATE);
				callerRva = caller - reinterpret_cast<uintptr_t>(m);
			}
		}
		logger::critical("[FidelityFX] {} FAULTED code=0x{:08X} addr=0x{:X} rva=0x{:X} caller=0x{:X} ({}+0x{:X})",
			where, (uint32_t)ep->ExceptionRecord->ExceptionCode, addr, addr >= base ? (addr - base) : 0,
			caller, callerMod, callerRva);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// Three generic SEH wrappers over the resolved FFX-API function-pointer table. One
	// SafeDispatch covers upscale + FG-prepare + FG dispatch (all are ffxDispatch with a
	// different header.type). Success is FFX_API_RETURN_OK (0).
	ffxReturnCode_t SafeCreateContext(const ffxFunctions* api, ffxContext* ctx, ffxCreateContextDescHeader* desc)
	{
		__try {
			return api->CreateContext(ctx, desc, nullptr);
		} __except (LogFfxException("ffxCreateContext", GetExceptionInformation())) {
			return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
		}
	}

	ffxReturnCode_t SafeDispatch(const ffxFunctions* api, ffxContext* ctx, const ffxDispatchDescHeader* desc)
	{
		__try {
			return api->Dispatch(ctx, desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
		}
	}

	ffxReturnCode_t SafeConfigure(const ffxFunctions* api, ffxContext* ctx, const ffxConfigureDescHeader* desc)
	{
		__try {
			return api->Configure(ctx, desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
		}
	}

	ffxReturnCode_t SafeQuery(const ffxFunctions* api, ffxContext* ctx, ffxQueryDescHeader* desc)
	{
		__try {
			return api->Query(ctx, desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
		}
	}

	void SafeDestroyContext(const ffxFunctions* api, ffxContext* ctx)
	{
		__try {
			api->DestroyContext(ctx, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}

	// ---- FFX VK frame-generation SWAPCHAIN support -----------------------------------------
	// The FG swapchain calls this per generated frame to run interpolation. It hands us a
	// pre-filled dispatch desc (its command list, the present color, the interpolation output);
	// we dispatch it on the frame-generation (interpolation) context, which already holds this
	// frame's prepared motion vectors / depth from the upscale-time prepare.
	ffxReturnCode_t FfxFrameGenDispatchCallback(ffxDispatchDescFrameGeneration* params, void* pUserCtx)
	{
		auto* self = static_cast<FidelityFX*>(pUserCtx);
		if (!self || !self->fgContext)
			return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
		return SafeDispatch(&self->ffxApi, &self->fgContext, &params->header);
	}

	// Surface FFX's own diagnostics (missing device features, pipeline failures, etc.).
	void FfxApiMessageCallback(uint32_t type, const wchar_t* message)
	{
		char buf[1024] = {};
		size_t converted = 0;
		wcstombs_s(&converted, buf, sizeof(buf), message ? message : L"", _TRUNCATE);
		if (type == FFX_API_MESSAGE_TYPE_ERROR)
			logger::error("[FidelityFX][FFX] {}", buf);
		else
			logger::warn("[FidelityFX][FFX] {}", buf);
	}

	// Frame-generation debug controls, read once from environment variables. Used to
	// diagnose the black interpolated frames:
	//   CS_FG_DEBUG_VIEW=1    FFX draws debug views into the interpolated output
	//   CS_FG_DEBUG_TEAR=1    FFX draws tear lines into the interpolated output
	//   CS_FG_DEBUG_PACING=1  FFX draws pacing lines into the generated output
	//   CS_FG_ONLY_INTERP=1   present only the interpolated frame (hide the real one)
	//   CS_FG_READBACK=1      log interpolated-frame pixel stats (all-black detection)
	struct FgDebug
	{
		uint32_t ffxFlags = 0;
		bool onlyInterpolated = false;
		bool readback = false;
	};

	const FgDebug& GetFgDebug()
	{
		static const FgDebug cfg = [] {
			auto on = [](const char* name) {
				char buf[16]{};
				size_t len = 0;
				getenv_s(&len, buf, sizeof(buf), name);
				return len > 1 && buf[0] != '0';
			};
			FgDebug c;
			// The FFX-API frame-gen flag bit positions differ from the native FSR3 flags;
			// remap explicitly rather than copying bit values.
			if (on("CS_FG_DEBUG_VIEW"))
				c.ffxFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;  // 1<<2
			if (on("CS_FG_DEBUG_TEAR"))
				c.ffxFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;  // 1<<0
			if (on("CS_FG_DEBUG_PACING"))
				c.ffxFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;  // 1<<4
			c.onlyInterpolated = on("CS_FG_ONLY_INTERP");
			c.readback = on("CS_FG_READBACK");
			if (c.ffxFlags || c.onlyInterpolated || c.readback)
				logger::info("[FidelityFX] FG debug: ffxFlags=0x{:X} onlyInterp={} readback={}",
					c.ffxFlags, c.onlyInterpolated, c.readback);
			return c;
		}();
		return cfg;
	}

	// Headless black-frame detector: copies the interpolated frame to a staging texture
	// and scans sampled bytes for any non-zero content. Logs throttled. Answers the key
	// question: is FFX producing a black frame, or is the present path blacking it?
	void LogInterpolatedStats(ID3D11Resource* tex)
	{
		static int counter = 0;
		if ((counter++ % 120) != 0 || !tex)
			return;

		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		D3D11_TEXTURE2D_DESC desc{};
		static_cast<ID3D11Texture2D*>(tex)->GetDesc(&desc);

		D3D11_TEXTURE2D_DESC sdesc = desc;
		sdesc.Usage = D3D11_USAGE_STAGING;
		sdesc.BindFlags = 0;
		sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sdesc.MiscFlags = 0;
		winrt::com_ptr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&sdesc, nullptr, staging.put())))
			return;
		context->CopyResource(staging.get(), tex);

		D3D11_MAPPED_SUBRESOURCE map{};
		if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map)))
			return;

		const uint8_t* base = static_cast<const uint8_t*>(map.pData);
		uint64_t nonZero = 0, sampled = 0;
		uint8_t maxByte = 0;
		for (uint32_t y = 0; y < desc.Height; y += 8) {
			const uint8_t* row = base + static_cast<size_t>(y) * map.RowPitch;
			for (uint32_t b = 0; b < map.RowPitch; b += 4) {
				++sampled;
				if (row[b]) {
					++nonZero;
					if (row[b] > maxByte)
						maxByte = row[b];
				}
			}
		}
		context->Unmap(staging.get(), 0);

		logger::info("[FG-READBACK] interp {}x{} fmt={} nonZero={}/{} maxByte={} -> {}",
			desc.Width, desc.Height, (int)desc.Format, nonZero, sampled, maxByte,
			nonZero == 0 ? "ALL BLACK" : "has content");
	}

	// Map the swap chain's DXGI format to the FFX surface format so the frame-interpolation
	// context's back-buffer format matches what we actually present.
	uint32_t DxgiToFfxApiSurfaceFormat(DXGI_FORMAT fmt)
	{
		switch (fmt) {
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		default:
			return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
		}
	}

	eastl::unique_ptr<Texture2D> MakeDisplayTexture(ID3D11Resource* templateTex, uint32_t w, uint32_t h, const char* name)
	{
		D3D11_TEXTURE2D_DESC tdesc{};
		static_cast<ID3D11Texture2D*>(templateTex)->GetDesc(&tdesc);

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = tdesc.Format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		auto tex = eastl::make_unique<Texture2D>(desc);
		Util::SetResourceName(tex->resource.get(), name);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = tdesc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		tex->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = tdesc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		tex->CreateUAV(uav);
		return tex;
	}
}

void FidelityFX::CreateFSRResources([[maybe_unused]] bool a_frameGeneration)
{
	if (upscaleContext || fgContext) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	// Resolve the 5 FFX-API entry points from the prebuilt signed DLL. It is staged into the
	// CS dxvk subfolder alongside DXVK. DxvkLoader loads its DLLs by ABSOLUTE path (the dxvk
	// subfolder is NOT on the loader search path), so load by absolute path here too.
	if (!ffxModule) {
		const auto ffxPath = (DxvkLoader::GetDxvkDir() / L"amd_fidelityfx_vk.dll").wstring();
		ffxModule = LoadLibraryExW(ffxPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!ffxModule) {
			logger::error("[FidelityFX] amd_fidelityfx_vk.dll not found in '{}' (err {}) — FSR (VK) cannot initialize", DxvkLoader::GetDxvkDir().string(), GetLastError());
			return;
		}
		ffxLoadFunctions(&ffxApi, ffxModule);
		if (!ffxApi.CreateContext || !ffxApi.DestroyContext || !ffxApi.Configure || !ffxApi.Dispatch) {
			logger::error("[FidelityFX] amd_fidelityfx_vk.dll missing FFX-API entry points");
			FreeLibrary(ffxModule);
			ffxModule = nullptr;
			return;
		}
		logger::info("[FidelityFX] Loaded official FFX-API DLL amd_fidelityfx_vk.dll");

		// Route the FFX-API's internal diagnostics (device-cap checks, pipeline failures) to our log.
		ffxConfigureDescGlobalDebug1 dbg{};
		dbg.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
		dbg.header.pNext = nullptr;
		dbg.fpMessage = FfxApiMessageCallback;
		dbg.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
		ffxApi.Configure(nullptr, &dbg.header);
	}

	auto* dxvk = DxvkInterop::GetSingleton();
	// Lazily bridge to DXVK's Vulkan device — CreateFSRResources can run before the
	// SetupResources probe, so don't rely on prior initialization. Idempotent.
	if (!dxvk->Initialize()) {
		logger::error("[FidelityFX] DXVK Vulkan device unavailable — FSR (VK) cannot initialize");
		return;
	}
	if (!dxvk->CreateCommandResources()) {
		logger::error("[FidelityFX] Failed to create command resources for FSR (VK)");
		return;
	}

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);
	uint32_t displayWidth = (uint32_t)screenSize.x;
	uint32_t displayHeight = (uint32_t)screenSize.y;
	uint32_t renderWidth = (uint32_t)renderSize.x;
	uint32_t renderHeight = (uint32_t)renderSize.y;

	const bool hdr = globals::features::hdrDisplay.loaded;

	// FG back-buffer format = actual swap-chain format (independent of HDR Display being on).
	uint32_t backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
	{
		DXGI_SWAP_CHAIN_DESC scDesc{};
		if (globals::d3d::swapChain && SUCCEEDED(globals::d3d::swapChain->GetDesc(&scDesc)))
			backBufferFormat = DxgiToFfxApiSurfaceFormat(scDesc.BufferDesc.Format);
	}

	// One backend-VK desc per create-context call. Pointers need only live for the call.
	auto makeBackendDesc = [&]() {
		ffxCreateBackendVKDesc d{};
		d.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;  // 0x3
		d.header.pNext = nullptr;
		d.vkDevice = dxvk->GetDevice();
		d.vkPhysicalDevice = dxvk->GetPhysicalDevice();
		// Route the backend's device-function resolution through our DXVK-compat shim so the
		// unmodified prebuilt DLL doesn't call DXVK's NULL aliases (see DxvkFfxGetDeviceProcAddr).
		g_realDeviceProcAddr = dxvk->GetDeviceProcAddr();
		d.vkDeviceProcAddr = &DxvkFfxGetDeviceProcAddr;
		return d;
	};

	// ffxCreateContext flushes internal GPU init jobs that record + SUBMIT onto DXVK's shared
	// graphics queue. Hold DXVK's submission lock (after flushing its pending D3D11 work).
	dxvk->FlushRenderingCommands();
	dxvk->LockSubmissionQueue();

	logger::info("[FidelityFX] DXVK VK handles: device=0x{:X} physicalDevice=0x{:X} procAddr=0x{:X}",
		(uintptr_t)dxvk->GetDevice(), (uintptr_t)dxvk->GetPhysicalDevice(), (uintptr_t)dxvk->GetDeviceProcAddr());

	// (1) Upscale context.
	ffxCreateBackendVKDesc upscaleBackend = makeBackendDesc();
	ffxCreateContextDescUpscale upscaleDesc{};
	upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;  // 0x10000
	upscaleDesc.header.pNext = &upscaleBackend.header;
	upscaleDesc.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
	if (hdr)
		upscaleDesc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
	upscaleDesc.maxRenderSize = { renderWidth, renderHeight };
	upscaleDesc.maxUpscaleSize = { displayWidth, displayHeight };
	upscaleDesc.fpMessage = FfxApiMessageCallback;
	ffxReturnCode_t rcUpscale = SafeCreateContext(&ffxApi, &upscaleContext, &upscaleDesc.header);

	// (2) Frame-generation context.
	ffxReturnCode_t rcFg = FFX_API_RETURN_OK;
	if (rcUpscale == FFX_API_RETURN_OK) {
		ffxCreateBackendVKDesc fgBackend = makeBackendDesc();
		ffxCreateContextDescFrameGeneration fgDesc{};
		fgDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;  // 0x20001
		fgDesc.header.pNext = &fgBackend.header;
		fgDesc.flags = 0;
		if (hdr)
			fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;  // 1<<5
		fgDesc.displaySize = { displayWidth, displayHeight };
		fgDesc.maxRenderSize = { renderWidth, renderHeight };
		fgDesc.backBufferFormat = backBufferFormat;
		rcFg = SafeCreateContext(&ffxApi, &fgContext, &fgDesc.header);
	}

	dxvk->ReleaseSubmissionQueue();

	if (rcUpscale != FFX_API_RETURN_OK || rcFg != FFX_API_RETURN_OK) {
		logger::critical("[FidelityFX] ffxCreateContext (VK) failed: upscale=0x{:08X} fg=0x{:08X}",
			(uint32_t)rcUpscale, (uint32_t)rcFg);
		if (upscaleContext) {
			ffxApi.DestroyContext(&upscaleContext, nullptr);
			upscaleContext = nullptr;
		}
		if (fgContext) {
			ffxApi.DestroyContext(&fgContext, nullptr);
			fgContext = nullptr;
		}
		if (dxvk->IsAvailable())
			dxvk->DestroyCommandResources();
		return;
	}
	contextCreated = true;

	// CS-owned display-resolution output + interpolated frame. The context is always
	// frame-gen capable; whether interpolation is dispatched is gated per-frame by the
	// user setting. interpolatedTexture is small relative to the FFX-internal buffers.
	auto& main = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	upscaledTexture = MakeDisplayTexture(main.texture, displayWidth, displayHeight, "FidelityFX::UpscaledColor").release();
	// Frame-gen textures match the swap chain (back buffer) format and size so the interpolated
	// frame and the captured real frame copy straight to/from the back buffer with no conversion.
	winrt::com_ptr<ID3D11Texture2D> backBuffer;
	if (globals::d3d::swapChain && SUCCEEDED(globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()))) && backBuffer) {
		D3D11_TEXTURE2D_DESC bbDesc{};
		backBuffer->GetDesc(&bbDesc);
		interpolatedTexture = MakeDisplayTexture(backBuffer.get(), bbDesc.Width, bbDesc.Height, "FidelityFX::InterpolatedFrame").release();
		fgPresentColor = MakeDisplayTexture(backBuffer.get(), bbDesc.Width, bbDesc.Height, "FidelityFX::FGPresentColor").release();
		hudlessColor = MakeDisplayTexture(backBuffer.get(), bbDesc.Width, bbDesc.Height, "FidelityFX::HudlessColor").release();
	} else {
		logger::critical("[FidelityFX] No swap chain back buffer available; frame generation disabled.");
	}

	frameGenContextActive = true;
	frameID = 0;
	logger::info("[FidelityFX] Created FFX-API upscale+FG contexts on DXVK VkDevice (Display: {}x{}, Render: {}x{})",
		displayWidth, displayHeight, renderWidth, renderHeight);
}

void FidelityFX::DestroyFSRResources()
{
	auto* dxvk = DxvkInterop::GetSingleton();

	if (contextCreated) {
		if (upscaleContext && ffxApi.DestroyContext && ffxApi.DestroyContext(&upscaleContext, nullptr) != FFX_API_RETURN_OK)
			logger::critical("[FidelityFX] Failed to destroy FFX upscale context!");
		if (fgContext && ffxApi.DestroyContext && ffxApi.DestroyContext(&fgContext, nullptr) != FFX_API_RETURN_OK)
			logger::critical("[FidelityFX] Failed to destroy FFX frame-generation context!");
	}
	upscaleContext = nullptr;
	fgContext = nullptr;
	contextCreated = false;

	auto destroyTex = [](Texture2D*& t) {
		if (t) {
			t->srv = nullptr;
			t->uav = nullptr;
			t->resource = nullptr;
			delete t;
			t = nullptr;
		}
	};
	destroyTex(upscaledTexture);
	destroyTex(interpolatedTexture);
	destroyTex(fgPresentColor);
	destroyTex(hudlessColor);

	if (dxvk->IsAvailable())
		dxvk->DestroyCommandResources();

	frameGenContextActive = false;
	currentCommandBuffer = VK_NULL_HANDLE;
	fsrDispatchCrashLogged = false;
	fgDispatchCrashLogged = false;
	// ffxModule intentionally kept loaded for the process lifetime.
}

void FidelityFX::CaptureHudlessFromBackBuffer()
{
	// Snapshot the current back buffer before the UI is drawn -> the HUD-less scene, in swapchain
	// format (matches fgPresentColor). Used as HUDLessColor for frame gen when HDR is OFF (HDR on
	// produces it via HDRDisplay's composite into GetHudlessUAV()). Frame generation owns the
	// resource now; null-safe before the FG context exists / when FG is inactive.
	if (!hudlessColor || !hudlessColor->resource || !globals::d3d::swapChain)
		return;

	winrt::com_ptr<ID3D11Texture2D> backBuffer;
	if (SUCCEEDED(globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()))) && backBuffer)
		globals::d3d::context->CopyResource(hudlessColor->resource.get(), backBuffer.get());
}

void FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->IsAvailable() || !dxvk->CommandResourcesReady() || !upscaledTexture || !upscaleContext)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);
	auto jitter = globals::features::upscaling.jitter;

	if (state->frameAnnotations)
		state->BeginPerfEvent("FSR VK Dispatch");

	VkCommandBuffer cb = dxvk->BeginFrameCommandBuffer();
	if (cb == VK_NULL_HANDLE) {
		if (state->frameAnnotations)
			state->EndPerfEvent();
		return;
	}
	currentCommandBuffer = cb;

	std::vector<LayoutGuard> guards;
	void* commandList = reinterpret_cast<void*>(cb);  // raw VkCommandBuffer; the FFX-API takes it directly

	ffxDispatchDescUpscale dp{};
	dp.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;  // 0x10001
	dp.commandList = commandList;
	dp.color = WrapAndPrepare(dxvk, a_upscalingTexture, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dp.depth = WrapAndPrepare(dxvk, depthTexture.texture, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_DEPTHTARGET, VK_IMAGE_ASPECT_DEPTH_BIT, guards);
	dp.motionVectors = WrapAndPrepare(dxvk, a_motionVectors, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dp.reactive = WrapAndPrepare(dxvk, a_reactiveMask, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dp.transparencyAndComposition = WrapAndPrepare(dxvk, a_transparencyCompositionMask, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dp.exposure = ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMPUTE_READ);
	dp.output = WrapAndPrepare(dxvk, upscaledTexture->resource.get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV, VK_IMAGE_ASPECT_COLOR_BIT, guards);

	dp.motionVectorScale = { renderSize.x, renderSize.y };
	dp.renderSize = { (uint32_t)renderSize.x, (uint32_t)renderSize.y };
	dp.upscaleSize = { (uint32_t)screenSize.x, (uint32_t)screenSize.y };
	dp.jitterOffset = { -jitter.x, -jitter.y };
	dp.frameTimeDelta = *globals::game::deltaTime * 1000.f;
	dp.cameraFar = *globals::game::cameraFar;
	dp.cameraNear = *globals::game::cameraNear;
	dp.enableSharpening = true;
	dp.sharpness = a_sharpness;
	dp.cameraFovAngleVertical = Util::GetVerticalFOVRad();
	dp.viewSpaceToMetersFactor = 0.01428222656f;
	dp.reset = false;
	dp.preExposure = 1.0f;
	dp.flags = 0;
	// NOTE: ffxDispatchDescUpscale has NO frameID field (unlike the native FSR3 upscale desc).

	bool dispatched = SafeDispatch(&ffxApi, &upscaleContext, &dp.header) == FFX_API_RETURN_OK;
	if (!dispatched && !fsrDispatchCrashLogged) {
		logger::critical("[FidelityFX] ffxDispatch(upscale, VK) failed or faulted.");
		fsrDispatchCrashLogged = true;
	}

	// FG-prepare on the FG context: records this frame's dilated depth/MV + camera/renderSize
	// that the present-time interpolation dispatch reads. Same frameID/cb as the upscale.
	if (dispatched && frameGenContextActive && fgContext) {
		ffxDispatchDescFrameGenerationPrepare prep{};
		prep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;  // 0x20004
		prep.frameID = frameID;
		prep.flags = 0;
		prep.commandList = commandList;
		prep.renderSize = dp.renderSize;
		prep.jitterOffset = dp.jitterOffset;
		prep.motionVectorScale = dp.motionVectorScale;
		prep.frameTimeDelta = dp.frameTimeDelta;
		prep.unused_reset = false;
		prep.cameraNear = dp.cameraNear;
		prep.cameraFar = dp.cameraFar;
		prep.cameraFovAngleVertical = dp.cameraFovAngleVertical;
		prep.viewSpaceToMetersFactor = dp.viewSpaceToMetersFactor;
		prep.depth = dp.depth;
		prep.motionVectors = dp.motionVectors;

		const bool prepared = SafeDispatch(&ffxApi, &fgContext, &prep.header) == FFX_API_RETURN_OK;
		if (!prepared && !fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] ffxDispatch(FG-prepare, VK) failed.");
			fsrDispatchCrashLogged = true;
		}
		// The present-time interpolation dispatch may only run when this frame was prepared.
		fgPreparedThisFrame = prepared;
	}

	// Submit the upscale (wait so the result is ready), THEN restore layouts. The layout
	// transitions run on DXVK's context and are flushed ahead of our queue submit, so the
	// restore MUST come after the FFX work has actually executed — otherwise FFX reads the
	// resources in the wrong (already-restored) layout and the GPU faults.
	dxvk->SubmitFrameCommandBuffer(cb, /*waitIdle*/ true);
	RestoreLayouts(dxvk, guards);
	currentCommandBuffer = VK_NULL_HANDLE;
	if (dispatched)
		context->CopyResource(a_upscalingTexture, upscaledTexture->resource.get());

	// Advance the frame id here only in upscaling-only mode; with frame generation the
	// FG dispatch (which shares this frame's id to read the prepared inputs) advances it.
	if (!frameGenContextActive)
		++frameID;

	if (state->frameAnnotations)
		state->EndPerfEvent();
}

void FidelityFX::UpscaleAndGenerate(ID3D11Resource* a_color, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	if (dispatchFaulted)
		return;
	// __try here contains only the dispatch CALL (no C++ unwinding objects in this scope; the
	// std::vector layout guards live inside Upscale), so SEH is legal. Frame interpolation is
	// dispatched later at present time (GenerateInterpolatedFrame); here we only upscale and
	// record the frame-gen prepare descriptions.
	__try {
		Upscale(a_color, a_reactiveMask, a_transparencyCompositionMask, a_motionVectors, a_sharpness);
	} __except (LogFfxException("FSR VK dispatch", GetExceptionInformation())) {
		dispatchFaulted = true;
		logger::critical("[FidelityFX] FSR VK dispatch faulted — disabling FSR VK dispatch this session to avoid repeated crashes");
	}
}

bool FidelityFX::GenerateInterpolatedFrame(ID3D11Resource* a_backBuffer, ID3D11Resource* a_hudlessColor, bool a_isHDR, float a_peakNits, uint32_t a_debugFlags)
{
	if (dispatchFaulted || !frameGenContextActive || !interpolatedTexture || !fgPresentColor || !a_backBuffer)
		return false;

	// Only interpolate when this exact frame was prepared by the upscale (matching frameID).
	// Presents without a fresh upscale (menus, loading) would otherwise dispatch on stale inputs.
	if (!fgPreparedThisFrame)
		return false;
	fgPreparedThisFrame = false;

	bool produced = false;
	// SEH-guard the present-time VK dispatch just like the upscale path.
	__try {
		// Snapshot the real, fully-composited back buffer: it is both the presentColor fed to
		// interpolation and the source used to restore the real frame after we present the
		// interpolated one. CopyResource handles the swap-chain image's layout.
		globals::d3d::context->CopyResource(fgPresentColor->resource.get(), a_backBuffer);
		produced = DispatchFrameGeneration(fgPresentColor->resource.get(), a_hudlessColor, a_isHDR, a_peakNits, a_debugFlags);
	} __except (LogFfxException("FSR VK frame-gen dispatch", GetExceptionInformation())) {
		dispatchFaulted = true;
		logger::critical("[FidelityFX] FSR VK frame-gen dispatch faulted — disabling frame generation this session");
		produced = false;
	}
	return produced;
}

void FidelityFX::BlitInterpolatedToBackBuffer(IDXGISwapChain* a_swapChain)
{
	if (!a_swapChain || !interpolatedTexture)
		return;
	winrt::com_ptr<ID3D11Texture2D> backBuffer;
	if (SUCCEEDED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()))) && backBuffer)
		globals::d3d::context->CopyResource(backBuffer.get(), interpolatedTexture->resource.get());
}

void FidelityFX::BlitRealToBackBuffer(IDXGISwapChain* a_swapChain)
{
	if (!a_swapChain || !fgPresentColor)
		return;
	winrt::com_ptr<ID3D11Texture2D> backBuffer;
	if (SUCCEEDED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()))) && backBuffer)
		globals::d3d::context->CopyResource(backBuffer.get(), fgPresentColor->resource.get());
}

bool FidelityFX::DispatchFrameGeneration(ID3D11Resource* a_presentColor, ID3D11Resource* a_hudlessColor, bool a_isHDR, float a_peakNits, uint32_t a_debugFlags)
{
	if (!frameGenContextActive || !fgContext || !interpolatedTexture)
		return false;

	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->IsAvailable() || !dxvk->CommandResourcesReady())
		return false;

	auto state = globals::state;

	if (state->frameAnnotations)
		state->BeginPerfEvent("FSR VK Frame Generation");

	// Own command buffer (the upscale already submitted and stored its dilated depth/MVs
	// in the FG context). presentColor is the upscaled real frame, now in a_presentColor.
	VkCommandBuffer cb = dxvk->BeginFrameCommandBuffer();
	if (cb == VK_NULL_HANDLE) {
		if (state->frameAnnotations)
			state->EndPerfEvent();
		return false;
	}

	std::vector<LayoutGuard> guards;
	bool produced = false;
	void* commandList = reinterpret_cast<void*>(cb);

	// PATH B (dispatch-only): NO swapchain + NO_SWAPCHAIN_CONTEXT_NOTIFY so the context only
	// runs interpolation and never touches a swapchain (DXVK owns it). With that flag set the
	// null-swapchain configure returns OK, so any non-OK is genuinely unexpected.
	{
		ffxConfigureDescFrameGeneration cfg{};
		cfg.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;  // 0x20002
		cfg.swapChain = nullptr;
		cfg.presentCallback = nullptr;
		cfg.presentCallbackUserContext = nullptr;
		cfg.frameGenerationCallback = nullptr;
		cfg.frameGenerationCallbackUserContext = nullptr;
		cfg.frameGenerationEnabled = true;
		cfg.allowAsyncWorkloads = false;
		// HUDLessColor (scene without UI): lets FFX detect + suppress UI-region interpolation so the
		// UI is preserved from presentColor instead of ghosted. Swapchain format, matches presentColor.
		cfg.HUDLessColor = a_hudlessColor ?
		                       WrapAndPrepare(dxvk, a_hudlessColor, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards) :
		                       ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMPUTE_READ);
		cfg.flags = a_debugFlags | GetFgDebug().ffxFlags | FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;  // | 1<<3
		cfg.onlyPresentGenerated = false;
		cfg.generationRect = { 0, 0, (int32_t)globals::game::graphicsState->screenWidth, (int32_t)globals::game::graphicsState->screenHeight };
		cfg.frameID = frameID;

		const ffxReturnCode_t cfgErr = SafeConfigure(&ffxApi, &fgContext, &cfg.header);
		if (cfgErr != FFX_API_RETURN_OK && !fgDispatchCrashLogged) {
			logger::critical("[FidelityFX] ffxConfigure(FG, VK) unexpected error: 0x{:08X}", (uint32_t)cfgErr);
			fgDispatchCrashLogged = true;
		}
	}

	{
		ffxDispatchDescFrameGeneration fg{};
		fg.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;  // 0x20003
		fg.commandList = commandList;
		fg.presentColor = WrapAndPrepare(dxvk, a_presentColor, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
		fg.outputs[0] = WrapAndPrepare(dxvk, interpolatedTexture->resource.get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV, VK_IMAGE_ASPECT_COLOR_BIT, guards);
		fg.numGeneratedFrames = 1;
		fg.reset = false;
		fg.frameID = frameID;
		fg.generationRect = { 0, 0, (int32_t)globals::game::graphicsState->screenWidth, (int32_t)globals::game::graphicsState->screenHeight };
		if (a_isHDR) {
			fg.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
			fg.minMaxLuminance[0] = 0.0f;
			fg.minMaxLuminance[1] = a_peakNits;
		} else {
			fg.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
			fg.minMaxLuminance[0] = 0.0f;
			fg.minMaxLuminance[1] = 1.0f;
		}
		produced = SafeDispatch(&ffxApi, &fgContext, &fg.header) == FFX_API_RETURN_OK;
		if (!produced && !fgDispatchCrashLogged) {
			logger::critical("[FidelityFX] ffxDispatch(FG, VK) failed or faulted.");
			fgDispatchCrashLogged = true;
		}
	}

	dxvk->SubmitFrameCommandBuffer(cb, /*waitIdle*/ true);
	RestoreLayouts(dxvk, guards);

	++frameID;

	// Headless black-frame diagnostic (CS_FG_READBACK): inspect what FFX actually wrote.
	if (produced && GetFgDebug().readback)
		LogInterpolatedStats(interpolatedTexture->resource.get());

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return produced;
}

bool FidelityFX::DebugOnlyInterpolated() const
{
	return GetFgDebug().onlyInterpolated;
}

// ===========================================================================================
// IFrameGenProvider: the proper FFX VK frame-generation SWAPCHAIN path.
// DxvkWsiHook injects our queues at vkCreateDevice and routes DXVK's WSI here when FG is on.
// ===========================================================================================

FrameGenDeviceRequirements FidelityFX::GetDeviceRequirements(VkPhysicalDevice)
{
	FrameGenDeviceRequirements req;
	req.presentCapableQueues = 2;  // FFX needs a present + an image-acquire queue, distinct from game
	return req;
}

void FidelityFX::OnDeviceCreated(VkPhysicalDevice, VkDevice, const FrameGenQueues& queues)
{
	fgQueues = queues;
}

bool FidelityFX::WantsToWrap() const
{
	// Wrap DXVK's swapchain only when FG is actually active and we have the injected queues.
	// (Re-evaluated at every swapchain (re)creation; toggling FG triggers a recreate.)
	return globals::features::upscaling.IsFrameGenerationActive() &&
	       fgQueues.presentQueue != VK_NULL_HANDLE &&
	       fgQueues.imageAcquireQueue != VK_NULL_HANDLE &&
	       ffxModule != nullptr && ffxApi.CreateContext != nullptr;
}

VkResult FidelityFX::CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
	auto* dxvk = DxvkInterop::GetSingleton();
	if (!dxvk->IsAvailable() || !ffxApi.CreateContext || fgQueues.presentQueue == VK_NULL_HANDLE)
		return VK_ERROR_INITIALIZATION_FAILED;

	*pSwapchain = VK_NULL_HANDLE;

	// gameQueue = DXVK's graphics queue (shared with DXVK's renderer). submitFunc serializes FFX's
	// own two-threaded game-queue access (interpolation on the calling thread + composition on FFX's
	// present thread) through a leaf mutex — see FfxGameQueueSubmit. We deliberately do NOT route it
	// through DXVK's LockSubmissionQueue: that calls synchronize() (drains DXVK's submit queue +
	// holds its queue mutex), which deadlocks when invoked from FFX's present thread.
	VkQueueInfoFFXAPI gameQ{};
	gameQ.queue = dxvk->GetQueue();
	gameQ.familyIndex = dxvk->GetQueueFamilyIndex();
	g_ffxGameQueue = gameQ.queue;
	if (!g_ffxRealQueueSubmit && dxvk->GetDeviceProcAddr())
		g_ffxRealQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(dxvk->GetDeviceProcAddr()(device, "vkQueueSubmit"));
	gameQ.submitFunc = g_ffxRealQueueSubmit ? &FfxGameQueueSubmit : nullptr;
	// present + image-acquire = the injected queues (exclusive to FFX; no submit serialization).
	VkQueueInfoFFXAPI presentQ{};
	presentQ.queue = fgQueues.presentQueue;
	presentQ.familyIndex = fgQueues.presentFamily;
	VkQueueInfoFFXAPI acquireQ{};
	acquireQ.queue = fgQueues.imageAcquireQueue;
	acquireQ.familyIndex = fgQueues.imageAcquireFamily;
	VkQueueInfoFFXAPI asyncQ{};  // optional; left null (no async compute queue on this device)

	// CONFIRM + guard: FFX's swapchain init fails with 0x3 if the present queue family can't present to
	// this surface. The WSI hook now sources present/acquire from the game family (which DXVK presents
	// on), so this should be VK_TRUE — log it so a regression is immediately visible.
	if (auto gipa = dxvk->GetInstanceProcAddr()) {
		auto vkGPDSS = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
			gipa(dxvk->GetInstance(), "vkGetPhysicalDeviceSurfaceSupportKHR"));
		if (vkGPDSS && pCreateInfo->surface) {
			VkBool32 presentOK = VK_FALSE;
			vkGPDSS(dxvk->GetPhysicalDevice(), presentQ.familyIndex, pCreateInfo->surface, &presentOK);
			logger::info("[FidelityFX] FG swapchain: present family {} surface-present support = {}",
				presentQ.familyIndex, (bool)presentOK);
		}
	}

	// The present family is now the game family (graphics-capable). Composition could run on the present
	// queue; keep it on the game queue (composeOnPresentQueue = false) while confirming the fix.
	ffxCreateContextDescFrameGenerationSwapChainModeVK mode{};
	mode.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK;
	mode.header.pNext = nullptr;
	mode.composeOnPresentQueue = false;

	ffxCreateContextDescFrameGenerationSwapChainVK desc{};
	desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
	desc.header.pNext = &mode.header;
	desc.physicalDevice = dxvk->GetPhysicalDevice();
	desc.device = device;
	desc.swapchain = pSwapchain;  // in: VK_NULL_HANDLE -> out: the wrapped handle
	desc.allocator = const_cast<VkAllocationCallbacks*>(pAllocator);
	desc.createInfo = *pCreateInfo;
	// FFX's swapchain init (getRealSwapchainCreateInfo) hard-fails with 0x3 on ANY createInfo.pNext
	// struct outside its small whitelist. DXVK chains extension structs (e.g. present-modes /
	// present-scaling from VK_EXT_swapchain_maintenance1) that FFX doesn't recognize. Strip the whole
	// chain — FFX builds a plain swapchain; format/colorspace come from direct fields, and the wrapped
	// present doesn't need DXVK's VRR / fullscreen-exclusive extension structs.
	for (auto* p = reinterpret_cast<const VkBaseInStructure*>(pCreateInfo->pNext); p; p = p->pNext)
		logger::info("[FidelityFX] FG swapchain: stripping createInfo.pNext sType={}", (uint32_t)p->sType);
	desc.createInfo.pNext = nullptr;
	desc.gameQueue = gameQ;
	desc.asyncComputeQueue = asyncQ;
	desc.presentQueue = presentQ;
	desc.imageAcquireQueue = acquireQ;

	// FFX records + submits swapchain init work; hold DXVK's lock around the create.
	dxvk->FlushRenderingCommands();
	dxvk->LockSubmissionQueue();
	ffxReturnCode_t rc = SafeCreateContext(&ffxApi, &fgSwapchainContext, &desc.header);
	dxvk->ReleaseSubmissionQueue();
	if (rc != FFX_API_RETURN_OK || !fgSwapchainContext) {
		logger::error("[FidelityFX] FG swapchain CreateContext failed: 0x{:08X}", (uint32_t)rc);
		fgSwapchainContext = nullptr;
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// Query FFX's replacement WSI functions (DXVK's acquire/getimages/present route to these).
	fgSwapchainFns = {};
	fgSwapchainFns.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
	fgSwapchainFns.header.pNext = nullptr;
	if (SafeQuery(&ffxApi, &fgSwapchainContext, &fgSwapchainFns.header) != FFX_API_RETURN_OK ||
		!fgSwapchainFns.pOutQueuePresentKHR) {
		logger::error("[FidelityFX] FG swapchain replacement-function query failed");
		SafeDestroyContext(&ffxApi, &fgSwapchainContext);
		fgSwapchainContext = nullptr;
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// The wrapped VkSwapchainKHR FFX wrote back into *pSwapchain IS the FrameInterpolationSwapChainVK
	// object; ffxConfigure(frameGeneration).swapChain must point to THAT (FFX reinterpret_casts it),
	// not the ffx-api context handle. Passing the context handle crashes inside FFX (returns 3).
	fgWrappedSwapchain = *pSwapchain;

	// Link the interpolation (FG) context to this swapchain and register our dispatch callback so
	// the swapchain drives interpolation per generated frame. (Per-frame frameID/HUDless state is
	// updated from the upscale path.)
	{
		ffxConfigureDescFrameGeneration cfg{};
		cfg.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		cfg.swapChain = reinterpret_cast<void*>(fgWrappedSwapchain);
		cfg.frameGenerationCallback = &FfxFrameGenDispatchCallback;
		cfg.frameGenerationCallbackUserContext = this;
		// Start DISABLED: enabling here (before any FG-prepare has run) spawns FFX's presenter
		// thread and lets it interpolate on empty prepared inputs -> GPU device-lost. The per-present
		// SetFrameGenForPresent enables it only on frames an upscale actually prepared (the canonical
		// sample likewise configures enabled per-frame, never force-true at create).
		cfg.frameGenerationEnabled = false;
		cfg.allowAsyncWorkloads = false;
		cfg.flags = GetFgDebug().ffxFlags;
		cfg.generationRect = { 0, 0, (int32_t)pCreateInfo->imageExtent.width, (int32_t)pCreateInfo->imageExtent.height };
		cfg.frameID = frameID;
		SafeConfigure(&ffxApi, &fgContext, &cfg.header);
	}

	logger::info("[FidelityFX] FFX FG swapchain created + linked ({}x{}, wrapped handle 0x{:X})",
		pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, (uintptr_t)*pSwapchain);
	return VK_SUCCESS;
}

VkResult FidelityFX::GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pCount, VkImage* pImages)
{
	return fgSwapchainFns.pOutGetSwapchainImagesKHR ?
	           fgSwapchainFns.pOutGetSwapchainImagesKHR(device, swapchain, pCount, pImages) :
	           VK_ERROR_INITIALIZATION_FAILED;
}

VkResult FidelityFX::AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
	VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
	return fgSwapchainFns.pOutAcquireNextImageKHR ?
	           fgSwapchainFns.pOutAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex) :
	           VK_ERROR_INITIALIZATION_FAILED;
}

VkResult FidelityFX::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
	return fgSwapchainFns.pOutQueuePresentKHR ?
	           fgSwapchainFns.pOutQueuePresentKHR(queue, pPresentInfo) :
	           VK_ERROR_INITIALIZATION_FAILED;
}

void FidelityFX::SetFrameGenForPresent(bool a_enabled, ID3D11Resource* a_hudlessColor)
{
	if (!fgSwapchainContext || !fgContext)
		return;
	auto* dxvk = DxvkInterop::GetSingleton();

	// HUD-less scene (back-buffer format, no UI). FFX diffs it against the presented frame to
	// detect the UI region and present the real UI over each generated frame instead of
	// interpolating it (which ghosts). Only needed while actually interpolating.
	FfxApiResource hudless = ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMPUTE_READ);
	if (a_enabled && a_hudlessColor && dxvk && dxvk->IsAvailable()) {
		// Queue the transition into shader-read on DXVK's context (WrapAndPrepare reads DXVK's
		// current layout and barriers to it). Ordering is implicit and free: we are inside the
		// present hook, before a_present, so this barrier is the last thing recorded on DXVK's
		// context this frame — DXVK's command stream submits it ahead of vkQueuePresentKHR (and
		// therefore ahead of FFX's interpolation, which reads HUDLessColor on the game queue from
		// within that present). No explicit drain needed (which would stall every generated frame).
		// We deliberately do NOT restore the layout: FFX leaves it in shader-read, and next frame's
		// D3D11 write to the hud-less texture transitions out of it (DXVK tracked the move).
		std::vector<LayoutGuard> guards;
		hudless = WrapAndPrepare(dxvk, a_hudlessColor, FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	}

	// Debug-draw flags: the in-game toggles (Upscaling settings) OR the env-var overrides. With
	// DRAW_DEBUG_VIEW set, FFX forces onlyPresentInterpolated, so every presented frame becomes the
	// debug visualization — a positive on-screen proof that interpolation actually ran.
	uint32_t dbgFlags = GetFgDebug().ffxFlags;
	const auto& s = globals::features::upscaling.settings;
	if (s.fgDebugView)
		dbgFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;
	if (s.fgDebugTearLines)
		dbgFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;
	if (s.fgDebugPacingLines)
		dbgFlags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;

	ffxConfigureDescFrameGeneration cfg{};
	cfg.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
	cfg.swapChain = reinterpret_cast<void*>(fgWrappedSwapchain);
	cfg.frameGenerationCallback = &FfxFrameGenDispatchCallback;
	cfg.frameGenerationCallbackUserContext = this;
	cfg.frameGenerationEnabled = a_enabled;
	cfg.allowAsyncWorkloads = false;
	cfg.HUDLessColor = hudless;
	cfg.flags = dbgFlags;
	// "Show Only Generated Frames": present the interpolated frame and SKIP the real one, so the
	// display rate drops to the generated cadence (~halves it) — the in-game toggle + the
	// CS_FG_ONLY_INTERP env both feed this. Was never wired to the swapchain path before (it only
	// affected the legacy dispatch-only fallback), so toggling it did nothing here.
	cfg.onlyPresentGenerated = s.fgShowOnlyGenerated || GetFgDebug().onlyInterpolated;
	cfg.generationRect = { 0, 0, (int32_t)globals::game::graphicsState->screenWidth, (int32_t)globals::game::graphicsState->screenHeight };
	cfg.frameID = frameID;
	SafeConfigure(&ffxApi, &fgContext, &cfg.header);
}

void FidelityFX::DestroySwapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*)
{
	// Unlink the interpolation context, then tear down the FG swapchain (stops its present +
	// interpolation threads and destroys the real VkSwapchainKHR it owns).
	if (fgContext) {
		ffxConfigureDescFrameGeneration cfg{};
		cfg.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		cfg.swapChain = nullptr;
		cfg.frameGenerationEnabled = false;
		cfg.frameID = frameID;
		SafeConfigure(&ffxApi, &fgContext, &cfg.header);
	}
	if (fgSwapchainContext) {
		auto* dxvk = DxvkInterop::GetSingleton();
		dxvk->LockSubmissionQueue();
		SafeDestroyContext(&ffxApi, &fgSwapchainContext);
		dxvk->ReleaseSubmissionQueue();
		fgSwapchainContext = nullptr;
	}
	fgSwapchainFns = {};
	fgWrappedSwapchain = VK_NULL_HANDLE;
	logger::info("[FidelityFX] FFX FG swapchain destroyed");
}
