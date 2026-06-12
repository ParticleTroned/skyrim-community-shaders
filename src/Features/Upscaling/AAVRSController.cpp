#include "AAVRSController.h"

#include "../FoveatedCommon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <nvapi.h>

namespace
{
	static_assert(AAVRSController::kTileWidth == NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH);
	static_assert(AAVRSController::kTileHeight == NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT);

	constexpr uint8_t kRateIndex1x1 = 0;
	constexpr uint8_t kRateIndex2x1 = 1;
	constexpr uint8_t kRateIndex1x2 = 2;
	constexpr uint8_t kRateIndex2x2 = 3;
	constexpr uint8_t kRateIndex4x4 = 4;
	constexpr float kOutsideMask2x2Fraction = 1.0f / 5.0f;
	constexpr float kPerformanceModeFullRateScale = 0.25f;
	constexpr float kPerformanceModeAnisotropicScale = 0.40f;
	constexpr float kPerformanceModeTwoByTwoScale = 0.70f;
	constexpr uint32_t kPerformanceAnisotropyAuto = 0;
	constexpr uint32_t kPerformanceAnisotropy2x1 = 1;
	constexpr uint32_t kPerformanceAnisotropy1x2 = 2;

	struct ViewportCountInfo
	{
		uint32_t reported = 0;
		uint32_t bound = 1;
		bool forcedMinimum = false;
	};

	uint32_t DivideAndRoundUp(uint32_t a_value, uint32_t a_divisor)
	{
		return a_divisor ? (a_value + a_divisor - 1u) / a_divisor : 0u;
	}

	int32_t Quantize(float a_value)
	{
		return std::isfinite(a_value) ? static_cast<int32_t>(std::lround(a_value * 10000.0f)) : 0;
	}

	uint32_t ClampPerformanceAnisotropy(uint32_t a_value)
	{
		return std::min<uint32_t>(a_value, kPerformanceAnisotropy1x2);
	}

	bool DimensionMatches(uint32_t a_actual, uint32_t a_expected)
	{
		const uint32_t minExpected = a_expected > 2u ? a_expected - 2u : 1u;
		return a_actual >= minExpected && a_actual <= a_expected + 2u;
	}

	bool IsTexture2DRenderTargetView(D3D11_RTV_DIMENSION a_dimension)
	{
		switch (a_dimension) {
		case D3D11_RTV_DIMENSION_TEXTURE2D:
		case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
		case D3D11_RTV_DIMENSION_TEXTURE2DMS:
		case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
			return true;
		default:
			return false;
		}
	}

	float ClampMaskScale(float a_value, float a_max)
	{
		if (!std::isfinite(a_value))
			return FoveatedCommon::kCenterScaleMax;
		return std::clamp(a_value, FoveatedCommon::kCenterScaleMin, a_max);
	}

	float MaskDistanceUV(float a_uvX, float a_uvY, float a_centerScale, float a_centerHorizontalScale, float a_centerOffsetX, float a_centerOffsetY, float a_scaleMax = FoveatedCommon::kCenterScaleMax)
	{
		a_centerScale = ClampMaskScale(a_centerScale, a_scaleMax);
		a_centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(a_centerHorizontalScale);

		const float radiusX = std::max(a_centerScale * a_centerHorizontalScale * 0.5f, 1e-4f);
		const float radiusY = std::max(a_centerScale * 0.5f, 1e-4f);
		const float centerX = std::clamp(0.5f + a_centerOffsetX, 0.0f, 1.0f);
		const float centerY = std::clamp(0.5f + a_centerOffsetY, 0.0f, 1.0f);
		const float normalizedX = std::abs((a_uvX - centerX) / radiusX);
		const float normalizedY = std::abs((a_uvY - centerY) / radiusY);
		const float pNorm = std::pow(normalizedX, FoveatedCommon::kMaskShapePower) + std::pow(normalizedY, FoveatedCommon::kMaskShapePower);
		return std::pow(std::max(pNorm, 0.0f), 1.0f / FoveatedCommon::kMaskShapePower);
	}

	float TileMinMaskDistance(
		float a_tileMinX,
		float a_tileMinY,
		float a_tileMaxX,
		float a_tileMaxY,
		float a_eyeWidth,
		float a_eyeHeight,
		float a_centerScale,
		float a_centerHorizontalScale,
		const AAVRSController::CenterOffset& a_centerOffset,
		float a_scaleMax = FoveatedCommon::kCenterScaleMax)
	{
		if (a_eyeWidth <= 0.0f || a_eyeHeight <= 0.0f)
			return 0.0f;

		const float minUvX = std::clamp(a_tileMinX / a_eyeWidth, 0.0f, 1.0f);
		const float maxUvX = std::clamp(a_tileMaxX / a_eyeWidth, 0.0f, 1.0f);
		const float minUvY = std::clamp(a_tileMinY / a_eyeHeight, 0.0f, 1.0f);
		const float maxUvY = std::clamp(a_tileMaxY / a_eyeHeight, 0.0f, 1.0f);
		const float centerUvX = std::clamp(0.5f + a_centerOffset.x, 0.0f, 1.0f);
		const float centerUvY = std::clamp(0.5f + a_centerOffset.y, 0.0f, 1.0f);

		return MaskDistanceUV(
			std::clamp(centerUvX, minUvX, maxUvX),
			std::clamp(centerUvY, minUvY, maxUvY),
			a_centerScale,
			a_centerHorizontalScale,
			a_centerOffset.x,
			a_centerOffset.y,
			a_scaleMax);
	}

