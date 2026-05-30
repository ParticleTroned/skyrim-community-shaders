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
	constexpr uint8_t kStereoRateCategory1x1 = 0;
	constexpr uint8_t kStereoRateCategory2x2 = 1;
	constexpr uint8_t kStereoRateCategory4x4 = 2;
	constexpr uint8_t kStereoRateCategoryCount = 3;
	constexpr float kOutsideMask2x2Fraction = 1.0f / 4.0f;
	static_assert(kStereoRateCategoryCount * kStereoRateCategoryCount <= NV_MAX_PIXEL_SHADING_RATES);

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

	float ClampMaskArea(float a_value, float a_max)
	{
		if (!std::isfinite(a_value))
			return FoveatedCommon::kCenterAreaMax;
		return std::clamp(a_value, FoveatedCommon::kCenterAreaMin, a_max);
	}

	float MaskDistanceUV(float a_uvX, float a_uvY, float a_centerArea, float a_centerHorizontalScale, float a_centerOffsetX, float a_centerOffsetY, float a_areaMax = FoveatedCommon::kCenterAreaMax)
	{
		a_centerArea = ClampMaskArea(a_centerArea, a_areaMax);
		a_centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(a_centerHorizontalScale);

		const float radiusX = std::max(a_centerArea * a_centerHorizontalScale * 0.5f, 1e-4f);
		const float radiusY = std::max(a_centerArea * 0.5f, 1e-4f);
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
		float a_centerArea,
		float a_centerHorizontalScale,
		const AAVRSController::CenterOffset& a_centerOffset,
		float a_areaMax = FoveatedCommon::kCenterAreaMax)
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
			a_centerArea,
			a_centerHorizontalScale,
			a_centerOffset.x,
			a_centerOffset.y,
			a_areaMax);
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

	uint8_t ToStereoRateCategory(uint8_t a_rateIndex)
	{
		switch (a_rateIndex) {
		case kRateIndex4x4:
			return kStereoRateCategory4x4;
		case kRateIndex1x1:
			return kStereoRateCategory1x1;
		case kRateIndex2x1:
		case kRateIndex1x2:
		case kRateIndex2x2:
		default:
			return kStereoRateCategory2x2;
		}
	}

	NV_PIXEL_SHADING_RATE StereoCategoryToPixelShadingRate(uint8_t a_category, bool a_enable4x4)
	{
		switch (a_category) {
		case kStereoRateCategory4x4:
			return a_enable4x4 ? NV_PIXEL_X1_PER_4X4_RASTER_PIXELS : NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
		case kStereoRateCategory2x2:
			return NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
		case kStereoRateCategory1x1:
		default:
			return NV_PIXEL_X1_PER_RASTER_PIXEL;
		}
	}

	uint8_t EncodeStereoRateCategories(uint8_t a_leftCategory, uint8_t a_rightCategory)
	{
		return a_leftCategory * kStereoRateCategoryCount + a_rightCategory;
	}

	void FillRateTable(NV_D3D11_VIEWPORT_SHADING_RATE_DESC& a_desc, bool a_enable4x4, bool a_stereo, uint32_t a_viewportIndex)
	{
		a_desc.enableVariablePixelShadingRate = true;
		for (auto& rate : a_desc.shadingRateTable) {
			rate = NV_PIXEL_X1_PER_RASTER_PIXEL;
		}

		if (a_stereo) {
			const bool rightEyeViewport = (a_viewportIndex % 2u) == 1u;
			for (uint8_t leftCategory = 0; leftCategory < kStereoRateCategoryCount; ++leftCategory) {
				for (uint8_t rightCategory = 0; rightCategory < kStereoRateCategoryCount; ++rightCategory) {
					const uint8_t tableIndex = EncodeStereoRateCategories(leftCategory, rightCategory);
					const uint8_t viewportCategory = rightEyeViewport ? rightCategory : leftCategory;
					a_desc.shadingRateTable[tableIndex] = StereoCategoryToPixelShadingRate(viewportCategory, a_enable4x4);
				}
			}
			return;
		}

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
		if (viewportCount == 0 || a_forceMinimum) {
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
			logger::warn("[Upscaling] AA VRS unavailable: NvAPI_Initialize failed with {}", static_cast<int>(status));
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
			logger::warn("[Upscaling] AA VRS unavailable: graphics capability query failed with {}", static_cast<int>(status));
			loggedSupportFailure = true;
		}
		lastDisableReason = "VRS capability query failed";
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
	DisableInternal(a_context);
	hasLastSettings = false;
	lastDisableReason = a_reason && a_reason[0] ? a_reason : "Disabled";
}

void AAVRSController::Suspend(ID3D11DeviceContext* a_context)
{
	++suspendDepth;
	if (suspendDepth != 1)
		return;

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
	surfaceWidth = 0;
	surfaceHeight = 0;
	patternData.clear();
	patternValid = false;
	patternUploaded = false;
	active = false;
	suspendDepth = 0;
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
	if (hasLastSettings) {
		status.renderWidth = lastSettings.renderWidth;
		status.renderHeight = lastSettings.renderHeight;
	}
	return status;
}

