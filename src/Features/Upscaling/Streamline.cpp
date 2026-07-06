#include "Streamline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <limits>
#include <string>

#include "../../Deferred.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

namespace
{
	constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;

	enum class D3D11IdleFenceResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};

	void ReleaseD3D11IdleFence(ID3D11Query*& a_query)
	{
		if (!a_query)
			return;

		a_query->Release();
		a_query = nullptr;
	}

	bool IsHDRDLSSInputFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return false;
		default:
			return true;
		}
	}

	bool TryGetTexture2DDesc(ID3D11Resource* a_resource, D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&a_desc);
		return true;
	}

	bool GetDLSSColorBuffersHDR(ID3D11Resource* a_colorIn)
	{
		D3D11_TEXTURE2D_DESC desc{};
		if (!TryGetTexture2DDesc(a_colorIn, desc))
			return true;

		return IsHDRDLSSInputFormat(desc.Format);
	}

	int32_t QuantizeDLSSSignatureFloat(float a_value)
	{
		if (!std::isfinite(a_value))
			return 0;

		const double scaled = static_cast<double>(a_value) * 1000000.0;
		if (scaled > static_cast<double>(std::numeric_limits<int32_t>::max()))
			return std::numeric_limits<int32_t>::max();
		if (scaled < static_cast<double>(std::numeric_limits<int32_t>::min()))
			return std::numeric_limits<int32_t>::min();

		return static_cast<int32_t>(std::lround(scaled));
	}

	D3D11IdleFenceResult BeginOrPollD3D11IdleFence(ID3D11DeviceContext* a_context, ID3D11Query*& a_query)
	{
		if (!a_context) {
			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Ready;
		}

		const auto pollFence = [&]() {
			BOOL completed = FALSE;
			const HRESULT dataResult = a_context->GetData(a_query, &completed, sizeof(completed), 0);
			if (dataResult == S_OK && completed) {
				ReleaseD3D11IdleFence(a_query);
				return D3D11IdleFenceResult::Ready;
			}

			if (dataResult == S_FALSE || dataResult == S_OK)
				return D3D11IdleFenceResult::Pending;

			ReleaseD3D11IdleFence(a_query);
			return D3D11IdleFenceResult::Failed;
		};

		if (a_query)
			return pollFence();

		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		if (!device) {
			a_context->Flush();
			return D3D11IdleFenceResult::Ready;
		}

		D3D11_QUERY_DESC queryDesc{};
		queryDesc.Query = D3D11_QUERY_EVENT;

		const HRESULT createResult = device->CreateQuery(&queryDesc, &a_query);
		device->Release();

		if (FAILED(createResult) || !a_query) {
			a_context->Flush();
			return D3D11IdleFenceResult::Failed;
		}

		a_context->End(a_query);
		a_context->Flush();
		return pollFence();
	}

}

Streamline::~Streamline()
{
	ResetDLSSIdleFences();
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

void Streamline::LoadInterposer()
{
	triedInitialization = true;
	featureCheckComplete = false;

	std::wstring interposerPath = std::wstring(Streamline::PluginDir) + L"\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		featureCheckComplete = true;
		return;
	}

	std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	Streamline::dllVersions = Util::EnumerateDllVersions(pluginDir);

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eOff;
	pref.logMessageCallback = nullptr;
	pref.showConsole = false;
	std::error_code pluginPathError;
	auto pluginDirAbsolute = std::filesystem::absolute(std::filesystem::path(Streamline::PluginDir), pluginPathError);
	if (pluginPathError)
		pluginDirAbsolute = std::filesystem::path(Streamline::PluginDir);
	static std::wstring pluginDirAbsoluteW;
	pluginDirAbsoluteW = pluginDirAbsolute.wstring();
	static const wchar_t* pluginPaths[1]{};
	pluginPaths[0] = pluginDirAbsoluteW.c_str();
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		featureCheckComplete = true;
	} else {
		initialized = true;
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		InvalidateDLSSOptionsCache();
		reflexOptionsCache = {};
		lastReflexSleepFrame = UINT32_MAX;
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	featureCheckComplete = false;
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);
	reflexSupportedOnCurrentAdapter = adapterDesc.VendorId == NVIDIA_VENDOR_ID;

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	auto checkFeatureAvailability = [&](sl::Feature feature, bool& outAvailable) {
		outAvailable = false;
		bool loaded = false;
		if (SL_FAILED(result, slIsFeatureLoaded(feature, loaded))) {
			return;
		}
		if (!loaded) {
			return;
		}

		outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
	};

	checkFeatureAvailability(sl::kFeatureDLSS, featureDLSS);
	if (reflexSupportedOnCurrentAdapter) {
		checkFeatureAvailability(sl::kFeatureReflex, featureReflex);
		checkFeatureAvailability(sl::kFeaturePCL, featurePCL);
	} else {
		featureReflex = false;
		featurePCL = false;
	}

	if (featureDLSS) {
		isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);
	}
	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
	featureCheckComplete = true;
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	featureReflex = false;
	featurePCL = false;

	if (slGetFeatureFunction && reflexSupportedOnCurrentAdapter) {
		if (slSetFeatureLoaded) {
			const auto requestFeatureLoad = [&](sl::Feature feature) {
				(void)slSetFeatureLoaded(feature, true);
			};

			requestFeatureLoad(sl::kFeatureReflex);
			requestFeatureLoad(sl::kFeaturePCL);
		}

		const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
			fn = nullptr;
			const sl::Result bindResult = slGetFeatureFunction(feature, functionName, fn);
			return bindResult == sl::Result::eOk && fn != nullptr;
		};

		bool reflexFnsBound = true;
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		featureReflex = reflexFnsBound && slReflexSetOptions && slReflexSleep;

		slPCLSetMarker = nullptr;
		bool pclFnBound = bindFeatureFn(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		featurePCL = pclFnBound && slPCLSetMarker;
	}

	InvalidateDLSSOptionsCache();
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
bool Streamline::EnsureFrameToken()
{
	if (!initialized || !slGetNewFrameToken || !globals::state)
		return false;

	if (!frameChecker.IsNewFrame())
		return frameToken != nullptr;

	if (SL_FAILED(result, slGetNewFrameToken(frameToken, &globals::state->frameCount))) {
		frameToken = nullptr;
		return false;
	}

	return frameToken != nullptr;
}

bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport, uint32_t eyeIndex, float viewportScaleX, float viewportScaleY, float pinholeOffsetX, float pinholeOffsetY, const DLSSDispatchSignature* dispatchSignature)
{
	if (!globals::features::upscaling.streamline.initialized)
		return false;

	if (!EnsureFrameToken())
		return false;

	// In VR, we need to set constants for each viewport/eye separately
	// In non-VR, this is called once per frame
	auto state = globals::state;
	auto& upscaling = globals::features::upscaling;
	if (!state)
		return false;
	bool applyCroppedConstantsCorrection = false;
	float clampedViewportScaleX = std::clamp(viewportScaleX, 1e-4f, 1.0f);
	float clampedViewportScaleY = std::clamp(viewportScaleY, 1e-4f, 1.0f);
	float clampedPinholeOffsetX = std::isfinite(pinholeOffsetX) ? std::clamp(pinholeOffsetX, -1.0f, 1.0f) : 0.0f;
	float clampedPinholeOffsetY = std::isfinite(pinholeOffsetY) ? std::clamp(pinholeOffsetY, -1.0f, 1.0f) : 0.0f;
	if (!globals::game::isVR) {
		clampedViewportScaleX = 1.0f;
		clampedViewportScaleY = 1.0f;
		clampedPinholeOffsetX = 0.0f;
		clampedPinholeOffsetY = 0.0f;
	}

	sl::Constants slConstants = {};

	// Calculate aspect ratio for the SINGLE EYE
	float2 fullOutputSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
		fullOutputSize = state->screenSize;
	float eyeWidth = fullOutputSize.x * (globals::game::isVR ? 0.5f : 1.0f);
	float eyeHeight = fullOutputSize.y;
	slConstants.cameraAspectRatio = (eyeWidth * clampedViewportScaleX) / (eyeHeight * clampedViewportScaleY);

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse(eyeIndex).Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered(eyeIndex).Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	if (globals::game::isVR) {
		const bool isCroppedViewport = clampedViewportScaleX < 0.999f || clampedViewportScaleY < 0.999f;
		applyCroppedConstantsCorrection = isCroppedViewport;
		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;

			// Match projection to the cropped DLSS viewport so temporal reprojection
			// operates in the same clip space as color/depth/mvec inputs.
			slConstants.cameraViewToClip[0].x *= invScaleX;
			slConstants.cameraViewToClip[0].y *= invScaleX;
			slConstants.cameraViewToClip[0].z *= invScaleX;
			slConstants.cameraViewToClip[0].w *= invScaleX;
			slConstants.cameraViewToClip[1].x *= invScaleY;
			slConstants.cameraViewToClip[1].y *= invScaleY;
			slConstants.cameraViewToClip[1].z *= invScaleY;
			slConstants.cameraViewToClip[1].w *= invScaleY;

			// cameraFOV is vertical; scale by cropped Y region.
			slConstants.cameraFOV = 2.0f * atanf(clampedViewportScaleY * tanf(slConstants.cameraFOV * 0.5f));
			slConstants.cameraPinholeOffset = {
				clampedPinholeOffsetX / clampedViewportScaleX,
				clampedPinholeOffsetY / clampedViewportScaleY
			};
		}

		// VR: compute clipToCameraView / clipToPrevClip / prevClipToClip from Skyrim's per-eye matrices.
		// recalculateCameraMatrices() uses a single static prev-frame slot -- unusable for two viewports.
		sl::matrixFullInvert(slConstants.clipToCameraView, slConstants.cameraViewToClip);

		auto currViewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Transpose();
		auto prevViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex).Transpose();

		sl::float4x4 currViewProjSL = *(sl::float4x4*)&currViewProj;
		sl::float4x4 prevViewProjSL = *(sl::float4x4*)&prevViewProj;

		sl::float4x4 invCurrViewProj;
		sl::matrixFullInvert(invCurrViewProj, currViewProjSL);
		sl::matrixMul(slConstants.clipToPrevClip, invCurrViewProj, prevViewProjSL);

		if (applyCroppedConstantsCorrection) {
			const float invScaleX = 1.0f / clampedViewportScaleX;
			const float invScaleY = 1.0f / clampedViewportScaleY;
			const float leftFactors[4] = { clampedViewportScaleX, clampedViewportScaleY, 1.0f, 1.0f };
			const float rightFactors[4] = { invScaleX, invScaleY, 1.0f, 1.0f };

			// Conjugate clipToPrevClip into cropped clip-space basis:
			// CTP_cropped = inv(S) * CTP * S
			float* ctpValues = &slConstants.clipToPrevClip[0].x;
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t col = 0; col < 4; ++col) {
					ctpValues[row * 4 + col] *= leftFactors[row] * rightFactors[col];
				}
			}
		}

		sl::matrixFullInvert(slConstants.prevClipToClip, slConstants.clipToPrevClip);
	} else {
		recalculateCameraMatrices(slConstants);
	}

	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	const bool requestHistoryReset = upscaling.ShouldResetHistoryThisFrame();
	slConstants.reset = requestHistoryReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	if (globals::game::isVR && applyCroppedConstantsCorrection) {
		slConstants.mvecScale = { 1.0f / clampedViewportScaleX, 1.0f / clampedViewportScaleY };
	} else {
		slConstants.mvecScale = { 1.0f, 1.0f };
	}
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	const auto makeFrameConstantsSignature = [&]() {
		DLSSFrameConstantsCache signature{};
		signature.valid = true;
		signature.frame = dispatchSignature ? dispatchSignature->frame : state->frameCount;
		signature.frameToken = reinterpret_cast<std::uintptr_t>(frameToken);
		signature.viewport = static_cast<uint32_t>(p_viewport);
		signature.eyeIndex = eyeIndex;
		signature.viewportRole = dispatchSignature ? static_cast<uint32_t>(dispatchSignature->viewportRole) : static_cast<uint32_t>(DLSSViewportRole::FullEye);
		signature.outputWidth = dispatchSignature ? dispatchSignature->outputWidth : 0u;
		signature.outputHeight = dispatchSignature ? dispatchSignature->outputHeight : 0u;
		signature.qualityMode = dispatchSignature ? dispatchSignature->qualityMode : 0u;
		signature.dlssPreset = dispatchSignature ? dispatchSignature->dlssPreset : 0u;
		signature.extentInWidth = dispatchSignature ? dispatchSignature->extentIn.width : 0u;
		signature.extentInHeight = dispatchSignature ? dispatchSignature->extentIn.height : 0u;
		signature.extentOutWidth = dispatchSignature ? dispatchSignature->extentOut.width : 0u;
		signature.extentOutHeight = dispatchSignature ? dispatchSignature->extentOut.height : 0u;
		signature.viewportScaleXQ = QuantizeDLSSSignatureFloat(clampedViewportScaleX);
		signature.viewportScaleYQ = QuantizeDLSSSignatureFloat(clampedViewportScaleY);
		signature.pinholeOffsetXQ = QuantizeDLSSSignatureFloat(clampedPinholeOffsetX);
		signature.pinholeOffsetYQ = QuantizeDLSSSignatureFloat(clampedPinholeOffsetY);
		signature.jitterXQ = QuantizeDLSSSignatureFloat(upscaling.jitter.x);
		signature.jitterYQ = QuantizeDLSSSignatureFloat(upscaling.jitter.y);
		signature.historyResetRequested = requestHistoryReset;
		return signature;
	};
	const auto frameConstantsMatch = [](const DLSSFrameConstantsCache& a_cached, const DLSSFrameConstantsCache& a_signature) {
		return a_cached.valid &&
		       a_cached.frame == a_signature.frame &&
		       a_cached.frameToken == a_signature.frameToken &&
		       a_cached.viewport == a_signature.viewport &&
		       a_cached.eyeIndex == a_signature.eyeIndex &&
		       a_cached.viewportRole == a_signature.viewportRole &&
		       a_cached.outputWidth == a_signature.outputWidth &&
		       a_cached.outputHeight == a_signature.outputHeight &&
		       a_cached.qualityMode == a_signature.qualityMode &&
		       a_cached.dlssPreset == a_signature.dlssPreset &&
		       a_cached.extentInWidth == a_signature.extentInWidth &&
		       a_cached.extentInHeight == a_signature.extentInHeight &&
		       a_cached.extentOutWidth == a_signature.extentOutWidth &&
		       a_cached.extentOutHeight == a_signature.extentOutHeight &&
		       a_cached.viewportScaleXQ == a_signature.viewportScaleXQ &&
		       a_cached.viewportScaleYQ == a_signature.viewportScaleYQ &&
		       a_cached.pinholeOffsetXQ == a_signature.pinholeOffsetXQ &&
		       a_cached.pinholeOffsetYQ == a_signature.pinholeOffsetYQ &&
		       a_cached.jitterXQ == a_signature.jitterXQ &&
		       a_cached.jitterYQ == a_signature.jitterYQ &&
		       a_cached.historyResetRequested == a_signature.historyResetRequested;
	};
	const bool canAcceptDuplicateConstants =
		dispatchSignature &&
		dispatchSignature->submitStageVRDLSS &&
		(dispatchSignature->viewportRole == DLSSViewportRole::FullEye ||
			dispatchSignature->viewportRole == DLSSViewportRole::SubmitStageFoveatedCenter);
	DLSSFrameConstantsCache frameConstantsSignature{};
	if (canAcceptDuplicateConstants)
		frameConstantsSignature = makeFrameConstantsSignature();
	const auto hasCachedFrameConstantsSignature = [&]() {
		if (!canAcceptDuplicateConstants)
			return false;

		for (const auto& cachedSignature : dlssFrameConstantsCache) {
			if (frameConstantsMatch(cachedSignature, frameConstantsSignature))
				return true;
		}
		return false;
	};
	if (hasCachedFrameConstantsSignature()) {
		lastDLSSFailureDuplicatedConstants = false;
		return true;
	}

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		lastDLSSFailureDuplicatedConstants = res == sl::Result::eErrorDuplicatedConstants;
		return false;
	}

	if (canAcceptDuplicateConstants) {
		auto* targetSlot = &dlssFrameConstantsCache[static_cast<uint32_t>(p_viewport) % dlssFrameConstantsCache.size()];
		for (auto& cachedSignature : dlssFrameConstantsCache) {
			if (!cachedSignature.valid ||
				(cachedSignature.viewport == frameConstantsSignature.viewport &&
					cachedSignature.eyeIndex == frameConstantsSignature.eyeIndex &&
					cachedSignature.viewportRole == frameConstantsSignature.viewportRole)) {
				targetSlot = &cachedSignature;
				break;
			}
		}
		*targetSlot = frameConstantsSignature;
	}
	return true;
}

