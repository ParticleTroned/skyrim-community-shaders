#include "FidelityFX.h"

#include "../../State.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DxvkInterop.h"

#include <vector>

namespace
{
	// Mirror of the FFX VK backend's getVKImageLayoutFromResourceState so we can
	// pre-transition DXVK interop images into the layout FFX assumes (FFX seeds its
	// first barrier oldLayout from the declared state).
	VkImageLayout FfxStateToLayout(FfxResourceStates state)
	{
		switch (state) {
		case FFX_RESOURCE_STATE_UNORDERED_ACCESS:
		case FFX_RESOURCE_STATE_COMMON:
			return VK_IMAGE_LAYOUT_GENERAL;
		case FFX_RESOURCE_STATE_COPY_SRC:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case FFX_RESOURCE_STATE_COPY_DEST:
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
	FfxResource WrapAndPrepare(SIE::DxvkInterop* dxvk, ID3D11Resource* resource,
		const wchar_t* name, FfxResourceStates state, FfxResourceUsage usage,
		VkImageAspectFlags aspect, std::vector<LayoutGuard>& guards)
	{
		if (!resource)
			return ffxGetResourceVK(nullptr, {}, name, state);

		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		if (!dxvk->GetVkImage(resource, &image, &layout, &info) || image == VK_NULL_HANDLE)
			return ffxGetResourceVK(nullptr, {}, name, state);

		VkImageLayout ffxLayout = FfxStateToLayout(state);
		if (layout != ffxLayout) {
			dxvk->TransitionImageLayout(resource, layout, ffxLayout, aspect);
			guards.push_back({ resource, layout, ffxLayout, aspect });
		}

		FfxResourceDescription desc = ffxGetImageResourceDescriptionVK(image, info, usage);
		return ffxGetResourceVK(reinterpret_cast<void*>(image), desc, name, state);
	}

	void RestoreLayouts(SIE::DxvkInterop* dxvk, std::vector<LayoutGuard>& guards)
	{
		// FFX leaves each resource in its declared-state (ffx) layout; move it back.
		for (auto it = guards.rbegin(); it != guards.rend(); ++it)
			dxvk->TransitionImageLayout(it->resource, it->ffx, it->original, it->aspect);
		guards.clear();
	}

	// SEH wrappers — kept free of any object that requires unwinding so __try is legal
	// (MSVC forbids __try in a function with C++ objects whose lifetime spans it). A
	// fault here is typically RenderDoc interfering with FFX; we degrade gracefully.
	FfxErrorCode SafeContextCreate(FfxFsr3Context* ctx, FfxFsr3ContextDescription* desc)
	{
		__try {
			return ffxFsr3ContextCreate(ctx, desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_ERROR_BACKEND_API_ERROR;
		}
	}

	FfxErrorCode SafeDispatchUpscale(FfxFsr3Context* ctx, const FfxFsr3DispatchUpscaleDescription* desc)
	{
		__try {
			return ffxFsr3ContextDispatchUpscale(ctx, desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_ERROR_BACKEND_API_ERROR;
		}
	}

	FfxErrorCode SafeConfigureFrameGeneration(FfxFsr3Context* ctx, const FfxFrameGenerationConfig* cfg)
	{
		__try {
			return ffxFsr3ConfigureFrameGeneration(ctx, cfg);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_ERROR_BACKEND_API_ERROR;
		}
	}

	FfxErrorCode SafeDispatchFrameGeneration(const FfxFrameGenerationDispatchDescription* desc)
	{
		__try {
			return ffxFsr3DispatchFrameGeneration(desc);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return FFX_ERROR_BACKEND_API_ERROR;
		}
	}

	// Surface FFX's own diagnostics (missing device features, pipeline failures, etc.).
	void FfxMessageCallback(FfxMsgType type, const wchar_t* message)
	{
		char buf[1024] = {};
		size_t converted = 0;
		wcstombs_s(&converted, buf, sizeof(buf), message ? message : L"", _TRUNCATE);
		if (type == FFX_MESSAGE_TYPE_ERROR)
			logger::error("[FidelityFX][FFX] {}", buf);
		else
			logger::warn("[FidelityFX][FFX] {}", buf);
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
	if (fsrScratchBuffers[0] || fsrScratchBuffers[1]) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	auto* dxvk = SIE::DxvkInterop::GetSingleton();
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

	VkDeviceContext vkCtx{};
	vkCtx.vkDevice = dxvk->GetDevice();
	vkCtx.vkPhysicalDevice = dxvk->GetPhysicalDevice();
	vkCtx.vkDeviceProcAddr = dxvk->GetDeviceProcAddr();
	fsrFfxDevice = ffxGetDeviceVK(&vkCtx);

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);
	uint32_t displayWidth = (uint32_t)screenSize.x;
	uint32_t displayHeight = (uint32_t)screenSize.y;
	uint32_t renderWidth = (uint32_t)renderSize.x;
	uint32_t renderHeight = (uint32_t)renderSize.y;

	FfxFsr3ContextDescription contextDescription{};
	contextDescription.maxRenderSize = { renderWidth, renderHeight };
	contextDescription.maxUpscaleSize = { displayWidth, displayHeight };
	contextDescription.displaySize = { displayWidth, displayHeight };

	contextDescription.fpMessage = FfxMessageCallback;

	const bool hdr = globals::features::hdrDisplay.loaded;
	contextDescription.flags = FFX_FSR3_ENABLE_AUTO_EXPOSURE;
	if (hdr) {
		contextDescription.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	} else {
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	}

	auto makeInterface = [&](int slot, size_t maxContexts) -> bool {
		size_t scratchSize = ffxGetScratchMemorySizeVK(dxvk->GetPhysicalDevice(), maxContexts);
		fsrScratchBuffers[slot] = calloc(scratchSize ? scratchSize : 1, 1);
		if (!fsrScratchBuffers[slot]) {
			logger::critical("[FidelityFX] Failed to allocate FSR3 VK scratch buffer {}", slot);
			return false;
		}
		FfxInterface iface{};
		FfxErrorCode ifErr = ffxGetInterfaceVK(&iface, fsrFfxDevice, fsrScratchBuffers[slot], scratchSize, maxContexts);
		logger::info("[FidelityFX] slot {} ffxGetInterfaceVK=0x{:08X} scratchSize={} ifScratchSize={} fpGetSDKVersion={} fpGetDeviceCapabilities={} fpCreateBackendContext={} fpDestroyBackendContext={}",
			slot, (uint32_t)ifErr, scratchSize, iface.scratchBufferSize,
			iface.fpGetSDKVersion != nullptr, iface.fpGetDeviceCapabilities != nullptr,
			iface.fpCreateBackendContext != nullptr, iface.fpDestroyBackendContext != nullptr);
		if (ifErr != FFX_OK) {
			logger::critical("[FidelityFX] ffxGetInterfaceVK failed for slot {}", slot);
			return false;
		}
		if (slot == 0)
			contextDescription.backendInterfaceSharedResources = iface;
		else if (slot == 1)
			contextDescription.backendInterfaceUpscaling = iface;
		else
			contextDescription.backendInterfaceFrameInterpolation = iface;
		return true;
	};

	// This FFX v1.1.4 build does NOT honor FFX_FSR3_ENABLE_UPSCALING_ONLY:
	// ffxFsr3ContextCreate computes a local `upscalingOnly` but never assigns it to
	// contextPrivate->upscalingOnly (ffx_fsr3.cpp:68), so the context always builds the
	// full upscaler + optical-flow + frame-interpolation pipeline and verifies all three
	// backend interfaces. Always provide them: shared(1), upscaling(1), frameInterp(2).
	if (!makeInterface(0, 1) || !makeInterface(1, 1) || !makeInterface(2, 2)) {
		for (auto& buf : fsrScratchBuffers) {
			if (buf) {
				free(buf);
				buf = nullptr;
			}
		}
		if (dxvk->IsAvailable())
			dxvk->DestroyCommandResources();
		return;
	}

	FfxErrorCode createErr = SafeContextCreate(&fsrContext[0], &contextDescription);
	if (createErr != FFX_OK) {
		logger::critical("[FidelityFX] ffxFsr3ContextCreate (VK) failed: 0x{:08X}", (uint32_t)createErr);
		// Context not created — free scratch/textures/commands WITHOUT destroying the
		// (non-existent) FFX context, which would fault.
		for (auto& buf : fsrScratchBuffers) {
			if (buf) {
				free(buf);
				buf = nullptr;
			}
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
	interpolatedTexture = MakeDisplayTexture(main.texture, displayWidth, displayHeight, "FidelityFX::InterpolatedFrame").release();

	frameGenContextActive = true;
	frameID = 0;
	logger::info("[FidelityFX] Created FSR3 context on DXVK VkDevice (Display: {}x{}, Render: {}x{}, frame-gen capable)",
		displayWidth, displayHeight, renderWidth, renderHeight);
}

void FidelityFX::DestroyFSRResources()
{
	auto* dxvk = SIE::DxvkInterop::GetSingleton();

	if (contextCreated && ffxFsr3ContextDestroy(&fsrContext[0]) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");
	contextCreated = false;

	for (auto& buf : fsrScratchBuffers) {
		if (buf) {
			free(buf);
			buf = nullptr;
		}
	}

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

	if (dxvk->IsAvailable())
		dxvk->DestroyCommandResources();

	frameGenContextActive = false;
	currentCommandBuffer = VK_NULL_HANDLE;
	fsrDispatchCrashLogged = false;
	fgDispatchCrashLogged = false;
}

void FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto* dxvk = SIE::DxvkInterop::GetSingleton();
	if (!dxvk->IsAvailable() || !dxvk->CommandResourcesReady() || !upscaledTexture)
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

	FfxFsr3DispatchUpscaleDescription dispatchParameters{};
	dispatchParameters.commandList = ffxGetCommandListVK(cb);
	dispatchParameters.color = WrapAndPrepare(dxvk, a_upscalingTexture, L"FSR3_Color", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dispatchParameters.depth = WrapAndPrepare(dxvk, depthTexture.texture, L"FSR3_Depth", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_DEPTHTARGET, VK_IMAGE_ASPECT_DEPTH_BIT, guards);
	dispatchParameters.motionVectors = WrapAndPrepare(dxvk, a_motionVectors, L"FSR3_MotionVectors", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dispatchParameters.reactive = WrapAndPrepare(dxvk, a_reactiveMask, L"FSR3_Reactive", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dispatchParameters.transparencyAndComposition = WrapAndPrepare(dxvk, a_transparencyCompositionMask, L"FSR3_TransComp", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
	dispatchParameters.exposure = ffxGetResourceVK(nullptr, {}, L"FSR3_Exposure", FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatchParameters.upscaleOutput = WrapAndPrepare(dxvk, upscaledTexture->resource.get(), L"FSR3_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS, FFX_RESOURCE_USAGE_UAV, VK_IMAGE_ASPECT_COLOR_BIT, guards);

	dispatchParameters.motionVectorScale = { renderSize.x, renderSize.y };
	dispatchParameters.renderSize = { (uint)renderSize.x, (uint)renderSize.y };
	dispatchParameters.jitterOffset = { -jitter.x, -jitter.y };
	dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;
	dispatchParameters.cameraFar = *globals::game::cameraFar;
	dispatchParameters.cameraNear = *globals::game::cameraNear;
	dispatchParameters.enableSharpening = true;
	dispatchParameters.sharpness = a_sharpness;
	dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
	dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
	dispatchParameters.reset = false;
	dispatchParameters.preExposure = 1.0f;
	dispatchParameters.flags = 0;
	dispatchParameters.frameID = frameID;

	bool dispatched = SafeDispatchUpscale(&fsrContext[0], &dispatchParameters) == FFX_OK;
	if (!dispatched && !fsrDispatchCrashLogged) {
		logger::critical("[FidelityFX] ffxFsr3ContextDispatchUpscale (VK) failed or faulted.");
		fsrDispatchCrashLogged = true;
	}

	RestoreLayouts(dxvk, guards);

	// Submit the upscale (wait so the result is ready) and copy it back into the game's
	// color target. Frame generation, if enabled, runs on its own command buffer next,
	// reading the prepared dilated depth/MVs that the upscale stored in the FSR3 context.
	dxvk->SubmitFrameCommandBuffer(cb, /*waitIdle*/ true);
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

bool FidelityFX::DispatchFrameGeneration(ID3D11Resource* a_presentColor, bool a_isHDR, float a_peakNits)
{
	if (!frameGenContextActive || !interpolatedTexture)
		return false;

	auto* dxvk = SIE::DxvkInterop::GetSingleton();
	if (!dxvk->IsAvailable() || !dxvk->CommandResourcesReady())
		return false;

	auto state = globals::state;

	if (state->frameAnnotations)
		state->BeginPerfEvent("FSR VK Frame Generation");

	// Own command buffer (the upscale already submitted and stored its dilated depth/MVs
	// in the FSR3 context). presentColor is the upscaled real frame, now in a_presentColor.
	VkCommandBuffer cb = dxvk->BeginFrameCommandBuffer();
	if (cb == VK_NULL_HANDLE) {
		if (state->frameAnnotations)
			state->EndPerfEvent();
		return false;
	}

	std::vector<LayoutGuard> guards;
	bool produced = false;

	FfxFrameGenerationConfig fgConfig{};
	fgConfig.swapChain = nullptr;
	fgConfig.presentCallback = nullptr;
	fgConfig.frameGenerationCallback = nullptr;
	fgConfig.frameGenerationEnabled = true;
	fgConfig.allowAsyncWorkloads = false;
	fgConfig.HUDLessColor = FfxResource({});
	fgConfig.flags = 0;
	fgConfig.onlyPresentInterpolated = false;

	if (SafeConfigureFrameGeneration(&fsrContext[0], &fgConfig) == FFX_OK) {
		FfxFrameGenerationDispatchDescription fgDesc{};
		fgDesc.commandList = ffxGetCommandListVK(cb);
		fgDesc.presentColor = WrapAndPrepare(dxvk, a_presentColor, L"FSR3_FG_PresentColor", FFX_RESOURCE_STATE_COMPUTE_READ, FFX_RESOURCE_USAGE_READ_ONLY, VK_IMAGE_ASPECT_COLOR_BIT, guards);
		fgDesc.outputs[0] = WrapAndPrepare(dxvk, interpolatedTexture->resource.get(), L"FSR3_FG_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS, FFX_RESOURCE_USAGE_UAV, VK_IMAGE_ASPECT_COLOR_BIT, guards);
		fgDesc.numInterpolatedFrames = 1;
		fgDesc.reset = false;
		fgDesc.frameID = frameID;
		fgDesc.interpolationRect = { 0, 0, (int)globals::game::graphicsState->screenWidth, (int)globals::game::graphicsState->screenHeight };
		if (a_isHDR) {
			fgDesc.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_PQ;
			fgDesc.minMaxLuminance[0] = 0.0f;
			fgDesc.minMaxLuminance[1] = a_peakNits;
		} else {
			fgDesc.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
			fgDesc.minMaxLuminance[0] = 0.0f;
			fgDesc.minMaxLuminance[1] = 1.0f;
		}
		produced = SafeDispatchFrameGeneration(&fgDesc) == FFX_OK;
		if (!produced && !fgDispatchCrashLogged) {
			logger::critical("[FidelityFX] ffxFsr3DispatchFrameGeneration (VK) failed or faulted.");
			fgDispatchCrashLogged = true;
		}
	}

	RestoreLayouts(dxvk, guards);
	dxvk->SubmitFrameCommandBuffer(cb, /*waitIdle*/ true);

	++frameID;

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return produced;
}