bool AAVRSController::EnsureSurface(ID3D11Device* a_device, const Settings& a_settings)
{
	const uint32_t desiredWidth = std::max(1u, DivideAndRoundUp(a_settings.renderWidth, AAVRSController::kTileWidth));
	const uint32_t desiredHeight = std::max(1u, DivideAndRoundUp(a_settings.renderHeight, AAVRSController::kTileHeight));
	if (shadingRateSurface && shadingRateView && surfaceWidth == desiredWidth && surfaceHeight == desiredHeight)
		return true;

	ReleaseView();
	shadingRateSurface = nullptr;
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
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	const HRESULT textureResult = a_device->CreateTexture2D(&desc, nullptr, shadingRateSurface.put());
	if (FAILED(textureResult) || !shadingRateSurface) {
		if (!loggedResourceFailure) {
			logger::warn("[Upscaling] AA VRS failed to create shading-rate surface: 0x{:08X}", static_cast<uint32_t>(textureResult));
			loggedResourceFailure = true;
		}
		lastDisableReason = "Failed to create shading-rate surface";
		return false;
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
			logger::warn("[Upscaling] AA VRS failed to create shading-rate view: {}", static_cast<int>(viewResult));
			loggedResourceFailure = true;
		}
		shadingRateSurface = nullptr;
		lastDisableReason = "Failed to create shading-rate view";
		return false;
	}

	shadingRateView = view;
	return true;
}