	NV_PIXEL_SHADING_RATE ToPixelShadingRate(uint8_t a_rateIndex, bool a_enable4x4)
	{
		switch (a_rateIndex) {
		case kRateIndex2x1:
			return NV_PIXEL_X1_PER_2X1_RASTER_PIXELS;
		case kRateIndex1x2:
			return NV_PIXEL_X1_PER_1X2_RASTER_PIXELS;
		case kRateIndex2x2:
			return NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
		case kRateIndex4x4:
			return a_enable4x4 ? NV_PIXEL_X1_PER_4X4_RASTER_PIXELS : NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
		case kRateIndex1x1:
		default:
			return NV_PIXEL_X1_PER_RASTER_PIXEL;
		}
	}

	uint8_t ChooseConservativeStereoRate(uint8_t a_leftRate, uint8_t a_rightRate)
	{
		if (a_leftRate == a_rightRate)
			return a_leftRate;
		if (a_leftRate == kRateIndex1x1 || a_rightRate == kRateIndex1x1)
			return kRateIndex1x1;
		if (a_leftRate == kRateIndex4x4)
			return a_rightRate;
		if (a_rightRate == kRateIndex4x4)
			return a_leftRate;
		if ((a_leftRate == kRateIndex2x1 && a_rightRate == kRateIndex1x2) ||
			(a_leftRate == kRateIndex1x2 && a_rightRate == kRateIndex2x1)) {
			return kRateIndex1x1;
		}
		if (a_leftRate == kRateIndex2x2)
			return a_rightRate;
		if (a_rightRate == kRateIndex2x2)
			return a_leftRate;
		return std::min(a_leftRate, a_rightRate);
	}

	uint8_t ResolvePerformanceAnisotropicRate(uint32_t a_anisotropy, float a_tileCenterX, float a_tileCenterY, float a_centerX, float a_centerY)
	{
		switch (ClampPerformanceAnisotropy(a_anisotropy)) {
		case kPerformanceAnisotropy2x1:
			return kRateIndex2x1;
		case kPerformanceAnisotropy1x2:
			return kRateIndex1x2;
		case kPerformanceAnisotropyAuto:
		default:
			return std::abs(a_tileCenterX - a_centerX) >= std::abs(a_tileCenterY - a_centerY) ? kRateIndex2x1 : kRateIndex1x2;
		}
	}

	void FillRateTable(NV_D3D11_VIEWPORT_SHADING_RATE_DESC& a_desc, bool a_enable4x4, bool a_forceFullRate)
	{
		a_desc.enableVariablePixelShadingRate = true;
		for (auto& rate : a_desc.shadingRateTable) {
			rate = NV_PIXEL_X1_PER_RASTER_PIXEL;
		}

		if (a_forceFullRate)
			return;

		a_desc.shadingRateTable[kRateIndex1x1] = ToPixelShadingRate(kRateIndex1x1, a_enable4x4);
		a_desc.shadingRateTable[kRateIndex2x1] = ToPixelShadingRate(kRateIndex2x1, a_enable4x4);
		a_desc.shadingRateTable[kRateIndex1x2] = ToPixelShadingRate(kRateIndex1x2, a_enable4x4);
		a_desc.shadingRateTable[kRateIndex2x2] = ToPixelShadingRate(kRateIndex2x2, a_enable4x4);
		a_desc.shadingRateTable[kRateIndex4x4] = ToPixelShadingRate(kRateIndex4x4, a_enable4x4);
	}