bool Streamline::IsRTXAndBelow40Series(IDXGIAdapter* a_adapter)
{
	DXGI_ADAPTER_DESC adapterDesc = {};

	a_adapter->GetDesc(&adapterDesc);

	UINT vendorId = adapterDesc.VendorId;
	UINT deviceId = adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != 0x10DE)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x2600)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

bool Streamline::SetDLSSOptions(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t width, uint32_t height, bool colorBuffersHDR, uint32_t qualityMode, uint32_t dlssPreset)
{
	if (!slDLSSSetOptions)
		return false;

	// Map custom render-scale presets to the nearest supported DLSS mode.
	qualityMode = std::min(qualityMode, Upscaling::kQualityModeMaxIndex);
	dlssPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);

	bool useLegacyProfile = isRTXBelow40series;
	auto& cache = GetDLSSOptionsCache(viewportRole, eyeIndex, qualityMode, dlssPreset);
	const uint32_t viewportKey = static_cast<uint32_t>(p_viewport);
	if (cache.valid &&
		cache.viewport == viewportKey &&
		cache.outputWidth == width &&
		cache.outputHeight == height &&
		cache.qualityMode == qualityMode &&
		cache.dlssPreset == dlssPreset &&
		cache.isHDR == colorBuffersHDR &&
		cache.useLegacyProfile == useLegacyProfile) {
		return true;
	}

	sl::DLSSOptions dlssOptions{};
	switch (qualityMode) {
	case 1:
	case 2:
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 5:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 6:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = height;
	dlssOptions.colorBuffersHDR = colorBuffersHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	sl::DLSSPreset selectedPreset = sl::DLSSPreset::ePresetK;
	switch (dlssPreset) {
	case Upscaling::kDLSSPresetJ:
		selectedPreset = sl::DLSSPreset::ePresetJ;
		break;
	case Upscaling::kDLSSPresetK:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	case Upscaling::kDLSSPresetL:
		selectedPreset = sl::DLSSPreset::ePresetL;
		break;
	case Upscaling::kDLSSPresetM:
		selectedPreset = sl::DLSSPreset::ePresetM;
		break;
	case Upscaling::kDLSSPresetF:
		selectedPreset = sl::DLSSPreset::ePresetF;
		break;
	case Upscaling::kDLSSPresetE:
		selectedPreset = sl::DLSSPreset::ePresetE;
		break;
	default:
		selectedPreset = sl::DLSSPreset::ePresetK;
		break;
	}

	dlssOptions.dlaaPreset = selectedPreset;
	dlssOptions.ultraQualityPreset = selectedPreset;
	dlssOptions.qualityPreset = selectedPreset;
	dlssOptions.balancedPreset = selectedPreset;
	dlssOptions.performancePreset = selectedPreset;
	dlssOptions.ultraPerformancePreset = selectedPreset;

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
		cache.valid = false;
		return false;
	}

	cache.valid = true;
	cache.viewport = viewportKey;
	cache.outputWidth = width;
	cache.outputHeight = height;
	cache.qualityMode = qualityMode;
	cache.dlssPreset = dlssPreset;
	cache.isHDR = colorBuffersHDR;
	cache.useLegacyProfile = useLegacyProfile;
	if (p_viewport == viewport) {
		activeDLSSViewportResourcesAllocated[0] = true;
	} else if (p_viewport == viewportRight) {
		activeDLSSViewportResourcesAllocated[1] = true;
	} else {
		for (auto& roleSlots : vrDLSSViewportSlots) {
			for (auto& slot : roleSlots) {
				for (uint32_t eye = 0; eye < 2; ++eye) {
					if (slot.viewport[eye] == p_viewport) {
						slot.resourcesAllocated[eye] = true;
						return true;
					}
				}
			}
		}
	}
	return true;
}