AAVRSController::PatternKey AAVRSController::MakePatternKey(const Settings& a_settings)
{
	PatternKey key{};
	key.stereo = a_settings.stereo;
	key.displayWidth = a_settings.displayWidth;
	key.displayHeight = a_settings.displayHeight;
	key.renderWidth = a_settings.renderWidth;
	key.renderHeight = a_settings.renderHeight;
	key.centerAreaQ = Quantize(a_settings.centerArea);
	key.centerHorizontalScaleQ = Quantize(a_settings.centerHorizontalScale);
	key.outerAreaQ = Quantize(a_settings.outerArea);
	key.coarseOutsideMask = a_settings.coarseOutsideMask;
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

	const float centerArea = FoveatedCommon::ClampCenterArea(a_settings.centerArea);
	const float outerArea = ClampMaskArea(std::max(a_settings.outerArea, centerArea), FoveatedCommon::kCenterAreaMax);
	const float coarseSplitArea = outerArea + (FoveatedCommon::kCenterAreaMax - outerArea) * kOutsideMask2x2Fraction;
	const float centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(a_settings.centerHorizontalScale);
	const bool fullRatePattern = centerArea >= FoveatedCommon::kFullCoverageThreshold;

	auto resolveRateIndex = [&](uint32_t a_eye, float a_tileMinX, float a_tileMinY, float a_tileMaxX, float a_tileMaxY) -> uint8_t {
		const float displayTileMinX = a_tileMinX / std::max(renderScaleX, 1e-4f);
		const float displayTileMaxX = a_tileMaxX / std::max(renderScaleX, 1e-4f);
		const float displayTileMinY = a_tileMinY / std::max(renderScaleY, 1e-4f);
		const float displayTileMaxY = a_tileMaxY / std::max(renderScaleY, 1e-4f);
		const auto& centerOffset = a_settings.centerOffsets[std::min<uint32_t>(a_eye, 1u)];
		const float centerDistance = TileMinMaskDistance(
			displayTileMinX,
			displayTileMinY,
			displayTileMaxX,
			displayTileMaxY,
			eyeDisplayWidth,
			displayHeight,
			centerArea,
			centerHorizontalScale,
			centerOffset);

		const float outerDistance = TileMinMaskDistance(
			displayTileMinX,
			displayTileMinY,
			displayTileMaxX,
			displayTileMaxY,
			eyeDisplayWidth,
			displayHeight,
			outerArea,
			centerHorizontalScale,
			centerOffset,
			FoveatedCommon::kCenterAreaMax);
		const float coarseSplitDistance = TileMinMaskDistance(
			displayTileMinX,
			displayTileMinY,
			displayTileMaxX,
			displayTileMaxY,
			eyeDisplayWidth,
			displayHeight,
			coarseSplitArea,
			centerHorizontalScale,
			centerOffset,
			FoveatedCommon::kCenterAreaMax);

		const float tileCenterX = (displayTileMinX + displayTileMaxX) * 0.5f;
		const float tileCenterY = (displayTileMinY + displayTileMaxY) * 0.5f;
		const float centerX = (0.5f + centerOffset.x) * eyeDisplayWidth;
		const float centerY = (0.5f + centerOffset.y) * displayHeight;

		if (fullRatePattern)
			return kRateIndex1x1;
		if (a_settings.coarseOutsideMask && outerDistance > 1.0f)
			return coarseSplitDistance > 1.0f ? kRateIndex4x4 : kRateIndex2x2;
		if (centerDistance <= 1.0f)
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
				const float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
				const uint32_t eye = static_cast<uint32_t>(std::clamp(tileCenterX / std::max(eyeRenderWidth, 1.0f), 0.0f, 1.0f));
				const float eyeOffsetX = eyeRenderWidth * static_cast<float>(eye);
				const float localTileMinX = std::clamp(tileMinX - eyeOffsetX, 0.0f, eyeRenderWidth);
				const float localTileMaxX = std::clamp(tileMaxX - eyeOffsetX, 0.0f, eyeRenderWidth);
				const uint8_t leftCategory = ToStereoRateCategory(resolveRateIndex(0, localTileMinX, tileMinY, localTileMaxX, tileMaxY));
				const uint8_t rightCategory = ToStereoRateCategory(resolveRateIndex(1, localTileMinX, tileMinY, localTileMaxX, tileMaxY));
				// Keep stereo VRS binocularly conservative: if either eye needs a finer rate,
				// both viewport tables decode that finer rate for the same local tile.
				const uint8_t stereoCategory = std::min(leftCategory, rightCategory);
				rateIndex = EncodeStereoRateCategories(stereoCategory, stereoCategory);
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

bool AAVRSController::Bind(ID3D11DeviceContext* a_context)
{
	if (!a_context || !shadingRateView)
		return false;

	ViewportCountInfo bindInfo{};
	bool usedViewportFallback = false;
	const auto bindWithTable = [&](bool a_enable4x4, uint32_t a_minViewportCount, bool a_forceMinViewportCount, NvAPI_Status& a_outViewportStatus, NvAPI_Status& a_outSurfaceStatus) {
		const bool stereo = hasLastSettings && lastSettings.stereo;
		const ViewportCountInfo viewportInfo = QueryViewportCount(a_context, a_minViewportCount, a_forceMinViewportCount);
		const uint32_t viewportCount = viewportInfo.bound;
		std::array<NV_D3D11_VIEWPORT_SHADING_RATE_DESC, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewportDescs{};
		for (uint32_t i = 0; i < viewportCount; ++i) {
			FillRateTable(viewportDescs[i], a_enable4x4, stereo, i);
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
	const auto bindWithFallbackViewportCount = [&](bool a_enable4x4, NvAPI_Status& a_outViewportStatus, NvAPI_Status& a_outSurfaceStatus) {
		const bool stereo = hasLastSettings && lastSettings.stereo;
		usedViewportFallback = false;
		if (bindWithTable(a_enable4x4, stereo ? 2u : 1u, stereo, a_outViewportStatus, a_outSurfaceStatus))
			return true;
		if (stereo && a_outViewportStatus != NVAPI_OK) {
			usedViewportFallback = true;
			return bindWithTable(a_enable4x4, 1u, false, a_outViewportStatus, a_outSurfaceStatus);
		}
		return false;
	};

	NvAPI_Status viewportResult = NVAPI_OK;
	NvAPI_Status surfaceResult = NVAPI_OK;
	const auto logViewportBindMode = [&]() {
		if (loggedViewportBindMode)
			return;

		logger::info(
			"[Upscaling] AA VRS viewport bind: stereo={}, reported={}, bound={}, forced={}, fallback={}, viewportStatus={}, surfaceStatus={}",
			hasLastSettings && lastSettings.stereo,
			bindInfo.reported,
			bindInfo.bound,
			bindInfo.forcedMinimum,
			usedViewportFallback,
			static_cast<int>(viewportResult),
			static_cast<int>(surfaceResult));
		loggedViewportBindMode = true;
	};

	if (!bindWithFallbackViewportCount(allow4x4Rate, viewportResult, surfaceResult)) {
		if (allow4x4Rate && bindWithFallbackViewportCount(false, viewportResult, surfaceResult)) {
			allow4x4Rate = false;
			if (!logged4x4Fallback) {
				logger::warn("[Upscaling] AA VRS: 4x4 shading rate unavailable, falling back to 2x2");
				logged4x4Fallback = true;
			}
			logViewportBindMode();
			active = true;
			lastDisableReason = "";
			return true;
		}
		if (!loggedResourceFailure) {
			logger::warn(
				"[Upscaling] AA VRS bind failed: viewport={}, surface={}",
				static_cast<int>(viewportResult),
				static_cast<int>(surfaceResult));
			loggedResourceFailure = true;
		}
		lastDisableReason = "Failed to bind shading-rate state";
		DisableInternal(a_context);
		return false;
	}

	logViewportBindMode();
	active = true;
	lastDisableReason = "";
	return true;
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
}

void AAVRSController::ReleaseView()
{
	if (shadingRateView) {
		shadingRateView->Release();
		shadingRateView = nullptr;
	}
}