	ViewportCountInfo QueryViewportCount(ID3D11DeviceContext* a_context, uint32_t a_fallback, bool a_forceMinimum)
	{
		ViewportCountInfo info{};
		if (!a_context)
			return { 0u, std::max(a_fallback, 1u), true };

		UINT viewportCount = 0;
		a_context->RSGetViewports(&viewportCount, nullptr);
		info.reported = viewportCount;
		if (viewportCount == 0 || a_forceMinimum || viewportCount < a_fallback) {
			const UINT forcedViewportCount = std::max<UINT>(viewportCount, std::max<UINT>(a_fallback, 1u));
			info.forcedMinimum = forcedViewportCount != viewportCount;
			viewportCount = forcedViewportCount;
		}

		info.bound = std::clamp<uint32_t>(
			viewportCount,
			1u,
			D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
		return info;
	}
}

bool AAVRSController::IsSupported(ID3D11Device* a_device)
{
	if (!a_device) {
		lastDisableReason = "Missing D3D11 device";
		return false;
	}

	if (supportDevice && supportDevice != a_device) {
		ReleaseView();
		shadingRateSurface = nullptr;
		shadingRateSRV = nullptr;
		shadingRateUAV = nullptr;
		surfaceWidth = 0;
		surfaceHeight = 0;
		patternData.clear();
		patternValid = false;
		patternUploaded = false;
		active = false;
		supportChecked = false;
		supported = false;
		allow4x4Rate = true;
		logged4x4Fallback = false;
		loggedViewportBindMode = false;
	}

	if (supportChecked && supportDevice == a_device)
		return supported;

	supportDevice = a_device;
	supportChecked = true;
	supported = false;

	if (!nvapiInitAttempted) {
		nvapiInitAttempted = true;
		const NvAPI_Status status = NvAPI_Initialize();
		nvapiReady = status == NVAPI_OK;
		if (!nvapiReady && !loggedNvapiFailure) {
			logger::warn("[Upscaling] Foveated Variable Rate Shading (VRS) unavailable: NvAPI_Initialize failed with {}", static_cast<int>(status));
			loggedNvapiFailure = true;
		}
	}

	if (!nvapiReady) {
		lastDisableReason = "NVAPI unavailable";
		return false;
	}

	NV_D3D1x_GRAPHICS_CAPS caps{};
	const NvAPI_Status status = NvAPI_D3D1x_GetGraphicsCapabilities(a_device, NV_D3D1x_GRAPHICS_CAPS_VER, &caps);
	if (status != NVAPI_OK) {
		if (!loggedSupportFailure) {
			logger::warn("[Upscaling] Foveated Variable Rate Shading (VRS) unavailable: graphics capability query failed with {}", static_cast<int>(status));
			loggedSupportFailure = true;
		}
		lastDisableReason = "Variable Rate Shading (VRS) capability query failed";
		return false;
	}

	supported = caps.bVariablePixelRateShadingSupported != 0;
	if (!supported)
		lastDisableReason = "Variable pixel rate shading is not supported";
	return supported;
}

bool AAVRSController::Update(const Settings& a_settings, ID3D11Device* a_device, ID3D11DeviceContext* a_context)
{
	Settings effectiveSettings = a_settings;
	effectiveSettings.displayWidth = std::max(effectiveSettings.displayWidth, 1u);
	effectiveSettings.displayHeight = std::max(effectiveSettings.displayHeight, 1u);
	effectiveSettings.renderWidth = effectiveSettings.renderWidth != 0 ? effectiveSettings.renderWidth : effectiveSettings.displayWidth;
	effectiveSettings.renderHeight = effectiveSettings.renderHeight != 0 ? effectiveSettings.renderHeight : effectiveSettings.displayHeight;
	effectiveSettings.renderWidth = std::max(effectiveSettings.renderWidth, 1u);
	effectiveSettings.renderHeight = std::max(effectiveSettings.renderHeight, 1u);
	effectiveSettings.maxRate = std::min(effectiveSettings.maxRate, 1u);
	effectiveSettings.performanceAnisotropy = ClampPerformanceAnisotropy(effectiveSettings.performanceAnisotropy);

	lastSettings = effectiveSettings;
	hasLastSettings = true;

	if (!effectiveSettings.enabled) {
		Disable(a_context);
		lastDisableReason = "Disabled";
		return false;
	}

	if (!a_context || !a_device) {
		Disable(a_context);
		lastDisableReason = "Missing D3D11 context";
		return false;
	}

	if (suspendDepth != 0)
		return false;

	if (!IsSupported(a_device)) {
		DisableInternal(a_context);
		return false;
	}

	if (!EnsureSurface(a_device, effectiveSettings)) {
		DisableInternal(a_context);
		return false;
	}

	if (!EnsurePattern(effectiveSettings)) {
		lastDisableReason = "Failed to build shading-rate pattern";
		DisableInternal(a_context);
		return false;
	}

	if (!UploadPattern(a_context)) {
		lastDisableReason = "Failed to upload shading-rate pattern";
		DisableInternal(a_context);
		return false;
	}

	return Bind(a_context);
}

void AAVRSController::Disable(ID3D11DeviceContext* a_context, const char* a_reason)
{
	suspendDepth = 0;
	fullRateOverrideDepth = 0;
	fullRateOverrideBound = false;
	DisableInternal(a_context);
	hasLastSettings = false;
	lastDisableReason = a_reason && a_reason[0] ? a_reason : "Disabled";
}

void AAVRSController::Suspend(ID3D11DeviceContext* a_context)
{
	++suspendDepth;
	if (suspendDepth != 1)
		return;

	fullRateOverrideDepth = 0;
	fullRateOverrideBound = false;
	DisableInternal(a_context);
	lastDisableReason = "Suspended";
}

void AAVRSController::Resume(ID3D11DeviceContext* a_context)
{
	if (suspendDepth == 0)
		return;

	--suspendDepth;
	if (suspendDepth != 0)
		return;

	if (!hasLastSettings || !lastSettings.enabled || !a_context)
		return;

	winrt::com_ptr<ID3D11Device> device;
	a_context->GetDevice(device.put());
	(void)Update(lastSettings, device.get(), a_context);
}

void AAVRSController::ReleaseResources()
{
	ReleaseView();
	shadingRateSurface = nullptr;
	shadingRateSRV = nullptr;
	shadingRateUAV = nullptr;
	surfaceWidth = 0;
	surfaceHeight = 0;
	patternData.clear();
	patternValid = false;
	patternUploaded = false;
	active = false;
	suspendDepth = 0;
	fullRateOverrideDepth = 0;
	fullRateOverrideBound = false;
	hasLastSettings = false;
	loggedViewportBindMode = false;
	lastDisableReason = "Resources released";
}

AAVRSController::Status AAVRSController::GetStatus() const
{
	Status status{};
	status.active = active;
	status.suspended = suspendDepth != 0;
	status.lastDisableReason = lastDisableReason;
	status.maskWidth = surfaceWidth;
	status.maskHeight = surfaceHeight;
	status.hasSettings = hasLastSettings;
	status.fullRateOverride = fullRateOverrideDepth != 0 && fullRateOverrideBound;
	if (hasLastSettings) {
		status.renderWidth = lastSettings.renderWidth;
		status.renderHeight = lastSettings.renderHeight;
		status.maxRate = lastSettings.maxRate != 0 && allow4x4Rate ? 1u : 0u;
		status.performanceMode = lastSettings.performanceMode;
		status.performanceAnisotropy = lastSettings.performanceMode ? lastSettings.performanceAnisotropy : kPerformanceAnisotropyAuto;
	}
	return status;
}

bool AAVRSController::GuardActiveRenderTarget(ID3D11DeviceContext* a_context)
{
	if (!active || suspendDepth != 0)
		return true;
	if (!a_context || !hasLastSettings)
		return true;

	auto disable = [&](const char* a_reason) {
		lastDisableReason = a_reason;
		DisableInternal(a_context);
		return false;
	};

	ID3D11RenderTargetView* rawRTV = nullptr;
	a_context->OMGetRenderTargets(1, &rawRTV, nullptr);

	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	rtv.attach(rawRTV);
	if (!rtv) {
		return disable("Unexpected missing render target");
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtv->GetDesc(&rtvDesc);
	if (!IsTexture2DRenderTargetView(rtvDesc.ViewDimension)) {
		return disable("Unexpected render target view");
	}

	winrt::com_ptr<ID3D11Resource> resource;
	rtv->GetResource(resource.put());
	if (!resource) {
		return disable("Missing render target resource");
	}

	ID3D11Texture2D* rawTexture = nullptr;
	const HRESULT textureQuery = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&rawTexture));