int Streamline::FindVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset) const
{
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		const auto& viewportSlot = vrDLSSViewportSlots[roleIndex][slot];
		if (viewportSlot.valid &&
			viewportSlot.qualityMode == clampedQualityMode &&
			viewportSlot.dlssPreset == clampedPreset) {
			return static_cast<int>(slot);
		}
	}

	return -1;
}

int Streamline::ChooseVRDLSSViewportSlotForAllocation(DLSSViewportRole viewportRole) const
{
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	for (uint32_t slot = 0; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (!vrDLSSViewportSlots[roleIndex][slot].valid)
			return static_cast<int>(slot);
	}

	uint32_t lruSlot = 0;
	uint64_t lruCounter = vrDLSSViewportSlots[roleIndex][0].lastUse;
	for (uint32_t slot = 1; slot < kVRDLSSViewportSlotCount; ++slot) {
		if (vrDLSSViewportSlots[roleIndex][slot].lastUse < lruCounter) {
			lruCounter = vrDLSSViewportSlots[roleIndex][slot].lastUse;
			lruSlot = slot;
		}
	}

	return static_cast<int>(lruSlot);
}

bool Streamline::FreeDLSSViewportResources(sl::ViewportHandle a_viewport)
{
	if (!slDLSSSetOptions || !slFreeResources)
		return true;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	slDLSSSetOptions(a_viewport, dlssOptions);

	const sl::Result freeResult = slFreeResources(sl::kFeatureDLSS, a_viewport);
	return freeResult == sl::Result::eOk;
}

bool Streamline::FreeVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t slotIndex)
{
	if (slotIndex >= kVRDLSSViewportSlotCount)
		return true;

	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);
	auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
	if (!slot.valid)
		return true;

	bool slotResourcesFreed = true;
	for (uint32_t eye = 0; eye < 2; ++eye) {
		slot.resourcesAllocated[eye] = slot.resourcesAllocated[eye] || slot.optionsCache[eye].valid;
		if (slot.resourcesAllocated[eye]) {
			const bool eyeFreed = FreeDLSSViewportResources(slot.viewport[eye]);
			slotResourcesFreed = eyeFreed && slotResourcesFreed;
			if (eyeFreed)
				slot.resourcesAllocated[eye] = false;
		}
		slot.optionsCache[eye] = {};
	}

	if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
		return false;

	slot.valid = false;
	slot.qualityMode = 0;
	slot.dlssPreset = 0;
	slot.lastUse = 0;
	return slotResourcesFreed;
}

