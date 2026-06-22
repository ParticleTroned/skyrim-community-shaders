#include "FidelityFX.h"

#include "../../State.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"

void FidelityFX::CreateFSRResources()
{
	// Prevent multiple allocations
	if (fsrScratchBuffer) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	auto fsrDevice = ffxGetDeviceDX11_Fsr31(globals::d3d::device);

	uint32_t numContexts = 1;
	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		return;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, numContexts) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t displayWidth = (uint32_t)screenSize.x;
	uint32_t displayHeight = (uint32_t)screenSize.y;
	uint32_t renderWidth = (uint32_t)renderSize.x;
	uint32_t renderHeight = (uint32_t)renderSize.y;

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = renderWidth;
	contextDescription.maxRenderSize.height = renderHeight;
	contextDescription.maxUpscaleSize.width = displayWidth;
	contextDescription.maxUpscaleSize.height = displayHeight;
	contextDescription.displaySize.width = displayWidth;
	contextDescription.displaySize.height = displayHeight;
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
	if (globals::features::hdrDisplay.loaded) {
		contextDescription.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;
	} else {
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	}
	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext[0], &contextDescription) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}
	logger::info("[FidelityFX] Created FSR3 context (Display: {}x{}, Render: {}x{})",
		displayWidth, displayHeight, renderWidth, renderHeight);
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext[0]) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");

	// Free the scratch buffer to prevent memory leak
	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}

	// Reset crash logging flag when resources are destroyed
	fsrDispatchCrashLogged = false;
}

static FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
	resource.state = state;
	resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
	if (ffxResName) {
		wcscpy_s(resource.name, ffxResName);
	}
#endif

	return resource;
}

void FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	auto& upscaling = globals::features::upscaling;
	auto jitter = upscaling.jitter;

	if (state->frameAnnotations)
		state->BeginPerfEvent("FSR Dispatch");

	FfxFsr3DispatchUpscaleDescription dispatchParameters{};
	dispatchParameters.commandList = ffxGetCommandListDX11(context);
	dispatchParameters.color = ffxGetResource(a_upscalingTexture, L"FSR3_Input_OutputColor");
	dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth");
	dispatchParameters.motionVectors = ffxGetResource(a_motionVectors, L"FSR3_InputMotionVectors");
	dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure");
	dispatchParameters.upscaleOutput = ffxGetResource(a_upscalingTexture, L"FSR3_OutputColor");
	dispatchParameters.reactive = ffxGetResource(a_reactiveMask, L"FSR3_InputReactiveMap");
	dispatchParameters.transparencyAndComposition = ffxGetResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");

	dispatchParameters.motionVectorScale.x = renderSize.x;
	dispatchParameters.motionVectorScale.y = renderSize.y;
	dispatchParameters.renderSize.width = (uint)renderSize.x;
	dispatchParameters.renderSize.height = (uint)renderSize.y;

	dispatchParameters.jitterOffset.x = -jitter.x;
	dispatchParameters.jitterOffset.y = -jitter.y;

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

	__try {
		if (ffxFsr3ContextDispatchUpscale(&fsrContext[0], &dispatchParameters) != FFX_OK)
			logger::critical("[FidelityFX] Failed to dispatch upscaling!");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (!fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] FSR3 dispatch crashed - this may be caused by RenderDoc capture interfering with FSR operations. Try disabling RenderDoc capture.");
			fsrDispatchCrashLogged = true;
		}
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}