	winrt::com_ptr<ID3D11Texture2D> texture;
	texture.attach(rawTexture);
	if (FAILED(textureQuery) || !texture) {
		return disable("Unexpected render target resource");
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	texture->GetDesc(&textureDesc);

	if (!DimensionMatches(textureDesc.Width, lastSettings.renderWidth) ||
		!DimensionMatches(textureDesc.Height, lastSettings.renderHeight)) {
		return disable("Unexpected render target size");
	}

	return true;
}

void AAVRSController::UnbindShadingRateResource(ID3D11DeviceContext* a_context) const
{
	if (a_context && nvapiReady)
		(void)NvAPI_D3D11_RSSetShadingRateResourceView(a_context, nullptr);
}

bool AAVRSController::EnsureSurface(ID3D11Device* a_device, const Settings& a_settings)
{
	const uint32_t desiredWidth = std::max(1u, DivideAndRoundUp(a_settings.renderWidth, AAVRSController::kTileWidth));
	const uint32_t desiredHeight = std::max(1u, DivideAndRoundUp(a_settings.renderHeight, AAVRSController::kTileHeight));
	if (shadingRateSurface && shadingRateView && surfaceWidth == desiredWidth && surfaceHeight == desiredHeight)
		return true;

	ReleaseView();
	shadingRateSurface = nullptr;
	shadingRateSRV = nullptr;
	shadingRateUAV = nullptr;
	surfaceWidth = desiredWidth;
	surfaceHeight = desiredHeight;
	patternValid = false;
	patternUploaded = false;

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = surfaceWidth;
	desc.Height = surfaceHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	HRESULT textureResult = a_device->CreateTexture2D(&desc, nullptr, shadingRateSurface.put());
	if (FAILED(textureResult) || !shadingRateSurface) {
		const HRESULT uavTextureResult = textureResult;
		shadingRateSurface = nullptr;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		textureResult = a_device->CreateTexture2D(&desc, nullptr, shadingRateSurface.put());
		if (SUCCEEDED(textureResult) && shadingRateSurface) {
			logger::debug("[Upscaling] Foveated Variable Rate Shading (VRS) created rate image without UAV support; content-aware refinement unavailable: 0x{:08X}", static_cast<uint32_t>(uavTextureResult));
		}
	}
	if (FAILED(textureResult) || !shadingRateSurface) {
		if (!loggedResourceFailure) {
			logger::warn("[Upscaling] Foveated Variable Rate Shading (VRS) failed to create shading-rate surface: 0x{:08X}", static_cast<uint32_t>(textureResult));
			loggedResourceFailure = true;
		}
		lastDisableReason = "Failed to create shading-rate surface";
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8_UINT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	const HRESULT srvResult = a_device->CreateShaderResourceView(shadingRateSurface.get(), &srvDesc, shadingRateSRV.put());
	if (FAILED(srvResult) || !shadingRateSRV) {
		logger::debug("[Upscaling] Foveated Variable Rate Shading (VRS) failed to create rate-image SRV; visualization unavailable: 0x{:08X}", static_cast<uint32_t>(srvResult));
		shadingRateSRV = nullptr;
	}

	if ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R8_UINT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		const HRESULT uavResult = a_device->CreateUnorderedAccessView(shadingRateSurface.get(), &uavDesc, shadingRateUAV.put());
		if (FAILED(uavResult) || !shadingRateUAV) {
			logger::debug("[Upscaling] Foveated Variable Rate Shading (VRS) failed to create rate-image UAV; content-aware refinement unavailable: 0x{:08X}", static_cast<uint32_t>(uavResult));
			shadingRateUAV = nullptr;
		}
	}

	NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC viewDesc{};
	viewDesc.version = NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
	viewDesc.Format = DXGI_FORMAT_R8_UINT;
	viewDesc.ViewDimension = NV_SRRV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;

	ID3D11NvShadingRateResourceView* view = nullptr;
	const NvAPI_Status viewResult = NvAPI_D3D11_CreateShadingRateResourceView(a_device, shadingRateSurface.get(), &viewDesc, &view);
	if (viewResult != NVAPI_OK || !view) {
		if (!loggedResourceFailure) {
			logger::warn("[Upscaling] Foveated Variable Rate Shading (VRS) failed to create shading-rate view: {}", static_cast<int>(viewResult));
			loggedResourceFailure = true;
		}
		shadingRateSurface = nullptr;
		shadingRateSRV = nullptr;
		shadingRateUAV = nullptr;
		lastDisableReason = "Failed to create shading-rate view";
		return false;
	}

	shadingRateView = view;
	return true;
}

AAVRSController::PatternKey AAVRSController::MakePatternKey(const Settings& a_settings)
{
	PatternKey key{};
	const bool performanceMode = a_settings.performanceMode;
	key.stereo = a_settings.stereo;
	key.displayWidth = a_settings.displayWidth;
	key.displayHeight = a_settings.displayHeight;
	key.renderWidth = a_settings.renderWidth;
	key.renderHeight = a_settings.renderHeight;
	key.centerScaleQ = performanceMode ? 0 : Quantize(a_settings.centerScale);
	key.centerHorizontalScaleQ = Quantize(a_settings.centerHorizontalScale);
	key.outerScaleQ = performanceMode ? 0 : Quantize(a_settings.outerScale);
	key.coarseOutsideMask = performanceMode ? false : a_settings.coarseOutsideMask;
	key.performanceMode = performanceMode;
	key.performanceAnisotropy = performanceMode ? ClampPerformanceAnisotropy(a_settings.performanceAnisotropy) : kPerformanceAnisotropyAuto;
	key.maxRate = performanceMode ? 0u : std::min(a_settings.maxRate, 1u);
	key.centerOffsetQ = {
		Quantize(a_settings.centerOffsets[0].x),
		Quantize(a_settings.centerOffsets[0].y),
		Quantize(a_settings.centerOffsets[1].x),
		Quantize(a_settings.centerOffsets[1].y),
	};
	return key;
}

bool AAVRSController::EnsurePattern(const Settings& a_settings)
{
	if (!surfaceWidth || !surfaceHeight)
		return false;

	const PatternKey key = MakePatternKey(a_settings);
	if (patternValid && key == patternKey)
		return true;

	patternData.assign(static_cast<size_t>(surfaceWidth) * static_cast<size_t>(surfaceHeight), kRateIndex1x1);
	patternKey = key;

	const uint32_t eyeCount = a_settings.stereo ? 2u : 1u;
	const float renderWidth = static_cast<float>(std::max(a_settings.renderWidth, 1u));
	const float renderHeight = static_cast<float>(std::max(a_settings.renderHeight, 1u));
	const float displayWidth = static_cast<float>(std::max(a_settings.displayWidth, 1u));
	const float displayHeight = static_cast<float>(std::max(a_settings.displayHeight, 1u));
	const float eyeRenderWidth = a_settings.stereo ? std::max(renderWidth / static_cast<float>(eyeCount), 1.0f) : renderWidth;
	const float eyeDisplayWidth = a_settings.stereo ? std::max(displayWidth / static_cast<float>(eyeCount), 1.0f) : displayWidth;
	const float renderScaleX = eyeDisplayWidth > 0.0f ? eyeRenderWidth / eyeDisplayWidth : 1.0f;
	const float renderScaleY = displayHeight > 0.0f ? renderHeight / displayHeight : 1.0f;
	const bool performanceMode = a_settings.performanceMode;
	const uint32_t performanceAnisotropy = ClampPerformanceAnisotropy(a_settings.performanceAnisotropy);
	const float centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(a_settings.centerHorizontalScale);
	const float centerScale = performanceMode ? 0.0f : FoveatedCommon::ClampCenterScale(a_settings.centerScale);
	const float outerScale = performanceMode ? 0.0f : ClampMaskScale(std::max(a_settings.outerScale, centerScale), FoveatedCommon::kCenterScaleMax);
	const float coarseSplitScale = performanceMode ? 0.0f : outerScale + (FoveatedCommon::kCenterScaleMax - outerScale) * kOutsideMask2x2Fraction;
	const float protectedScale = performanceMode ? 0.0f : (a_settings.coarseOutsideMask ? outerScale : centerScale);
	const bool fullRatePattern = !performanceMode && protectedScale >= FoveatedCommon::kFullCoverageThreshold;
	const uint8_t maxCoarseRateIndex = a_settings.maxRate == 0 ? kRateIndex2x2 : kRateIndex4x4;

	auto resolveRateIndex = [&](uint32_t a_eye, float a_tileMinX, float a_tileMinY, float a_tileMaxX, float a_tileMaxY) -> uint8_t {
		const float displayTileMinX = a_tileMinX / std::max(renderScaleX, 1e-4f);
		const float displayTileMaxX = a_tileMaxX / std::max(renderScaleX, 1e-4f);
		const float displayTileMinY = a_tileMinY / std::max(renderScaleY, 1e-4f);
		const float displayTileMaxY = a_tileMaxY / std::max(renderScaleY, 1e-4f);
		const auto& centerOffset = a_settings.centerOffsets[std::min<uint32_t>(a_eye, 1u)];
		const auto maskDistance = [&](float a_scale, float a_scaleMax = FoveatedCommon::kCenterScaleMax) {
			return TileMinMaskDistance(
				displayTileMinX,
				displayTileMinY,
				displayTileMaxX,
				displayTileMaxY,
				eyeDisplayWidth,
				displayHeight,
				a_scale,
				centerHorizontalScale,
				centerOffset,
				a_scaleMax);
		};
		const float tileCenterX = (displayTileMinX + displayTileMaxX) * 0.5f;
		const float tileCenterY = (displayTileMinY + displayTileMaxY) * 0.5f;
		const float centerX = (0.5f + centerOffset.x) * eyeDisplayWidth;
		const float centerY = (0.5f + centerOffset.y) * displayHeight;

		if (performanceMode) {
			if (maskDistance(kPerformanceModeFullRateScale) <= 1.0f)
				return kRateIndex1x1;

			if (maskDistance(kPerformanceModeAnisotropicScale) <= 1.0f)
				return ResolvePerformanceAnisotropicRate(performanceAnisotropy, tileCenterX, tileCenterY, centerX, centerY);

			return maskDistance(kPerformanceModeTwoByTwoScale) <= 1.0f ? kRateIndex2x2 : kRateIndex4x4;
		}

		if (fullRatePattern)
			return kRateIndex1x1;
		if (a_settings.coarseOutsideMask) {
			// In coarse-outside mode, outerScale is the filled protected mask:
			// active foveated coverage and VRS safety padding stay 1x1.
			if (maskDistance(outerScale) <= 1.0f)
				return kRateIndex1x1;

			return maskDistance(coarseSplitScale) > 1.0f ? maxCoarseRateIndex : kRateIndex2x2;
		}
		if (maskDistance(centerScale) <= 1.0f)
			return kRateIndex1x1;
		return std::abs(tileCenterX - centerX) >= std::abs(tileCenterY - centerY) ? kRateIndex2x1 : kRateIndex1x2;
	};

	for (uint32_t y = 0; y < surfaceHeight; ++y) {
		const float tileMinY = static_cast<float>(y * AAVRSController::kTileHeight);
		const float tileMaxY = std::min(tileMinY + static_cast<float>(AAVRSController::kTileHeight), renderHeight);
		if (tileMinY >= renderHeight)
			continue;

		for (uint32_t x = 0; x < surfaceWidth; ++x) {
			const float tileMinX = static_cast<float>(x * AAVRSController::kTileWidth);
			const float tileMaxX = std::min(tileMinX + static_cast<float>(AAVRSController::kTileWidth), renderWidth);
			if (tileMinX >= renderWidth)
				continue;

			uint8_t rateIndex = kRateIndex1x1;
			if (a_settings.stereo) {
				// Skyrim VR renders eyes as a packed side-by-side surface. Protect
				// both halves with the binocular union of the local left/right masks;
				// otherwise a tile that is coarse in either eye can still shimmer.
				const float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
				const uint32_t eye = static_cast<uint32_t>(std::clamp(tileCenterX / std::max(eyeRenderWidth, 1.0f), 0.0f, 1.0f));
				const float eyeOffsetX = eyeRenderWidth * static_cast<float>(eye);
				const float localTileMinX = std::clamp(tileMinX - eyeOffsetX, 0.0f, eyeRenderWidth);
				const float localTileMaxX = std::clamp(tileMaxX - eyeOffsetX, 0.0f, eyeRenderWidth);
				const uint8_t leftRate = resolveRateIndex(0, localTileMinX, tileMinY, localTileMaxX, tileMaxY);
				const uint8_t rightRate = resolveRateIndex(1, localTileMinX, tileMinY, localTileMaxX, tileMaxY);
				rateIndex = ChooseConservativeStereoRate(leftRate, rightRate);
			} else {
				rateIndex = resolveRateIndex(0, tileMinX, tileMinY, tileMaxX, tileMaxY);
			}

			patternData[static_cast<size_t>(y) * surfaceWidth + x] = rateIndex;
		}
	}

	patternValid = true;
	patternUploaded = false;
	return true;
}

bool AAVRSController::UploadPattern(ID3D11DeviceContext* a_context)
{
	if (patternUploaded)
		return true;
	if (!a_context || !shadingRateSurface || patternData.empty())
		return false;

	a_context->UpdateSubresource(
		shadingRateSurface.get(),
		0,
		nullptr,
		patternData.data(),
		surfaceWidth * sizeof(uint8_t),
		0);
	patternUploaded = true;
	return true;
}

bool AAVRSController::RefineContentAware(
	ID3D11DeviceContext* a_context,
	ID3D11ComputeShader* a_shader,
	ID3D11Buffer* a_constantBuffer,
	ID3D11ShaderResourceView* a_colorSRV,
	ID3D11ShaderResourceView* a_motionVectorSRV,
	ID3D11ShaderResourceView* a_depthSRV)
{
	if (!active || suspendDepth != 0)
		return false;
	if (!a_context || !a_shader || !a_constantBuffer || !a_colorSRV || !a_motionVectorSRV || !a_depthSRV || !shadingRateUAV)
		return false;

	UnbindShadingRateResource(a_context);

	ID3D11ShaderResourceView* srvs[3] = { a_colorSRV, a_motionVectorSRV, a_depthSRV };
	ID3D11UnorderedAccessView* uavs[1] = { shadingRateUAV.get() };
	ID3D11Buffer* cbs[1] = { a_constantBuffer };

	a_context->CSSetShader(a_shader, nullptr, 0);
	a_context->CSSetConstantBuffers(0, 1, cbs);
	a_context->CSSetShaderResources(0, 3, srvs);
	a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	a_context->Dispatch((surfaceWidth + 7u) >> 3, (surfaceHeight + 7u) >> 3, 1);

	ID3D11ShaderResourceView* nullSRVs[3] = {};
	ID3D11UnorderedAccessView* nullUAVs[1] = {};
	ID3D11Buffer* nullCBs[1] = {};
	a_context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	a_context->CSSetShaderResources(0, 3, nullSRVs);
	a_context->CSSetConstantBuffers(0, 1, nullCBs);
	a_context->CSSetShader(nullptr, nullptr, 0);

	patternUploaded = false;
	return Bind(a_context);
}

bool AAVRSController::Bind(ID3D11DeviceContext* a_context, bool a_forceFullRate)
{
	if (!a_context || !shadingRateView)
		return false;

	ViewportCountInfo bindInfo{};
	const auto bindWithTable = [&](bool a_enable4x4, uint32_t a_minViewportCount, bool a_forceMinViewportCount, NvAPI_Status& a_outViewportStatus, NvAPI_Status& a_outSurfaceStatus) {
		const ViewportCountInfo viewportInfo = QueryViewportCount(a_context, a_minViewportCount, a_forceMinViewportCount);
		const uint32_t viewportCount = viewportInfo.bound;
		std::array<NV_D3D11_VIEWPORT_SHADING_RATE_DESC, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewportDescs{};
		for (uint32_t i = 0; i < viewportCount; ++i) {
			FillRateTable(viewportDescs[i], a_enable4x4, a_forceFullRate);
		}

		NV_D3D11_VIEWPORTS_SHADING_RATE_DESC shadingRateDesc{};
		shadingRateDesc.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
		shadingRateDesc.numViewports = viewportCount;
		shadingRateDesc.pViewports = viewportDescs.data();
		a_outViewportStatus = NvAPI_D3D11_RSSetViewportsPixelShadingRates(a_context, &shadingRateDesc);
		a_outSurfaceStatus = NvAPI_D3D11_RSSetShadingRateResourceView(
			a_context,
			reinterpret_cast<ID3D11NvShadingRateResourceView*>(shadingRateView));
		if (a_outViewportStatus == NVAPI_OK && a_outSurfaceStatus == NVAPI_OK)
			bindInfo = viewportInfo;
		return a_outViewportStatus == NVAPI_OK && a_outSurfaceStatus == NVAPI_OK;
	};
	const bool wants4x4Rate = !a_forceFullRate && allow4x4Rate && (!hasLastSettings || lastSettings.performanceMode || lastSettings.maxRate != 0);
	const auto bindRateTable = [&](bool a_enable4x4, NvAPI_Status& a_outViewportStatus, NvAPI_Status& a_outSurfaceStatus) {
		const bool stereo = hasLastSettings && lastSettings.stereo;
		// Skyrim VR can report one viewport at this hook point, then render with
		// per-eye viewport indices. Populate both rate tables for stereo frames.
		const uint32_t minimumViewportCount = stereo ? 2u : 1u;
		if (bindWithTable(a_enable4x4, minimumViewportCount, stereo, a_outViewportStatus, a_outSurfaceStatus))
			return true;
		return false;
	};

	NvAPI_Status viewportResult = NVAPI_OK;
	NvAPI_Status surfaceResult = NVAPI_OK;
	const auto logViewportBindMode = [&]() {
		if (loggedViewportBindMode)
			return;

		logger::info(
			"[Upscaling] Foveated Variable Rate Shading (VRS) viewport bind: packedStereo={}, reported={}, bound={}, forced={}, viewportStatus={}, surfaceStatus={}",
			hasLastSettings && lastSettings.stereo,
			bindInfo.reported,
			bindInfo.bound,
			bindInfo.forcedMinimum,
			static_cast<int>(viewportResult),
			static_cast<int>(surfaceResult));
		loggedViewportBindMode = true;
	};

	if (!bindRateTable(wants4x4Rate, viewportResult, surfaceResult)) {
		if (wants4x4Rate && bindRateTable(false, viewportResult, surfaceResult)) {
			allow4x4Rate = false;
			if (!logged4x4Fallback) {
				logger::warn("[Upscaling] Foveated Variable Rate Shading (VRS): 4x4 shading rate unavailable, falling back to 2x2");
				logged4x4Fallback = true;
			}
			logViewportBindMode();
			active = true;
			lastDisableReason = "";
			return true;
		}
		if (!loggedResourceFailure) {
			logger::warn(
				"[Upscaling] Foveated Variable Rate Shading (VRS) bind failed: viewport={}, surface={}",
				static_cast<int>(viewportResult),
				static_cast<int>(surfaceResult));
			loggedResourceFailure = true;
		}
		lastDisableReason = "Failed to bind shading-rate state";
		if (a_forceFullRate)
			lastDisableReason = "Failed to bind full-rate shading override";
		DisableInternal(a_context);
		return false;
	}

	logViewportBindMode();
	active = true;
	lastDisableReason = "";
	return true;
}

void AAVRSController::BeginFullRateOverride(ID3D11DeviceContext* a_context)
{
	++fullRateOverrideDepth;
	if (fullRateOverrideDepth != 1)
		return;

	fullRateOverrideBound = false;
	if (!a_context || !active || suspendDepth != 0 || !shadingRateView) {
		fullRateOverrideDepth = 0;
		return;
	}
	if (!GuardActiveRenderTarget(a_context)) {
		fullRateOverrideDepth = 0;
		fullRateOverrideBound = false;
		return;
	}

	if (Bind(a_context, true)) {
		fullRateOverrideBound = true;
	} else {
		fullRateOverrideDepth = 0;
		fullRateOverrideBound = false;
	}
}

void AAVRSController::EndFullRateOverride(ID3D11DeviceContext* a_context)
{
	if (fullRateOverrideDepth == 0)
		return;

	--fullRateOverrideDepth;
	if (fullRateOverrideDepth != 0)
		return;

	const bool restoreCoarseRates = fullRateOverrideBound;
	fullRateOverrideBound = false;
	if (restoreCoarseRates && active && suspendDepth == 0 && a_context) {
		if (!GuardActiveRenderTarget(a_context))
			return;
		(void)Bind(a_context);
	}
}

void AAVRSController::DisableInternal(ID3D11DeviceContext* a_context)
{
	if (a_context && nvapiReady) {
		NV_D3D11_VIEWPORTS_SHADING_RATE_DESC shadingRateDesc{};
		shadingRateDesc.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
		shadingRateDesc.numViewports = 0;
		shadingRateDesc.pViewports = nullptr;
		(void)NvAPI_D3D11_RSSetViewportsPixelShadingRates(a_context, &shadingRateDesc);

		(void)NvAPI_D3D11_RSSetShadingRateResourceView(a_context, nullptr);
	}

	active = false;
	fullRateOverrideDepth = 0;
	fullRateOverrideBound = false;
}

void AAVRSController::ReleaseView()
{
	if (shadingRateView) {
		shadingRateView->Release();
		shadingRateView = nullptr;
	}
}