bool Streamline::ResolveDLSSViewport(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset, sl::ViewportHandle& outViewport)
{
	outViewport = p_viewport;
	if (!globals::game::isVR)
		return true;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);

	int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex < 0) {
		slotIndex = ChooseVRDLSSViewportSlotForAllocation(viewportRole);
		if (slotIndex < 0)
			slotIndex = 0;

		auto& slot = vrDLSSViewportSlots[roleIndex][slotIndex];
		if (slot.valid) {
			if (auto context = globals::d3d::context) {
				if (BeginOrPollD3D11IdleFence(context, pendingVRDLSSSlotRecycleIdleFence) != D3D11IdleFenceResult::Ready) {
					nonVRDLSSOptionsCache.valid = false;
					return false;
				}
			} else {
				ReleaseD3D11IdleFence(pendingVRDLSSSlotRecycleIdleFence);
			}
			if (!FreeVRDLSSViewportSlot(viewportRole, static_cast<uint32_t>(slotIndex))) {
				nonVRDLSSOptionsCache.valid = false;
				return false;
			}
		}

		slot.valid = true;
		slot.qualityMode = clampedQualityMode;
		slot.dlssPreset = clampedPreset;
		slot.lastUse = 0;
		slot.resourcesAllocated[0] = false;
		slot.resourcesAllocated[1] = false;
		for (auto& optionsCache : slot.optionsCache)
			optionsCache = {};

		const uint32_t viewportBase =
			kVRDLSSSlotViewportBase +
			(roleIndex * kVRDLSSSlotViewportRoleStride) +
			(static_cast<uint32_t>(slotIndex) * kVRDLSSSlotViewportEyeStride);
		slot.viewport[0] = sl::ViewportHandle(viewportBase);
		slot.viewport[1] = sl::ViewportHandle(viewportBase + 1);
	}

	auto& activeSlot = vrDLSSViewportSlots[roleIndex][slotIndex];
	activeSlot.lastUse = ++vrDLSSViewportUseCounter;
	outViewport = activeSlot.viewport[eye];
	return true;
}

Streamline::DLSSOptionsCache& Streamline::GetDLSSOptionsCache(DLSSViewportRole viewportRole, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset)
{
	if (!globals::game::isVR)
		return nonVRDLSSOptionsCache;

	const uint32_t eye = eyeIndex > 0 ? 1u : 0u;
	const uint32_t clampedQualityMode = std::min<uint32_t>(qualityMode, Upscaling::kQualityModeMaxIndex);
	const uint32_t clampedPreset = Upscaling::ClampDLSSPresetUInt(dlssPreset);
	const uint32_t roleIndex = GetDLSSViewportRoleIndex(viewportRole);

	const int slotIndex = FindVRDLSSViewportSlot(viewportRole, clampedQualityMode, clampedPreset);
	if (slotIndex >= 0)
		return vrDLSSViewportSlots[roleIndex][slotIndex].optionsCache[eye];

	// Fallback for unexpected ordering; keeps behavior deterministic and forces option re-apply.
	nonVRDLSSOptionsCache.valid = false;
	return nonVRDLSSOptionsCache;
}

void Streamline::InvalidateDLSSOptionsCache()
{
	nonVRDLSSOptionsCache = {};
	dlssFrameConstantsCache = {};
	for (auto& roleSlots : vrDLSSViewportSlots) {
		for (auto& slot : roleSlots) {
			for (auto& optionsCache : slot.optionsCache)
				optionsCache = {};
		}
	}
}

void Streamline::ResetDLSSIdleFences()
{
	ReleaseD3D11IdleFence(pendingDLSSResourceFreeIdleFence);
	ReleaseD3D11IdleFence(pendingVRDLSSSlotRecycleIdleFence);
}

void Streamline::ResetFrameTracking()
{
	frameToken = nullptr;
	frameChecker = {};
	dlssFrameConstantsCache = {};
}

bool Streamline::HasDLSSResourcesPendingTeardown() const
{
	if (pendingDLSSResourceFreeIdleFence || pendingVRDLSSSlotRecycleIdleFence)
		return true;

	// If DLSS is not active/available in this process, cached slot metadata
	// should not trigger a teardown cooldown by itself.
	if (!initialized || !featureDLSS)
		return false;

	if (activeDLSSViewportResourcesAllocated[0] || activeDLSSViewportResourcesAllocated[1])
		return true;

	if (nonVRDLSSOptionsCache.valid)
		return true;

	for (const auto& roleSlots : vrDLSSViewportSlots) {
		for (const auto& slot : roleSlots) {
			if (slot.valid)
				return true;

			if (slot.resourcesAllocated[0] || slot.resourcesAllocated[1])
				return true;

			for (const auto& optionsCache : slot.optionsCache) {
				if (optionsCache.valid)
					return true;
			}
		}
	}

	return false;
}

