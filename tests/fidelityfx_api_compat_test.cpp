// Snapshot of the x64 descriptor ABI used by the preserved SE upscaling/FG providers.
// Recorded against SE base e1989826771a1faf9fbb9e7e77a3c38e2256051d.
#include <cstddef>
#ifndef _WIN32
#	define __declspec(...)
#endif
#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable: 4996)
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#	pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif
#include "FidelityFX/api/include/ffx_api.h"
#include "FidelityFX/api/include/ffx_api_types.h"
#include "FidelityFX/framegeneration/include/ffx_framegeneration.h"
#include "FidelityFX/upscalers/include/ffx_upscale.h"

static_assert(sizeof(void*) == 8, "SE provider ABI is x64");

static_assert(sizeof(FfxApiDimensions2D) == 8 && alignof(FfxApiDimensions2D) == 4);
static_assert(offsetof(FfxApiDimensions2D, width) == 0);
static_assert(offsetof(FfxApiDimensions2D, height) == 4);

static_assert(sizeof(FfxApiEffectMemoryUsage) == 16 && alignof(FfxApiEffectMemoryUsage) == 8);
static_assert(offsetof(FfxApiEffectMemoryUsage, totalUsageInBytes) == 0);
static_assert(offsetof(FfxApiEffectMemoryUsage, aliasableUsageInBytes) == 8);

static_assert(sizeof(FfxApiFloatCoords2D) == 8 && alignof(FfxApiFloatCoords2D) == 4);
static_assert(offsetof(FfxApiFloatCoords2D, x) == 0);
static_assert(offsetof(FfxApiFloatCoords2D, y) == 4);

static_assert(sizeof(FfxApiRect2D) == 16 && alignof(FfxApiRect2D) == 4);
static_assert(offsetof(FfxApiRect2D, left) == 0);
static_assert(offsetof(FfxApiRect2D, top) == 4);
static_assert(offsetof(FfxApiRect2D, width) == 8);
static_assert(offsetof(FfxApiRect2D, height) == 12);

static_assert(sizeof(FfxApiResource) == 48 && alignof(FfxApiResource) == 8);
static_assert(offsetof(FfxApiResource, resource) == 0);
static_assert(offsetof(FfxApiResource, description) == 8);
static_assert(offsetof(FfxApiResource, state) == 40);

static_assert(sizeof(FfxApiResourceDescription) == 32 && alignof(FfxApiResourceDescription) == 4);
static_assert(offsetof(FfxApiResourceDescription, type) == 0);
static_assert(offsetof(FfxApiResourceDescription, format) == 4);
static_assert(offsetof(FfxApiResourceDescription, width) == 8);
static_assert(offsetof(FfxApiResourceDescription, size) == 8);

static_assert(sizeof(ffxAllocationCallbacks) == 24 && alignof(ffxAllocationCallbacks) == 8);
static_assert(offsetof(ffxAllocationCallbacks, pUserData) == 0);
static_assert(offsetof(ffxAllocationCallbacks, alloc) == 8);
static_assert(offsetof(ffxAllocationCallbacks, dealloc) == 16);

static_assert(sizeof(ffxApiHeader) == 16 && alignof(ffxApiHeader) == 8);
static_assert(offsetof(ffxApiHeader, type) == 0);
static_assert(offsetof(ffxApiHeader, pNext) == 8);

static_assert(sizeof(ffxCallbackDescFrameGenerationPresent) == 192 && alignof(ffxCallbackDescFrameGenerationPresent) == 8);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, header) == 0);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, device) == 16);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, commandList) == 24);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, currentBackBuffer) == 32);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, currentUI) == 80);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, outputSwapChainBuffer) == 128);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, isGeneratedFrame) == 176);
static_assert(offsetof(ffxCallbackDescFrameGenerationPresent, frameID) == 184);

static_assert(sizeof(ffxConfigureDescFrameGeneration) == 144 && alignof(ffxConfigureDescFrameGeneration) == 8);
static_assert(offsetof(ffxConfigureDescFrameGeneration, header) == 0);
static_assert(offsetof(ffxConfigureDescFrameGeneration, swapChain) == 16);
static_assert(offsetof(ffxConfigureDescFrameGeneration, presentCallback) == 24);
static_assert(offsetof(ffxConfigureDescFrameGeneration, presentCallbackUserContext) == 32);
static_assert(offsetof(ffxConfigureDescFrameGeneration, frameGenerationCallback) == 40);
static_assert(offsetof(ffxConfigureDescFrameGeneration, frameGenerationCallbackUserContext) == 48);
static_assert(offsetof(ffxConfigureDescFrameGeneration, frameGenerationEnabled) == 56);
static_assert(offsetof(ffxConfigureDescFrameGeneration, allowAsyncWorkloads) == 57);
static_assert(offsetof(ffxConfigureDescFrameGeneration, HUDLessColor) == 64);
static_assert(offsetof(ffxConfigureDescFrameGeneration, flags) == 112);
static_assert(offsetof(ffxConfigureDescFrameGeneration, onlyPresentGenerated) == 116);
static_assert(offsetof(ffxConfigureDescFrameGeneration, generationRect) == 120);
static_assert(offsetof(ffxConfigureDescFrameGeneration, frameID) == 136);