bool Streamline::EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth,
	float pinholeOffsetX, float pinholeOffsetY, DLSSViewportRole viewportRole)
{
	auto context = globals::d3d::context;
	if (!initialized || !featureDLSS || !slEvaluateFeature || !context ||
		!colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;
	if (globals::game::isVR && eyeIndex > 1)
		return false;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	auto& upscaling = globals::features::upscaling;
	auto state = globals::state;
	float viewportScaleX = 1.0f;
	float viewportScaleY = 1.0f;
	if (state) {
		const auto& resolutionPlan = upscaling.GetRuntimeResolutionPlan();
		auto fullOutputSize = resolutionPlan.finalOutputSize;
		if (fullOutputSize.x <= 0.0f || fullOutputSize.y <= 0.0f)
			fullOutputSize = state->screenSize;

		const float fullOutputWidth = globals::game::isVR ? (fullOutputSize.x * 0.5f) : fullOutputSize.x;
		const float fullOutputHeight = fullOutputSize.y;
		if (fullOutputWidth > 0.0f && fullOutputHeight > 0.0f) {
			viewportScaleX = std::clamp(static_cast<float>(extentOut.width) / fullOutputWidth, 1e-4f, 1.0f);
			viewportScaleY = std::clamp(static_cast<float>(extentOut.height) / fullOutputHeight, 1e-4f, 1.0f);
		}
	}

	const bool colorBuffersHDR = GetDLSSColorBuffersHDR(colorIn);
	const uint32_t qualityMode = std::min(upscaling.GetRuntimeQualityMode(), Upscaling::kQualityModeMaxIndex);
	const uint32_t dlssPreset = upscaling.GetRuntimeDLSSPreset();
	const bool submitStageVRDLSS =
		globals::game::isVR &&
		upscaling.IsPresentationUpscalingActive();

	DLSSDispatchSignature dispatchSignature{};
	dispatchSignature.frame = state ? state->frameCount : 0u;
	dispatchSignature.extentIn = extentIn;
	dispatchSignature.extentOut = extentOut;
	dispatchSignature.outputWidth = outputWidth;
	dispatchSignature.outputHeight = extentOut.height;
	dispatchSignature.qualityMode = qualityMode;
	dispatchSignature.dlssPreset = dlssPreset;
	dispatchSignature.viewportRole = viewportRole;
	dispatchSignature.submitStageVRDLSS = submitStageVRDLSS;

	if (!ResolveDLSSViewport(viewportRole, vp, eyeIndex, qualityMode, dlssPreset, vp))
		return false;

	if (!CheckFrameConstants(vp, eyeIndex, viewportScaleX, viewportScaleY, pinholeOffsetX, pinholeOffsetY, &dispatchSignature))
		return false;
	if (!SetDLSSOptions(viewportRole, vp, eyeIndex, outputWidth, extentOut.height, colorBuffersHDR, qualityMode, dlssPreset))
		return false;

	const bool emitPCLMarkers =
		!submitStageVRDLSS &&
		upscaling.settings.reflexUseMarkersToOptimize &&
		reflexOptionsCache.useMarkersToOptimize &&
		featurePCL;
	const auto emitPCLMarker = [&](sl::PCLMarker marker) {
		if (!emitPCLMarkers || !slPCLSetMarker || !frameToken)
			return;
		slPCLSetMarker(marker, *frameToken);
	};

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn }
	};

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = {
		&view,
		&tags[0],
		&tags[1],
		&tags[2],
		&tags[3],
		&tags[4],
		&tags[5]
	};

	if (state && state->frameAnnotations) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "DLSS Evaluate Eye %u", eyeIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("DLSS Evaluate");
		}
	}

	emitPCLMarker(sl::PCLMarker::eRenderSubmitStart);
	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
	emitPCLMarker(sl::PCLMarker::eRenderSubmitEnd);

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	return evalResult == sl::Result::eOk;
}