static_assert(sizeof(ffxConfigureDescFrameGenerationKeyValue) == 40 && alignof(ffxConfigureDescFrameGenerationKeyValue) == 8);
static_assert(offsetof(ffxConfigureDescFrameGenerationKeyValue, header) == 0);
static_assert(offsetof(ffxConfigureDescFrameGenerationKeyValue, key) == 16);
static_assert(offsetof(ffxConfigureDescFrameGenerationKeyValue, u64) == 24);
static_assert(offsetof(ffxConfigureDescFrameGenerationKeyValue, ptr) == 32);

static_assert(sizeof(ffxConfigureDescFrameGenerationRegisterDistortionFieldResource) == 64 && alignof(ffxConfigureDescFrameGenerationRegisterDistortionFieldResource) == 8);
static_assert(offsetof(ffxConfigureDescFrameGenerationRegisterDistortionFieldResource, header) == 0);
static_assert(offsetof(ffxConfigureDescFrameGenerationRegisterDistortionFieldResource, distortionField) == 16);

static_assert(sizeof(ffxConfigureDescGlobalDebug1) == 32 && alignof(ffxConfigureDescGlobalDebug1) == 8);
static_assert(offsetof(ffxConfigureDescGlobalDebug1, header) == 0);
static_assert(offsetof(ffxConfigureDescGlobalDebug1, fpMessage) == 16);
static_assert(offsetof(ffxConfigureDescGlobalDebug1, debugLevel) == 24);

static_assert(sizeof(ffxConfigureDescUpscaleKeyValue) == 40 && alignof(ffxConfigureDescUpscaleKeyValue) == 8);
static_assert(offsetof(ffxConfigureDescUpscaleKeyValue, header) == 0);
static_assert(offsetof(ffxConfigureDescUpscaleKeyValue, key) == 16);
static_assert(offsetof(ffxConfigureDescUpscaleKeyValue, u64) == 24);
static_assert(offsetof(ffxConfigureDescUpscaleKeyValue, ptr) == 32);

static_assert(sizeof(ffxCreateContextDescFrameGeneration) == 40 && alignof(ffxCreateContextDescFrameGeneration) == 8);
static_assert(offsetof(ffxCreateContextDescFrameGeneration, header) == 0);
static_assert(offsetof(ffxCreateContextDescFrameGeneration, flags) == 16);
static_assert(offsetof(ffxCreateContextDescFrameGeneration, displaySize) == 20);
static_assert(offsetof(ffxCreateContextDescFrameGeneration, maxRenderSize) == 28);
static_assert(offsetof(ffxCreateContextDescFrameGeneration, backBufferFormat) == 36);

static_assert(sizeof(ffxCreateContextDescFrameGenerationHudless) == 24 && alignof(ffxCreateContextDescFrameGenerationHudless) == 8);
static_assert(offsetof(ffxCreateContextDescFrameGenerationHudless, header) == 0);
static_assert(offsetof(ffxCreateContextDescFrameGenerationHudless, hudlessBackBufferFormat) == 16);

static_assert(sizeof(ffxCreateContextDescUpscale) == 48 && alignof(ffxCreateContextDescUpscale) == 8);
static_assert(offsetof(ffxCreateContextDescUpscale, header) == 0);
static_assert(offsetof(ffxCreateContextDescUpscale, flags) == 16);
static_assert(offsetof(ffxCreateContextDescUpscale, maxRenderSize) == 20);
static_assert(offsetof(ffxCreateContextDescUpscale, maxUpscaleSize) == 28);
static_assert(offsetof(ffxCreateContextDescUpscale, fpMessage) == 40);

static_assert(sizeof(ffxCreateContextDescUpscaleVersion) == 24 && alignof(ffxCreateContextDescUpscaleVersion) == 8);
static_assert(offsetof(ffxCreateContextDescUpscaleVersion, header) == 0);
static_assert(offsetof(ffxCreateContextDescUpscaleVersion, version) == 16);

static_assert(sizeof(ffxDispatchDescFrameGeneration) == 312 && alignof(ffxDispatchDescFrameGeneration) == 8);
static_assert(offsetof(ffxDispatchDescFrameGeneration, header) == 0);
static_assert(offsetof(ffxDispatchDescFrameGeneration, commandList) == 16);
static_assert(offsetof(ffxDispatchDescFrameGeneration, presentColor) == 24);
static_assert(offsetof(ffxDispatchDescFrameGeneration, outputs) == 72);
static_assert(offsetof(ffxDispatchDescFrameGeneration, numGeneratedFrames) == 264);
static_assert(offsetof(ffxDispatchDescFrameGeneration, reset) == 268);
static_assert(offsetof(ffxDispatchDescFrameGeneration, backbufferTransferFunction) == 272);
static_assert(offsetof(ffxDispatchDescFrameGeneration, minMaxLuminance) == 276);
static_assert(offsetof(ffxDispatchDescFrameGeneration, generationRect) == 284);
static_assert(offsetof(ffxDispatchDescFrameGeneration, frameID) == 304);

static_assert(sizeof(ffxDispatchDescFrameGenerationPrepare) == 184 && alignof(ffxDispatchDescFrameGenerationPrepare) == 8);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, header) == 0);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, frameID) == 16);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, flags) == 24);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, commandList) == 32);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, renderSize) == 40);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, jitterOffset) == 48);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, motionVectorScale) == 56);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, frameTimeDelta) == 64);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, unused_reset) == 68);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, cameraNear) == 72);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, cameraFar) == 76);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, cameraFovAngleVertical) == 80);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, viewSpaceToMetersFactor) == 84);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, depth) == 88);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, motionVectors) == 136);

static_assert(sizeof(ffxDispatchDescFrameGenerationPrepareCameraInfo) == 64 && alignof(ffxDispatchDescFrameGenerationPrepareCameraInfo) == 8);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepareCameraInfo, header) == 0);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepareCameraInfo, cameraPosition) == 16);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepareCameraInfo, cameraUp) == 28);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepareCameraInfo, cameraRight) == 40);
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepareCameraInfo, cameraForward) == 52);

static_assert(sizeof(ffxDispatchDescUpscale) == 432 && alignof(ffxDispatchDescUpscale) == 8);
static_assert(offsetof(ffxDispatchDescUpscale, header) == 0);
static_assert(offsetof(ffxDispatchDescUpscale, commandList) == 16);
static_assert(offsetof(ffxDispatchDescUpscale, color) == 24);
static_assert(offsetof(ffxDispatchDescUpscale, depth) == 72);
static_assert(offsetof(ffxDispatchDescUpscale, motionVectors) == 120);
static_assert(offsetof(ffxDispatchDescUpscale, exposure) == 168);
static_assert(offsetof(ffxDispatchDescUpscale, reactive) == 216);
static_assert(offsetof(ffxDispatchDescUpscale, transparencyAndComposition) == 264);
static_assert(offsetof(ffxDispatchDescUpscale, output) == 312);
static_assert(offsetof(ffxDispatchDescUpscale, jitterOffset) == 360);
static_assert(offsetof(ffxDispatchDescUpscale, motionVectorScale) == 368);
static_assert(offsetof(ffxDispatchDescUpscale, renderSize) == 376);
static_assert(offsetof(ffxDispatchDescUpscale, upscaleSize) == 384);
static_assert(offsetof(ffxDispatchDescUpscale, enableSharpening) == 392);
static_assert(offsetof(ffxDispatchDescUpscale, sharpness) == 396);
static_assert(offsetof(ffxDispatchDescUpscale, frameTimeDelta) == 400);
static_assert(offsetof(ffxDispatchDescUpscale, preExposure) == 404);
static_assert(offsetof(ffxDispatchDescUpscale, reset) == 408);
static_assert(offsetof(ffxDispatchDescUpscale, cameraNear) == 412);
static_assert(offsetof(ffxDispatchDescUpscale, cameraFar) == 416);
static_assert(offsetof(ffxDispatchDescUpscale, cameraFovAngleVertical) == 420);
static_assert(offsetof(ffxDispatchDescUpscale, viewSpaceToMetersFactor) == 424);
static_assert(offsetof(ffxDispatchDescUpscale, flags) == 428);

static_assert(sizeof(ffxDispatchDescUpscaleGenerateReactiveMask) == 192 && alignof(ffxDispatchDescUpscaleGenerateReactiveMask) == 8);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, header) == 0);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, commandList) == 16);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, colorOpaqueOnly) == 24);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, colorPreUpscale) == 72);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, outReactive) == 120);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, renderSize) == 168);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, scale) == 176);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, cutoffThreshold) == 180);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, binaryValue) == 184);
static_assert(offsetof(ffxDispatchDescUpscaleGenerateReactiveMask, flags) == 188);