bool Streamline::UpscaleRegion(uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
	float pinholeOffsetX, float pinholeOffsetY)
{
	if (!initialized || !featureDLSS || !colorIn || !colorOut || !depth || !mvec || !reactiveMask || !transparencyMask)
		return false;

	sl::ViewportHandle vp = (globals::game::isVR && eyeIndex == 1) ? viewportRight : viewport;
	sl::Extent extentIn{ 0u, 0u, renderWidth, renderHeight };
	sl::Extent extentOut{ 0u, 0u, outputWidth, outputHeight };

	return EvaluateDLSS(vp, eyeIndex, colorIn, colorOut, depth, mvec, reactiveMask, transparencyMask, extentIn, extentOut, outputWidth, pinholeOffsetX, pinholeOffsetY);
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	auto state = globals::state;

	auto renderer = globals::game::renderer;
	if (!state || !renderer)
		return;

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto& upscaling = globals::features::upscaling;
	if (globals::game::isVR && upscaling.IsPresentationUpscalingActive()) {
		upscaling.dlssUpscaleOutputInSharpenerTexture = false;
		return;
	}

	auto screenSize = upscaling.GetRuntimeResolutionPlan().finalOutputSize;
	auto renderSize = upscaling.GetRuntimeResolutionPlan().engineRenderSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
		screenSize = state->screenSize;
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
		renderSize = Util::ConvertToDynamic(screenSize);
	auto& mainTarget = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	const bool useSharpenerOutput =
		upscaling.ShouldApplyDLSSSharpening() &&
		upscaling.sharpenerTexture &&
		upscaling.sharpenerTexture->resource &&
		upscaling.sharpenerTexture->uav;
	ID3D11Resource* colorOut = useSharpenerOutput ? upscaling.sharpenerTexture->resource.get() : a_upscalingTexture;
	ID3D11UnorderedAccessView* colorOutUAV = useSharpenerOutput ? upscaling.sharpenerTexture->uav.get() : mainTarget.UAV;
	const bool outputToSharpener = useSharpenerOutput;

	// VR: Combined-buffer mode with extent offsets causes temporal ghosting on the right eye
	// because DLSS's internal history buffers use extent offsets as indices.
	// Per-eye isolation with extents at {0,0} is required.
	if (globals::game::isVR) {
		auto context = globals::d3d::context;
		uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
		uint32_t eyeHeightOut = (uint32_t)screenSize.y;
		uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
		uint32_t eyeHeightIn = (uint32_t)renderSize.y;

		// Split the combined stereo inputs up front. The direct left-eye path still
		// uses the native depth buffer, but isolated-output fallback needs valid
		// per-eye depth for both eyes.
		if (!upscaling.PreparePerEyeInputs(
				a_upscalingTexture,
				depthTexture.texture,
				a_motionVectors,
				a_reactiveMask,
				a_transparencyCompositionMask,
				false,
				true)) {
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return;
		}

		const bool perEyeResourcesReady = upscaling.AreVRPerEyeUpscalingResourcesReady(true, false);
		if (!perEyeResourcesReady) {
			upscaling.dlssUpscaleOutputInSharpenerTexture = false;
			return;
		}

		sl::Extent extentIn{ 0, 0, eyeWidthIn, eyeHeightIn };
		sl::Extent extentOut{ 0, 0, eyeWidthOut, eyeHeightOut };
		auto presentStretchFallback = [&]() {
			bool stretched = true;
			for (uint32_t i = 0; i < 2; ++i)
				stretched = upscaling.StretchSubmitStageEyeOutput(i, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut) && stretched;
			if (stretched)
				upscaling.FinalizePerEyeOutputs(colorOut);
			return stretched;
		};

		const bool canUseDirectEye0 =
			perEyeResourcesReady;

		if (!canUseDirectEye0) {
			bool allEvaluated = true;
			for (uint32_t i = 0; i < 2; ++i) {
				sl::ViewportHandle vp = (i == 1) ? viewportRight : viewport;
				const bool eyeEvaluated = EvaluateDLSS(vp, i,
					upscaling.vrIntermediateColorIn[i]->resource.get(), upscaling.vrIntermediateColorOut[i]->resource.get(),
					upscaling.vrIntermediateDepth[i]->resource.get(), upscaling.vrIntermediateMotionVectors[i]->resource.get(),
					upscaling.vrIntermediateReactiveMask[i]->resource.get(), upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
					extentIn, extentOut, eyeWidthOut,
					0.0f,
					0.0f);
				upscaling.RecordVRDLSSFullEyeEvaluation(i, eyeEvaluated);
				allEvaluated &= eyeEvaluated;
			}

			bool fallbackPresented = false;
			if (allEvaluated) {
				upscaling.FinalizePerEyeOutputs(colorOut);
			} else {
				upscaling.RequestHistoryReset();
				fallbackPresented = presentStretchFallback();
			}
			upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && (allEvaluated || fallbackPresented);
			return;
		}

		// Copy right-eye depth before eye 0 evaluation; eye 0 output can overlap right-eye input
		// in the combined target at non-DLAA scales.
		D3D11_BOX rightIn = { eyeWidthIn, 0, 0, eyeWidthIn * 2, eyeHeightIn, 1 };
		context->CopySubresourceRegion(upscaling.vrIntermediateDepth[1]->resource.get(), 0, 0, 0, 0, depthTexture.texture, 0, &rightIn);
		const bool canRestoreDirectEye0Output =
			!outputToSharpener &&
			upscaling.vrIntermediateColorOut[0] &&
			upscaling.vrIntermediateColorOut[0]->resource;
		if (canRestoreDirectEye0Output) {
			D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(upscaling.vrIntermediateColorOut[0]->resource.get(), 0, 0, 0, 0, colorOut, 0, &leftOutBackup);
		}

		// Eye 0 writes directly to combined output.
		const bool leftEvaluated = EvaluateDLSS(viewport, 0,
			upscaling.vrIntermediateColorIn[0]->resource.get(), colorOut,
			depthTexture.texture, upscaling.vrIntermediateMotionVectors[0]->resource.get(),
			upscaling.vrIntermediateReactiveMask[0]->resource.get(), upscaling.vrIntermediateTransparencyMask[0]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f);
		upscaling.RecordVRDLSSFullEyeEvaluation(0, leftEvaluated);

		// Eye 1 writes to intermediate, then copy into right half of combined output.
		const bool rightEvaluated = EvaluateDLSS(viewportRight, 1,
			upscaling.vrIntermediateColorIn[1]->resource.get(), upscaling.vrIntermediateColorOut[1]->resource.get(),
			upscaling.vrIntermediateDepth[1]->resource.get(), upscaling.vrIntermediateMotionVectors[1]->resource.get(),
			upscaling.vrIntermediateReactiveMask[1]->resource.get(), upscaling.vrIntermediateTransparencyMask[1]->resource.get(),
			extentIn, extentOut, eyeWidthOut,
			0.0f,
			0.0f);
		upscaling.RecordVRDLSSFullEyeEvaluation(1, rightEvaluated);

		if (leftEvaluated && rightEvaluated) {
			if (depthTexture.depthSRV) {
				upscaling.ClearVRDirectUpscaledEyeOutput(0, colorOutUAV, depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
				upscaling.ClearVRDirectUpscaledEyeOutput(1, upscaling.vrIntermediateColorOut[1]->uav.get(), depthTexture.depthSRV, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
			}

			D3D11_BOX rightOut = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
			context->CopySubresourceRegion(colorOut, 0, eyeWidthOut, 0, 0, upscaling.vrIntermediateColorOut[1]->resource.get(), 0, &rightOut);
		}

		bool fallbackPresented = false;
		if (!leftEvaluated || !rightEvaluated) {
			upscaling.RequestHistoryReset();
			fallbackPresented = presentStretchFallback();
			if (!fallbackPresented && canRestoreDirectEye0Output) {
				D3D11_BOX leftOutBackup = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
				context->CopySubresourceRegion(colorOut, 0, 0, 0, 0, upscaling.vrIntermediateColorOut[0]->resource.get(), 0, &leftOutBackup);
			}
		}

		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && ((leftEvaluated && rightEvaluated) || fallbackPresented);

	} else {
		// Non-VR: Simple full-texture upscale
		sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		const bool evaluated = EvaluateDLSS(viewport, 0,
			a_upscalingTexture, colorOut,
			depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, (uint)screenSize.x,
			0.0f,
			0.0f);
		upscaling.dlssUpscaleOutputInSharpenerTexture = outputToSharpener && evaluated;
		if (!evaluated) {
			upscaling.RequestHistoryReset();
		}
	}
}
/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
bool Streamline::DestroyDLSSResources()
{
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slFreeResources) {
		ResetDLSSIdleFences();
		InvalidateDLSSOptionsCache();
		activeDLSSViewportResourcesAllocated = {};
		ResetFrameTracking();
		return true;
	}

	if (auto context = globals::d3d::context) {
		if (BeginOrPollD3D11IdleFence(context, pendingDLSSResourceFreeIdleFence) != D3D11IdleFenceResult::Ready) {
			return false;
		}
	} else {
		ResetDLSSIdleFences();
	}

	bool activeViewportResourcesFreed = true;
	if (activeDLSSViewportResourcesAllocated[0]) {
		const bool leftFreed = FreeDLSSViewportResources(viewport);
		activeViewportResourcesFreed = leftFreed && activeViewportResourcesFreed;
		if (leftFreed)
			activeDLSSViewportResourcesAllocated[0] = false;
	}

	if (globals::game::isVR) {
		if (activeDLSSViewportResourcesAllocated[1]) {
			const bool rightFreed = FreeDLSSViewportResources(viewportRight);
			activeViewportResourcesFreed = rightFreed && activeViewportResourcesFreed;
			if (rightFreed)
				activeDLSSViewportResourcesAllocated[1] = false;
		}
		for (uint32_t roleIndex = 0; roleIndex < kVRDLSSViewportRoleCount; ++roleIndex) {
			for (uint32_t slotIndex = 0; slotIndex < kVRDLSSViewportSlotCount; ++slotIndex) {
				const bool slotFreed = FreeVRDLSSViewportSlot(static_cast<DLSSViewportRole>(roleIndex), slotIndex);
				activeViewportResourcesFreed = slotFreed && activeViewportResourcesFreed;
			}
		}
	}

	ResetDLSSIdleFences();
	InvalidateDLSSOptionsCache();
	vrDLSSViewportUseCounter = 0;
	ResetFrameTracking();
	return activeViewportResourcesFreed;
}

void Streamline::UpdateReflex()
{
	if (!initialized || !reflexSupportedOnCurrentAdapter || !featureReflex || !slReflexSetOptions)
		return;

	const auto& upscaling = globals::features::upscaling;
	const bool reflexBlockedByFrameGeneration = upscaling.IsFrameGenerationDx12PathActive();
	if (reflexBlockedByFrameGeneration) {
		const bool reflexAlreadyOff = reflexOptionsCache.valid &&
		                              reflexOptionsCache.mode == sl::ReflexMode::eOff &&
		                              reflexOptionsCache.frameLimitUs == 0 &&
		                              !reflexOptionsCache.useMarkersToOptimize;
		if (!reflexAlreadyOff) {
			sl::ReflexOptions disableOptions{};
			disableOptions.mode = sl::ReflexMode::eOff;
			disableOptions.frameLimitUs = 0;
			disableOptions.useMarkersToOptimize = false;
			if (slReflexSetOptions(disableOptions) == sl::Result::eOk) {
				reflexOptionsCache.valid = true;
				reflexOptionsCache.mode = disableOptions.mode;
				reflexOptionsCache.frameLimitUs = disableOptions.frameLimitUs;
				reflexOptionsCache.useMarkersToOptimize = disableOptions.useMarkersToOptimize;
			}
		}
		lastReflexSleepFrame = UINT32_MAX;
		return;
	}

	auto& settings = globals::features::upscaling.settings;

	sl::ReflexOptions options{};
	if (!settings.reflexLowLatencyMode) {
		options.mode = sl::ReflexMode::eOff;
	} else {
		options.mode = settings.reflexLowLatencyBoost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
	}

	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	float reflexFPSLimit = originalReflexFPSLimit;
	if (!std::isfinite(reflexFPSLimit)) {
		reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = reflexFPSLimit;
	}
	const float fpsLimit = std::clamp(reflexFPSLimit, 20.0f, 240.0f);
	options.frameLimitUs = settings.reflexUseFPSLimit ? static_cast<uint32_t>(std::lround(1000000.0 / static_cast<double>(fpsLimit))) : 0u;
	options.useMarkersToOptimize = settings.reflexUseMarkersToOptimize && featurePCL;

	if (!reflexOptionsCache.valid ||
		reflexOptionsCache.mode != options.mode ||
		reflexOptionsCache.frameLimitUs != options.frameLimitUs ||
		reflexOptionsCache.useMarkersToOptimize != options.useMarkersToOptimize) {
		if (slReflexSetOptions(options) == sl::Result::eOk) {
			reflexOptionsCache.valid = true;
			reflexOptionsCache.mode = options.mode;
			reflexOptionsCache.frameLimitUs = options.frameLimitUs;
			reflexOptionsCache.useMarkersToOptimize = options.useMarkersToOptimize;
		}
	}

	if (!slReflexSleep)
		return;

	if (options.mode == sl::ReflexMode::eOff && options.frameLimitUs == 0)
		return;

	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	if (lastReflexSleepFrame == currentFrame)
		return;

	if (!EnsureFrameToken())
		return;

	lastReflexSleepFrame = currentFrame;
	slReflexSleep(*frameToken);
}