static_assert(sizeof(ffxOverrideVersion) == 24 && alignof(ffxOverrideVersion) == 8);
static_assert(offsetof(ffxOverrideVersion, header) == 0);
static_assert(offsetof(ffxOverrideVersion, versionId) == 16);

static_assert(sizeof(ffxQueryDescFrameGenerationGetGPUMemoryUsage) == 24 && alignof(ffxQueryDescFrameGenerationGetGPUMemoryUsage) == 8);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsage, header) == 0);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsage, gpuMemoryUsageFrameGeneration) == 16);

static_assert(sizeof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2) == 64 && alignof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2) == 8);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, header) == 0);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, device) == 16);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, maxRenderSize) == 24);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, displaySize) == 32);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, createFlags) == 40);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, dispatchFlags) == 44);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, backBufferFormat) == 48);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, hudlessBackBufferFormat) == 52);
static_assert(offsetof(ffxQueryDescFrameGenerationGetGPUMemoryUsageV2, gpuMemoryUsageFrameGeneration) == 56);

static_assert(sizeof(ffxQueryDescGetVersions) == 56 && alignof(ffxQueryDescGetVersions) == 8);
static_assert(offsetof(ffxQueryDescGetVersions, header) == 0);
static_assert(offsetof(ffxQueryDescGetVersions, createDescType) == 16);
static_assert(offsetof(ffxQueryDescGetVersions, device) == 24);
static_assert(offsetof(ffxQueryDescGetVersions, outputCount) == 32);
static_assert(offsetof(ffxQueryDescGetVersions, versionIds) == 40);
static_assert(offsetof(ffxQueryDescGetVersions, versionNames) == 48);

static_assert(sizeof(ffxQueryDescUpscaleGetGPUMemoryUsage) == 24 && alignof(ffxQueryDescUpscaleGetGPUMemoryUsage) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsage, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsage, gpuMemoryUsageUpscaler) == 16);

static_assert(sizeof(ffxQueryDescUpscaleGetGPUMemoryUsageV2) == 56 && alignof(ffxQueryDescUpscaleGetGPUMemoryUsageV2) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, device) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, maxRenderSize) == 24);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, maxUpscaleSize) == 32);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, flags) == 40);
static_assert(offsetof(ffxQueryDescUpscaleGetGPUMemoryUsageV2, gpuMemoryUsageUpscaler) == 48);

static_assert(sizeof(ffxQueryDescUpscaleGetJitterOffset) == 40 && alignof(ffxQueryDescUpscaleGetJitterOffset) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterOffset, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterOffset, index) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterOffset, phaseCount) == 20);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterOffset, pOutX) == 24);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterOffset, pOutY) == 32);

static_assert(sizeof(ffxQueryDescUpscaleGetJitterPhaseCount) == 32 && alignof(ffxQueryDescUpscaleGetJitterPhaseCount) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterPhaseCount, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterPhaseCount, renderWidth) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterPhaseCount, displayWidth) == 20);
static_assert(offsetof(ffxQueryDescUpscaleGetJitterPhaseCount, pOutPhaseCount) == 24);

static_assert(sizeof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode) == 48 && alignof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, displayWidth) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, displayHeight) == 20);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, qualityMode) == 24);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, pOutRenderWidth) == 32);
static_assert(offsetof(ffxQueryDescUpscaleGetRenderResolutionFromQualityMode, pOutRenderHeight) == 40);

static_assert(sizeof(ffxQueryDescUpscaleGetResourceRequirements) == 32 && alignof(ffxQueryDescUpscaleGetResourceRequirements) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetResourceRequirements, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetResourceRequirements, required_resources) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetResourceRequirements, optional_resources) == 24);

static_assert(sizeof(ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode) == 32 && alignof(ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode) == 8);
static_assert(offsetof(ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode, header) == 0);
static_assert(offsetof(ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode, qualityMode) == 16);
static_assert(offsetof(ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode, pOutUpscaleRatio) == 24);

static_assert(sizeof(ffxQueryGetProviderVersion) == 32 && alignof(ffxQueryGetProviderVersion) == 8);
static_assert(offsetof(ffxQueryGetProviderVersion, header) == 0);
static_assert(offsetof(ffxQueryGetProviderVersion, versionId) == 16);
static_assert(offsetof(ffxQueryGetProviderVersion, versionName) == 24);

#if defined(_MSC_VER)
#	pragma warning(pop)
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif
int main() {}
