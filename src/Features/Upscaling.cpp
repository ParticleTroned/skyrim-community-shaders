#include "Upscaling.h"

#include "Deferred.h"
#include "FoveatedCommon.h"
#include "Hooks.h"
#include "Menu/Fonts.h"
#include "RE/B/BSOpenVR.h"
#include "State.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Utils/OpenCompositeInterop.h"
#include "Utils/UI.h"
#include "VR.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cwctype>
#include <directx/d3dx12.h>
#include <filesystem>
#include <format>
#include <string_view>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Upscaling::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	dlssPreset,
	frameLimitMode,
	frameGenerationMode,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessDLSS,
	fsr4RuntimeEnable,
	foveatedVendorDispatch,
	foveatedCenterArea,
	foveatedCenterHorizontalScale,
	foveatedLeftEyeMaskOffsetX,
	foveatedLeftEyeMaskOffsetY,
	foveatedRightEyeMaskOffsetX,
	foveatedRightEyeMaskOffsetY,
	periphery_taa_center_area,
	foveatedPeripheryMaskVisualization,
	periphery_taa_enable,
	periphery_taa_outer_scale,
	periphery_taa_center_blend_feather,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

namespace
{
	constexpr float kPeripheryTAAOuterScaleMin = 0.30f;
	constexpr float kPeripheryTAAOuterScaleMax = 1.0f;
	constexpr float kPeripheryTAACenterBlendFeatherMin = 0.0f;
	constexpr float kPeripheryTAACenterBlendFeatherMax = 0.10f;
	constexpr float kDynamicResolutionUpscalingScaleThreshold = 0.99f;
	constexpr float kFoveatedMaskOffsetAdjustMin = -0.15f;
	constexpr float kFoveatedMaskOffsetAdjustMax = 0.15f;
	constexpr float kFoveatedMaskOffsetResolvedMin = -0.25f;
	constexpr float kFoveatedMaskOffsetResolvedMax = 0.25f;
	std::atomic_bool g_vrLoadingMenuOpenFromEvent{ false };
	constexpr const char* kFoveatedUpscalingMethodAvailabilityText = "VR FOV mask setup is available only with DLSS or FSR.";
	constexpr const char* kFoveatedUpscalingSetupIntro = R"(- Upscaling FOV renders the green center with DLSS/DLAA or FSR and uses a cheaper outer mask. Smaller green area means more performance, but more risk of peripheral shimmer.

- Upscaling FOV + Peripheral TAA adds a yellow TAA ring around the green center to reduce shimmer. It costs more than Upscaling FOV alone, but can let you keep the green center smaller and thereby increase performance wins compared to Upscaling FOV alone.

- Shader foveation features reuse this shared mask; they do not have separate area sliders.)";
	constexpr const char* kFoveatedUpscalingSetupInstructions = R"(1) Activate FOV Mask Visualization
2) Use the Upscaling FOV Area slider to decrease FOV Area to 0.25 and place the green center mask in the center of each eye. Per-eye positions do not have to be vertically or horizontally aligned.
3) Expand Upscaling FOV Area until the green mask touches the top and bottom view of your HMD. If needed, reposition right and left eye to get the best top and bottom fit.
4) Use the Expand FOV Area R/L slider to horizontally expand the mask until the green part just touches the field of view.
5) Ideally, you do not see the blue outer mask anymore, except in the corners, or only a tiny bit.
6) The larger the green center area, the less performance savings you have.
7) Test in game that you do not have strong peripheral shimmer. If yes, increase the green mask area. If not, reduce it to just before shimmer appears for best performance.)";
	constexpr const char* kFoveatedUpscalingPeripheralTaaSetupInstructions = R"(1) Activate FOV Mask Visualization
2) Lower the Upscaling FOV Area slider to 0.30. You can later try 0.25 if these settings work for you for even more performance wins.
3) Use the TAA Peripheral Range slider until the yellow ring touches the top and bottom view of your HMD. If needed, reposition right and left eye to get the best top and bottom fit.
4) Ideally, you do not see the blue outer ring anymore, except in the corners, or only a tiny bit.
5) The larger the green center area, the less performance savings you have.
6) Test in game that you do not have strong peripheral shimmer. If yes, increase the yellow mask area. If not, reduce it to just before shimmer appears for best performance.)";

	uint32_t g_submitStageOutputEyeWidth = 0;
	uint32_t g_submitStageOutputEyeHeight = 0;
	bool g_submitStageTargetSizeKnown = false;

	bool IsVendorUpscalingMethod(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return a_upscaleMethod == Upscaling::UpscaleMethod::kFSR || a_upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
	}

	float GetSubmitStageRequestedQualityScale()
	{
		const uint32_t qualityMode = std::min<uint32_t>(
			globals::features::upscaling.settings.qualityMode,
			Upscaling::kQualityModeMaxIndex);
		return Upscaling::GetQualityModeResolutionScale(qualityMode);
	}

	bool IsSubmitStageRequestedUpscalingActive()
	{
		return GetSubmitStageRequestedQualityScale() < kDynamicResolutionUpscalingScaleThreshold;
	}

	bool IsSubmitStageDynamicResolutionActive()
	{
		if (!REL::Module::IsVR())
			return false;

		const auto upscaleMethod = globals::features::upscaling.GetUpscaleMethod();
		if (!IsVendorUpscalingMethod(upscaleMethod))
			return false;

		return IsSubmitStageRequestedUpscalingActive();
	}

	bool ShouldUseStableSubmitStageDLSSInputs()
	{
		return globals::game::isVR &&
		       globals::features::upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
		       g_submitStageTargetSizeKnown;
	}

	uint32_t GetStableSubmitStageInputDimension(uint32_t a_requestedDimension, uint32_t a_outputDimension)
	{
		if (!ShouldUseStableSubmitStageDLSSInputs())
			return a_requestedDimension;

		const float maxUpscalingInputScale = Upscaling::GetQualityModeResolutionScale(1u);
		const uint32_t maxUpscalingInputDimension =
			static_cast<uint32_t>(std::ceil(static_cast<float>(a_outputDimension) * maxUpscalingInputScale));
		return std::max(a_requestedDimension, maxUpscalingInputDimension);
	}

	float GetSubmitStageRequestedRenderScale()
	{
		return std::clamp(GetSubmitStageRequestedQualityScale(), 0.1f, 1.0f);
	}

	float GetSubmitStageInternalDynamicResolutionScale()
	{
		return GetSubmitStageRequestedRenderScale();
	}

	void CopyResourceIfNonAliased(ID3D11DeviceContext* a_context, ID3D11Resource* a_dst, ID3D11Resource* a_src)
	{
		if (a_context && a_dst && a_src && a_dst != a_src) {
			a_context->CopyResource(a_dst, a_src);
		}
	}

	float ClampFoveatedCenterArea(float value)
	{
		return FoveatedCommon::ClampCenterArea(value);
	}

	float ClampFoveatedCenterHorizontalScale(float value)
	{
		return FoveatedCommon::ClampCenterHorizontalScale(value);
	}

	float ClampFoveatedMaskOffsetAdjustment(float value)
	{
		if (!std::isfinite(value))
			return 0.0f;
		return std::clamp(value, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax);
	}

	uint ClampToggleUInt(uint value)
	{
		return std::min<uint>(value, 1u);
	}

	uint ClampQualityModeUInt(uint value)
	{
		return std::min<uint>(value, Upscaling::kQualityModeMaxIndex);
	}

	struct NormalizedTextureRegion
	{
		float2 scale{ 1.0f, 1.0f };
		float2 offset{ 0.0f, 0.0f };
	};

	float ClampUnitOrDefault(float value, float fallback)
	{
		return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
	}

	NormalizedTextureRegion ClampNormalizedTextureRegion(float scaleX, float scaleY, float offsetX, float offsetY)
	{
		NormalizedTextureRegion region{};
		region.offset = {
			ClampUnitOrDefault(offsetX, 0.0f),
			ClampUnitOrDefault(offsetY, 0.0f)
		};
		region.scale = {
			ClampUnitOrDefault(scaleX, 1.0f),
			ClampUnitOrDefault(scaleY, 1.0f)
		};
		region.scale.x = std::min(region.scale.x, 1.0f - region.offset.x);
		region.scale.y = std::min(region.scale.y, 1.0f - region.offset.y);
		return region;
	}

	float ComputeTopLeftValidScale(uint32_t validDimension, uint32_t textureDimension)
	{
		if (!validDimension || !textureDimension)
			return 1.0f;
		return static_cast<float>(validDimension) / static_cast<float>(textureDimension);
	}

	NormalizedTextureRegion BuildTopLeftValidTextureRegion(uint32_t validWidth, uint32_t validHeight, uint32_t textureWidth, uint32_t textureHeight)
	{
		return ClampNormalizedTextureRegion(
			ComputeTopLeftValidScale(validWidth, textureWidth),
			ComputeTopLeftValidScale(validHeight, textureHeight),
			0.0f,
			0.0f);
	}

	uint MigrateLegacyQualityModeUInt(uint value)
	{
		switch (value) {
		case 0:
			return 0u;
		case 1:
			return 3u;
		case 2:
			return 4u;
		case 3:
			return 5u;
		case 4:
			return 6u;
		// Preserve values written by transitional builds that introduced 0-6 modes
		// before `qualityModeSchemaVersion` existed.
		case 5:
		case 6:
			return value;
		default:
			return 6u;
		}
	}

	const char* GetQualityModeName(uint value, bool isDLSS)
	{
		switch (ClampQualityModeUInt(value)) {
		case 1:
			return "Hoshipa";
		case 2:
			return "Ultra Quality";
		case 3:
			return "Quality";
		case 4:
			return "Balanced";
		case 5:
			return "Performance";
		case 6:
			return "Ultra Performance";
		default:
			return isDLSS ? "DLAA" : "Native AA";
		}
	}

	uint ClampStreamlineLogLevelUInt(uint value)
	{
		return std::min<uint>(value, 2u);
	}

	void DestroyTexture(Texture2D*& texture)
	{
		if (!texture)
			return;

		texture->srv = nullptr;
		texture->uav = nullptr;
		texture->rtv = nullptr;
		texture->dsv = nullptr;
		texture->resource = nullptr;
		delete texture;
		texture = nullptr;
	}

	struct OpenCompositeSettingValue
	{
		bool value = false;
		std::string configPath;
	};

	struct OpenCompositeUpscalingSettings
	{
		OpenCompositeSettingValue dlssEnabled;
		OpenCompositeSettingValue fsrEnabled;
		OpenCompositeSettingValue dlaaEnabled;
		OpenCompositeSettingValue fsrNativeAA;
		OpenCompositeSettingValue fsr3PostAAEnabled;
	};

	struct DetectedOpenCompositeUpscalingBlocker
	{
		bool active = false;
		std::string settingName;
		std::string configPath;
	};

	std::string_view TrimAsciiWhitespace(std::string_view value)
	{
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
			value.remove_prefix(1);
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
			value.remove_suffix(1);
		return value;
	}

	std::string ToLowerAscii(std::string_view value)
	{
		std::string result(value);
		std::ranges::transform(result, result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}

	bool TryParseOpenCompositeBool(std::string value, bool& outValue)
	{
		value = ToLowerAscii(TrimAsciiWhitespace(value));
		if (value == "true" || value == "on" || value == "enabled" || value == "1") {
			outValue = true;
			return true;
		}
		if (value == "false" || value == "off" || value == "disabled" || value == "0") {
			outValue = false;
			return true;
		}
		return false;
	}

	std::string PathToDisplayString(const std::filesystem::path& path)
	{
		return path.string();
	}

	void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
	{
		if (path.empty())
			return;

		auto normalized = path.lexically_normal().wstring();
		std::ranges::transform(normalized, normalized.begin(), [](wchar_t c) {
			return static_cast<wchar_t>(std::towlower(c));
		});

		const bool alreadyAdded = std::ranges::any_of(paths, [&](const std::filesystem::path& existing) {
			auto existingNormalized = existing.lexically_normal().wstring();
			std::ranges::transform(existingNormalized, existingNormalized.begin(), [](wchar_t c) {
				return static_cast<wchar_t>(std::towlower(c));
			});
			return existingNormalized == normalized;
		});
		if (!alreadyAdded)
			paths.push_back(path);
	}

	std::filesystem::path GetCurrentDirectoryPath()
	{
		std::wstring buffer(MAX_PATH, L'\0');
		const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
		if (length == 0)
			return {};

		if (length >= buffer.size()) {
			buffer.resize(length + 1);
			const DWORD retryLength = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
			if (retryLength == 0 || retryLength >= buffer.size())
				return {};
			buffer.resize(retryLength);
		} else {
			buffer.resize(length);
		}

		return std::filesystem::path(buffer);
	}

	std::filesystem::path GetLoadedOpenVRDirectory()
	{
		HMODULE openVRModule = GetModuleHandleW(L"openvr_api.dll");
		if (!openVRModule)
			return {};

		std::wstring buffer(MAX_PATH, L'\0');
		const DWORD length = GetModuleFileNameW(openVRModule, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return {};

		buffer.resize(length);
		return std::filesystem::path(buffer).parent_path();
	}

	bool ShouldProbeOpenCompositeConfig()
	{
		if (!globals::game::isVR)
			return false;

		const auto& cachedInfo = globals::features::vr.openVRInfo;
		if (cachedInfo.isAvailable)
			return cachedInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;

		const auto detectedInfo = VRDetection::Detect();
		return detectedInfo.isAvailable &&
		       detectedInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;
	}

	std::vector<std::filesystem::path> GetOpenCompositeConfigCandidates()
	{
		std::vector<std::filesystem::path> candidates;

		const auto loadedOpenVRDirectory = GetLoadedOpenVRDirectory();
		if (!loadedOpenVRDirectory.empty())
			AddUniquePath(candidates, loadedOpenVRDirectory / L"opencomposite.ini");

		const auto currentDirectory = GetCurrentDirectoryPath();
		if (!currentDirectory.empty()) {
			AddUniquePath(candidates, currentDirectory / L"opencomposite.ini");
			AddUniquePath(candidates, currentDirectory / L"opencomposite_ext.ini");
		}

		return candidates;
	}

	bool TryReadIniBoolSetting(const CSimpleIniA& ini, const char* key, bool& outValue)
	{
		auto tryReadSection = [&](const char* section) {
			const char* rawValue = ini.GetValue(section, key, nullptr);
			return rawValue && TryParseOpenCompositeBool(rawValue, outValue);
		};

		if (tryReadSection(""))
			return true;

		CSimpleIniA::TNamesDepend sections;
		ini.GetAllSections(sections);
		for (const auto& section : sections) {
			if (section.pItem && tryReadSection(section.pItem))
				return true;
		}

		return false;
	}

	void UpdateOpenCompositeSettingValue(OpenCompositeSettingValue& setting, const CSimpleIniA& ini, const char* key, const std::filesystem::path& path)
	{
		bool parsedValue = false;
		if (!TryReadIniBoolSetting(ini, key, parsedValue))
			return;

		setting.value = parsedValue;
		setting.configPath = PathToDisplayString(path);
	}

	OpenCompositeUpscalingSettings ReadOpenCompositeUpscalingSettings()
	{
		OpenCompositeUpscalingSettings settings;

		std::error_code ec;
		for (const auto& path : GetOpenCompositeConfigCandidates()) {
			if (!std::filesystem::exists(path, ec))
				continue;
			ec.clear();

			CSimpleIniA ini;
			ini.SetUnicode();
			const SI_Error rc = ini.LoadFile(path.c_str());
			if (rc < 0) {
				logger::warn("[Upscaling] Failed to read Open Composite config '{}': {}", PathToDisplayString(path), rc);
				continue;
			}

			UpdateOpenCompositeSettingValue(settings.dlssEnabled, ini, "dlssEnabled", path);
			UpdateOpenCompositeSettingValue(settings.fsrEnabled, ini, "fsrEnabled", path);
			UpdateOpenCompositeSettingValue(settings.dlaaEnabled, ini, "dlaaEnabled", path);
			UpdateOpenCompositeSettingValue(settings.fsrNativeAA, ini, "fsrNativeAA", path);
			UpdateOpenCompositeSettingValue(settings.fsr3PostAAEnabled, ini, "fsr3PostAAEnabled", path);
		}

		return settings;
	}

	DetectedOpenCompositeUpscalingBlocker FindOpenCompositeUpscalingBlocker()
	{
		DetectedOpenCompositeUpscalingBlocker blocker;
		if (!globals::game::isVR)
			return blocker;

		Util::OCUExternalUpscalerState externalState{};
		if (Util::TryReadOCUExternalUpscalerState(externalState)) {
			blocker.active = true;
			blocker.settingName = "OpenCompositeUnleashedSharedState";
			blocker.configPath = "Local\\OpenCompositeUnleashedUpscalingState";
			return blocker;
		}

		if (!ShouldProbeOpenCompositeConfig())
			return blocker;

		const auto settings = ReadOpenCompositeUpscalingSettings();
		auto setBlocker = [&](const char* settingName, const OpenCompositeSettingValue& setting) {
			blocker.active = true;
			blocker.settingName = settingName;
			blocker.configPath = setting.configPath;
		};

		if (settings.dlaaEnabled.value)
			setBlocker("dlaaEnabled", settings.dlaaEnabled);
		else if (settings.fsrNativeAA.value)
			setBlocker("fsrNativeAA", settings.fsrNativeAA);
		else if (settings.fsr3PostAAEnabled.value)
			setBlocker("fsr3PostAAEnabled", settings.fsr3PostAAEnabled);
		else if (settings.dlssEnabled.value)
			setBlocker("dlssEnabled", settings.dlssEnabled);
		else if (settings.fsrEnabled.value)
			setBlocker("fsrEnabled", settings.fsrEnabled);

		return blocker;
	}

	float ClampPeripheryTAACenterBlendFeather(float value)
	{
		if (!std::isfinite(value))
			return FoveatedCommon::kCenterFeather;
		return std::clamp(value, kPeripheryTAACenterBlendFeatherMin, kPeripheryTAACenterBlendFeatherMax);
	}

	float ClampPeripheryTAAOuterScale(float value)
	{
		if (!std::isfinite(value))
			return 1.0f;
		return std::clamp(value, kPeripheryTAAOuterScaleMin, kPeripheryTAAOuterScaleMax);
	}

	float GetPeripheryTAAOuterScaleFloor(float centerScale, float centerHorizontalScale, float centerFeather)
	{
		centerScale = ClampFoveatedCenterArea(centerScale);
		centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
		centerFeather = std::max(ClampPeripheryTAACenterBlendFeather(centerFeather), 1e-4f);

		const float radiusX = std::max(centerScale * centerHorizontalScale * 0.5f, 1e-4f);
		const float radiusY = std::max(centerScale * 0.5f, 1e-4f);
		const float baseRadius = std::max(std::min(radiusX, radiusY), 1e-4f);
		const float normalizedFeather = centerFeather / baseRadius;
		const float minOuterScale = centerScale * (1.0f + normalizedFeather);
		return std::clamp(minOuterScale, kPeripheryTAAOuterScaleMin, kPeripheryTAAOuterScaleMax);
	}

	float ClampPeripheryTAAOuterScaleForCenter(float value, float centerScale, float centerHorizontalScale, float centerFeather)
	{
		const float minOuterScale = GetPeripheryTAAOuterScaleFloor(centerScale, centerHorizontalScale, centerFeather);
		return std::clamp(ClampPeripheryTAAOuterScale(value), minOuterScale, kPeripheryTAAOuterScaleMax);
	}

	float ClampFiniteUnitRange(float value, float fallback)
	{
		if (!std::isfinite(value))
			return fallback;
		return std::clamp(value, 0.0f, 1.0f);
	}

	int32_t QuantizePeripheryTAATileParam(float value)
	{
		if (!std::isfinite(value))
			value = 0.0f;
		return static_cast<int32_t>(std::lround(value * 10000.0f));
	}

	uint32_t MapOutputToInputMin(uint32_t outputValue, uint32_t outputExtent, uint32_t inputExtent)
	{
		if (outputExtent == 0)
			return 0u;
		const double scale = static_cast<double>(inputExtent) / static_cast<double>(outputExtent);
		return static_cast<uint32_t>(std::floor(static_cast<double>(outputValue) * scale));
	}

	uint32_t MapOutputToInputMax(uint32_t outputValue, uint32_t outputExtent, uint32_t inputExtent)
	{
		if (outputExtent == 0)
			return 0u;
		const double scale = static_cast<double>(inputExtent) / static_cast<double>(outputExtent);
		return static_cast<uint32_t>(std::ceil(static_cast<double>(outputValue) * scale));
	}

	struct MappedRect
	{
		uint32_t minX = 0;
		uint32_t minY = 0;
		uint32_t maxX = 0;
		uint32_t maxY = 0;

		bool IsValid() const
		{
			return maxX > minX && maxY > minY;
		}
	};

	MappedRect MapOutputRectToInputRect(
		uint32_t outputMinX,
		uint32_t outputMinY,
		uint32_t outputMaxX,
		uint32_t outputMaxY,
		uint32_t outputWidth,
		uint32_t outputHeight,
		uint32_t inputWidth,
		uint32_t inputHeight,
		uint32_t padding = 0u)
	{
		MappedRect mapped{};
		if (outputWidth == 0 || outputHeight == 0 || inputWidth == 0 || inputHeight == 0)
			return mapped;

		outputMinX = std::min(outputMinX, outputWidth);
		outputMinY = std::min(outputMinY, outputHeight);
		outputMaxX = std::min(outputMaxX, outputWidth);
		outputMaxY = std::min(outputMaxY, outputHeight);
		if (outputMaxX < outputMinX)
			std::swap(outputMaxX, outputMinX);
		if (outputMaxY < outputMinY)
			std::swap(outputMaxY, outputMinY);
		if (outputMaxX <= outputMinX || outputMaxY <= outputMinY)
			return mapped;

		uint32_t inputMinX = MapOutputToInputMin(outputMinX, outputWidth, inputWidth);
		uint32_t inputMaxX = MapOutputToInputMax(outputMaxX, outputWidth, inputWidth);
		uint32_t inputMinY = MapOutputToInputMin(outputMinY, outputHeight, inputHeight);
		uint32_t inputMaxY = MapOutputToInputMax(outputMaxY, outputHeight, inputHeight);

		if (padding > 0) {
			inputMinX = inputMinX > padding ? inputMinX - padding : 0u;
			inputMinY = inputMinY > padding ? inputMinY - padding : 0u;
			inputMaxX = static_cast<uint32_t>(std::min<uint64_t>(inputWidth, static_cast<uint64_t>(inputMaxX) + padding));
			inputMaxY = static_cast<uint32_t>(std::min<uint64_t>(inputHeight, static_cast<uint64_t>(inputMaxY) + padding));
		}

		inputMinX = std::min(inputMinX, inputWidth);
		inputMinY = std::min(inputMinY, inputHeight);
		inputMaxX = std::min(inputMaxX, inputWidth);
		inputMaxY = std::min(inputMaxY, inputHeight);
		if (inputMaxX <= inputMinX || inputMaxY <= inputMinY)
			return mapped;

		mapped.minX = inputMinX;
		mapped.minY = inputMinY;
		mapped.maxX = inputMaxX;
		mapped.maxY = inputMaxY;
		return mapped;
	}

	struct FoveatedMaskProfileParams
	{
		float centerArea = 0.6f;
		float centerHorizontalScale = 1.0f;
		float leftOffsetX = 0.0f;
		float leftOffsetY = 0.0f;
		float rightOffsetX = 0.0f;
		float rightOffsetY = 0.0f;
	};

	FoveatedMaskProfileParams GetFoveatedMaskProfileParams(const Upscaling::Settings& settings, bool usePeripheryTAAProfile)
	{
		FoveatedMaskProfileParams params{};
		params.centerArea = ClampFoveatedCenterArea(usePeripheryTAAProfile ? settings.periphery_taa_center_area : settings.foveatedCenterArea);
		params.centerHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
		params.leftOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
		params.leftOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
		params.rightOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
		params.rightOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);
		return params;
	}

	float FoveatedMaskDistanceUV(float uvX, float uvY, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		centerScale = ClampFoveatedCenterArea(centerScale);
		centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);

		const float radiusX = std::max(centerScale * centerHorizontalScale * 0.5f, 1e-4f);
		const float radiusY = std::max(centerScale * 0.5f, 1e-4f);
		const float centerX = std::clamp(0.5f + centerOffsetX, 0.0f, 1.0f);
		const float centerY = std::clamp(0.5f + centerOffsetY, 0.0f, 1.0f);
		const float normalizedX = std::abs((uvX - centerX) / radiusX);
		const float normalizedY = std::abs((uvY - centerY) / radiusY);
		const float pNorm = std::pow(normalizedX, FoveatedCommon::kMaskShapePower) + std::pow(normalizedY, FoveatedCommon::kMaskShapePower);
		return std::pow(std::max(pNorm, 0.0f), 1.0f / FoveatedCommon::kMaskShapePower);
	}

	float FoveatedMaskDistancePixelCenter(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
		const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
		return FoveatedMaskDistanceUV((static_cast<float>(x) + 0.5f) * invWidth, (static_cast<float>(y) + 0.5f) * invHeight, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
	}

	float FoveatedMaskTileMinDistance(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
		const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
		const float minUvX = (static_cast<float>(minX) + 0.5f) * invWidth;
		const float minUvY = (static_cast<float>(minY) + 0.5f) * invHeight;
		const float maxUvX = (static_cast<float>(maxX - 1u) + 0.5f) * invWidth;
		const float maxUvY = (static_cast<float>(maxY - 1u) + 0.5f) * invHeight;
		const float centerUvX = std::clamp(0.5f + centerOffsetX, 0.0f, 1.0f);
		const float centerUvY = std::clamp(0.5f + centerOffsetY, 0.0f, 1.0f);
		return FoveatedMaskDistanceUV(
			std::clamp(centerUvX, minUvX, maxUvX),
			std::clamp(centerUvY, minUvY, maxUvY),
			centerScale,
			centerHorizontalScale,
			centerOffsetX,
			centerOffsetY);
	}

	float FoveatedMaskTileMaxDistance(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, uint32_t width, uint32_t height, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY)
	{
		const uint32_t maxPixelX = maxX - 1u;
		const uint32_t maxPixelY = maxY - 1u;
		return std::max({
			FoveatedMaskDistancePixelCenter(minX, minY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(maxPixelX, minY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(minX, maxPixelY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY),
			FoveatedMaskDistancePixelCenter(maxPixelX, maxPixelY, width, height, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY) });
	}

	void SanitizeFoveatedSettings(Upscaling::Settings& settings)
	{
		settings.foveatedCenterArea = ClampFoveatedCenterArea(settings.foveatedCenterArea);
		settings.foveatedCenterHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
		settings.foveatedLeftEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
		settings.foveatedLeftEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
		settings.foveatedRightEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
		settings.foveatedRightEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);
		settings.periphery_taa_center_area = ClampFoveatedCenterArea(settings.periphery_taa_center_area);
	}

	bool IsDefaultFoveatedMaskGeometry(const Upscaling::Settings& settings)
	{
		const Upscaling::Settings defaults{};
		auto nearlyEqual = [](float lhs, float rhs) {
			return std::abs(lhs - rhs) <= 0.0001f;
		};

		return nearlyEqual(settings.foveatedCenterArea, defaults.foveatedCenterArea) &&
		       nearlyEqual(settings.foveatedCenterHorizontalScale, defaults.foveatedCenterHorizontalScale) &&
		       nearlyEqual(settings.foveatedLeftEyeMaskOffsetX, defaults.foveatedLeftEyeMaskOffsetX) &&
		       nearlyEqual(settings.foveatedLeftEyeMaskOffsetY, defaults.foveatedLeftEyeMaskOffsetY) &&
		       nearlyEqual(settings.foveatedRightEyeMaskOffsetX, defaults.foveatedRightEyeMaskOffsetX) &&
		       nearlyEqual(settings.foveatedRightEyeMaskOffsetY, defaults.foveatedRightEyeMaskOffsetY) &&
		       nearlyEqual(settings.periphery_taa_center_area, defaults.periphery_taa_center_area) &&
		       nearlyEqual(settings.periphery_taa_outer_scale, defaults.periphery_taa_outer_scale) &&
		       nearlyEqual(settings.periphery_taa_center_blend_feather, defaults.periphery_taa_center_blend_feather);
	}

	void SanitizeUpscalingSettings(Upscaling::Settings& settings)
	{
		settings.upscaleMethod = std::min<uint>(settings.upscaleMethod, static_cast<uint>(Upscaling::UpscaleMethod::kDLSS));
		settings.upscaleMethodNoDLSS = std::min<uint>(settings.upscaleMethodNoDLSS, static_cast<uint>(Upscaling::UpscaleMethod::kFSR));
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		settings.dlssPreset = std::min<uint>(settings.dlssPreset, Upscaling::kDLSSPresetMaxIndex);
		settings.frameLimitMode = ClampToggleUInt(settings.frameLimitMode);
		settings.frameGenerationMode = ClampToggleUInt(settings.frameGenerationMode);
		settings.frameGenerationForceEnable = ClampToggleUInt(settings.frameGenerationForceEnable);
		settings.streamlineLogLevel = ClampStreamlineLogLevelUInt(settings.streamlineLogLevel);
		settings.sharpnessFSR = ClampFiniteUnitRange(settings.sharpnessFSR, 0.0f);
		settings.sharpnessDLSS = ClampFiniteUnitRange(settings.sharpnessDLSS, 0.1f);
		settings.periphery_taa_center_blend_feather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
		SanitizeFoveatedSettings(settings);
		settings.periphery_taa_outer_scale = ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area,
			settings.foveatedCenterHorizontalScale,
			settings.periphery_taa_center_blend_feather);
	}

	void ResetVRSpecificUpscalingSettings(Upscaling::Settings& settings)
	{
		settings.foveatedVendorDispatch = false;
		settings.foveatedCenterArea = 0.6f;
		settings.foveatedCenterHorizontalScale = 1.0f;
		settings.foveatedLeftEyeMaskOffsetX = 0.0f;
		settings.foveatedLeftEyeMaskOffsetY = 0.0f;
		settings.foveatedRightEyeMaskOffsetX = 0.0f;
		settings.foveatedRightEyeMaskOffsetY = 0.0f;
		settings.periphery_taa_center_area = 0.6f;
		settings.foveatedPeripheryMaskVisualization = false;
		settings.periphery_taa_enable = false;
		settings.periphery_taa_outer_scale = 0.70f;
		settings.periphery_taa_center_blend_feather = FoveatedCommon::kCenterFeather;
	}

	void StripVRSpecificUpscalingSettings(json& o_json)
	{
		o_json.erase("foveatedVendorDispatch");
		o_json.erase("foveatedCenterArea");
		o_json.erase("foveatedCenterHorizontalScale");
		o_json.erase("foveatedLeftEyeMaskOffsetX");
		o_json.erase("foveatedLeftEyeMaskOffsetY");
		o_json.erase("foveatedRightEyeMaskOffsetX");
		o_json.erase("foveatedRightEyeMaskOffsetY");
		o_json.erase("periphery_taa_center_area");
		o_json.erase("foveatedPeripheryMaskVisualization");
		o_json.erase("periphery_taa_enable");
		o_json.erase("periphery_taa_outer_scale");
		o_json.erase("periphery_taa_center_blend_feather");
	}

	bool SupportsFoveatedVendorDispatch(Upscaling::UpscaleMethod a_upscaleMethod)
	{
		if (!globals::game::isVR)
			return false;

		switch (a_upscaleMethod) {
		case Upscaling::UpscaleMethod::kDLSS:
			return true;
		case Upscaling::UpscaleMethod::kFSR:
			return true;
		default:
			return false;
		}
	}

	bool IsFoveatedVendorDispatchRequested(const Upscaling::Settings& settings, Upscaling::UpscaleMethod a_upscaleMethod)
	{
		return SupportsFoveatedVendorDispatch(a_upscaleMethod) && settings.foveatedVendorDispatch;
	}

	bool ShouldUnlockDynamicResolutionForUpscaling(Upscaling::UpscaleMethod a_upscaleMethod, const float2& a_resolutionScale)
	{
		return IsVendorUpscalingMethod(a_upscaleMethod) &&
		       (a_resolutionScale.x < kDynamicResolutionUpscalingScaleThreshold ||
				   a_resolutionScale.y < kDynamicResolutionUpscalingScaleThreshold);
	}

	void SetDynamicResolutionEnabledForUpscaling(bool a_enabled)
	{
		if (!globals::game::isVR)
			return;

		static bool initialized = false;
		static bool originalEnabled = false;
		static bool changedByUpscaling = false;

		const static auto address = REL::RelocationID{ 508794, 380760 }.address();
		auto* enabled = reinterpret_cast<bool*>(address);
		if (!initialized) {
			originalEnabled = *enabled;
			initialized = true;
		}

		const bool targetEnabled = a_enabled ? true : (changedByUpscaling ? originalEnabled : *enabled);
		if (*enabled != targetEnabled) {
			*enabled = targetEnabled;
		}

		changedByUpscaling = a_enabled;
	}

	void DisableAutoDynamicResolutionSetting()
	{
		if (!globals::game::isVR)
			return;

		constexpr const char* settingNames[] = {
			"bEnableAutoDynamicResolution:Display",
			"bEnableAutoDynamicResolution"
		};

		bool found = false;
		bool changed = false;
		auto disableInCollection = [&](auto* a_collection, const char* a_collectionName) {
			if (!a_collection)
				return;

			for (const auto* settingName : settingNames) {
				auto* setting = a_collection->GetSetting(settingName);
				if (!setting)
					continue;

				found = true;
				if (setting->data.b) {
					setting->data.b = false;
					changed = true;
					if (a_collection->WriteSetting(setting)) {
						logger::info("[Upscaling] Disabled {} in {}.", settingName, a_collectionName);
					} else {
						logger::warn("[Upscaling] Disabled {} in memory, but failed to write {}.", settingName, a_collectionName);
					}
				}
				return;
			}
		};

		disableInCollection(globals::game::iniSettingCollection, "Skyrim.ini");
		disableInCollection(globals::game::iniPrefSettingCollection, "SkyrimPrefs.ini");

		for (const auto* settingName : settingNames) {
			auto* setting = RE::GetINISetting(settingName);
			if (!setting)
				continue;

			found = true;
			if (setting->data.b) {
				setting->data.b = false;
				changed = true;
				logger::info("[Upscaling] Disabled {} in runtime settings.", settingName);
			}
			break;
		}

		if (!found) {
			logger::debug("[Upscaling] bEnableAutoDynamicResolution setting was not found.");
		} else if (!changed) {
			logger::debug("[Upscaling] bEnableAutoDynamicResolution was already disabled.");
		}
	}

	bool IsVRRuntimeActive()
	{
		return globals::game::isVR;
	}

	bool IsLoadingMenuContextActive()
	{
		auto state = globals::state;
		auto ui = globals::game::ui;
		return g_vrLoadingMenuOpenFromEvent.load(std::memory_order_relaxed) ||
		       (state && state->isLoadingMenuOpen) ||
		       (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	}

	bool IsKnownGameMenuContextActive()
	{
		auto state = globals::state;
		auto ui = globals::game::ui;
		return (state && (state->isMapMenuOpen || state->isMainMenuOpen)) ||
		       IsLoadingMenuContextActive() ||
		       (ui && ui->GameIsPaused());
	}

	bool IsGameMenuContextActive()
	{
		auto state = globals::state;
		const bool nonWorldPresentation =
			state && state->lastWorldRenderFrame != state->frameCount;
		return IsKnownGameMenuContextActive() || nonWorldPresentation;
	}

	bool TryGetTexture2DDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& outDesc)
	{
		if (!resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
			return false;

		texture->GetDesc(&outDesc);
		return true;
	}

	eastl::unique_ptr<Texture2D> CreateNamedTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format, bool createSRV, bool createUAV, bool createRTV, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = (createSRV ? D3D11_BIND_SHADER_RESOURCE : 0u) | (createUAV ? D3D11_BIND_UNORDERED_ACCESS : 0u) | (createRTV ? D3D11_BIND_RENDER_TARGET : 0u);

		auto texture = eastl::make_unique<Texture2D>(desc);
		if (name) {
			Util::SetResourceName(texture->resource.get(), name);
		}

		if (createSRV) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);
		}

		if (createUAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
		}

		if (createRTV) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;
			texture->CreateRTV(rtvDesc);
		}

		return texture;
	}
}

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced upscaling and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainUpscaling(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	auto& upscaling = globals::features::upscaling;
	upscaling.LoadUpscalingSDKs();

	if (upscaling.IsBackendInitialized())
		upscaling.CheckBackendFeatures(pAdapter);

	// Use better swap effect to prevent tearing and improve performance
	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// FLIP_DISCARD requires at least two buffers.
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;
	// This branch currently runs without HDRDisplay integration; normalize sRGB
	// swapchain formats to UNORM for the D3D12 proxy/inter-op path.
	if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	const bool isVR = REL::Module::IsVR();
	bool shouldProxy = !isVR;
	if (shouldProxy)
		if (!pSwapChainDesc->Windowed)
			shouldProxy = false;

	auto refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);
	upscaling.refreshRate = refreshRate;

	if (shouldProxy) {
		if (upscaling.settings.frameGenerationMode)
			if (refreshRate >= 120)
				shouldProxy = true;
			else if (upscaling.settings.frameGenerationForceEnable)
				shouldProxy = true;
			else
				shouldProxy = false;
		else
			shouldProxy = false;
	}

	upscaling.lowRefreshRate = refreshRate < 120;
	upscaling.isWindowed = pSwapChainDesc->Windowed;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		const bool hasFrameGenModule = upscaling.HasFrameGenModule();
		if (hasFrameGenModule) {
			DX::ThrowIfFailed(D3D11CreateDevice(
				pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext));

			upscaling.SetProxyD3D11Device(*ppDevice);
			upscaling.SetProxyD3D11DeviceContext(*ppImmediateContext);
			upscaling.CreateProxySwapChain(pAdapter, *pSwapChainDesc);
			upscaling.CreateProxyInterop();

			*ppSwapChain = upscaling.GetProxySwapChain();

			upscaling.d3d12SwapChainActive = true;

			if (upscaling.IsBackendInitialized()) {
				upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
				upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
				upscaling.SetBackendD3DDevice(*ppDevice);
				// Some Streamline features (notably Reflex/PCL) may not report
				// load/support status reliably until the D3D device is bound.
				upscaling.CheckBackendFeatures(pAdapter);
				upscaling.PostBackendDevice();
			}

			return S_OK;
		} else {
			logger::warn("[Frame Generation] FidelityFX DLLs are not loaded, skipping proxy");
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (upscaling.IsBackendInitialized()) {
		upscaling.UpgradeBackendInterface((void**)&(*ppDevice));
		upscaling.UpgradeBackendInterface((void**)&(*ppSwapChain));
		upscaling.SetBackendD3DDevice(*ppDevice);
		// Re-check after device bind to ensure feature availability is accurate.
		upscaling.CheckBackendFeatures(pAdapter);
		upscaling.PostBackendDevice();
	}

	return ret;
}

void Upscaling::DrawSettings()
{
	struct UpscaleUiChoice
	{
		UpscaleMethod method;
		bool useRuntimeFsr4;
		const char* label;
	};

	const bool isNvidiaAdapter = fidelityFX.IsNvidiaAdapterDetected();
	const bool runtimeUpscalerPresent = fidelityFX.IsRuntimeUpscalerPresent();
	const bool runtimeFsr4AutoEligible = fidelityFX.IsRuntimeFsr4AutoEligible();
	const bool featureDLSS = streamline.featureDLSS;
	ApplyOpenCompositeUpscalingBlocker();
	const auto& openCompositeBlocker = GetOpenCompositeUpscalingBlocker();
	const bool openCompositeBlocksUpscaling = openCompositeBlocker.active;

	uint32_t* currentUpscaleMode = &settings.upscaleMethod;
	if (!featureDLSS)
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;
	if (*currentUpscaleMode == static_cast<uint32_t>(UpscaleMethod::kFSR) && !runtimeFsr4AutoEligible)
		settings.fsr4RuntimeEnable = false;

	std::vector<UpscaleUiChoice> upscaleChoices = {
		{ UpscaleMethod::kNONE, false, "None" }
	};

	if (!openCompositeBlocksUpscaling) {
		upscaleChoices.push_back({ UpscaleMethod::kTAA, false, "TAA" });
		upscaleChoices.push_back({ UpscaleMethod::kFSR, false, "AMD FSR 3.1.5" });

		if (runtimeFsr4AutoEligible)
			upscaleChoices.push_back({ UpscaleMethod::kFSR, true, "AMD FSR 4" });

		if (featureDLSS)
			upscaleChoices.push_back({ UpscaleMethod::kDLSS, false, "NVIDIA DLSS" });
	}

	auto matchesCurrentChoice = [&](const UpscaleUiChoice& choice) {
		if (static_cast<uint32_t>(choice.method) != *currentUpscaleMode)
			return false;
		if (choice.method == UpscaleMethod::kFSR)
			return settings.fsr4RuntimeEnable == choice.useRuntimeFsr4;
		return true;
	};

	int methodUiIndex = 0;
	for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
		if (matchesCurrentChoice(upscaleChoices[i])) {
			methodUiIndex = i;
			break;
		}
	}
	if (methodUiIndex == 0 && !matchesCurrentChoice(upscaleChoices[0])) {
		for (int i = 0; i < static_cast<int>(upscaleChoices.size()); ++i) {
			if (static_cast<uint32_t>(upscaleChoices[i].method) == *currentUpscaleMode) {
				methodUiIndex = i;
				break;
			}
		}
	}

	const char* currentMethodLabel = upscaleChoices[methodUiIndex].label;
	if (openCompositeBlocksUpscaling)
		ImGui::BeginDisabled();
	const bool methodChanged = ImGui::SliderInt("Method", &methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1), currentMethodLabel);
	if (openCompositeBlocksUpscaling)
		ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (openCompositeBlocksUpscaling) {
			ImGui::Text("Locked to None while Open Composite has %s=true.", openCompositeBlocker.settingName.c_str());
		} else {
			ImGui::TextUnformatted("Selects the upscaling backend.");
			if (runtimeFsr4AutoEligible)
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR 3.1.5, Runtime FSR 4, or None.");
			else
				ImGui::TextUnformatted("Range: choose between TAA, DLSS, FSR 3.1.5, or None.");
		}
	}
	methodUiIndex = std::clamp(methodUiIndex, 0, static_cast<int>(upscaleChoices.size() - 1));
	const auto& selectedUpscaleChoice = upscaleChoices[methodUiIndex];
	const bool shouldApplyMethodSelection = methodChanged || !matchesCurrentChoice(selectedUpscaleChoice);
	if (shouldApplyMethodSelection) {
		*currentUpscaleMode = static_cast<uint32_t>(selectedUpscaleChoice.method);
		if (selectedUpscaleChoice.method == UpscaleMethod::kFSR)
			settings.fsr4RuntimeEnable = selectedUpscaleChoice.useRuntimeFsr4;
	}
	if (openCompositeBlocksUpscaling) {
		ApplyOpenCompositeUpscalingBlocker();
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
		if (openCompositeBlocker.configPath.empty()) {
			ImGui::TextWrapped(
				"Community Shaders Upscaling is locked to None because Open Composite has %s=true.",
				openCompositeBlocker.settingName.c_str());
		} else {
			ImGui::TextWrapped(
				"Community Shaders Upscaling is locked to None because Open Composite has %s=true in %s.",
				openCompositeBlocker.settingName.c_str(),
				openCompositeBlocker.configPath.c_str());
		}
		ImGui::PopStyleColor();
	}

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();
	const bool runtimeFsr4Requested =
		upscaleMethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable;

	const bool runtimeFsrPathRequested =
		upscaleMethod == UpscaleMethod::kFSR &&
		fidelityFX.ShouldUseRuntimeUpscalerForFSR();
	const bool showRuntimeFsrFramePath = runtimeFsr4Requested || runtimeFsrPathRequested;

	if (showRuntimeFsrFramePath) {
		ImGui::TextDisabled("Current frame path: %s", fidelityFX.GetRuntimeUpscalerLastFramePathLabel());
		if (fidelityFX.IsRuntimeUpscalerFailureLatched()) {
			ImGui::TextDisabled("Runtime FSR path is latched off after a runtime failure; using host FSR 3.1.5 fallback.");
		} else if (fidelityFX.IsRuntimeFsr4FailureLatched()) {
			ImGui::TextDisabled("Runtime FSR 4 is latched off after a runtime failure; using runtime FSR 3.1.5 fallback.");
		} else if (fidelityFX.HasRuntimeUpscalerSupportCheckResult() &&
		           !fidelityFX.IsRuntimeUpscalerSupportConfirmed()) {
			ImGui::TextDisabled("Runtime FSR context creation failed; using host FSR 3.1.5 fallback.");
		}
		if (!runtimeUpscalerPresent && runtimeFsr4Requested)
			ImGui::TextDisabled("Runtime FSR 4 unavailable: missing FidelityFX upscaler runtime.");
	}

	// Display warning for DLSS resolution limits (non-VR only; VR handles this automatically)
	if (!globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS) {
		auto screenSize = globals::state->screenSize;
		if (screenSize.x > streamline.MAX_RESOLUTION || screenSize.y > streamline.MAX_RESOLUTION) {
			Util::Text::Warning("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenSize.x, screenSize.y, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			Util::Text::Warning("DLSS will not function. Lower your resolution or select a different upscaling method.");
		}
	}

	// Display upscaling settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		settings.qualityMode = ClampQualityModeUInt(settings.qualityMode);
		const char* baseLabel = GetQualityModeName(settings.qualityMode, upscaleMethod == UpscaleMethod::kDLSS);
		std::string labelWithScale = std::format(
			"{} ( {:.2f}x )",
			baseLabel,
			Upscaling::GetQualityModeResolutionScale(settings.qualityMode));

		int qualityMode = static_cast<int>(settings.qualityMode);
		if (ImGui::SliderInt(
				"Upscale Preset",
				&qualityMode,
				0,
				static_cast<int>(kQualityModeMaxIndex),
				labelWithScale.c_str())) {
			settings.qualityMode = static_cast<uint>(std::clamp(qualityMode, 0, static_cast<int>(kQualityModeMaxIndex)));
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls the shared DLSS/FSR/FSR4 internal render scale / quality level.");
			ImGui::TextUnformatted(
				"Range: low 0 (highest quality, lowest performance gain) to high 6 (highest performance gain, lowest quality).");
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat("Sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for FSR.");
				ImGui::TextUnformatted("Range: low 0.0 (softest) to high 1.0 (sharpest).");
			}
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			// Keep persisted preset values stable (0=J,1=K,2=L,3=M,4=F) while
			// presenting an alphabetical selection list in the UI.
			const uint32_t dlssProfileOrder[] = { 4u, 0u, 1u, 2u, 3u };  // F, J, K, L, M
			const char* dlssProfiles[] = { "F", "J", "K", "L", "M" };
			settings.dlssPreset = std::min(settings.dlssPreset, kDLSSPresetMaxIndex);

			int dlssProfileUiIndex = 0;
			for (int i = 0; i < IM_ARRAYSIZE(dlssProfileOrder); ++i) {
				if (dlssProfileOrder[i] == settings.dlssPreset) {
					dlssProfileUiIndex = i;
					break;
				}
			}

			ImGui::SliderInt("DLSS Profile", &dlssProfileUiIndex, 0, static_cast<int>(kDLSSPresetMaxIndex), dlssProfiles[dlssProfileUiIndex]);
			dlssProfileUiIndex = std::clamp(dlssProfileUiIndex, 0, static_cast<int>(kDLSSPresetMaxIndex));
			settings.dlssPreset = dlssProfileOrder[dlssProfileUiIndex];

			if (auto _tt = Util::HoverTooltipWrapper()) {
				switch (settings.dlssPreset) {
				case 0:
					ImGui::Text("DLAA/Quality/Balanced preset. Slightly less ghosting than K, but more flicker. Speed: ~K. Use only if K ghosts.");
					break;
				case 1:
					ImGui::Text("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
					break;
				case 2:
					ImGui::Text("Default for Ultra Performance on newer RTX cards. Sharper and more stable, but higher cost than J/K/F.");
					ImGui::Text("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
					break;
				case 3:
					ImGui::Text("Default for Performance on newer RTX cards. Similar image-quality improvements to L, closer in speed to J/K.");
					ImGui::Text("For RTX 3000-series cards, F is usually the better Performance/Ultra Performance choice.");
					break;
				case 4:
					ImGui::Text("Intended for Ultra Performance/DLAA. Default preset for Ultra Performance.");
					ImGui::Text("Best Performance/Ultra Performance choice for RTX 3000-series cards.");
					break;
				default:
					ImGui::Text("Default for DLAA/Quality/Balanced. Best all-round stability and image quality. Speed: fast. Recommended for most users.");
					break;
				}
			}

			ImGui::SliderFloat("Sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Adjusts post-upscale sharpness for DLSS.");
				ImGui::TextUnformatted("Range: low 0.0 (softest) to high 1.0 (sharpest).");
			}

			if (isNvidiaAdapter) {
				ImGui::TextWrapped("Note: Use K for DLAA/Quality/Balanced. For Performance and Ultra Performance, use L/M on newer RTX cards and F on RTX 3000-series cards.");
			}
		}

		if (globals::game::isVR) {
			SanitizeFoveatedSettings(settings);
			const bool foveatedDispatchSupportedForMethod = SupportsFoveatedVendorDispatch(upscaleMethod);
			if (foveatedDispatchSupportedForMethod) {
				const auto foveatedProfile = GetActiveUpscalingFoveatedProfile();
				const bool fovActive = foveatedProfile.available && FoveatedCommon::IsActiveCoverage(foveatedProfile.coverageArea);
				ImGui::TextDisabled("Foveation setup is configured in VR > Foveation.");
				ImGui::SameLine();
				ImGui::TextColored(
					fovActive ? ImVec4(0.40f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
					"FOV: %s",
					fovActive ? "active" : "inactive");
			} else {
				ImGui::TextDisabled(kFoveatedUpscalingMethodAvailabilityText);
			}

			if (streamline.reflexSupportedOnCurrentAdapter)
				ImGui::Separator();
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (!globals::game::isVR) {
		if (ImGui::TreeNodeEx("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Frame Generation interpolates real frames with generated ones for a smoother experience");
			ImGui::Text("Uses AMD FSR Frame Generation technology");
			if (HasFrameGenModule())
				ImGui::Text("AMD FSR Frame Generation is available.");
			ImGui::Text("Requires a D3D11 to D3D12 proxy which can create compatibility issues");
			ImGui::Text("Toggling this setting requires a restart to work correctly");

			bool onlyRequiresRestart = true;

			if (!isWindowed) {
				Util::Text::Warning("Warning: Requires windowed mode");

				onlyRequiresRestart = false;
			}

			if (lowRefreshRate && !settings.frameGenerationForceEnable) {
				Util::Text::Warning("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");

				onlyRequiresRestart = false;
			}

			if (settings.frameGenerationMode && !HasFrameGenModule()) {
				Util::Text::Warning("Warning: FidelityFX DLLs are not loaded");

				onlyRequiresRestart = false;
			}

			if (onlyRequiresRestart && settings.frameGenerationMode && !frameGenerationDx12PathActive)
				Util::Text::Warning("Warning: Requires restart");

			if (!settings.frameGenerationMode && frameGenerationDx12PathActive)
				Util::Text::Warning("Warning: Requires restart");

			std::string enabledLabel = "Enabled";
			const char* toggleModes[] = { "Disabled", "Enabled" };
			const char* toggleModesFG[] = { "Disabled", enabledLabel.c_str() };

			ImGui::SliderInt("Frame Generation", (int*)&settings.frameGenerationMode, 0, 1, toggleModesFG[settings.frameGenerationMode]);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Enables generated intermediate frames for higher apparent framerate.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			if (!frameGenerationDx12PathActive)
				ImGui::BeginDisabled();

			ImGui::SliderInt("Frame Limit (Variable Refresh Rate)", (int*)&settings.frameLimitMode, 0, 1, std::format("{}", toggleModes[settings.frameLimitMode]).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Applies VRR-aware frame limiting for smoother pacing with Frame Generation.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			if (!frameGenerationDx12PathActive)
				ImGui::EndDisabled();

			ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", refreshRate);
			ImGui::SliderInt("Force Enable Frame Generation", (int*)&settings.frameGenerationForceEnable, 0, 1, std::format("{}", toggleModes[settings.frameGenerationForceEnable]).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Forces Frame Generation on unsupported/low-refresh setups.");
				ImGui::TextUnformatted("Range: 0 Disabled, 1 Enabled.");
			}

			ImGui::Checkbox("Frame Generation in Menus", &settings.frameGenerationAllowInMenus);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Keeps frame generation active while game menus are open.");
				ImGui::TextUnformatted("May feel smoother, but increases menu input latency.");
			}

			ImGui::TreePop();
		}
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx("NVIDIA Reflex", ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool reflexAvailable = streamline.initialized && streamline.featureReflex;
		const bool markerOptimizationAvailable = reflexAvailable && streamline.featurePCL;
		const bool reflexBlockedByFrameGeneration = IsFrameGenerationDx12PathActive();
		const char* toggleModes[] = { "Disabled", "Enabled" };

		if (!reflexAvailable) {
			ImGui::TextDisabled("Reflex is not available. Ensure sl.reflex.dll is present and restart.");
		}

		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("Reflex is disabled while Frame Generation is active on the DX12 swap chain.");
		}

		if (!reflexAvailable || reflexBlockedByFrameGeneration)
			ImGui::BeginDisabled();

		int lowLatencyMode = settings.reflexLowLatencyMode ? 1 : 0;
		ImGui::SliderInt("Low Latency Mode", &lowLatencyMode, 0, 1, toggleModes[lowLatencyMode]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Cuts input delay by syncing CPU work closer to the GPU.");
			ImGui::TextUnformatted("May reduce max FPS a little, but usually feels much more responsive.");
		}
		settings.reflexLowLatencyMode = lowLatencyMode > 0;

		if (!settings.reflexLowLatencyMode)
			ImGui::BeginDisabled();

		int lowLatencyBoost = settings.reflexLowLatencyBoost ? 1 : 0;
		ImGui::SliderInt("Low Latency Boost", &lowLatencyBoost, 0, 1, toggleModes[lowLatencyBoost]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Keeps GPU clocks higher to avoid latency spikes at low GPU load.");
			ImGui::TextUnformatted("Useful if frametime jumps and responsiveness feels inconsistent.");
			ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "Increases power draw and heat, so leave Off unless needed.");
		}
		settings.reflexLowLatencyBoost = lowLatencyBoost > 0;

		if (!settings.reflexLowLatencyMode)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		int markersToOptimize = settings.reflexUseMarkersToOptimize ? 1 : 0;
		ImGui::SliderInt("Use Markers To Optimize", &markersToOptimize, 0, 1, toggleModes[markersToOptimize]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses frame markers for tighter Reflex timing.");
			ImGui::TextUnformatted("Try On first; turn Off if it causes stutter on your setup.");
		}
		settings.reflexUseMarkersToOptimize = markersToOptimize > 0;

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("Marker optimization unavailable (PCL not loaded).");
		}

		int useFPSLimit = settings.reflexUseFPSLimit ? 1 : 0;
		ImGui::SliderInt("Use FPS Limit", &useFPSLimit, 0, 1, toggleModes[useFPSLimit]);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Uses Reflex's internal FPS cap for steadier frametimes.");
			ImGui::TextUnformatted("Can lower latency versus uncapped rendering.");
		}
		settings.reflexUseFPSLimit = useFPSLimit > 0;

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat("FPS Limit", &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Set your frame cap target.");
			ImGui::TextUnformatted("Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz).");
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexAvailable || reflexBlockedByFrameGeneration)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Backend Diagnostics")) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevelIdx = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo("Streamline Logging", &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted("Changing this requires a restart to take effect.");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G.");
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::Separator();
			const bool showRuntimeFsrDiagnostics =
				settings.fsr4RuntimeEnable ||
				runtimeFsrPathRequested ||
				fidelityFX.HasRuntimeUpscalerSupportCheckResult();
			const bool runtimeFsr4EffectiveRequested =
				upscaleMethod == UpscaleMethod::kFSR &&
				fidelityFX.ShouldRequestRuntimeFsr4();
			const char* fsrModeLabel = settings.fsr4RuntimeEnable ?
				(runtimeFsr4EffectiveRequested ? "Runtime FSR 4 requested" :
					(runtimeFsrPathRequested ? "Runtime FSR 3.1.5 fallback requested" : "Host FSR 3.1.5 fallback requested")) :
				(runtimeFsrPathRequested ? "Runtime FSR 3.1.5 requested" : "Host FSR 3.1.5 requested");
			ImGui::Text("AMD FSR Mode: %s", fsrModeLabel);
			ImGui::Text("Current Frame Path: %s", fidelityFX.GetRuntimeUpscalerLastFramePathLabel());
			if (showRuntimeFsrDiagnostics) {
				const bool supportKnown = fidelityFX.HasRuntimeUpscalerSupportCheckResult();
				const bool supportConfirmed = fidelityFX.IsRuntimeUpscalerSupportConfirmed();
				const bool runtimeFailureLatched = fidelityFX.IsRuntimeUpscalerFailureLatched();
				const bool runtimeFsr4FailureLatched = fidelityFX.IsRuntimeFsr4FailureLatched();
				const std::string requestedVersion = fidelityFX.GetRuntimeUpscalerRequestedVersionString();
				const std::string providerName = fidelityFX.GetRuntimeUpscalerProviderName();
				const bool providerMismatch =
					supportKnown &&
					supportConfirmed &&
					!providerName.empty() &&
					!fidelityFX.IsRuntimeUpscalerProviderMatchingRequestedVersion();
				const auto getRuntimePathSupportLabel = [&]() -> const char* {
					if (!runtimeUpscalerPresent)
						return "Unavailable (missing runtime)";
					if (!runtimeFsrPathRequested && settings.fsr4RuntimeEnable)
						return "Unavailable for adapter";
					if (runtimeFailureLatched)
						return "Unavailable (latched fallback)";
					if (runtimeFsr4FailureLatched)
						return "Available (FSR 4 fallback latched)";
					if (!supportKnown)
						return "Pending";
					if (supportConfirmed && providerMismatch)
						return "Available (provider fallback)";
					return supportConfirmed ? "Available" : "Unavailable";
				};
				std::string providerDisplay = providerName.empty() ? "(not reported by SDK)" : providerName;
				if (providerMismatch)
					providerDisplay += " (requested version unavailable)";
				ImGui::Text("Runtime Path Support: %s", getRuntimePathSupportLabel());
				ImGui::Text("Failure Latch: %s", runtimeFailureLatched ? "Active" : "Clear");
				ImGui::Text("Runtime Requested FSR Version: %s", requestedVersion.c_str());
				ImGui::Text("Runtime Provider: %s", providerDisplay.c_str());
			}
		}

		// VR Debug visualization -- per-eye buffers and native inputs
		if (globals::game::isVR) {
			ImGui::Separator();
			static float debugRescale = 0.15f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.05f, 1.f);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Scales debug buffer previews in the diagnostics panel.");
				ImGui::TextUnformatted("Range: low 0.05 (small previews) to high 1.00 (full-size previews).");
			}

			if (ImGui::TreeNode("Upscaling Intermediates")) {
				if (vrIntermediateMotionVectors[0]) {
					bool isDLSS = GetUpscaleMethod() == UpscaleMethod::kDLSS;
					if (vrIntermediateColorIn[0] && vrIntermediateColorOut[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[0], "Left Eye In", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorIn[1], "Right Eye In", debugRescale)
						if (!isDLSS)
							BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[0], "Left Eye Out", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateColorOut[1], "Right Eye Out", debugRescale)
					}
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[0], "Left Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateMotionVectors[1], "Right Eye MVec", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[0], "Left Eye Reactive", debugRescale)
					BUFFER_VIEWER_NODE_TITLE(vrIntermediateReactiveMask[1], "Right Eye Reactive", debugRescale)
					if (vrIntermediateTransparencyMask[0]) {
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[0], "Left Eye Transparency", debugRescale)
						BUFFER_VIEWER_NODE_TITLE(vrIntermediateTransparencyMask[1], "Right Eye Transparency", debugRescale)
					}
				} else {
					ImGui::TextDisabled("VR intermediates not yet created (enter game world)");
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Native Inputs")) {
				auto renderer = globals::game::renderer;
				auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
				auto& mvec = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
				auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

				auto DisplayRT = [&](const char* label, ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv) {
					if (srv && tex) {
						D3D11_TEXTURE2D_DESC desc;
						tex->GetDesc(&desc);
						char buf[128];
						snprintf(buf, sizeof(buf), "%s (%ux%u)", label, desc.Width, desc.Height);
						if (ImGui::TreeNode(buf)) {
							ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
							ImGui::TreePop();
						}
					}
				};

				DisplayRT("kMAIN (Color Input)", (ID3D11Texture2D*)main.texture, (ID3D11ShaderResourceView*)main.SRV);
				DisplayRT("Motion Vectors", (ID3D11Texture2D*)mvec.texture, (ID3D11ShaderResourceView*)mvec.SRV);
				DisplayRT("Depth", depth.texture, depth.depthSRV);

				if (reactiveMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(reactiveMaskTexture, "Reactive Mask", debugRescale)
				if (transparencyCompositionMaskTexture)
					BUFFER_VIEWER_NODE_TITLE(transparencyCompositionMaskTexture, "Transparency Mask", debugRescale)

				ImGui::TreePop();
			}
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
		ImGui::TreePop();
	}
}

void Upscaling::DrawFoveatedSettings()
{
	if (!globals::game::isVR) {
		ImGui::TextDisabled("VR FOV mask setup is available only in VR.");
		return;
	}
	if (!loaded) {
		ImGui::TextDisabled("VR FOV mask setup requires Upscaling.");
		return;
	}

	SanitizeFoveatedSettings(settings);
	const UpscaleMethod upscaleMethod = GetUpscaleMethod();
	const bool foveatedDispatchSupportedForMethod = SupportsFoveatedVendorDispatch(upscaleMethod);

	if (foveatedDispatchSupportedForMethod) {
		{
			Util::BlueFrameStyleWrapper foveatedStyle(true);
			ImGui::Checkbox("Foveated Upscaling (FOV)", &settings.foveatedVendorDispatch);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Master switch for VR FOV-mask upscaling.");
			ImGui::TextUnformatted("On: enables foveated upscaling controls and the shared FOV mask used by VR foveated effects.");
		}
	} else {
		ImGui::TextDisabled(kFoveatedUpscalingMethodAvailabilityText);
	}

	const bool foveatedDispatchRequestedForMethod = IsFoveatedVendorDispatchRequested(settings, upscaleMethod);
	if (!foveatedDispatchRequestedForMethod)
		return;

	if (IsDefaultFoveatedMaskGeometry(settings)) {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.05f, 1.0f), "Default FOV mask active. Tune it for your HMD for best image and performance.");
	}

	{
		Util::BlueFrameStyleWrapper maskStyle(true);
		ImGui::Checkbox("FOV Mask Visualization", &settings.foveatedPeripheryMaskVisualization);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Use this while tuning FOV masks.");
		ImGui::TextUnformatted("Green = upscaling center mask.");
		if (settings.periphery_taa_enable)
			ImGui::TextUnformatted("Gold = TAA ring, blue = outer lightweight ring.");
		else
			ImGui::TextUnformatted("Dark = outside the upscaling FOV mask.");
	}

	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	const bool showFovSetupInstructions = ImGui::CollapsingHeader("Upscaling FOV Setup Instructions");
	if (showFovSetupInstructions) {
		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const float availableHeight = ImGui::GetContentRegionAvail().y;
		const float instructionHeight = std::clamp(availableHeight - (lineHeight * 2.0f), lineHeight * 5.0f, lineHeight * 14.0f);
		ImGui::BeginChild("##UpscalingFOVSetupInstructions", ImVec2(0.0f, instructionHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::PushTextWrapPos(0.0f);
		auto drawInstructionHeadline = [](const char* a_label) {
			MenuFonts::FontRoleGuard headingFont(Menu::FontRole::Subheading);
			ImGui::SeparatorText(a_label);
		};
		ImGui::TextUnformatted(kFoveatedUpscalingSetupIntro);
		ImGui::Spacing();
		drawInstructionHeadline("Upscaling FOV setup");
		ImGui::TextUnformatted(kFoveatedUpscalingSetupInstructions);
		ImGui::Spacing();
		drawInstructionHeadline("Upscaling FOV + Peripheral TAA setup");
		ImGui::TextUnformatted(kFoveatedUpscalingPeripheralTaaSetupInstructions);
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
	}

	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::TextUnformatted("Upscaling FOV Controls");

	{
		Util::BlueFrameStyleWrapper areaStyle;
		auto areaGuard = Util::DisableGuard(settings.periphery_taa_enable);
		ImGui::SliderFloat("Upscaling FOV Area", &settings.foveatedCenterArea, FoveatedCommon::kCenterAreaMin, FoveatedCommon::kCenterAreaMax, "%.2f");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (settings.periphery_taa_enable) {
			ImGui::TextUnformatted("Inactive while Peripheral TAA is enabled.");
		} else {
			ImGui::TextUnformatted("Active upscaling center mask size.");
			ImGui::TextUnformatted("Lower values = smaller center mask and more performance.");
			ImGui::TextUnformatted("Range: low 0.25 (smallest center) to high 1.00 (largest center).");
		}
	}
	settings.foveatedCenterArea = ClampFoveatedCenterArea(settings.foveatedCenterArea);

	{
		Util::BlueFrameStyleWrapper baseExpandStyle;
		ImGui::SliderFloat("Expand FOV Area R/L", &settings.foveatedCenterHorizontalScale, FoveatedCommon::kCenterHorizontalScaleMin, FoveatedCommon::kCenterHorizontalScaleMax, "%.2f");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Widens the upscaling center mask horizontally.");
		if (settings.periphery_taa_enable)
			ImGui::TextUnformatted("Peripheral TAA uses this shared horizontal expansion.");
		ImGui::TextUnformatted("Range: low 1.00 (no extra width) to high 2.00 (maximum extra width).");
	}

	auto drawEyeOffsetTooltip = [&](const char* eye, const char* axis, const char* direction) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (settings.periphery_taa_enable)
				ImGui::Text("%s-eye %s offset shared by upscaling and Peripheral TAA.", eye, axis);
			else
				ImGui::Text("%s-eye %s offset for the upscaling center mask.", eye, axis);
			ImGui::TextUnformatted(direction);
		}
	};
	{
		Util::BlueFrameStyleWrapper baseOffsetStyle;
		ImGui::SliderFloat("FOV Left Eye Offset X", &settings.foveatedLeftEyeMaskOffsetX, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Left", "horizontal", "+X moves right, -X moves left.");
		ImGui::SliderFloat("FOV Left Eye Offset Y", &settings.foveatedLeftEyeMaskOffsetY, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Left", "vertical", "+Y moves down, -Y moves up.");
		ImGui::SliderFloat("FOV Right Eye Offset X", &settings.foveatedRightEyeMaskOffsetX, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Right", "horizontal", "+X moves right, -X moves left.");
		ImGui::SliderFloat("FOV Right Eye Offset Y", &settings.foveatedRightEyeMaskOffsetY, kFoveatedMaskOffsetAdjustMin, kFoveatedMaskOffsetAdjustMax, "%.3f");
		drawEyeOffsetTooltip("Right", "vertical", "+Y moves down, -Y moves up.");
	}

	settings.foveatedCenterHorizontalScale = ClampFoveatedCenterHorizontalScale(settings.foveatedCenterHorizontalScale);
	settings.foveatedLeftEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetX);
	settings.foveatedLeftEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedLeftEyeMaskOffsetY);
	settings.foveatedRightEyeMaskOffsetX = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetX);
	settings.foveatedRightEyeMaskOffsetY = ClampFoveatedMaskOffsetAdjustment(settings.foveatedRightEyeMaskOffsetY);

	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::Separator();
	ImGui::TextUnformatted("Upscaling FOV + Peripheral TAA Settings");
	{
		Util::YellowFrameStyleWrapper taaStyle(true);
		ImGui::Checkbox("FOV + Peripheral TAA", &settings.periphery_taa_enable);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Enables periphery-only TAA outside the upscaling center region.");
		ImGui::TextUnformatted("When ON, the reduced FOV Area below becomes the active center mask.");
		ImGui::TextUnformatted("Expand and eye offsets are shared with the upscaling controls above.");
	}
	ImGui::BeginDisabled(!settings.periphery_taa_enable);
	if (!settings.periphery_taa_enable)
		ImGui::TextDisabled("Enable Peripheral TAA to edit the reduced FOV area, transition, and range.");
	{
		Util::YellowFrameStyleWrapper taaAreaStyle;
		ImGui::SliderFloat("Upscaling FOV Area##PeripheralTAA", &settings.periphery_taa_center_area, FoveatedCommon::kCenterAreaMin, FoveatedCommon::kCenterAreaMax, "%.2f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Reduced upscaling center mask size.");
			ImGui::TextUnformatted("Lower values = smaller upscaling center and more Peripheral TAA coverage.");
			ImGui::TextUnformatted("Range: low 0.25 (smallest center) to high 1.00 (largest center).");
		}
	}
	settings.periphery_taa_center_area = ClampFoveatedCenterArea(settings.periphery_taa_center_area);
	{
		Util::YellowFrameStyleWrapper transitionStyle;
		ImGui::SliderFloat(
			"Center Blend/TAA Transition",
			&settings.periphery_taa_center_blend_feather,
			kPeripheryTAACenterBlendFeatherMin,
			kPeripheryTAACenterBlendFeatherMax,
			"%.3f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls softness of the center-to-TAA transition edge.");
			ImGui::TextUnformatted("Lower = harder edge, higher = softer edge.");
			ImGui::Text("Range: low %.2f (harder transition) to high %.2f (softer transition).", kPeripheryTAACenterBlendFeatherMin, kPeripheryTAACenterBlendFeatherMax);
		}
	}
	settings.periphery_taa_center_blend_feather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float taaOuterRangeMin = GetPeripheryTAAOuterScaleFloor(
		settings.periphery_taa_center_area,
		settings.foveatedCenterHorizontalScale,
		settings.periphery_taa_center_blend_feather);
	{
		Util::YellowFrameStyleWrapper taaRangeStyle;
		ImGui::SliderFloat(
			"TAA Peripheral Range",
			&settings.periphery_taa_outer_scale,
			taaOuterRangeMin,
			kPeripheryTAAOuterScaleMax,
			"%.2f");
	}
	if (settings.periphery_taa_enable) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls how far Peripheral TAA extends outside the upscaling center mask.");
			ImGui::Text("Range: low %.2f (minimum allowed by current FOV Area) to high %.2f (full range).", taaOuterRangeMin, kPeripheryTAAOuterScaleMax);
			ImGui::TextUnformatted("Lower values are faster.");
			ImGui::TextUnformatted("Increase until the gold ring reaches the edge of your visible field of view.");
		}
	}
	ImGui::EndDisabled();

	settings.periphery_taa_outer_scale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		settings.periphery_taa_center_area,
		settings.foveatedCenterHorizontalScale,
		settings.periphery_taa_center_blend_feather);
}

const Upscaling::OpenCompositeUpscalingBlocker& Upscaling::GetOpenCompositeUpscalingBlocker(bool a_forceRefresh) const
{
	const ULONGLONG now = GetTickCount64();

	if (!a_forceRefresh && openCompositeUpscalingBlockerCacheValid) {
		return openCompositeUpscalingBlocker;
	}

	const auto detectedBlocker = FindOpenCompositeUpscalingBlocker();
	openCompositeUpscalingBlocker.active = detectedBlocker.active;
	openCompositeUpscalingBlocker.settingName = detectedBlocker.settingName;
	openCompositeUpscalingBlocker.configPath = detectedBlocker.configPath;
	openCompositeUpscalingBlockerCacheValid = true;
	openCompositeUpscalingBlockerLastRefresh = now;

	return openCompositeUpscalingBlocker;
}

void Upscaling::ApplyOpenCompositeUpscalingBlocker(bool a_forceRefresh)
{
	const auto& blocker = GetOpenCompositeUpscalingBlocker(a_forceRefresh);
	if (!blocker.active)
		return;

	if (settings.upscaleMethod != static_cast<uint>(UpscaleMethod::kNONE) ||
	    settings.upscaleMethodNoDLSS != static_cast<uint>(UpscaleMethod::kNONE)) {
		if (blocker.configPath.empty()) {
			logger::warn(
				"[Upscaling] Forcing Community Shaders Upscaling to None because Open Composite has {}=true.",
				blocker.settingName);
		} else {
			logger::warn(
				"[Upscaling] Forcing Community Shaders Upscaling to None because Open Composite has {}=true in {}.",
				blocker.settingName,
				blocker.configPath);
		}
	}

	settings.upscaleMethod = static_cast<uint>(UpscaleMethod::kNONE);
	settings.upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kNONE);
}

void Upscaling::SaveSettings(json& o_json)
{
	ApplyOpenCompositeUpscalingBlocker(true);
	SanitizeUpscalingSettings(settings);
	o_json = settings;
	o_json["qualityModeSchemaVersion"] = 2;
	if (!IsVRRuntimeActive()) {
		StripVRSpecificUpscalingSettings(o_json);
	}
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		if (auto setting = iniSettingCollection->GetSetting("bUseTAA:Display"))
			iniSettingCollection->WriteSetting(setting);
	}
}

void Upscaling::LoadSettings(json& o_json)
{
	const bool hasQualityModeSchemaVersion = o_json.contains("qualityModeSchemaVersion");
	settings = o_json;
	if (!hasQualityModeSchemaVersion) {
		settings.qualityMode = MigrateLegacyQualityModeUInt(settings.qualityMode);
	}
	if (!IsVRRuntimeActive()) {
		ResetVRSpecificUpscalingSettings(settings);
	}
	// Force mask visualization OFF on load for all existing profiles.
	settings.foveatedPeripheryMaskVisualization = false;

	if (settings.upscaleMethod > static_cast<uint>(UpscaleMethod::kDLSS)) {
		logger::warn("[Upscaling] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, static_cast<uint>(UpscaleMethod::kDLSS));
	}
	if (settings.upscaleMethodNoDLSS > static_cast<uint>(UpscaleMethod::kFSR)) {
		logger::warn("[Upscaling] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, static_cast<uint>(UpscaleMethod::kFSR));
	}
	SanitizeUpscalingSettings(settings);
	ApplyOpenCompositeUpscalingBlocker(true);
	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	if (!std::isfinite(settings.reflexFPSLimit)) {
		settings.reflexFPSLimit = 60.0f;
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} is not finite, resetting to {}",
			originalReflexFPSLimit,
			settings.reflexFPSLimit);
	}
	const float clampedReflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	if (clampedReflexFPSLimit != settings.reflexFPSLimit) {
		logger::warn(
			"[Upscaling] Loaded reflexFPSLimit {} out of range, clamping to {}",
			settings.reflexFPSLimit,
			clampedReflexFPSLimit);
	}
	settings.reflexFPSLimit = clampedReflexFPSLimit;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		if (auto setting = iniSettingCollection->GetSetting("bUseTAA:Display"))
			iniSettingCollection->ReadSetting(setting);
	}
}

void Upscaling::RestoreDefaultSettings()
{
	settings = {};
	settings.foveatedVendorDispatch = false;
	settings.foveatedPeripheryMaskVisualization = false;
	settings.reflexLowLatencyMode = true;
	settings.reflexUseMarkersToOptimize = true;
	settings.reflexLowLatencyBoost = false;
	settings.reflexUseFPSLimit = false;
	SanitizeUpscalingSettings(settings);
	ApplyOpenCompositeUpscalingBlocker(true);
}

struct BSOpenVR_GetRenderTargetSize
{
	static void thunk(RE::BSOpenVR* a_this, std::uint32_t* a_width, std::uint32_t* a_height)
	{
		func(a_this, a_width, a_height);

		if (!a_width || !a_height || !*a_width || !*a_height)
			return;

		g_submitStageOutputEyeWidth = *a_width;
		g_submitStageOutputEyeHeight = *a_height;
		g_submitStageTargetSizeKnown = true;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::DataLoaded()
{
	DisableAutoDynamicResolutionSetting();
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping data-loaded upscaling adjustments because Open Composite has {}=true.", blocker.settingName);
		return;
	}

	// Fix screenshots fix from Engine Fixes
	RE::GetINISetting("bUseTAA:Display")->data.b = false;

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;

	// VR + DLSS workaround: loading transitions need a temporal reset, but full
	// DLSS resource rebuilds on every door load can flicker and stress the driver.
	if (globals::game::isVR)
		MenuOpenCloseEventHandler::Register();
}

RE::BSEventNotifyControl Upscaling::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		g_vrLoadingMenuOpenFromEvent.store(a_event->opening, std::memory_order_relaxed);
		if (!a_event->opening) {
			globals::features::upscaling.pendingDLSSHistoryReset.store(true, std::memory_order_relaxed);
			globals::features::upscaling.pendingDLSSReset.store(true, std::memory_order_relaxed);
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

bool Upscaling::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static std::atomic<bool> registered{ false };

	if (registered.load(std::memory_order_acquire))
		return true;

	auto ui = globals::game::ui;
	if (!ui) {
		logger::error("[Upscaling] UI event source not found; DLSS history reset-on-load disabled");
		return false;
	}

	auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[Upscaling] MenuOpenCloseEvent source not found; DLSS history reset-on-load disabled");
		return false;
	}

	g_vrLoadingMenuOpenFromEvent.store(ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME), std::memory_order_relaxed);
	eventSource->AddEventSink(&singleton);
	registered.store(true, std::memory_order_release);
	logger::info("[Upscaling] Registered MenuOpenCloseEventHandler for DLSS history reset-on-load");
	return true;
}

void Upscaling::Load()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping D3D11 device hook because Open Composite has {}=true.", blocker.settingName);
		return;
	}

	if (REL::Module::IsVR()) {
		stl::write_vfunc<0x12, BSOpenVR_GetRenderTargetSize>(RE::VTABLE_BSOpenVR[0]);
	}

	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		// Force FXAA off safely
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
void Upscaling::PostPostLoad()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping upscaling render hooks because Open Composite has {}=true.", blocker.settingName);
		return;
	}

	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

	// Keep vanilla/manual dynamic resolution active. Vendor upscaling replaces
	// the vanilla upsample pass at the image-space hook points below.

	// Performs upscaling in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

	stl::write_vfunc<0x1, UpsampleDynamicResolution_Render>(
		RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[3]);
	stl::write_vfunc<0x1, CopyDynamicFetchDisabled_Render>(
		RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[3]);
	stl::write_vfunc<0xC, UpsampleDynamicResolution_Dispatch>(
		RE::VTABLE_BSImagespaceShaderISUpsampleDynamicResolution[0]);
	stl::write_vfunc<0xC, CopyDynamicFetchDisabled_Dispatch>(
		RE::VTABLE_BSImagespaceShaderCopyDynamicFetchDisabled[0]);
	if (globals::game::isVR) {
		stl::write_vfunc<0x1, FullScreenVR_Render>(
			RE::VTABLE_BSImagespaceShaderISFullScreenVR[3]);
		stl::write_vfunc<0xC, FullScreenVR_Dispatch>(
			RE::VTABLE_BSImagespaceShaderISFullScreenVR[0]);
	}

	// Patches RSSetScissorRect calls to use dynamic resolution
	// This is a PC-specific function hence it was missing
	if (!globals::game::isVR)
		stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	logger::info("[Upscaling] Installed hooks");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
{
	if (GetOpenCompositeUpscalingBlocker().active)
		return UpscaleMethod::kNONE;

	if (streamline.featureDLSS)
		return (UpscaleMethod)settings.upscaleMethod;
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Creating texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		if (!reactiveMaskTexture) {
			reactiveMaskTexture = new Texture2D(texDesc);
			reactiveMaskTexture->CreateSRV(srvDesc);
			reactiveMaskTexture->CreateUAV(uavDesc);
		}

		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = new Texture2D(texDesc);
			transparencyCompositionMaskTexture->CreateSRV(srvDesc);
			transparencyCompositionMaskTexture->CreateUAV(uavDesc);
		}
	}

	// Motion vector copy texture is used by DLSS and FSR encode pass.
	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = new Texture2D(motionTexDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}

	}

	// RCAS sharpener texture - matches kMAIN format for HDR sharpening
	if (a_upscalemethod == UpscaleMethod::kDLSS) {
		if (!sharpenerTexture) {
			main.texture->GetDesc(&texDesc);
			main.SRV->GetDesc(&srvDesc);

			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			sharpenerTexture = new Texture2D(texDesc);
			sharpenerTexture->CreateSRV(srvDesc);
			sharpenerTexture->CreateUAV(uavDesc);
		}
	}
}

void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[Upscaling] Destroying texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	// Clean up D3D11 textures that are no longer needed
	// Only destroy textures when switching away from methods that use them
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		DestroyTexture(reactiveMaskTexture);
		DestroyTexture(transparencyCompositionMaskTexture);
	}

	// Motion vector copy texture is used by DLSS/FSR - destroy when switching away from both.
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		DestroyTexture(motionVectorCopyTexture);
	}

	// RCAS sharpener texture is only needed for DLSS.
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		DestroyTexture(sharpenerTexture);
	}
}

void Upscaling::DestroyCommonUpscalingTextures()
{
	DestroyTexture(reactiveMaskTexture);
	DestroyTexture(transparencyCompositionMaskTexture);
	DestroyTexture(motionVectorCopyTexture);
	DestroyTexture(sharpenerTexture);
}

namespace
{
	bool HasVRIntermediateTextureCache(const Upscaling::VRIntermediateTextureCache& a_cache)
	{
		return a_cache.colorIn[0] && a_cache.colorIn[1] &&
		       a_cache.colorOut[0] && a_cache.colorOut[1] &&
		       a_cache.depth[0] && a_cache.depth[1] &&
		       a_cache.linearDepth[0] && a_cache.linearDepth[1] &&
		       a_cache.motionVectors[0] && a_cache.motionVectors[1] &&
		       a_cache.reactiveMask[0] && a_cache.reactiveMask[1] &&
		       a_cache.transparencyMask[0] && a_cache.transparencyMask[1] &&
		       a_cache.colorOut[0]->uav && a_cache.colorOut[1]->uav &&
		       a_cache.linearDepth[0]->uav && a_cache.linearDepth[1]->uav &&
		       a_cache.motionVectors[0]->uav && a_cache.motionVectors[1]->uav &&
		       a_cache.reactiveMask[0]->uav && a_cache.reactiveMask[1]->uav &&
		       a_cache.transparencyMask[0]->uav && a_cache.transparencyMask[1]->uav;
	}

	bool MatchesVRIntermediateTextureCache(const Upscaling::VRIntermediateTextureCache& a_cache,
		uint32_t a_inWidth, uint32_t a_inHeight, uint32_t a_outWidth, uint32_t a_outHeight)
	{
		return HasVRIntermediateTextureCache(a_cache) &&
		       a_cache.inWidth == a_inWidth &&
		       a_cache.inHeight == a_inHeight &&
		       a_cache.outWidth == a_outWidth &&
		       a_cache.outHeight == a_outHeight;
	}

	void ClearVRIntermediateTextureCache(Upscaling::VRIntermediateTextureCache& a_cache)
	{
		for (uint32_t i = 0; i < 2; ++i) {
			a_cache.colorIn[i].reset();
			a_cache.colorOut[i].reset();
			a_cache.depth[i].reset();
			a_cache.linearDepth[i].reset();
			a_cache.motionVectors[i].reset();
			a_cache.reactiveMask[i].reset();
			a_cache.transparencyMask[i].reset();
		}
		a_cache.inWidth = 0;
		a_cache.inHeight = 0;
		a_cache.outWidth = 0;
		a_cache.outHeight = 0;
	}
}

void Upscaling::DestroyVRIntermediateTextures()
{
	RetiredVRIntermediateTextures retired{};
	retired.retireFrame = globals::state ? globals::state->frameCount : 0u;
	bool hasRetiredTextures = false;
	const auto retireArray = [&hasRetiredTextures](auto& a_source, auto& a_destination) {
		for (uint32_t i = 0; i < 2; ++i) {
			if (a_source[i])
				hasRetiredTextures = true;
			a_destination[i] = std::move(a_source[i]);
		}
	};

	retireArray(vrIntermediateColorIn, retired.colorIn);
	retireArray(vrIntermediateColorOut, retired.colorOut);
	retireArray(vrIntermediateDepth, retired.depth);
	retireArray(vrIntermediateLinearDepth, retired.linearDepth);
	retireArray(vrIntermediateMotionVectors, retired.motionVectors);
	retireArray(vrIntermediateReactiveMask, retired.reactiveMask);
	retireArray(vrIntermediateTransparencyMask, retired.transparencyMask);

	if (hasRetiredTextures) {
		retiredVRIntermediateTextures.push_back(std::move(retired));
		const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0u;
		std::erase_if(retiredVRIntermediateTextures, [currentFrame](const RetiredVRIntermediateTextures& entry) {
			return currentFrame >= entry.retireFrame && currentFrame - entry.retireFrame > 4u;
		});
		while (retiredVRIntermediateTextures.size() > 4) {
			retiredVRIntermediateTextures.erase(retiredVRIntermediateTextures.begin());
		}
	}

	for (uint32_t i = 0; i < 2; ++i) {
		vrIntermediateColorIn[i].reset();
		vrIntermediateColorOut[i].reset();
		vrIntermediateDepth[i].reset();
		vrIntermediateLinearDepth[i].reset();
		vrIntermediateMotionVectors[i].reset();
		vrIntermediateReactiveMask[i].reset();
		vrIntermediateTransparencyMask[i].reset();
	}
	ClearVRIntermediateTextureCache(cachedVRIntermediateTextures);
	peripheryTAAHistoryReadIndex = 0;
	peripheryTAAHistoryValid = false;

	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageHandoffTexture = nullptr;
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
}

void Upscaling::UnbindUpscalingResources()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
	context->VSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->PSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->GSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->HSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->DSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11Buffer* nullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
	context->VSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->PSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->GSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->HSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->DSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
	context->CSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);

	context->CSSetShader(nullptr, nullptr, 0);
}

void Upscaling::RequestPostLoadRuntimeReset()
{
	if (!globals::game::isVR)
		return;

	postLoadRuntimeResetPending.store(true, std::memory_order_release);
	logger::info("[Upscaling] Armed VR post-load runtime reset");
}

void Upscaling::ResetVRSubmitStageState(bool a_destroyDLSSResources)
{
	if (!globals::game::isVR)
		return;

	UnbindUpscalingResources();
	DestroyVRIntermediateTextures();
	DestroyFoveatedResources();

	if (a_destroyDLSSResources && streamline.initialized && streamline.featureDLSS && streamline.slDLSSSetOptions && streamline.slFreeResources) {
		streamline.DestroyDLSSResources();
	} else {
		streamline.InvalidateDLSSOptionsCache();
	}

	submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	submitStageHandoffFrame = std::numeric_limits<uint32_t>::max();
	submitStageHandoffTexture = nullptr;
	submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	submitStageMirrorEyeReady = {};
	submitStageMirrorSourceTexture = nullptr;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
	historyResetTrackingInitialized = false;
	historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	historyResetThisFrame = false;
	RequestHistoryReset();
}

void Upscaling::RequestVRSubmitStageHistoryReset()
{
	if (!globals::game::isVR)
		return;

	RequestHistoryReset();
}

bool Upscaling::ApplyPendingPostLoadRuntimeReset(UpscaleMethod a_upscaleMethod)
{
	if (!postLoadRuntimeResetPending.exchange(false, std::memory_order_acq_rel))
		return true;

	if (!globals::game::isVR)
		return true;

	logger::info("[Upscaling] Applying VR post-load runtime reset for method {}",
		magic_enum::enum_name(a_upscaleMethod));

	try {
		UnbindUpscalingResources();

		if (streamline.initialized && streamline.featureDLSS && streamline.slDLSSSetOptions && streamline.slFreeResources) {
			streamline.DestroyDLSSResources();
		} else {
			streamline.InvalidateDLSSOptionsCache();
		}

		fidelityFX.DestroyFSRResources();

		DestroyVRIntermediateTextures();
		DestroyCommonUpscalingTextures();
		DestroyFoveatedResources();

		historyResetTrackingInitialized = false;
		historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
		historyResetThisFrame = false;
		RequestHistoryReset();

		if (a_upscaleMethod == UpscaleMethod::kDLSS || a_upscaleMethod == UpscaleMethod::kFSR) {
			CreateUpscalingTextureResources(a_upscaleMethod);
		}

		if (a_upscaleMethod == UpscaleMethod::kFSR) {
			fidelityFX.CreateFSRResources();
		}
	} catch (const std::exception& e) {
		logger::error("[Upscaling] VR post-load runtime reset failed: {}", e.what());
		postLoadRuntimeResetPending.store(true, std::memory_order_release);
		return false;
	} catch (...) {
		logger::error("[Upscaling] VR post-load runtime reset failed with an unknown exception");
		postLoadRuntimeResetPending.store(true, std::memory_order_release);
		return false;
	}

	logger::info("[Upscaling] Applied VR post-load runtime reset");
	return true;
}

void Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
{
	struct FoveatedLayoutKey
	{
		int32_t centerAreaQ = 0;
		int32_t centerHorizontalScaleQ = 0;
		int32_t centerFeatherQ = 0;
		std::array<int32_t, 4> centerOffsetQ{};
	};

	const auto makeFoveatedLayoutKey = [&](bool usePeripheryTAAProfile, bool usePeripheryTAAPath) {
		const auto profile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
		const float centerFeather = usePeripheryTAAPath ?
			ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) :
			FoveatedCommon::kCenterFeather;
		const auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(usePeripheryTAAProfile);

		FoveatedLayoutKey key{};
		key.centerAreaQ = QuantizePeripheryTAATileParam(profile.centerArea);
		key.centerHorizontalScaleQ = QuantizePeripheryTAATileParam(profile.centerHorizontalScale);
		key.centerFeatherQ = QuantizePeripheryTAATileParam(centerFeather);
		key.centerOffsetQ = {
			QuantizePeripheryTAATileParam(centerOffsets[0].x),
			QuantizePeripheryTAATileParam(centerOffsets[0].y),
			QuantizePeripheryTAATileParam(centerOffsets[1].x),
			QuantizePeripheryTAATileParam(centerOffsets[1].y)
		};
		return key;
	};

	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;
	static bool previousFoveatedDispatch = false;
	static bool previousPeripheryTAA = false;
	static bool previousFSRRuntimePathActive = false;
	static bool previousFSRRuntimeFsr4Configured = false;
	static bool previousFSRRuntimeFsr4Active = false;
	static uint32_t previousQualityMode = ClampQualityModeUInt(settings.qualityMode);
	static uint32_t previousDLSSPreset = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	static FoveatedLayoutKey previousFoveatedLayout = makeFoveatedLayoutKey(settings.periphery_taa_enable, settings.periphery_taa_enable && !settings.foveatedPeripheryMaskVisualization);

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);
	const uint32_t qualityModeCurrent = ClampQualityModeUInt(settings.qualityMode);
	const uint32_t dlssPresetCurrent = std::min<uint>(settings.dlssPreset, kDLSSPresetMaxIndex);
	const bool qualityModeChanged = previousQualityMode != qualityModeCurrent;
	const bool dlssPresetChanged = previousDLSSPreset != dlssPresetCurrent;
	const bool dlssQualityModeChanged = qualityModeChanged && (previousUpscaleMode == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kDLSS);
	const bool dlssPresetResourceChanged = dlssPresetChanged && (previousUpscaleMode == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kDLSS);
	const bool dlssResourceSettingsChanged = dlssQualityModeChanged || dlssPresetResourceChanged;
	const bool dlssOptionSettingsChanged =
		dlssResourceSettingsChanged &&
		previousUpscaleMode == UpscaleMethod::kDLSS &&
		a_upscalemethod == UpscaleMethod::kDLSS;
	const bool fsrQualityModeChanged = qualityModeChanged && (previousUpscaleMode == UpscaleMethod::kFSR || a_upscalemethod == UpscaleMethod::kFSR);
	const bool foveatedDispatchCurrent = IsFoveatedVendorDispatchEnabled(a_upscalemethod);
	const bool peripheryTAACurrent = IsPeripheryTAAEnabled(a_upscalemethod);
	const bool peripheryTAAPathCurrent = IsPeripheryTAAPathActive(a_upscalemethod);
	const bool fsrRuntimePathCurrent = IsFSRRuntimePathActive(a_upscalemethod);
	const bool fsrRuntimeFsr4Configured =
		a_upscalemethod == UpscaleMethod::kFSR &&
		settings.fsr4RuntimeEnable &&
		fidelityFX.IsRuntimeFsr4Available();
	const bool fsrRuntimeFsr4Current = IsFSRRuntimeFsr4PathActive(a_upscalemethod);
	const FoveatedLayoutKey foveatedLayoutCurrent = makeFoveatedLayoutKey(peripheryTAACurrent, peripheryTAAPathCurrent);
	const bool compareFoveatedArea = foveatedDispatchCurrent || previousFoveatedDispatch;
	const bool foveatedDispatchToggleChanged = previousFoveatedDispatch != foveatedDispatchCurrent;
	const bool foveatedGeometryChanged =
		compareFoveatedArea &&
		(previousFoveatedLayout.centerAreaQ != foveatedLayoutCurrent.centerAreaQ ||
		 previousFoveatedLayout.centerHorizontalScaleQ != foveatedLayoutCurrent.centerHorizontalScaleQ ||
		 previousFoveatedLayout.centerFeatherQ != foveatedLayoutCurrent.centerFeatherQ ||
		 previousFoveatedLayout.centerOffsetQ != foveatedLayoutCurrent.centerOffsetQ);
	const bool foveatedDispatchChanged = foveatedDispatchToggleChanged || foveatedGeometryChanged;
	const bool peripheryTAAChanged = previousPeripheryTAA != peripheryTAACurrent;
	const bool compareFSRRuntimePath = a_upscalemethod == UpscaleMethod::kFSR || previousUpscaleMode == UpscaleMethod::kFSR;
	const bool fsrRuntimePathChanged = compareFSRRuntimePath && previousFSRRuntimePathActive != fsrRuntimePathCurrent;
	const bool fsrRuntimeFsr4ConfiguredChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Configured != fsrRuntimeFsr4Configured;
	const bool fsrRuntimeVersionChanged =
		compareFSRRuntimePath &&
		(fsrRuntimePathCurrent || previousFSRRuntimePathActive) &&
		previousFSRRuntimeFsr4Active != fsrRuntimeFsr4Current;
	const bool fsrRuntimeFoveatedLayoutChanged =
		a_upscalemethod == UpscaleMethod::kFSR &&
		fsrRuntimePathCurrent &&
		foveatedDispatchChanged;

	if (upscaleModeChanged || frameGenModeChanged || foveatedDispatchChanged || peripheryTAAChanged || fsrRuntimePathChanged || fsrRuntimeFsr4ConfiguredChanged || fsrRuntimeVersionChanged || qualityModeChanged || dlssPresetResourceChanged) {
		logger::debug("[Upscaling] Resource change detected - Upscale: {} ({}) -> {} ({}), Quality: {} -> {}, DLSSPreset: {} -> {}, FrameGen: {} -> {} (d3d12Active={}), FSRRuntimePath: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, qualityModeCurrent, previousDLSSPreset, dlssPresetCurrent, previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive, previousFSRRuntimePathActive, fsrRuntimePathCurrent);

		const bool requiresFullPipelineUnbind =
			upscaleModeChanged ||
			frameGenModeChanged ||
			fsrRuntimePathChanged ||
			fsrRuntimeFsr4ConfiguredChanged ||
			(fsrRuntimeVersionChanged && !fidelityFX.IsRuntimeFsr4FailureLatched()) ||
			dlssResourceSettingsChanged ||
			fsrQualityModeChanged;
		if (requiresFullPipelineUnbind)
			UnbindUpscalingResources();

		bool fsrResourcesDestroyedForQuality = false;
		bool fsrResourcesRecreatedForQuality = false;
		if (qualityModeChanged || dlssPresetResourceChanged) {
			const auto destroyVRQualityResources = [&]() {
				if (!globals::game::isVR)
					return;
				DestroyVRIntermediateTextures();
				DestroyFoveatedResources();
			};

			RequestHistoryReset();
			if (dlssResourceSettingsChanged) {
				if (globals::game::isVR && a_upscalemethod == UpscaleMethod::kDLSS) {
					pendingDLSSHistoryReset.store(true, std::memory_order_relaxed);
				}
				if (!(globals::game::isVR && previousUpscaleMode == UpscaleMethod::kDLSS && a_upscalemethod == UpscaleMethod::kDLSS))
					streamline.InvalidateDLSSOptionsCache();
				if (!dlssOptionSettingsChanged && qualityModeChanged && a_upscalemethod != UpscaleMethod::kDLSS)
					destroyVRQualityResources();
			} else if (fsrQualityModeChanged) {
				fidelityFX.DestroyFSRResources();
				fsrResourcesDestroyedForQuality = true;
				destroyVRQualityResources();
				if (a_upscalemethod == UpscaleMethod::kFSR) {
					fidelityFX.CreateFSRResources();
					fsrResourcesRecreatedForQuality = true;
				}
			}
		}

		// Destroy previous vendor resources even for Native AA/DLAA, where the method is selected
		// but IsUpscalingActive() is false because the render scale is 1:1.
		if (upscaleModeChanged) {
			if (previousVendorUpscalerSelected) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS)
					streamline.DestroyDLSSResources();
				else if (previousUpscaleMode == UpscaleMethod::kFSR && !fsrResourcesDestroyedForQuality)
					fidelityFX.DestroyFSRResources();

				if (globals::game::isVR && !fsrResourcesDestroyedForQuality) {
					DestroyVRIntermediateTextures();
				}
			}
			DestroyUpscalingTextureResources(a_upscalemethod);

			if (a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality)
				fidelityFX.CreateFSRResources();
		}

		// Create new upscaling method resources
		if (upscaleModeChanged) {
			CreateUpscalingTextureResources(a_upscalemethod);
		}

		// Host FSR 3.1.5 and runtime upscaler providers keep separate temporal state; rebuild on path changes.
		if (!upscaleModeChanged && fsrRuntimePathChanged && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality) {
			fidelityFX.DestroyFSRResources();
			fidelityFX.CreateFSRResources();
			RequestHistoryReset();
		} else if (!upscaleModeChanged && (fsrRuntimeFsr4ConfiguredChanged || fsrRuntimeVersionChanged) && a_upscalemethod == UpscaleMethod::kFSR && !fsrResourcesRecreatedForQuality) {
			if (fsrRuntimeFsr4ConfiguredChanged || !fidelityFX.IsRuntimeFsr4FailureLatched())
				fidelityFX.ResetRuntimeUpscalerResources(true);
			RequestHistoryReset();
		} else if (!upscaleModeChanged && fsrRuntimeFoveatedLayoutChanged && !fsrResourcesRecreatedForQuality) {
			// Keep runtime contexts alive; the dispatch reset flag is enough for layout-only temporal changes.
			RequestHistoryReset();
		}

		if (upscaleModeChanged || foveatedDispatchChanged) {
			if (!foveatedDispatchCurrent)
				DestroyFoveatedResources();
		}

		if ((upscaleModeChanged || foveatedDispatchChanged || peripheryTAAChanged) && !peripheryTAACurrent) {
			DestroyPeripheryTAAResources();
		}

		// Update tracking for next call
		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12SwapChainActive);
		previousFoveatedDispatch = foveatedDispatchCurrent;
		previousPeripheryTAA = peripheryTAACurrent;
		previousFSRRuntimePathActive = fsrRuntimePathCurrent;
		previousFSRRuntimeFsr4Configured = fsrRuntimeFsr4Configured;
		previousFSRRuntimeFsr4Active = fsrRuntimeFsr4Current;
		previousQualityMode = qualityModeCurrent;
		previousDLSSPreset = dlssPresetCurrent;
		previousFoveatedLayout = foveatedLayoutCurrent;
		previousVendorUpscalerSelected = a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR;
	}
}

ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
{
	auto upscaleMethod = GetUpscaleMethod();
	uint methodIndex = (uint)upscaleMethod;

	// VR FSR requires a depth-output variant so we can feed FidelityFX a typed
	// R32_FLOAT depth texture instead of relying on typeless depth resources.
	if (globals::game::isVR && upscaleMethod == UpscaleMethod::kFSR) {
		if (!encodeTexturesCSDepthOutput) {
			logger::debug("Compiling EncodeTexturesCS.hlsl for VR FSR (FSR + DEPTH_OUTPUT)");
			std::vector<std::pair<const char*, const char*>> defines = {
				{ "FSR", "" },
				{ "DEPTH_OUTPUT", "" }
			};
			encodeTexturesCSDepthOutput.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
		}
		return encodeTexturesCSDepthOutput.get();
	}

	if (!encodeTexturesCS[methodIndex]) {
		logger::debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

		std::vector<std::pair<const char*, const char*>> defines;

		// Add upscale method define
		switch (upscaleMethod) {
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
			break;
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			break;
		default:
			// No define for NONE or TAA
			break;
		}

		encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
	}
	return encodeTexturesCS[methodIndex].get();
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* Upscaling::GetUnderwaterMaskUpscalePS(bool a_useRawSceneDepth)
{
	auto& shader = a_useRawSceneDepth ? underwaterMaskUpscaleRawDepthNoStencilPS : underwaterMaskUpscalePS;
	if (!shader) {
		logger::debug("Compiling UnderwaterMaskPS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		if (globals::game::isVR) {
			defines.push_back({ "VR", "" });
			if (a_useRawSceneDepth) {
				defines.push_back({ "NO_HMD_STENCIL_MASK", "" });
				defines.push_back({ "RAW_SCENE_DEPTH", "" });
			}
		}
		shader.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return shader.get();
}

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0"));
	}

	return upscaleVS.get();
}

ID3D11ComputeShader* Upscaling::GetFoveatedPeripheryCS()
{
	if (!foveatedPeripheryCS) {
		logger::debug("Compiling FoveatedPeripheryCS.hlsl");
		foveatedPeripheryCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/FoveatedPeripheryCS.hlsl", {}, "cs_5_0"));
	}

	return foveatedPeripheryCS.get();
}

ID3D11ComputeShader* Upscaling::GetFoveatedCenterBlendCS()
{
	if (!foveatedCenterBlendCS) {
		logger::debug("Compiling FoveatedCenterBlendCS.hlsl");
		foveatedCenterBlendCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", {}, "cs_5_0"));
	}

	return foveatedCenterBlendCS.get();
}

ID3D11ComputeShader* Upscaling::GetPeripheryTAACS()
{
	if (!peripheryTAACS) {
		logger::debug("Compiling PeripheryTAACS.hlsl");
		peripheryTAACS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/PeripheryTAACS.hlsl", {}, "cs_5_0"));
	}

	return peripheryTAACS.get();
}

ID3D11ComputeShader* Upscaling::GetSubmitStageStretchCS()
{
	if (!submitStageStretchCS) {
		logger::debug("Compiling SubmitStageStretchCS.hlsl");
		submitStageStretchCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/SubmitStageStretchCS.hlsl", {}, "cs_5_0"));
	}

	return submitStageStretchCS.get();
}

eastl::unique_ptr<Texture2D> Upscaling::CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
	bool copyBindFlags, bool createSRV, bool createUAV, const char* name, bool createRTV)
{
	D3D11_TEXTURE2D_DESC srcDesc;
	static_cast<ID3D11Texture2D*>(src)->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = copyBindFlags ? srcDesc.BindFlags : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	if (createRTV)
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

	auto tex = eastl::make_unique<Texture2D>(desc);

	if (name) {
		Util::SetResourceName(tex->resource.get(), name);
	}

	if (createSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srcDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		tex->CreateSRV(srvDesc);
	}
	if (createUAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = srcDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		tex->CreateUAV(uavDesc);
	}
	if (createRTV) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = srcDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		tex->CreateRTV(rtvDesc);
	}
	return tex;
}

bool Upscaling::IsFoveatedVendorDispatchEnabled(UpscaleMethod a_upscaleMethod) const
{
	if (!IsFoveatedVendorDispatchRequested(settings, a_upscaleMethod))
		return false;

	const bool usePeripheryTAAProfile = settings.periphery_taa_enable;
	const float centerArea = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile).centerArea;
	// 1.0 is effectively full-frame vendor dispatch, so keep the default path.
	return FoveatedCommon::IsActiveCoverage(centerArea);
}

bool Upscaling::IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       fidelityFX.ShouldUseRuntimeUpscalerForFSR();
}

bool Upscaling::IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const
{
	return a_upscaleMethod == UpscaleMethod::kFSR &&
	       fidelityFX.ShouldRequestRuntimeFsr4();
}

bool Upscaling::IsPeripheryTAAEnabled(UpscaleMethod a_upscaleMethod) const
{
	return IsFoveatedVendorDispatchEnabled(a_upscaleMethod) && settings.periphery_taa_enable;
}

bool Upscaling::IsPeripheryTAAPathActive(UpscaleMethod a_upscaleMethod) const
{
	return IsPeripheryTAAEnabled(a_upscaleMethod) && !settings.foveatedPeripheryMaskVisualization;
}

bool Upscaling::UseActiveFoveatedPeripheryTAAProfile() const
{
	const auto upscaleMethod = GetUpscaleMethod();
	return IsPeripheryTAAEnabled(upscaleMethod);
}

bool Upscaling::IsActiveUpscalingFoveatedProfileAvailable() const
{
	return IsFoveatedVendorDispatchEnabled(GetUpscaleMethod());
}

Upscaling::ActiveUpscalingFoveatedProfile Upscaling::GetActiveUpscalingFoveatedProfile() const
{
	ActiveUpscalingFoveatedProfile profile{};
	const auto upscaleMethod = GetUpscaleMethod();
	profile.available = IsActiveUpscalingFoveatedProfileAvailable();
	profile.usesPeripheryTAAOuterMask = profile.available && IsPeripheryTAAEnabled(upscaleMethod);

	if (profile.usesPeripheryTAAOuterMask) {
		// For Upscaling FOV + Peripheral TAA, consumers that need the full visible
		// foveated region should use the outside edge of the TAA mask.
		profile.coverageArea = ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area,
			settings.foveatedCenterHorizontalScale,
			settings.periphery_taa_center_blend_feather);
	} else {
		profile.coverageArea = GetFoveatedMaskProfileParams(settings, false).centerArea;
	}

	profile.centerHorizontalScale = GetFoveatedMaskProfileParams(settings, profile.usesPeripheryTAAOuterMask).centerHorizontalScale;
	profile.centerOffsets = GetResolvedFoveatedMaskCenterOffsets(profile.usesPeripheryTAAOuterMask);
	if (!globals::game::isVR)
		profile.centerOffsets[1] = { 0.0f, 0.0f };
	return profile;
}

float Upscaling::GetActiveFoveatedCenterArea() const
{
	if (UseActiveFoveatedPeripheryTAAProfile()) {
		return ClampPeripheryTAAOuterScaleForCenter(
			settings.periphery_taa_outer_scale,
			settings.periphery_taa_center_area,
			settings.foveatedCenterHorizontalScale,
			settings.periphery_taa_center_blend_feather);
	}

	return GetFoveatedMaskProfileParams(settings, false).centerArea;
}

float Upscaling::GetActiveFoveatedCenterHorizontalScale() const
{
	if (!globals::game::isVR)
		return 1.0f;

	return GetFoveatedMaskProfileParams(settings, UseActiveFoveatedPeripheryTAAProfile()).centerHorizontalScale;
}

float2 Upscaling::GetDefaultFoveatedMaskCenterOffset(uint32_t eyeIndex) const
{
	(void)eyeIndex;
	return { 0.0f, 0.0f };
}

float2 Upscaling::GetResolvedFoveatedMaskCenterOffset(uint32_t eyeIndex, bool usePeripheryTAAProfile) const
{
	float2 resolved = GetDefaultFoveatedMaskCenterOffset(eyeIndex);
	const bool isLeftEye = eyeIndex == 0;
	const auto params = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float userAdjustX = isLeftEye ? params.leftOffsetX : params.rightOffsetX;
	const float userAdjustY = isLeftEye ? params.leftOffsetY : params.rightOffsetY;
	resolved.x += ClampFoveatedMaskOffsetAdjustment(userAdjustX);
	resolved.y += ClampFoveatedMaskOffsetAdjustment(userAdjustY);

	if (globals::game::isVR) {
		const float centerScale = params.centerArea;
		const float centerHorizontalScale = params.centerHorizontalScale;
		const float outwardExpansion = centerScale * 0.5f * std::max(0.0f, centerHorizontalScale - 1.0f);
		resolved.x += isLeftEye ? -outwardExpansion : outwardExpansion;
	}

	resolved.x = std::clamp(resolved.x, kFoveatedMaskOffsetResolvedMin, kFoveatedMaskOffsetResolvedMax);
	resolved.y = std::clamp(resolved.y, kFoveatedMaskOffsetResolvedMin, kFoveatedMaskOffsetResolvedMax);
	return resolved;
}

std::array<float2, 2> Upscaling::GetResolvedFoveatedMaskCenterOffsets(bool usePeripheryTAAProfile) const
{
	return { GetResolvedFoveatedMaskCenterOffset(0, usePeripheryTAAProfile), GetResolvedFoveatedMaskCenterOffset(1, usePeripheryTAAProfile) };
}

std::array<float2, 2> Upscaling::GetActiveResolvedFoveatedMaskCenterOffsets() const
{
	auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(UseActiveFoveatedPeripheryTAAProfile());
	if (!globals::game::isVR)
		centerOffsets[1] = { 0.0f, 0.0f };
	return centerOffsets;
}

bool Upscaling::BuildFoveatedDispatchRects(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool isVR, float centerScale, float centerFeather, float centerHorizontalScale, bool usePeripheryTAAProfile)
{
	centerScale = ClampFoveatedCenterArea(centerScale);
	centerFeather = std::isfinite(centerFeather) ? std::max(0.0f, centerFeather) : FoveatedCommon::kCenterFeather;
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);

	auto& cache = foveatedRectCache;
	auto centerOffsets = GetResolvedFoveatedMaskCenterOffsets(usePeripheryTAAProfile);
	if (!isVR)
		centerOffsets[1] = { 0.0f, 0.0f };
	const bool cacheDirty =
		cache.inputWidthPerEye != inputWidthPerEye ||
		cache.inputHeight != inputHeight ||
		cache.outputWidthPerEye != outputWidthPerEye ||
		cache.outputHeight != outputHeight ||
		cache.isVR != isVR ||
		std::abs(cache.centerScale - centerScale) > 1e-6f ||
		std::abs(cache.centerFeather - centerFeather) > 1e-6f ||
		std::abs(cache.centerHorizontalScale - centerHorizontalScale) > 1e-6f ||
		std::abs(cache.centerOffsets[0].x - centerOffsets[0].x) > 1e-6f ||
		std::abs(cache.centerOffsets[0].y - centerOffsets[0].y) > 1e-6f ||
		(isVR && (std::abs(cache.centerOffsets[1].x - centerOffsets[1].x) > 1e-6f ||
		          std::abs(cache.centerOffsets[1].y - centerOffsets[1].y) > 1e-6f));

	if (!cacheDirty)
		return true;

	cache.inputWidthPerEye = inputWidthPerEye;
	cache.inputHeight = inputHeight;
	cache.outputWidthPerEye = outputWidthPerEye;
	cache.outputHeight = outputHeight;
	cache.isVR = isVR;
	cache.centerScale = centerScale;
	cache.centerFeather = centerFeather;
	cache.centerHorizontalScale = centerHorizontalScale;
	cache.centerOffsets = centerOffsets;
	cache.rects = {};

	auto buildRect = [&](uint32_t eyeIndex) {
		FoveatedDispatchRect rect{};
		if (!inputWidthPerEye || !inputHeight || !outputWidthPerEye || !outputHeight)
			return rect;

		const float2 centerOffset = centerOffsets[eyeIndex];
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(0, outputWidthPerEye, outputHeight, centerScale, centerOffset.x, centerOffset.y, centerFeather, centerHorizontalScale);
		const int minX = bounds.minX;
		const int maxX = bounds.maxX;
		const int minY = bounds.minY;
		const int maxY = bounds.maxY;

		if (maxX <= minX || maxY <= minY)
			return rect;

		rect.outputOffsetX = static_cast<uint32_t>(minX);
		rect.outputOffsetY = static_cast<uint32_t>(minY);
		rect.outputWidth = static_cast<uint32_t>(maxX - minX);
		rect.outputHeight = static_cast<uint32_t>(maxY - minY);

		const uint32_t outputRectMaxX = static_cast<uint32_t>(std::min<uint64_t>(outputWidthPerEye, static_cast<uint64_t>(rect.outputOffsetX) + rect.outputWidth));
		const uint32_t outputRectMaxY = static_cast<uint32_t>(std::min<uint64_t>(outputHeight, static_cast<uint64_t>(rect.outputOffsetY) + rect.outputHeight));
		const auto mappedInputRect = MapOutputRectToInputRect(
			rect.outputOffsetX,
			rect.outputOffsetY,
			outputRectMaxX,
			outputRectMaxY,
			outputWidthPerEye,
			outputHeight,
			inputWidthPerEye,
			inputHeight);
		if (!mappedInputRect.IsValid())
			return FoveatedDispatchRect{};

		rect.inputOffsetX = mappedInputRect.minX;
		rect.inputOffsetY = mappedInputRect.minY;
		rect.inputWidth = mappedInputRect.maxX - mappedInputRect.minX;
		rect.inputHeight = mappedInputRect.maxY - mappedInputRect.minY;

		(void)eyeIndex;
		return rect;
	};

	cache.rects[0] = buildRect(0);
	if (isVR)
		cache.rects[1] = buildRect(1);

	return true;
}

bool Upscaling::EnsureFoveatedTexture(eastl::unique_ptr<Texture2D>& texture, ID3D11Resource* source, uint32_t width, uint32_t height, bool copyBindFlags, bool createSRV, bool createUAV, bool createRTV, const char* name)
{
	if (!source || !width || !height)
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	if (!TryGetTexture2DDesc(source, sourceDesc))
		return false;

	bool recreate = !texture;
	if (!recreate) {
		recreate = texture->desc.Width != width ||
		           texture->desc.Height != height ||
		           texture->desc.Format != sourceDesc.Format;
		if (createSRV && !texture->srv)
			recreate = true;
		if (createUAV && !texture->uav)
			recreate = true;
		if (createRTV && !texture->rtv)
			recreate = true;
	}

	if (recreate) {
		texture = CreateTextureFromSource(source, width, height, copyBindFlags, createSRV, createUAV, name, createRTV);
		if (!texture)
			return false;
	}

	if (createRTV && !texture->rtv) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = texture->desc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		texture->CreateRTV(rtvDesc);
	}

	if (createSRV && !texture->srv) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texture->desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);
	}

	if (createUAV && !texture->uav) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = texture->desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texture->CreateUAV(uavDesc);
	}

	return true;
}

bool Upscaling::EnsurePeripheryTAAResources(uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* colorSource)
{
	if (!outputWidthPerEye || !outputHeight || !colorSource)
		return false;

	D3D11_TEXTURE2D_DESC colorDesc{};
	if (!TryGetTexture2DDesc(colorSource, colorDesc))
		return false;

	bool recreatedResources = false;

	for (uint32_t eye = 0; eye < 2; ++eye) {
		const std::string suffix = eye == 0 ? "Left" : "Right";

		for (uint32_t historySlot = 0; historySlot < 2; ++historySlot) {
			auto& historyColorTexture = peripheryTAAHistoryColor[eye][historySlot];
			const bool recreateHistoryColor =
				!historyColorTexture ||
				historyColorTexture->desc.Width != outputWidthPerEye ||
				historyColorTexture->desc.Height != outputHeight ||
				historyColorTexture->desc.Format != colorDesc.Format ||
				!historyColorTexture->srv || !historyColorTexture->uav;
			recreatedResources = recreatedResources || recreateHistoryColor;

			if (!EnsureFoveatedTexture(
					historyColorTexture,
					colorSource,
					outputWidthPerEye,
					outputHeight,
					false,
					true,
					true,
					false,
					(std::format("Upscale_PeripheryTAA_HistoryColor_{}_{}", suffix, historySlot)).c_str())) {
				return false;
			}

			auto& velocityTexture = peripheryTAAVelocityHistory[eye][historySlot];
			const bool recreateVelocity =
				!velocityTexture ||
				velocityTexture->desc.Width != outputWidthPerEye ||
				velocityTexture->desc.Height != outputHeight ||
				velocityTexture->desc.Format != DXGI_FORMAT_R16G16_FLOAT ||
				!velocityTexture->srv || !velocityTexture->uav;
			if (recreateVelocity) {
				velocityTexture = CreateNamedTexture2D(
					outputWidthPerEye,
					outputHeight,
					DXGI_FORMAT_R16G16_FLOAT,
					true,
					true,
					false,
					(std::format("Upscale_PeripheryTAA_Velocity_{}_{}", suffix, historySlot)).c_str());
				recreatedResources = true;
			}

			auto& lockTexture = peripheryTAALockHistory[eye][historySlot];
			const bool recreateLock =
				!lockTexture ||
				lockTexture->desc.Width != outputWidthPerEye ||
				lockTexture->desc.Height != outputHeight ||
				lockTexture->desc.Format != DXGI_FORMAT_R16_FLOAT ||
				!lockTexture->srv || !lockTexture->uav;
			if (recreateLock) {
				lockTexture = CreateNamedTexture2D(
					outputWidthPerEye,
					outputHeight,
					DXGI_FORMAT_R16_FLOAT,
					true,
					true,
					false,
					(std::format("Upscale_PeripheryTAA_Lock_{}_{}", suffix, historySlot)).c_str());
				recreatedResources = true;
			}
		}
	}

	if (recreatedResources) {
		// Any recreated history surface invalidates temporal continuity.
		peripheryTAAHistoryReadIndex = 0;
		peripheryTAAHistoryValid = false;
		submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
		submitStageFoveatedPeripheryTAAEyeReady = {};
	}

	return true;
}

bool Upscaling::EnsurePeripheryTAATileBuffer(uint32_t eyeIndex, uint32_t tileCapacity)
{
	if (eyeIndex >= 2 || tileCapacity == 0)
		return false;
	if (tileCapacity > std::numeric_limits<uint32_t>::max() / sizeof(PeripheryTAATile))
		return false;

	auto& tileBuffer = peripheryTAATileBuffer[eyeIndex];
	auto& tileCapacityCurrent = peripheryTAATileCapacity[eyeIndex];
	if (tileBuffer && tileCapacityCurrent >= tileCapacity && tileBuffer->srv)
		return true;

	D3D11_BUFFER_DESC sbDesc{};
	sbDesc.Usage = D3D11_USAGE_DYNAMIC;
	sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	sbDesc.StructureByteStride = sizeof(PeripheryTAATile);
	sbDesc.ByteWidth = static_cast<uint32_t>(sizeof(PeripheryTAATile) * tileCapacity);

	tileBuffer = eastl::make_unique<Buffer>(sbDesc);
	tileCapacityCurrent = tileCapacity;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = tileCapacity;
	tileBuffer->CreateSRV(srvDesc);

	peripheryTAATileCache[eyeIndex].uploaded = false;
	return tileBuffer->srv != nullptr;
}

bool Upscaling::BuildPeripheryTAATileList(uint32_t eyeIndex, uint32_t outputWidth, uint32_t outputHeight, float centerScale, float taaOuterScale, float centerHorizontalScale, float centerFeather, float centerOffsetX, float centerOffsetY, uint32_t coveragePadding, uint32_t& outTileCount)
{
	outTileCount = 0;
	if (eyeIndex >= 2 || outputWidth == 0 || outputHeight == 0)
		return false;

	const uint32_t tileSize = static_cast<uint32_t>(FoveatedCommon::kThreadGroupSize);
	const uint32_t tileColumns = (outputWidth + tileSize - 1u) / tileSize;
	const uint32_t tileRows = (outputHeight + tileSize - 1u) / tileSize;
	if (tileColumns != 0 && tileRows > (std::numeric_limits<uint32_t>::max() / tileColumns))
		return false;
	const uint32_t maxTileCount = tileColumns * tileRows;
	if (!EnsurePeripheryTAATileBuffer(eyeIndex, maxTileCount))
		return false;

	centerScale = ClampFoveatedCenterArea(centerScale);
	centerFeather = ClampPeripheryTAACenterBlendFeather(centerFeather);
	taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(
		taaOuterScale,
		centerScale,
		centerHorizontalScale,
		centerFeather);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);

	PeripheryTAATileCacheKey cacheKey{};
	cacheKey.outputWidth = outputWidth;
	cacheKey.outputHeight = outputHeight;
	cacheKey.coveragePadding = coveragePadding;
	cacheKey.centerScaleQ = QuantizePeripheryTAATileParam(centerScale);
	cacheKey.taaOuterScaleQ = QuantizePeripheryTAATileParam(taaOuterScale);
	cacheKey.centerHorizontalScaleQ = QuantizePeripheryTAATileParam(centerHorizontalScale);
	cacheKey.centerOffsetXQ = QuantizePeripheryTAATileParam(centerOffsetX);
	cacheKey.centerOffsetYQ = QuantizePeripheryTAATileParam(centerOffsetY);

	auto& cacheState = peripheryTAATileCache[eyeIndex];
	const bool keyMatches =
		cacheState.valid &&
		cacheState.key.outputWidth == cacheKey.outputWidth &&
		cacheState.key.outputHeight == cacheKey.outputHeight &&
		cacheState.key.coveragePadding == cacheKey.coveragePadding &&
		cacheState.key.centerScaleQ == cacheKey.centerScaleQ &&
		cacheState.key.taaOuterScaleQ == cacheKey.taaOuterScaleQ &&
		cacheState.key.centerHorizontalScaleQ == cacheKey.centerHorizontalScaleQ &&
		cacheState.key.centerOffsetXQ == cacheKey.centerOffsetXQ &&
		cacheState.key.centerOffsetYQ == cacheKey.centerOffsetYQ;

	if (!keyMatches) {
		cacheState.tiles.clear();
		cacheState.tiles.reserve(maxTileCount);

		const auto coverageBounds = FoveatedCommon::BuildCenteredDispatchBounds(0, outputWidth, outputHeight, taaOuterScale, centerOffsetX, centerOffsetY, 0.0f, centerHorizontalScale);
		const uint32_t coverageMinX = coverageBounds.minX > static_cast<int>(coveragePadding) ? static_cast<uint32_t>(coverageBounds.minX) - coveragePadding : 0u;
		const uint32_t coverageMinY = coverageBounds.minY > static_cast<int>(coveragePadding) ? static_cast<uint32_t>(coverageBounds.minY) - coveragePadding : 0u;
		const uint32_t coverageMaxX = coverageBounds.maxX > coverageBounds.minX ? std::min(outputWidth, static_cast<uint32_t>(coverageBounds.maxX) + coveragePadding) : 0u;
		const uint32_t coverageMaxY = coverageBounds.maxY > coverageBounds.minY ? std::min(outputHeight, static_cast<uint32_t>(coverageBounds.maxY) + coveragePadding) : 0u;
		const bool useRectangularCoverage = coveragePadding > 0 && coverageMaxX > coverageMinX && coverageMaxY > coverageMinY;

		for (uint32_t tileY = 0; tileY < outputHeight; tileY += tileSize) {
			const uint32_t maxY = std::min(tileY + tileSize, outputHeight);
			for (uint32_t tileX = 0; tileX < outputWidth; tileX += tileSize) {
				const uint32_t maxX = std::min(tileX + tileSize, outputWidth);
				if (useRectangularCoverage) {
					if (maxX <= coverageMinX || tileX >= coverageMaxX || maxY <= coverageMinY || tileY >= coverageMaxY)
						continue;
				} else {
					const float outerMinDistance = FoveatedMaskTileMinDistance(tileX, tileY, maxX, maxY, outputWidth, outputHeight, taaOuterScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
					if (outerMinDistance > 1.0f + 1e-4f)
						continue;
				}

				const uint32_t centerTestMinX = tileX > coveragePadding ? tileX - coveragePadding : 0u;
				const uint32_t centerTestMinY = tileY > coveragePadding ? tileY - coveragePadding : 0u;
				const uint32_t centerTestMaxX = std::min(outputWidth, maxX + coveragePadding);
				const uint32_t centerTestMaxY = std::min(outputHeight, maxY + coveragePadding);
				const float centerMaxDistance = FoveatedMaskTileMaxDistance(centerTestMinX, centerTestMinY, centerTestMaxX, centerTestMaxY, outputWidth, outputHeight, centerScale, centerHorizontalScale, centerOffsetX, centerOffsetY);
				if (centerMaxDistance <= 1.0f - 1e-4f)
					continue;

				cacheState.tiles.push_back({ tileX, tileY });
			}
		}

		cacheState.key = cacheKey;
		cacheState.tileCount = static_cast<uint32_t>(cacheState.tiles.size());
		cacheState.valid = true;
		cacheState.uploaded = false;
	}

	outTileCount = cacheState.tileCount;
	if (outTileCount == 0)
		return true;
	if (cacheState.uploaded)
		return true;

	auto context = globals::d3d::context;
	auto& tileBuffer = peripheryTAATileBuffer[eyeIndex];
	const uint32_t tileCapacity = peripheryTAATileCapacity[eyeIndex];
	if (!context || !tileBuffer || !tileBuffer->resource || tileCapacity < outTileCount)
		return false;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(tileBuffer->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	const size_t bytes = sizeof(PeripheryTAATile) * outTileCount;
	memcpy_s(mapped.pData, sizeof(PeripheryTAATile) * tileCapacity, cacheState.tiles.data(), bytes);
	context->Unmap(tileBuffer->resource.get(), 0);
	cacheState.uploaded = true;
	return true;
}

void Upscaling::DestroyFoveatedResources()
{
	for (uint32_t i = 0; i < 2; ++i) {
		foveatedCenterColorIn[i].reset();
		foveatedCenterColorOut[i].reset();
		foveatedCenterDepth[i].reset();
		foveatedCenterMotionVectors[i].reset();
		foveatedCenterReactiveMask[i].reset();
		foveatedCenterTransparencyMask[i].reset();
	}
	foveatedRectCache = {};
	DestroyPeripheryTAAResources();
}

void Upscaling::DestroyPeripheryTAAResources()
{
	for (uint32_t eye = 0; eye < 2; ++eye) {
		for (uint32_t historySlot = 0; historySlot < 2; ++historySlot) {
			peripheryTAAHistoryColor[eye][historySlot].reset();
			peripheryTAAVelocityHistory[eye][historySlot].reset();
			peripheryTAALockHistory[eye][historySlot].reset();
		}
		peripheryTAATileBuffer[eye].reset();
		peripheryTAATileCapacity[eye] = 0;
		peripheryTAATileCache[eye] = {};
	}
	peripheryTAAHistoryReadIndex = 0;
	peripheryTAAHistoryValid = false;
	submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	submitStageFoveatedPeripheryTAAEyeReady = {};
}

void Upscaling::DispatchFoveatedPeripheryPass(ID3D11ShaderResourceView* sourceSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, bool keepBindingsBound, float sourceScaleX, float sourceScaleY, float sourceOffsetX, float sourceOffsetY, float centerOffsetX, float centerOffsetY)
{
	auto* peripheryCS = GetFoveatedPeripheryCS();
	if (!peripheryCS || !sourceSRV || !outputUAV || !foveatedPeripheryCB)
		return;
	if (!dispatchWidth || !dispatchHeight)
		return;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler)
		return;
	if (outputOffsetX >= outputWidth || outputOffsetY >= outputHeight)
		return;
	dispatchWidth = std::min(dispatchWidth, outputWidth - outputOffsetX);
	dispatchHeight = std::min(dispatchHeight, outputHeight - outputOffsetY);
	if (!dispatchWidth || !dispatchHeight)
		return;

	FoveatedPeripheryCB cbData{};
	cbData.outputDim = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	cbData.invOutputDim = {
		outputWidth > 0 ? 1.0f / static_cast<float>(outputWidth) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.invSourceDim = {
		sourceWidth > 0 ? 1.0f / static_cast<float>(sourceWidth) : 0.0f,
		sourceHeight > 0 ? 1.0f / static_cast<float>(sourceHeight) : 0.0f
	};
	const auto sourceRegion = ClampNormalizedTextureRegion(sourceScaleX, sourceScaleY, sourceOffsetX, sourceOffsetY);
	cbData.sourceScale = sourceRegion.scale;
	cbData.sourceOffset = sourceRegion.offset;
	cbData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
	cbData.outputOffset = { static_cast<float>(outputOffsetX), static_cast<float>(outputOffsetY) };
	cbData.jitter = jitter;
	centerScale = ClampFoveatedCenterArea(centerScale);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool showThreeZoneMask = visualizeMask && settings.periphery_taa_enable;
	const float centerFeather = showThreeZoneMask ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	const float taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(settings.periphery_taa_outer_scale, centerScale, centerHorizontalScale, centerFeather);
	cbData.centerAndMask = {
		centerOffsetX,
		centerOffsetY,
		visualizeMask ? 1.0f : 0.0f,
		showThreeZoneMask ? 1.0f : 0.0f
	};
	cbData.tuning0 = {
		centerScale,
		centerFeather,
		centerHorizontalScale,
		taaOuterScale
	};
	foveatedPeripheryCB->Update(cbData);

	if (keepBindingsBound) {
		auto state = globals::state;
		if (state && state->frameAnnotations)
			state->BeginPerfEvent("Foveated Periphery");
		context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
		if (state && state->frameAnnotations)
			state->EndPerfEvent();
	} else {
		ID3D11Buffer* cb = foveatedPeripheryCB->CB();
		ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
		ID3D11ShaderResourceView* srvs[1] = { sourceSRV };
		ID3D11UnorderedAccessView* uavs[1] = { outputUAV };

		context->CSSetShader(peripheryCS, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		auto state = globals::state;
		if (state && state->frameAnnotations)
			state->BeginPerfEvent("Foveated Periphery");
		context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
		if (state && state->frameAnnotations)
			state->EndPerfEvent();

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetSamplers(0, 1, nullSampler);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}

void Upscaling::DispatchPeripheryTAAPass(ID3D11ShaderResourceView* currentColorSRV, ID3D11ShaderResourceView* currentDepthSRV, ID3D11ShaderResourceView* currentMotionVectorSRV,
	ID3D11ShaderResourceView* currentReactiveSRV, ID3D11ShaderResourceView* currentTransparencySRV, ID3D11ShaderResourceView* historyColorSRV,
	ID3D11ShaderResourceView* historyVelocitySRV, ID3D11ShaderResourceView* historyLockSRV, ID3D11UnorderedAccessView* outputColorUAV, ID3D11UnorderedAccessView* outputHistoryColorUAV,
	ID3D11UnorderedAccessView* outputVelocityUAV, ID3D11UnorderedAccessView* outputLockUAV, ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount,
	uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth,
	uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, const float4x4& currentViewProjInverse,
	const float4x4& previousViewProj, const float4& currentCameraPosAdjust, const float4& previousCameraPosAdjust, bool resetHistory, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY,
	float inputTextureScaleX, float inputTextureScaleY, float inputTextureOffsetX, float inputTextureOffsetY)
{
	// This custom periphery-only TAA path adapts MIT-licensed ideas from:
	// - Godot's TAA resolve / Spartan Engine lineage (taa_resolve.glsl, copyright Panos Karabelas)
	// - AMD FidelityFX FSR2/FSR3 lock/reactivity/luminance-instability heuristics.
	// - Temporal AA survey background: Yang, Liu, Salvi, "A Survey of Temporal Antialiasing Techniques" (2020).
	// The implementation below is purpose-built for Community Shaders VR periphery resolve and is not a verbatim copy.
	auto* peripheryTAA = GetPeripheryTAACS();
	if (!peripheryTAA || !peripheryTAACB)
		return;
	if (!currentColorSRV || !currentDepthSRV || !currentMotionVectorSRV || !currentReactiveSRV || !currentTransparencySRV)
		return;
	if (!historyColorSRV || !historyVelocitySRV || !historyLockSRV)
		return;
	if (!outputColorUAV || !outputHistoryColorUAV || !outputVelocityUAV || !outputLockUAV)
		return;
	if (!inputWidth || !inputHeight || !outputWidth || !outputHeight)
		return;
	const bool useTileList = tileListSRV && tileCount > 0;
	if (!useTileList && (!dispatchWidth || !dispatchHeight))
		return;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler)
		return;

	uint32_t dispatchGroupsX = 0;
	uint32_t dispatchGroupsY = 0;
	if (useTileList) {
		dispatchGroupsX = std::min(tileCount, 65535u);
		dispatchGroupsY = (tileCount + dispatchGroupsX - 1u) / dispatchGroupsX;
		outputOffsetX = 0;
		outputOffsetY = 0;
		dispatchWidth = tileCount;
		dispatchHeight = 1;
	} else {
		if (outputOffsetX >= outputWidth || outputOffsetY >= outputHeight)
			return;

		dispatchWidth = std::min(dispatchWidth, outputWidth - outputOffsetX);
		dispatchHeight = std::min(dispatchHeight, outputHeight - outputOffsetY);
		if (!dispatchWidth || !dispatchHeight)
			return;

		dispatchGroupsX = (dispatchWidth + 7u) >> 3;
		dispatchGroupsY = (dispatchHeight + 7u) >> 3;
	}

	PeripheryTAACB cbData{};
	cbData.outputDim = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	cbData.invOutputDim = {
		outputWidth > 0 ? 1.0f / static_cast<float>(outputWidth) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.inputDim = { static_cast<float>(inputWidth), static_cast<float>(inputHeight) };
	cbData.invInputDim = {
		inputWidth > 0 ? 1.0f / static_cast<float>(inputWidth) : 0.0f,
		inputHeight > 0 ? 1.0f / static_cast<float>(inputHeight) : 0.0f
	};
	const auto inputTextureRegion = ClampNormalizedTextureRegion(inputTextureScaleX, inputTextureScaleY, inputTextureOffsetX, inputTextureOffsetY);
	cbData.inputTextureScale = inputTextureRegion.scale;
	cbData.inputTextureOffset = inputTextureRegion.offset;
	cbData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
	cbData.outputOffset = { static_cast<float>(outputOffsetX), static_cast<float>(outputOffsetY) };
	cbData.jitter = jitter;
	cbData.centerOffset = { centerOffsetX, centerOffsetY };
	centerScale = ClampFoveatedCenterArea(centerScale);
	centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	const float centerFeather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		centerScale,
		centerHorizontalScale,
		centerFeather);
	const auto taaColorWriteBounds = FoveatedCommon::BuildCenteredDispatchBounds(
		0,
		outputWidth,
		outputHeight,
		taaOuterScale,
		centerOffsetX,
		centerOffsetY,
		0.0f,
		centerHorizontalScale);
	cbData.tuning0 = {
		centerScale,
		centerFeather,
		resetHistory ? 1.0f : 0.0f,
		taaOuterScale
	};
	cbData.tuning1 = {
		peripheryTAAHistoryValid && !resetHistory ? 1.0f : 0.0f,
		centerHorizontalScale,
		useTileList ? 1.0f : 0.0f,
		static_cast<float>(dispatchGroupsX)
	};
	cbData.tuning2 = {
		1.0f,
		1.25f,
		0.10f,
		0.92f
	};
	cbData.tuning3 = {
		static_cast<float>(taaColorWriteBounds.minX),
		static_cast<float>(taaColorWriteBounds.minY),
		static_cast<float>(taaColorWriteBounds.maxX),
		static_cast<float>(taaColorWriteBounds.maxY)
	};
	cbData.currentViewProjInverse = currentViewProjInverse;
	cbData.previousViewProj = previousViewProj;
	cbData.currentCameraPosAdjust = currentCameraPosAdjust;
	cbData.previousCameraPosAdjust = previousCameraPosAdjust;
	peripheryTAACB->Update(cbData);

	ID3D11Buffer* cb = peripheryTAACB->CB();
	ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
	ID3D11ShaderResourceView* srvs[9] = {
		currentColorSRV,
		currentDepthSRV,
		currentMotionVectorSRV,
		currentReactiveSRV,
		currentTransparencySRV,
		historyColorSRV,
		historyVelocitySRV,
		historyLockSRV,
		tileListSRV
	};
	ID3D11UnorderedAccessView* uavs[4] = { outputColorUAV, outputVelocityUAV, outputLockUAV, outputHistoryColorUAV };

	context->CSSetShader(peripheryTAA, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, 1, samplers);
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	auto state = globals::state;
	if (state && state->frameAnnotations) {
		char buf[64];
		if (useTileList)
			snprintf(buf, sizeof(buf), "Periphery TAA Tiles %u", tileCount);
		else
			snprintf(buf, sizeof(buf), "Periphery TAA Rect %ux%u", dispatchWidth, dispatchHeight);
		state->BeginPerfEvent(buf);
	}
	context->Dispatch(dispatchGroupsX, dispatchGroupsY, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11ShaderResourceView* nullSRV[9] = {};
	ID3D11UnorderedAccessView* nullUAV[4] = {};
	ID3D11SamplerState* nullSampler[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);
	context->CSSetSamplers(0, 1, nullSampler);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

void Upscaling::DispatchFoveatedBlendPass(ID3D11ShaderResourceView* centerSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t outputWidthPerEye, uint32_t outputHeight, const FoveatedDispatchRect& rect, uint32_t dispatchOffsetX, uint32_t dispatchOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather)
{
	if (!centerSRV || !outputUAV || rect.outputWidth == 0 || rect.outputHeight == 0 || !foveatedCenterBlendCB)
		return;
	if (!dispatchWidth || !dispatchHeight)
		return;

	auto* blendCS = GetFoveatedCenterBlendCS();
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!blendCS || !context || !deferred || !deferred->linearSampler)
		return;
	if (dispatchOffsetX >= outputWidthPerEye || dispatchOffsetY >= outputHeight)
		return;

	dispatchWidth = std::min(dispatchWidth, outputWidthPerEye - dispatchOffsetX);
	dispatchHeight = std::min(dispatchHeight, outputHeight - dispatchOffsetY);
	if (!dispatchWidth || !dispatchHeight)
		return;

	const uint32_t rectMinX = rect.outputOffsetX;
	const uint32_t rectMinY = rect.outputOffsetY;
	const uint32_t rectMaxX = rect.outputOffsetX + rect.outputWidth;
	const uint32_t rectMaxY = rect.outputOffsetY + rect.outputHeight;

	const uint32_t dispatchMinX = std::max(dispatchOffsetX, rectMinX);
	const uint32_t dispatchMinY = std::max(dispatchOffsetY, rectMinY);
	const uint32_t dispatchMaxX = std::min(dispatchOffsetX + dispatchWidth, rectMaxX);
	const uint32_t dispatchMaxY = std::min(dispatchOffsetY + dispatchHeight, rectMaxY);
	if (dispatchMaxX <= dispatchMinX || dispatchMaxY <= dispatchMinY)
		return;

	const uint32_t actualDispatchWidth = dispatchMaxX - dispatchMinX;
	const uint32_t actualDispatchHeight = dispatchMaxY - dispatchMinY;
	const uint32_t sourceOffsetX = dispatchMinX - rectMinX;
	const uint32_t sourceOffsetY = dispatchMinY - rectMinY;

	FoveatedCenterBlendCB cbData{};
	cbData.invOutputDim = {
		outputWidthPerEye > 0 ? 1.0f / static_cast<float>(outputWidthPerEye) : 0.0f,
		outputHeight > 0 ? 1.0f / static_cast<float>(outputHeight) : 0.0f
	};
	cbData.centerScale = ClampFoveatedCenterArea(centerScale);
	cbData.centerFeather = std::isfinite(centerFeather) ? std::max(0.0f, centerFeather) : FoveatedCommon::kCenterFeather;
	cbData.centerOffset = centerOffset;
	cbData.outputOffset = { static_cast<float>(dispatchMinX), static_cast<float>(dispatchMinY) };
	cbData.dispatchDim = { static_cast<float>(actualDispatchWidth), static_cast<float>(actualDispatchHeight) };
	cbData.sourceOffset = { static_cast<float>(sourceOffsetX), static_cast<float>(sourceOffsetY) };
	cbData.invSourceDim = {
		rect.outputWidth > 0 ? 1.0f / static_cast<float>(rect.outputWidth) : 0.0f,
		rect.outputHeight > 0 ? 1.0f / static_cast<float>(rect.outputHeight) : 0.0f
	};
	cbData.centerHorizontalScale = ClampFoveatedCenterHorizontalScale(centerHorizontalScale);
	cbData.centerHorizontalScalePadding = 0.0f;
	foveatedCenterBlendCB->Update(cbData);

	ID3D11Buffer* cb = foveatedCenterBlendCB->CB();
	ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
	ID3D11ShaderResourceView* srvs[1] = { centerSRV };
	ID3D11UnorderedAccessView* uavs[1] = { outputUAV };

	context->CSSetShader(blendCS, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, 1, samplers);
	context->CSSetShaderResources(0, 1, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	auto state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent("Foveated Center Blend");
	context->Dispatch((actualDispatchWidth + 7u) >> 3, (actualDispatchHeight + 7u) >> 3, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11SamplerState* nullSampler[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetSamplers(0, 1, nullSampler);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetShader(nullptr, nullptr, 0);
}

bool Upscaling::DispatchSingleFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* depthIn, ID3D11Resource* motionVectorsIn, ID3D11Resource* reactiveMaskIn, ID3D11Resource* transparencyMaskIn, uint32_t outputWidthPerEye, uint32_t outputHeight, uint32_t inputWidthPerEye, uint32_t inputHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather, uint32_t colorInputBaseOffsetX, uint32_t depthInputBaseOffsetX, uint32_t auxInputBaseOffsetX)
{
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;

	const bool useDLSS = a_upscaleMethod == UpscaleMethod::kDLSS;
	const bool useFSR = a_upscaleMethod == UpscaleMethod::kFSR;

	if (eyeIndex > 1)
		return false;

	const auto& rect = foveatedRectCache.rects[eyeIndex];
	if (!rect.outputWidth || !rect.outputHeight || !rect.inputWidth || !rect.inputHeight)
		return false;

	const std::string suffix = eyeIndex == 0 ? "Left" : "Right";
	const bool createFsrViews = useFSR;

	if (!EnsureFoveatedTexture(foveatedCenterColorIn[eyeIndex], colorIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_ColorIn_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterColorOut[eyeIndex], colorIn, rect.outputWidth, rect.outputHeight, false, true, createFsrViews, false, ("Upscale_FoveatedCenter_ColorOut_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterDepth[eyeIndex], depthIn, rect.inputWidth, rect.inputHeight, true, createFsrViews, false, false, ("Upscale_FoveatedCenter_Depth_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterMotionVectors[eyeIndex], motionVectorsIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_MVec_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterReactiveMask[eyeIndex], reactiveMaskIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_Reactive_" + suffix).c_str()))
		return false;
	if (!EnsureFoveatedTexture(foveatedCenterTransparencyMask[eyeIndex], transparencyMaskIn, rect.inputWidth, rect.inputHeight, false, createFsrViews, false, false, ("Upscale_FoveatedCenter_Transparency_" + suffix).c_str()))
		return false;

	auto context = globals::d3d::context;
	if (!context)
		return false;

	D3D11_BOX colorSrcBox{
		colorInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		colorInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};
	D3D11_BOX depthSrcBox{
		depthInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		depthInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};
	D3D11_BOX auxSrcBox{
		auxInputBaseOffsetX + rect.inputOffsetX,
		rect.inputOffsetY,
		0u,
		auxInputBaseOffsetX + rect.inputOffsetX + rect.inputWidth,
		rect.inputOffsetY + rect.inputHeight,
		1u
	};

	context->CopySubresourceRegion(foveatedCenterColorIn[eyeIndex]->resource.get(), 0, 0, 0, 0, colorIn, 0, &colorSrcBox);
	context->CopySubresourceRegion(foveatedCenterDepth[eyeIndex]->resource.get(), 0, 0, 0, 0, depthIn, 0, &depthSrcBox);
	context->CopySubresourceRegion(foveatedCenterMotionVectors[eyeIndex]->resource.get(), 0, 0, 0, 0, motionVectorsIn, 0, &auxSrcBox);
	context->CopySubresourceRegion(foveatedCenterReactiveMask[eyeIndex]->resource.get(), 0, 0, 0, 0, reactiveMaskIn, 0, &auxSrcBox);
	context->CopySubresourceRegion(foveatedCenterTransparencyMask[eyeIndex]->resource.get(), 0, 0, 0, 0, transparencyMaskIn, 0, &auxSrcBox);

	const float outputWidthPerEyeF = std::max(1.0f, static_cast<float>(outputWidthPerEye));
	const float outputHeightF = std::max(1.0f, static_cast<float>(outputHeight));
	const float rectCenterX = (static_cast<float>(rect.outputOffsetX) + static_cast<float>(rect.outputWidth) * 0.5f) / outputWidthPerEyeF;
	const float rectCenterY = (static_cast<float>(rect.outputOffsetY) + static_cast<float>(rect.outputHeight) * 0.5f) / outputHeightF;
	const float pinholeOffsetX = std::clamp((rectCenterX - 0.5f) * 2.0f, -1.0f, 1.0f);
	// Texture-space Y grows downward, while clip-space Y grows upward.
	const float pinholeOffsetY = std::clamp((0.5f - rectCenterY) * 2.0f, -1.0f, 1.0f);

	bool dispatchOK = false;
	if (useDLSS) {
		dispatchOK = streamline.UpscaleRegion(
			eyeIndex,
			foveatedCenterColorIn[eyeIndex]->resource.get(),
			foveatedCenterColorOut[eyeIndex]->resource.get(),
			foveatedCenterDepth[eyeIndex]->resource.get(),
			foveatedCenterMotionVectors[eyeIndex]->resource.get(),
			foveatedCenterReactiveMask[eyeIndex]->resource.get(),
			foveatedCenterTransparencyMask[eyeIndex]->resource.get(),
			rect.inputWidth,
			rect.inputHeight,
			rect.outputWidth,
			rect.outputHeight,
			pinholeOffsetX,
			pinholeOffsetY);
	} else if (useFSR) {
		dispatchOK = fidelityFX.UpscaleRegion(
			eyeIndex,
			foveatedCenterColorIn[eyeIndex]->resource.get(),
			foveatedCenterDepth[eyeIndex]->resource.get(),
			foveatedCenterMotionVectors[eyeIndex]->resource.get(),
			foveatedCenterReactiveMask[eyeIndex]->resource.get(),
			foveatedCenterTransparencyMask[eyeIndex]->resource.get(),
			foveatedCenterColorOut[eyeIndex]->resource.get(),
			rect.inputWidth,
			rect.inputHeight,
			rect.outputWidth,
			rect.outputHeight,
			static_cast<float>(std::max(inputWidthPerEye, 1u)),
			static_cast<float>(std::max(inputHeight, 1u)),
			settings.sharpnessFSR);
	}
	if (!dispatchOK)
		return false;

	if (!foveatedCenterColorOut[eyeIndex] || !foveatedCenterColorOut[eyeIndex]->resource || !foveatedCenterColorOut[eyeIndex]->srv)
		return false;
	if (!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->uav || !vrIntermediateColorOut[eyeIndex]->resource)
		return false;

	const uint32_t rectMinX = rect.outputOffsetX;
	const uint32_t rectMinY = rect.outputOffsetY;
	const uint32_t rectMaxX = rect.outputOffsetX + rect.outputWidth;
	const uint32_t rectMaxY = rect.outputOffsetY + rect.outputHeight;

	ID3D11UnorderedAccessView* outputUAV = vrIntermediateColorOut[eyeIndex]->uav.get();
	ID3D11ShaderResourceView* centerSRV = foveatedCenterColorOut[eyeIndex]->srv.get();
	const float centerBlendFeather = std::isfinite(centerFeather) ?
		ClampPeripheryTAACenterBlendFeather(centerFeather) :
		ClampPeripheryTAACenterBlendFeather(FoveatedCommon::kCenterFeather);

	DispatchFoveatedBlendPass(
		centerSRV,
		outputUAV,
		outputWidthPerEye,
		outputHeight,
		rect,
		rectMinX,
		rectMinY,
		rectMaxX - rectMinX,
		rectMaxY - rectMinY,
		centerScale,
		centerHorizontalScale,
		centerOffset,
		centerBlendFeather);
	return true;
}

void Upscaling::ConfigureFoveatedPeripherySourceRegion(FoveatedEyeDispatchParams& params, const eastl::unique_ptr<Texture2D>& sourceTexture, uint32_t validWidth, uint32_t validHeight) const
{
	params.peripherySourceSRV = sourceTexture && sourceTexture->srv ? sourceTexture->srv.get() : nullptr;

	const uint32_t textureWidth = sourceTexture ? sourceTexture->desc.Width : 0u;
	const uint32_t textureHeight = sourceTexture ? sourceTexture->desc.Height : 0u;
	params.peripherySourceWidth = textureWidth ? textureWidth : validWidth;
	params.peripherySourceHeight = textureHeight ? textureHeight : validHeight;

	const auto sourceRegion = BuildTopLeftValidTextureRegion(validWidth, validHeight, params.peripherySourceWidth, params.peripherySourceHeight);
	params.peripherySourceScaleX = sourceRegion.scale.x;
	params.peripherySourceScaleY = sourceRegion.scale.y;
	params.peripherySourceOffsetX = sourceRegion.offset.x;
	params.peripherySourceOffsetY = sourceRegion.offset.y;
}

bool Upscaling::DispatchFoveatedVendorEyeComposite(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, const FoveatedEyeDispatchParams& params)
{
	if (!globals::game::isVR || eyeIndex >= 2)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;
	if (!params.inputWidthPerEye || !params.inputHeight || !params.outputWidthPerEye || !params.outputHeight)
		return false;
	if (!params.peripherySourceSRV || !params.peripherySourceWidth || !params.peripherySourceHeight)
		return false;

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !context || !deferred || !deferred->linearSampler)
		return false;

	if (!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->uav || !vrIntermediateColorOut[eyeIndex]->resource)
		return false;

	if (!params.visualizeMask &&
		(!params.centerColorInput || !params.centerDepthInput || !params.centerMotionVectorsInput ||
			!params.centerReactiveMaskInput || !params.centerTransparencyMaskInput)) {
		return false;
	}

	if (params.usePeripheryTAA) {
		if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->srv ||
			!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->srv ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->srv ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->srv ||
			!peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav ||
			!peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav ||
			!peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex] || !peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv ||
			!peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex] || !peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav) {
			return false;
		}
	}

	const float2 centerOffset = GetResolvedFoveatedMaskCenterOffset(eyeIndex, params.usePeripheryTAAProfile);
	const float taaOuterScale = params.usePeripheryTAA ? ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		params.centerScale,
		params.centerHorizontalScale,
		params.centerBlendFeather) :
		0.0f;
	const auto innerBounds = FoveatedCommon::BuildCenteredInscribedMaskRect(params.outputWidthPerEye, params.outputHeight, params.centerScale, centerOffset.x, centerOffset.y, params.centerHorizontalScale);
	const uint32_t innerMinX = static_cast<uint32_t>(innerBounds.minX);
	const uint32_t innerMaxX = static_cast<uint32_t>(innerBounds.maxX);
	const uint32_t innerMinY = static_cast<uint32_t>(innerBounds.minY);
	const uint32_t innerMaxY = static_cast<uint32_t>(innerBounds.maxY);
	const bool hasCenterInterior = innerMaxX > innerMinX && innerMaxY > innerMinY;
	constexpr uint32_t kCenterUnderlayHolePadding = 2u;
	const uint32_t underlayHoleMinX = hasCenterInterior ? std::min(innerMinX + kCenterUnderlayHolePadding, innerMaxX) : 0u;
	const uint32_t underlayHoleMinY = hasCenterInterior ? std::min(innerMinY + kCenterUnderlayHolePadding, innerMaxY) : 0u;
	const uint32_t underlayHoleMaxX = hasCenterInterior ? (innerMaxX > kCenterUnderlayHolePadding ? innerMaxX - kCenterUnderlayHolePadding : innerMinX) : 0u;
	const uint32_t underlayHoleMaxY = hasCenterInterior ? (innerMaxY > kCenterUnderlayHolePadding ? innerMaxY - kCenterUnderlayHolePadding : innerMinY) : 0u;
	const bool hasCenterUnderlayHole = underlayHoleMaxX > underlayHoleMinX && underlayHoleMaxY > underlayHoleMinY;
	const auto taaOuterBounds = FoveatedCommon::BuildCenteredDispatchBounds(0, params.outputWidthPerEye, params.outputHeight, taaOuterScale, centerOffset.x, centerOffset.y, 0.0f, params.centerHorizontalScale);
	const uint32_t taaOuterMinX = static_cast<uint32_t>(taaOuterBounds.minX);
	const uint32_t taaOuterMaxX = static_cast<uint32_t>(taaOuterBounds.maxX);
	const uint32_t taaOuterMinY = static_cast<uint32_t>(taaOuterBounds.minY);
	const uint32_t taaOuterMaxY = static_cast<uint32_t>(taaOuterBounds.maxY);
	const bool hasTaaOuterRegion = taaOuterMaxX > taaOuterMinX && taaOuterMaxY > taaOuterMinY;
	constexpr uint32_t kPeripheryHistoryPadding = 2u;
	const uint32_t taaHistoryMinX = hasTaaOuterRegion ? (taaOuterMinX > kPeripheryHistoryPadding ? taaOuterMinX - kPeripheryHistoryPadding : 0u) : 0u;
	const uint32_t taaHistoryMinY = hasTaaOuterRegion ? (taaOuterMinY > kPeripheryHistoryPadding ? taaOuterMinY - kPeripheryHistoryPadding : 0u) : 0u;
	const uint32_t taaHistoryMaxX = hasTaaOuterRegion ? std::min(params.outputWidthPerEye, taaOuterMaxX + kPeripheryHistoryPadding) : 0u;
	const uint32_t taaHistoryMaxY = hasTaaOuterRegion ? std::min(params.outputHeight, taaOuterMaxY + kPeripheryHistoryPadding) : 0u;
	const bool hasTaaHistoryRegion = taaHistoryMaxX > taaHistoryMinX && taaHistoryMaxY > taaHistoryMinY;
	const uint32_t taaDispatchMinX = hasTaaHistoryRegion ? taaHistoryMinX : taaOuterMinX;
	const uint32_t taaDispatchMinY = hasTaaHistoryRegion ? taaHistoryMinY : taaOuterMinY;
	const uint32_t taaDispatchMaxX = hasTaaHistoryRegion ? taaHistoryMaxX : taaOuterMaxX;
	const uint32_t taaDispatchMaxY = hasTaaHistoryRegion ? taaHistoryMaxY : taaOuterMaxY;

	float4x4 currentViewProjInverse{};
	float4x4 previousViewProj{};
	float4 currentCameraPosAdjust{};
	float4 previousCameraPosAdjust{};
	if (params.usePeripheryTAA) {
		currentViewProjInverse = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Invert();
		previousViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex);
		currentCameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
		previousCameraPosAdjust = globals::game::frameBufferCached.GetCameraPreviousPosAdjust(eyeIndex);
	}

	ID3D11UnorderedAccessView* outputColorUAV = vrIntermediateColorOut[eyeIndex]->uav.get();

	bool peripheryBindingsBound = false;
	auto bindPeripheryBindings = [&]() -> bool {
		if (peripheryBindingsBound)
			return true;

		auto* peripheryShader = GetFoveatedPeripheryCS();
		if (!peripheryShader || !foveatedPeripheryCB || !params.peripherySourceSRV || !outputColorUAV)
			return false;

		ID3D11Buffer* cb = foveatedPeripheryCB->CB();
		ID3D11SamplerState* samplers[1] = { deferred->linearSampler };
		ID3D11ShaderResourceView* srvs[1] = { params.peripherySourceSRV };
		ID3D11UnorderedAccessView* uavs[1] = { outputColorUAV };
		context->CSSetShader(peripheryShader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetSamplers(0, 1, samplers);
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		peripheryBindingsBound = true;
		return true;
	};

	auto unbindPeripheryBindings = [&]() {
		if (!peripheryBindingsBound)
			return;

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11SamplerState* nullSampler[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetSamplers(0, 1, nullSampler);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
		peripheryBindingsBound = false;
	};

	auto dispatchPeripheryBand = [&](uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		if (!dispatchWidth || !dispatchHeight)
			return true;
		if (!bindPeripheryBindings())
			return false;

		DispatchFoveatedPeripheryPass(
			params.peripherySourceSRV,
			outputColorUAV,
			params.peripherySourceWidth,
			params.peripherySourceHeight,
			params.outputWidthPerEye,
			params.outputHeight,
			outputOffsetX,
			outputOffsetY,
			dispatchWidth,
			dispatchHeight,
			params.centerScale,
			params.centerHorizontalScale,
			true,
			params.peripherySourceScaleX,
			params.peripherySourceScaleY,
			params.peripherySourceOffsetX,
			params.peripherySourceOffsetY,
			centerOffset.x,
			centerOffset.y);
		return true;
	};

	auto dispatchPeripheryTAA = [&](ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		DispatchPeripheryTAAPass(
			vrIntermediateColorIn[eyeIndex]->srv.get(),
			vrIntermediateDepth[eyeIndex]->srv.get(),
			vrIntermediateMotionVectors[eyeIndex]->srv.get(),
			vrIntermediateReactiveMask[eyeIndex]->srv.get(),
			vrIntermediateTransparencyMask[eyeIndex]->srv.get(),
			peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryReadIndex]->srv.get(),
			outputColorUAV,
			peripheryTAAHistoryColor[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			peripheryTAAVelocityHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			peripheryTAALockHistory[eyeIndex][params.peripheryTAAHistoryWriteIndex]->uav.get(),
			tileListSRV,
			tileCount,
			params.inputWidthPerEye,
			params.inputHeight,
			params.outputWidthPerEye,
			params.outputHeight,
			outputOffsetX,
			outputOffsetY,
			dispatchWidth,
			dispatchHeight,
			currentViewProjInverse,
			previousViewProj,
			currentCameraPosAdjust,
			previousCameraPosAdjust,
			params.resetPeripheryTAA,
			params.centerScale,
			params.centerHorizontalScale,
			centerOffset.x,
			centerOffset.y,
			params.peripherySourceScaleX,
			params.peripherySourceScaleY,
			params.peripherySourceOffsetX,
			params.peripherySourceOffsetY);
		return true;
	};

	auto dispatchPeripheryTAABand = [&](uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight) -> bool {
		if (!dispatchWidth || !dispatchHeight)
			return true;
		return dispatchPeripheryTAA(nullptr, 0, outputOffsetX, outputOffsetY, dispatchWidth, dispatchHeight);
	};

	auto dispatchRectMinusHole = [&](uint32_t outerMinX, uint32_t outerMinY, uint32_t outerMaxX, uint32_t outerMaxY, uint32_t holeMinX, uint32_t holeMinY, uint32_t holeMaxX, uint32_t holeMaxY, auto&& dispatchBand) -> bool {
		if (outerMaxX <= outerMinX || outerMaxY <= outerMinY)
			return true;

		const uint32_t clampedHoleMinX = std::clamp(holeMinX, outerMinX, outerMaxX);
		const uint32_t clampedHoleMaxX = std::clamp(holeMaxX, outerMinX, outerMaxX);
		const uint32_t clampedHoleMinY = std::clamp(holeMinY, outerMinY, outerMaxY);
		const uint32_t clampedHoleMaxY = std::clamp(holeMaxY, outerMinY, outerMaxY);
		const bool hasHole = clampedHoleMaxX > clampedHoleMinX && clampedHoleMaxY > clampedHoleMinY;
		if (!hasHole)
			return dispatchBand(outerMinX, outerMinY, outerMaxX - outerMinX, outerMaxY - outerMinY);

		const uint32_t outerWidth = outerMaxX - outerMinX;
		const uint32_t middleHeight = clampedHoleMaxY - clampedHoleMinY;
		return dispatchBand(outerMinX, outerMinY, outerWidth, clampedHoleMinY - outerMinY) &&
		       dispatchBand(outerMinX, clampedHoleMaxY, outerWidth, outerMaxY - clampedHoleMaxY) &&
		       dispatchBand(outerMinX, clampedHoleMinY, clampedHoleMinX - outerMinX, middleHeight) &&
		       dispatchBand(clampedHoleMaxX, clampedHoleMinY, outerMaxX - clampedHoleMaxX, middleHeight);
	};

	auto failAfterUnbind = [&]() {
		unbindPeripheryBindings();
		return false;
	};

	if (params.usePeripheryTAA) {
		if (hasTaaOuterRegion) {
			uint32_t tileCount = 0;
			const bool tileListBuilt = BuildPeripheryTAATileList(eyeIndex, params.outputWidthPerEye, params.outputHeight, params.centerScale, taaOuterScale, params.centerHorizontalScale, params.centerBlendFeather, centerOffset.x, centerOffset.y, kPeripheryHistoryPadding, tileCount);
			const bool hasTileListSRV = peripheryTAATileBuffer[eyeIndex] && peripheryTAATileBuffer[eyeIndex]->srv;
			if (tileListBuilt && tileCount > 0 && hasTileListSRV) {
				if (!dispatchPeripheryTAA(peripheryTAATileBuffer[eyeIndex]->srv.get(), tileCount, 0, 0, params.outputWidthPerEye, params.outputHeight))
					return false;
			} else if (!tileListBuilt || tileCount == 0 || (tileCount > 0 && !hasTileListSRV)) {
				if (state->frameAnnotations)
					state->BeginPerfEvent("Periphery TAA Fallback Rect");
				const bool fallbackDispatched = hasCenterUnderlayHole ?
					dispatchRectMinusHole(
						taaDispatchMinX,
						taaDispatchMinY,
						taaDispatchMaxX,
						taaDispatchMaxY,
						underlayHoleMinX,
						underlayHoleMinY,
						underlayHoleMaxX,
						underlayHoleMaxY,
						dispatchPeripheryTAABand) :
					dispatchPeripheryTAABand(taaDispatchMinX, taaDispatchMinY, taaDispatchMaxX - taaDispatchMinX, taaDispatchMaxY - taaDispatchMinY);
				if (state->frameAnnotations)
					state->EndPerfEvent();
				if (!fallbackDispatched)
					return failAfterUnbind();
			}

			if (!dispatchRectMinusHole(
					0,
					0,
					params.outputWidthPerEye,
					params.outputHeight,
					taaOuterMinX,
					taaOuterMinY,
					taaOuterMaxX,
					taaOuterMaxY,
					dispatchPeripheryBand)) {
				return failAfterUnbind();
			}
		} else if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight)) {
			return failAfterUnbind();
		}
	} else if (params.visualizeMask) {
		if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight))
			return failAfterUnbind();
	} else if (hasCenterUnderlayHole) {
		if (!dispatchRectMinusHole(
				0,
				0,
				params.outputWidthPerEye,
				params.outputHeight,
				underlayHoleMinX,
				underlayHoleMinY,
				underlayHoleMaxX,
				underlayHoleMaxY,
				dispatchPeripheryBand)) {
			return failAfterUnbind();
		}
	} else if (!dispatchPeripheryBand(0, 0, params.outputWidthPerEye, params.outputHeight)) {
		return failAfterUnbind();
	}

	unbindPeripheryBindings();

	if (params.visualizeMask)
		return true;

	return DispatchSingleFoveatedVendorEye(
		a_upscaleMethod,
		eyeIndex,
		params.centerColorInput,
		params.centerDepthInput,
		params.centerMotionVectorsInput,
		params.centerReactiveMaskInput,
		params.centerTransparencyMaskInput,
		params.outputWidthPerEye,
		params.outputHeight,
		params.inputWidthPerEye,
		params.inputHeight,
		params.centerScale,
		params.centerHorizontalScale,
		centerOffset,
		params.centerBlendFeather,
		params.centerColorInputBaseOffsetX,
		params.centerDepthInputBaseOffsetX,
		params.centerAuxInputBaseOffsetX);
}

bool Upscaling::DispatchFoveatedVendorUpscaling(UpscaleMethod a_upscaleMethod, ID3D11Resource* colorTexture, ID3D11Resource* depthTexture, ID3D11Resource* motionVectors, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask, ID3D11Resource* colorOutput)
{
	if (!globals::game::isVR)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;

	if (!colorTexture || !depthTexture || !motionVectors || !reactiveMask || !transparencyMask)
		return false;

	auto state = globals::state;
	if (!state)
		return false;

	auto renderSize = Util::ConvertToDynamic(state->screenSize);
	const uint32_t outputWidthPerEye = static_cast<uint32_t>(state->screenSize.x / 2.0f);
	const uint32_t outputHeight = static_cast<uint32_t>(state->screenSize.y);
	const uint32_t inputWidthPerEye = static_cast<uint32_t>(renderSize.x / 2.0f);
	const uint32_t inputHeight = static_cast<uint32_t>(renderSize.y);

	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool usePeripheryTAA = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(a_upscaleMethod);
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float centerScale = foveatedProfile.centerArea;
	const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float effectiveCenterBlendFeather = usePeripheryTAA ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	if (!BuildFoveatedDispatchRects(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, true, centerScale, effectiveCenterBlendFeather, centerHorizontalScale, usePeripheryTAAProfile))
		return false;

	auto* peripheryCS = GetFoveatedPeripheryCS();
	auto* peripheryTAA = usePeripheryTAA ? GetPeripheryTAACS() : nullptr;
	auto* blendCS = visualizeMask ? nullptr : GetFoveatedCenterBlendCS();
	if (!peripheryCS || !foveatedPeripheryCB ||
		(usePeripheryTAA && (!peripheryTAA || !peripheryTAACB)) ||
		(!visualizeMask && (!blendCS || !foveatedCenterBlendCB))) {
		return false;
	}

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	auto renderer = globals::game::renderer;
	if (!context || !deferred || !deferred->linearSampler || !renderer)
		return false;

	// Keep all foveated VR paths on per-eye inputs. The old DLAA/direct-source
	// shortcut, and the Peripheral TAA center pass, sampled kMAIN directly and
	// bypassed the HMD hidden-area cleanup from PreparePerEyeInputs.
	// Keep copyAuxiliaryInputs=false here: Encode Upscaling Textures has already
	// written the per-eye motion/reactive/transparency resources used by
	// Periphery TAA, and this pass must not overwrite them.
	PreparePerEyeInputs(colorTexture, depthTexture, motionVectors, reactiveMask, transparencyMask, false, true);
	if (usePeripheryTAA && !EnsurePeripheryTAAResources(outputWidthPerEye, outputHeight, colorTexture))
		return false;

	const bool resetPeripheryTAA = usePeripheryTAA && (ShouldResetHistoryThisFrame() || !peripheryTAAHistoryValid);
	const uint32_t peripheryTAAReadIndex = peripheryTAAHistoryReadIndex;
	const uint32_t peripheryTAAWriteIndex = 1u - peripheryTAAReadIndex;

	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!vrIntermediateColorIn[eye] || !vrIntermediateColorIn[eye]->srv || !vrIntermediateColorIn[eye]->resource ||
			!vrIntermediateColorOut[eye] || !vrIntermediateColorOut[eye]->uav || !vrIntermediateColorOut[eye]->resource) {
			return false;
		}

		ID3D11Resource* centerDepthInput = nullptr;
		if (!visualizeMask) {
			centerDepthInput = a_upscaleMethod == UpscaleMethod::kFSR ?
				(vrIntermediateLinearDepth[eye] ? vrIntermediateLinearDepth[eye]->resource.get() : nullptr) :
				(vrIntermediateDepth[eye] ? vrIntermediateDepth[eye]->resource.get() : nullptr);
			if (!centerDepthInput)
				return false;
		}

		FoveatedEyeDispatchParams params{};
		params.inputWidthPerEye = inputWidthPerEye;
		params.inputHeight = inputHeight;
		params.outputWidthPerEye = outputWidthPerEye;
		params.outputHeight = outputHeight;
		params.centerScale = centerScale;
		params.centerHorizontalScale = centerHorizontalScale;
		params.centerBlendFeather = effectiveCenterBlendFeather;
		params.usePeripheryTAA = usePeripheryTAA;
		params.usePeripheryTAAProfile = usePeripheryTAAProfile;
		params.visualizeMask = visualizeMask;
		params.resetPeripheryTAA = resetPeripheryTAA;
		params.peripheryTAAHistoryReadIndex = peripheryTAAReadIndex;
		params.peripheryTAAHistoryWriteIndex = peripheryTAAWriteIndex;
		ConfigureFoveatedPeripherySourceRegion(params, vrIntermediateColorIn[eye], inputWidthPerEye, inputHeight);
		params.centerColorInput = vrIntermediateColorIn[eye]->resource.get();
		params.centerDepthInput = centerDepthInput;
		params.centerMotionVectorsInput = vrIntermediateMotionVectors[eye] ? vrIntermediateMotionVectors[eye]->resource.get() : nullptr;
		params.centerReactiveMaskInput = vrIntermediateReactiveMask[eye] ? vrIntermediateReactiveMask[eye]->resource.get() : nullptr;
		params.centerTransparencyMaskInput = vrIntermediateTransparencyMask[eye] ? vrIntermediateTransparencyMask[eye]->resource.get() : nullptr;

		if (!DispatchFoveatedVendorEyeComposite(a_upscaleMethod, eye, params))
			return false;
	}

	if (usePeripheryTAA) {
		peripheryTAAHistoryReadIndex = peripheryTAAWriteIndex;
		peripheryTAAHistoryValid = true;
	}

	FinalizePerEyeOutputs(colorOutput ? colorOutput : colorTexture);
	return true;
}

bool Upscaling::DispatchSubmitStageFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight)
{
	if (!globals::game::isVR || eyeIndex >= 2)
		return false;
	if (!SupportsFoveatedVendorDispatch(a_upscaleMethod))
		return false;
	if (!inputWidthPerEye || !inputHeight || !outputWidthPerEye || !outputHeight)
		return false;

	auto state = globals::state;
	auto deferred = globals::deferred;
	auto renderer = globals::game::renderer;
	if (!state || !deferred || !deferred->linearSampler || !renderer)
		return false;

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource || !vrIntermediateColorIn[eyeIndex]->srv ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource || !vrIntermediateColorOut[eyeIndex]->uav) {
		return false;
	}

	const bool visualizeMask = settings.foveatedPeripheryMaskVisualization;
	const bool usePeripheryTAA = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(a_upscaleMethod);
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
	const float centerScale = foveatedProfile.centerArea;
	const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float effectiveCenterBlendFeather = usePeripheryTAA ? ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather) : FoveatedCommon::kCenterFeather;
	if (!BuildFoveatedDispatchRects(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight, true, centerScale, effectiveCenterBlendFeather, centerHorizontalScale, usePeripheryTAAProfile))
		return false;

	auto* peripheryCS = GetFoveatedPeripheryCS();
	auto* peripheryTAA = usePeripheryTAA ? GetPeripheryTAACS() : nullptr;
	auto* blendCS = visualizeMask ? nullptr : GetFoveatedCenterBlendCS();
	if (!peripheryCS || !foveatedPeripheryCB ||
		(usePeripheryTAA && (!peripheryTAA || !peripheryTAACB)) ||
		(!visualizeMask && (!blendCS || !foveatedCenterBlendCB))) {
		return false;
	}

	if (!visualizeMask) {
		if (!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->resource || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->resource ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->resource ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->resource) {
			return false;
		}
		if (a_upscaleMethod == UpscaleMethod::kFSR &&
			(!vrIntermediateLinearDepth[eyeIndex] || !vrIntermediateLinearDepth[eyeIndex]->resource)) {
			return false;
		}
	}

	if (usePeripheryTAA) {
		if (!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->srv ||
			!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->srv ||
			!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->srv ||
			!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->srv) {
			return false;
		}
		if (!EnsurePeripheryTAAResources(outputWidthPerEye, outputHeight, vrIntermediateColorOut[eyeIndex]->resource.get()))
			return false;
	}

	const bool resetPeripheryTAA = usePeripheryTAA && (ShouldResetHistoryThisFrame() || !peripheryTAAHistoryValid);
	const uint32_t peripheryTAAReadIndex = peripheryTAAHistoryReadIndex;
	const uint32_t peripheryTAAWriteIndex = 1u - peripheryTAAReadIndex;

	ID3D11Resource* centerDepthInput = nullptr;
	if (!visualizeMask) {
		centerDepthInput = a_upscaleMethod == UpscaleMethod::kFSR ?
			(vrIntermediateLinearDepth[eyeIndex] ? vrIntermediateLinearDepth[eyeIndex]->resource.get() : nullptr) :
			(vrIntermediateDepth[eyeIndex] ? vrIntermediateDepth[eyeIndex]->resource.get() : nullptr);
		if (!centerDepthInput)
			return false;
	}

	FoveatedEyeDispatchParams params{};
	params.inputWidthPerEye = inputWidthPerEye;
	params.inputHeight = inputHeight;
	params.outputWidthPerEye = outputWidthPerEye;
	params.outputHeight = outputHeight;
	params.centerScale = centerScale;
	params.centerHorizontalScale = centerHorizontalScale;
	params.centerBlendFeather = effectiveCenterBlendFeather;
	params.usePeripheryTAA = usePeripheryTAA;
	params.usePeripheryTAAProfile = usePeripheryTAAProfile;
	params.visualizeMask = visualizeMask;
	params.resetPeripheryTAA = resetPeripheryTAA;
	params.peripheryTAAHistoryReadIndex = peripheryTAAReadIndex;
	params.peripheryTAAHistoryWriteIndex = peripheryTAAWriteIndex;
	ConfigureFoveatedPeripherySourceRegion(params, vrIntermediateColorIn[eyeIndex], inputWidthPerEye, inputHeight);
	params.centerColorInput = vrIntermediateColorIn[eyeIndex]->resource.get();
	params.centerDepthInput = centerDepthInput;
	params.centerMotionVectorsInput = vrIntermediateMotionVectors[eyeIndex] ? vrIntermediateMotionVectors[eyeIndex]->resource.get() : nullptr;
	params.centerReactiveMaskInput = vrIntermediateReactiveMask[eyeIndex] ? vrIntermediateReactiveMask[eyeIndex]->resource.get() : nullptr;
	params.centerTransparencyMaskInput = vrIntermediateTransparencyMask[eyeIndex] ? vrIntermediateTransparencyMask[eyeIndex]->resource.get() : nullptr;

	if (!DispatchFoveatedVendorEyeComposite(a_upscaleMethod, eyeIndex, params))
		return false;

	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (depthTexture.depthSRV) {
		ClearHMDMask(
			vrIntermediateColorOut[eyeIndex]->uav.get(),
			depthTexture.depthSRV,
			inputWidthPerEye,
			inputHeight,
			outputWidthPerEye,
			outputHeight,
			eyeIndex == 1 ? inputWidthPerEye : 0u,
			0u);
	}

	if (usePeripheryTAA) {
		const uint32_t currentFrame = state->frameCount;
		if (submitStageFoveatedPeripheryTAAFrame != currentFrame) {
			submitStageFoveatedPeripheryTAAFrame = currentFrame;
			submitStageFoveatedPeripheryTAAEyeReady = {};
		}

		submitStageFoveatedPeripheryTAAEyeReady[eyeIndex] = true;
		if (submitStageFoveatedPeripheryTAAEyeReady[0] && submitStageFoveatedPeripheryTAAEyeReady[1]) {
			peripheryTAAHistoryReadIndex = peripheryTAAWriteIndex;
			peripheryTAAHistoryValid = true;
			submitStageFoveatedPeripheryTAAEyeReady = {};
		}
	}

	return true;
}

void Upscaling::CreateVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc)
{
	// All buffers are per-eye: Streamline validates all extents against the input color texture
	// dimensions, so every tagged resource must be isolated per-eye at {0,0}.
	D3D11_TEXTURE2D_DESC colorSrcDesc{};
	static_cast<ID3D11Texture2D*>(colorSrc)->GetDesc(&colorSrcDesc);
	const bool submitStageActive = IsSubmitStageUpscalingActive();
	const DXGI_FORMAT colorOutFormat = submitStageActive ?
	                                       DXGI_FORMAT_R8G8B8A8_UNORM :
	                                       colorSrcDesc.Format;
	const bool requiresColorOutRTV = submitStageActive;
	const uint32_t allocationInWidth = GetStableSubmitStageInputDimension(inWidth, outWidth);
	const uint32_t allocationInHeight = GetStableSubmitStageInputDimension(inHeight, outHeight);

	for (int i = 0; i < 2; i++) {
		std::string suffix = (i == 0) ? "Left" : "Right";

		vrIntermediateColorIn[i] = CreateTextureFromSource(colorSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_ColorIn_" + suffix).c_str());
		vrIntermediateColorOut[i] =
			colorOutFormat == colorSrcDesc.Format ?
				CreateTextureFromSource(colorSrc, outWidth, outHeight, false, true, true, ("Upscale_ColorOut_" + suffix).c_str(), requiresColorOutRTV) :
				CreateNamedTexture2D(outWidth, outHeight, colorOutFormat, true, true, requiresColorOutRTV, ("Upscale_ColorOut_" + suffix).c_str());

		// Depth copy: R24G8_TYPELESS matches the game's D24S8 typeless cast-group.
		// This avoids format-group copy failures on some drivers.
		{
			D3D11_TEXTURE2D_DESC depthDesc = {};
			depthDesc.Width = allocationInWidth;
			depthDesc.Height = allocationInHeight;
			depthDesc.MipLevels = 1;
			depthDesc.ArraySize = 1;
			depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
			depthDesc.SampleDesc.Count = 1;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			vrIntermediateDepth[i] = eastl::make_unique<Texture2D>(depthDesc);

			Util::SetResourceName(vrIntermediateDepth[i]->resource.get(), ("Upscale_Depth_" + suffix).c_str());

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			vrIntermediateDepth[i]->CreateSRV(srvDesc);
		}

		// FSR input depth: typed R32_FLOAT so FidelityFX receives a known surface format.
		{
			D3D11_TEXTURE2D_DESC linearDepthDesc = {};
			linearDepthDesc.Width = allocationInWidth;
			linearDepthDesc.Height = allocationInHeight;
			linearDepthDesc.MipLevels = 1;
			linearDepthDesc.ArraySize = 1;
			linearDepthDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearDepthDesc.SampleDesc.Count = 1;
			linearDepthDesc.Usage = D3D11_USAGE_DEFAULT;
			linearDepthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			vrIntermediateLinearDepth[i] = eastl::make_unique<Texture2D>(linearDepthDesc);

			Util::SetResourceName(vrIntermediateLinearDepth[i]->resource.get(), ("Upscale_LinearDepth_" + suffix).c_str());

			D3D11_SHADER_RESOURCE_VIEW_DESC linearSRVDesc = {};
			linearSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			linearSRVDesc.Texture2D.MipLevels = 1;
			vrIntermediateLinearDepth[i]->CreateSRV(linearSRVDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC linearUAVDesc = {};
			linearUAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			linearUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			linearUAVDesc.Texture2D.MipSlice = 0;
			vrIntermediateLinearDepth[i]->CreateUAV(linearUAVDesc);
		}

		vrIntermediateMotionVectors[i] = CreateTextureFromSource(mvecSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_MVec_" + suffix).c_str());
		vrIntermediateReactiveMask[i] = CreateTextureFromSource(reactiveSrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_Reactive_" + suffix).c_str());
		vrIntermediateTransparencyMask[i] = CreateTextureFromSource(transparencySrc, allocationInWidth, allocationInHeight, false, true, true, ("Upscale_Transparency_" + suffix).c_str());
	}

	logger::info("[Upscaling] Created VR intermediate textures: per-eye in {}x{}, out {}x{}",
		inWidth, inHeight, outWidth, outHeight);
}

void Upscaling::EnsureVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
	ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc)
{
	D3D11_TEXTURE2D_DESC colorSrcDesc{};
	static_cast<ID3D11Texture2D*>(colorSrc)->GetDesc(&colorSrcDesc);
	D3D11_TEXTURE2D_DESC mvecSrcDesc{};
	static_cast<ID3D11Texture2D*>(mvecSrc)->GetDesc(&mvecSrcDesc);
	D3D11_TEXTURE2D_DESC reactiveSrcDesc{};
	static_cast<ID3D11Texture2D*>(reactiveSrc)->GetDesc(&reactiveSrcDesc);
	D3D11_TEXTURE2D_DESC transparencySrcDesc{};
	static_cast<ID3D11Texture2D*>(transparencySrc)->GetDesc(&transparencySrcDesc);
	const bool submitStageActive = IsSubmitStageUpscalingActive();
	const DXGI_FORMAT expectedColorOutFormat = submitStageActive ?
	                                               DXGI_FORMAT_R8G8B8A8_UNORM :
	                                               colorSrcDesc.Format;
	const bool requiresColorOutRTV = submitStageActive;
	const uint32_t allocationInWidth = GetStableSubmitStageInputDimension(inWidth, outWidth);
	const uint32_t allocationInHeight = GetStableSubmitStageInputDimension(inHeight, outHeight);
	const auto coversInput = [allocationInWidth, allocationInHeight](const eastl::unique_ptr<Texture2D>& texture, DXGI_FORMAT format, bool requireUAV) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       (!requireUAV || texture->uav) &&
		       texture->desc.Width >= allocationInWidth &&
		       texture->desc.Height >= allocationInHeight &&
		       texture->desc.Format == format;
	};
	const auto matchesOutput = [outWidth, outHeight, expectedColorOutFormat, requiresColorOutRTV](const eastl::unique_ptr<Texture2D>& texture) {
		return texture &&
		       texture->resource &&
		       texture->srv &&
		       texture->uav &&
		       (!requiresColorOutRTV || texture->rtv) &&
		       texture->desc.Width == outWidth &&
		       texture->desc.Height == outHeight &&
		       texture->desc.Format == expectedColorOutFormat;
	};

	bool hasAllIntermediates =
		vrIntermediateColorIn[0] && vrIntermediateColorIn[1] &&
		vrIntermediateColorOut[0] && vrIntermediateColorOut[1] &&
		vrIntermediateDepth[0] && vrIntermediateDepth[1] &&
		vrIntermediateLinearDepth[0] && vrIntermediateLinearDepth[1] &&
		vrIntermediateMotionVectors[0] && vrIntermediateMotionVectors[1] &&
		vrIntermediateReactiveMask[0] && vrIntermediateReactiveMask[1] &&
		vrIntermediateTransparencyMask[0] && vrIntermediateTransparencyMask[1];

	bool needsRecreate = !hasAllIntermediates;
	uint32_t currentInWidth = 0;
	uint32_t currentInHeight = 0;
	uint32_t currentOutWidth = 0;
	uint32_t currentOutHeight = 0;
	bool currentHasRequiredViews = false;
	if (!needsRecreate) {
		currentInWidth = vrIntermediateColorIn[0]->desc.Width;
		currentInHeight = vrIntermediateColorIn[0]->desc.Height;
		currentOutWidth = vrIntermediateColorOut[0]->desc.Width;
		currentOutHeight = vrIntermediateColorOut[0]->desc.Height;
		currentHasRequiredViews = true;
		for (uint32_t eye = 0; eye < 2 && !needsRecreate; ++eye) {
			const bool eyeHasRequiredViews =
				coversInput(vrIntermediateColorIn[eye], colorSrcDesc.Format, true) &&
				matchesOutput(vrIntermediateColorOut[eye]) &&
				coversInput(vrIntermediateDepth[eye], DXGI_FORMAT_R24G8_TYPELESS, false) &&
				coversInput(vrIntermediateLinearDepth[eye], DXGI_FORMAT_R32_FLOAT, true) &&
				coversInput(vrIntermediateMotionVectors[eye], mvecSrcDesc.Format, true) &&
				coversInput(vrIntermediateReactiveMask[eye], reactiveSrcDesc.Format, true) &&
				coversInput(vrIntermediateTransparencyMask[eye], transparencySrcDesc.Format, true);
			currentHasRequiredViews = currentHasRequiredViews && eyeHasRequiredViews;
			needsRecreate = !eyeHasRequiredViews;
		}
	}

	if (needsRecreate) {
		if (MatchesVRIntermediateTextureCache(cachedVRIntermediateTextures, inWidth, inHeight, outWidth, outHeight)) {
			logger::info("[Upscaling] Reusing cached VR intermediates: per-eye in {}x{}, out {}x{}",
				inWidth, inHeight, outWidth, outHeight);

			for (uint32_t i = 0; i < 2; ++i) {
				if (hasAllIntermediates && currentHasRequiredViews) {
					std::swap(vrIntermediateColorIn[i], cachedVRIntermediateTextures.colorIn[i]);
					std::swap(vrIntermediateColorOut[i], cachedVRIntermediateTextures.colorOut[i]);
					std::swap(vrIntermediateDepth[i], cachedVRIntermediateTextures.depth[i]);
					std::swap(vrIntermediateLinearDepth[i], cachedVRIntermediateTextures.linearDepth[i]);
					std::swap(vrIntermediateMotionVectors[i], cachedVRIntermediateTextures.motionVectors[i]);
					std::swap(vrIntermediateReactiveMask[i], cachedVRIntermediateTextures.reactiveMask[i]);
					std::swap(vrIntermediateTransparencyMask[i], cachedVRIntermediateTextures.transparencyMask[i]);
				} else {
					vrIntermediateColorIn[i] = std::move(cachedVRIntermediateTextures.colorIn[i]);
					vrIntermediateColorOut[i] = std::move(cachedVRIntermediateTextures.colorOut[i]);
					vrIntermediateDepth[i] = std::move(cachedVRIntermediateTextures.depth[i]);
					vrIntermediateLinearDepth[i] = std::move(cachedVRIntermediateTextures.linearDepth[i]);
					vrIntermediateMotionVectors[i] = std::move(cachedVRIntermediateTextures.motionVectors[i]);
					vrIntermediateReactiveMask[i] = std::move(cachedVRIntermediateTextures.reactiveMask[i]);
					vrIntermediateTransparencyMask[i] = std::move(cachedVRIntermediateTextures.transparencyMask[i]);
				}
			}

			if (hasAllIntermediates && currentHasRequiredViews) {
				cachedVRIntermediateTextures.inWidth = currentInWidth;
				cachedVRIntermediateTextures.inHeight = currentInHeight;
				cachedVRIntermediateTextures.outWidth = currentOutWidth;
				cachedVRIntermediateTextures.outHeight = currentOutHeight;
			} else {
				ClearVRIntermediateTextureCache(cachedVRIntermediateTextures);
			}
			return;
		}

		if (hasAllIntermediates && currentHasRequiredViews) {
			for (uint32_t i = 0; i < 2; ++i) {
				cachedVRIntermediateTextures.colorIn[i] = std::move(vrIntermediateColorIn[i]);
				cachedVRIntermediateTextures.colorOut[i] = std::move(vrIntermediateColorOut[i]);
				cachedVRIntermediateTextures.depth[i] = std::move(vrIntermediateDepth[i]);
				cachedVRIntermediateTextures.linearDepth[i] = std::move(vrIntermediateLinearDepth[i]);
				cachedVRIntermediateTextures.motionVectors[i] = std::move(vrIntermediateMotionVectors[i]);
				cachedVRIntermediateTextures.reactiveMask[i] = std::move(vrIntermediateReactiveMask[i]);
				cachedVRIntermediateTextures.transparencyMask[i] = std::move(vrIntermediateTransparencyMask[i]);
			}
			cachedVRIntermediateTextures.inWidth = currentInWidth;
			cachedVRIntermediateTextures.inHeight = currentInHeight;
			cachedVRIntermediateTextures.outWidth = currentOutWidth;
			cachedVRIntermediateTextures.outHeight = currentOutHeight;
		}

		logger::info("[Upscaling] (Re)creating VR intermediates: per-eye in {}x{}, out {}x{}",
			inWidth, inHeight, outWidth, outHeight);
		CreateVRIntermediateTextures(inWidth, inHeight, outWidth, outHeight, colorSrc, mvecSrc, reactiveSrc, transparencySrc);
	}
}

void Upscaling::PreparePerEyeInputs(ID3D11Resource* colorSrc, ID3D11Resource* depthSrc, ID3D11Resource* mvecSrc,
	ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, bool copyAuxiliaryInputs, bool copyDepthInput)
{
	if (!globals::game::isVR)
		return;

	auto state = globals::state;
	if (state->frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Prepare");

	auto context = globals::d3d::context;
	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;
	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;

	EnsureVRIntermediateTextures(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, colorSrc, mvecSrc, reactiveSrc, transparencySrc);

	// Extract both eyes' required inputs from combined stereo buffers.
	// Reactive / transparency / encoded motion vectors can be pre-generated directly per-eye by the encode pass.
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t offsetXIn = (i == 1) ? eyeWidthIn : 0;
		D3D11_BOX srcBox = { offsetXIn, 0, 0, offsetXIn + eyeWidthIn, eyeHeightIn, 1 };

		context->CopySubresourceRegion(vrIntermediateColorIn[i]->resource.get(), 0, 0, 0, 0, colorSrc, 0, &srcBox);
		if (copyDepthInput)
			context->CopySubresourceRegion(vrIntermediateDepth[i]->resource.get(), 0, 0, 0, 0, depthSrc, 0, &srcBox);
		if (copyAuxiliaryInputs) {
			context->CopySubresourceRegion(vrIntermediateMotionVectors[i]->resource.get(), 0, 0, 0, 0, mvecSrc, 0, &srcBox);
			context->CopySubresourceRegion(vrIntermediateTransparencyMask[i]->resource.get(), 0, 0, 0, 0, transparencySrc, 0, &srcBox);
			context->CopySubresourceRegion(vrIntermediateReactiveMask[i]->resource.get(), 0, 0, 0, 0, reactiveSrc, 0, &srcBox);
		}
	}

	// Zero color in the HMD hidden area, including a tiny mask-edge expansion,
	// in each per-eye buffer before temporal reuse.
	// Bind CS/SRV/CB once for both eyes to reduce per-frame CPU overhead.
	auto& depthTexture = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (!vrClearHMDMaskCS) {
		vrClearHMDMaskCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/ClearHMDMaskCS.hlsl", {}, "cs_5_0"));

		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth = 32;  // 8 uints
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = 0;
		DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&cbDesc, nullptr, vrClearHMDMaskCB.put()));
	}

	if (vrClearHMDMaskCS && vrClearHMDMaskCB) {
		auto dispatchX = (eyeWidthIn + 7) / 8;
		auto dispatchY = (eyeHeightIn + 7) / 8;

		context->CSSetShader(vrClearHMDMaskCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthTexture.depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11Buffer* cbs[1] = { vrClearHMDMaskCB.get() };
		context->CSSetConstantBuffers(0, 1, cbs);

		for (uint32_t i = 0; i < 2; ++i) {
			uint32_t depthOffset = (i == 1) ? eyeWidthIn : 0;
			uint32_t clearMaskParams[8] = {
				depthOffset,
				0,
				0,
				0,
				eyeWidthIn,
				eyeHeightIn,
				eyeWidthIn,
				eyeHeightIn
			};
			context->UpdateSubresource(vrClearHMDMaskCB.get(), 0, nullptr, clearMaskParams, 0, 0);

			ID3D11UnorderedAccessView* uavs[1] = { vrIntermediateColorIn[i]->uav.get() };
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}

void Upscaling::FinalizePerEyeOutputs(ID3D11Resource* colorDst)
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VR Upscaling - Finalize Per Eye");

	if (!globals::game::isVR)
		return;

	auto state = globals::state;
	if (state->frameAnnotations)
		state->BeginPerfEvent("VR Upscaling Finalize");

	auto context = globals::d3d::context;
	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
	uint32_t eyeHeightOut = (uint32_t)screenSize.y;
	uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
	uint32_t eyeHeightIn = (uint32_t)renderSize.y;

	// Final display-color scrub only. Periphery TAA history, velocity, and lock
	// resources are left untouched so the temporal path remains active.
	auto renderer = globals::game::renderer;
	if (renderer) {
		auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (depthTexture.depthSRV) {
			for (uint32_t i = 0; i < 2; ++i) {
				if (!vrIntermediateColorOut[i] || !vrIntermediateColorOut[i]->uav)
					continue;

				ClearHMDMask(
					vrIntermediateColorOut[i]->uav.get(),
					depthTexture.depthSRV,
					eyeWidthIn,
					eyeHeightIn,
					eyeWidthOut,
					eyeHeightOut,
					i == 1 ? eyeWidthIn : 0u,
					0u);
			}
		}
	}

	// Write upscaled outputs back
	for (uint32_t i = 0; i < 2; ++i) {
		uint32_t offsetXOut = (i == 1) ? eyeWidthOut : 0;
		D3D11_BOX outBox = { 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
		context->CopySubresourceRegion(colorDst, 0, offsetXOut, 0, 0, vrIntermediateColorOut[i]->resource.get(), 0, &outBox);
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}

bool Upscaling::EncodeSubmitStageVRInputs(ID3D11Resource* colorSource, ID3D11Resource* motionVectors, ID3D11Resource* depthSource,
	uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight)
{
	if (!globals::game::isVR || !colorSource || !motionVectors || !depthSource || !inputWidthPerEye || !inputHeight || !outputWidthPerEye || !outputHeight)
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context || !globals::deferred || !upscalingDataCB || !reactiveMaskTexture || !transparencyCompositionMaskTexture)
		return false;

	if (!reactiveMaskTexture->resource || !reactiveMaskTexture->uav ||
		!transparencyCompositionMaskTexture->resource || !transparencyCompositionMaskTexture->uav)
		return false;

	auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
	auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
	auto& sourceMotionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	const auto upscaleMethod = GetUpscaleMethod();
	auto* encodeShader = GetEncodeTexturesCS();
	if (!temporalAAMask.SRV || !normals.SRV || !sourceMotionVector.SRV || !depth.depthSRV || !encodeShader)
		return false;

	try {
		EnsureVRIntermediateTextures(inputWidthPerEye, inputHeight, outputWidthPerEye, outputHeight,
			colorSource, motionVectors, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get());
	} catch (const std::exception& e) {
		logger::warn("[Upscaling] Submit-stage vendor upscaling failed to create intermediates: {}", e.what());
		return false;
	} catch (...) {
		logger::warn("[Upscaling] Submit-stage vendor upscaling failed to create intermediates.");
		return false;
	}

	for (uint32_t eye = 0; eye < 2; ++eye) {
		if (!vrIntermediateDepth[eye] || !vrIntermediateDepth[eye]->resource ||
			(upscaleMethod == UpscaleMethod::kFSR && (!vrIntermediateLinearDepth[eye] || !vrIntermediateLinearDepth[eye]->resource || !vrIntermediateLinearDepth[eye]->uav)) ||
			!vrIntermediateMotionVectors[eye] || !vrIntermediateMotionVectors[eye]->uav ||
			!vrIntermediateReactiveMask[eye] || !vrIntermediateReactiveMask[eye]->uav ||
			!vrIntermediateTransparencyMask[eye] || !vrIntermediateTransparencyMask[eye]->uav) {
			return false;
		}
	}

	if (state->frameAnnotations)
		state->BeginPerfEvent("Submit Stage Encode Upscaling Inputs");

	ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, sourceMotionVector.SRV, depth.depthSRV };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	auto upscalingBuffer = upscalingDataCB->CB();
	context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
	context->CSSetShader(encodeShader, nullptr, 0);

	const float2 renderSize = { static_cast<float>(inputWidthPerEye * 2), static_cast<float>(inputHeight) };
	const uint32_t dispatchX = (inputWidthPerEye + 7u) >> 3;
	const uint32_t dispatchY = (inputHeight + 7u) >> 3;

	for (uint32_t eye = 0; eye < 2; ++eye) {
		UpscalingDataCB upscalingData{};
		upscalingData.dispatchDim = { static_cast<float>(inputWidthPerEye), static_cast<float>(inputHeight) };
		upscalingData.trueSamplingDim = renderSize;
		upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
		upscalingData.seamCenterX = renderSize.x * 0.5f;
		upscalingData.seamHalfWidthPx = 2.0f;
		upscalingData.maskDepthThreshold = 1e-6f;
		upscalingData.vrSeamHardening = 1.0f;
		upscalingData.sourceOffset = { static_cast<float>(eye * inputWidthPerEye), 0.0f };
		upscalingData.outputOffset = { 0.0f, 0.0f };
		upscalingDataCB->Update(upscalingData);

		ID3D11UnorderedAccessView* uavs[4] = {
			vrIntermediateReactiveMask[eye]->uav.get(),
			vrIntermediateTransparencyMask[eye]->uav.get(),
			vrIntermediateMotionVectors[eye]->uav.get(),
			upscaleMethod == UpscaleMethod::kFSR ? vrIntermediateLinearDepth[eye]->uav.get() : nullptr
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
		context->Dispatch(dispatchX, dispatchY, 1);

		const uint32_t offsetX = eye * inputWidthPerEye;
		D3D11_BOX srcBox{ offsetX, 0, 0, offsetX + inputWidthPerEye, inputHeight, 1 };
		context->CopySubresourceRegion(vrIntermediateDepth[eye]->resource.get(), 0, 0, 0, 0, depthSource, 0, &srcBox);
	}

	ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);

	ID3D11UnorderedAccessView* nullUAV[4] = { nullptr, nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);

	ID3D11Buffer* nullBuffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullBuffer);
	context->CSSetShader(nullptr, nullptr, 0);

	if (state->frameAnnotations)
		state->EndPerfEvent();

	return true;
}

bool Upscaling::StretchSubmitStageEyeOutput(uint32_t eyeIndex, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight)
{
	if (eyeIndex >= 2 || !inputWidth || !inputHeight || !outputWidth || !outputHeight)
		return false;

	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!context || !deferred || !deferred->linearSampler || !dynamicResolutionStretchCB)
		return false;

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource || !vrIntermediateColorIn[eyeIndex]->srv ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource || !vrIntermediateColorOut[eyeIndex]->uav)
		return false;

	auto* stretchCS = GetSubmitStageStretchCS();
	if (!stretchCS) {
		float clearColor[4] = {};
		context->ClearUnorderedAccessViewFloat(vrIntermediateColorOut[eyeIndex]->uav.get(), clearColor);

		D3D11_TEXTURE2D_DESC inputDesc{};
		D3D11_TEXTURE2D_DESC outputDesc{};
		if (TryGetTexture2DDesc(vrIntermediateColorIn[eyeIndex]->resource.get(), inputDesc) &&
			TryGetTexture2DDesc(vrIntermediateColorOut[eyeIndex]->resource.get(), outputDesc) &&
			inputDesc.Format == outputDesc.Format) {
			D3D11_BOX copyBox{
				0,
				0,
				0,
				std::min(inputWidth, outputWidth),
				std::min(inputHeight, outputHeight),
				1
			};
			context->CopySubresourceRegion(vrIntermediateColorOut[eyeIndex]->resource.get(), 0, 0, 0, 0,
				vrIntermediateColorIn[eyeIndex]->resource.get(), 0, &copyBox);
		}

		static bool loggedEmergencyFallback[2] = {};
		if (!loggedEmergencyFallback[eyeIndex]) {
			logger::warn(
				"[Upscaling] Submit-stage fallback shader unavailable for eye {}; returning a full-size emergency fallback texture.",
				eyeIndex);
			loggedEmergencyFallback[eyeIndex] = true;
		}
		return true;
	}

	ID3D11ComputeShader* previousCS = nullptr;
	ID3D11ShaderResourceView* previousSRV = nullptr;
	ID3D11UnorderedAccessView* previousUAV = nullptr;
	ID3D11Buffer* previousCB = nullptr;
	ID3D11SamplerState* previousSampler = nullptr;

	context->CSGetShader(&previousCS, nullptr, nullptr);
	context->CSGetShaderResources(0, 1, &previousSRV);
	context->CSGetUnorderedAccessViews(0, 1, &previousUAV);
	context->CSGetConstantBuffers(0, 1, &previousCB);
	context->CSGetSamplers(0, 1, &previousSampler);

	DynamicResolutionStretchCB stretchData{};
	stretchData.inputSize = { static_cast<float>(inputWidth), static_cast<float>(inputHeight) };
	stretchData.outputSize = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	stretchData.sourceTextureSize = {
		static_cast<float>(vrIntermediateColorIn[eyeIndex]->desc.Width),
		static_cast<float>(vrIntermediateColorIn[eyeIndex]->desc.Height)
	};
	dynamicResolutionStretchCB->Update(stretchData);

	ID3D11ShaderResourceView* sourceSRV = vrIntermediateColorIn[eyeIndex]->srv.get();
	ID3D11UnorderedAccessView* outputUAV = vrIntermediateColorOut[eyeIndex]->uav.get();
	ID3D11Buffer* stretchBuffer = dynamicResolutionStretchCB->CB();
	ID3D11SamplerState* sampler = deferred->linearSampler;

	context->CSSetShader(stretchCS, nullptr, 0);
	context->CSSetShaderResources(0, 1, &sourceSRV);
	context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, &stretchBuffer);
	context->CSSetSamplers(0, 1, &sampler);

	auto state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent("Submit Stage Stretch Fallback");
	context->Dispatch((outputWidth + 7u) >> 3, (outputHeight + 7u) >> 3, 1);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	ID3D11Buffer* nullCB[1] = { nullptr };
	ID3D11SamplerState* nullSampler[1] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, nullCB);
	context->CSSetSamplers(0, 1, nullSampler);

	context->CSSetShader(previousCS, nullptr, 0);
	context->CSSetShaderResources(0, 1, &previousSRV);
	context->CSSetUnorderedAccessViews(0, 1, &previousUAV, nullptr);
	context->CSSetConstantBuffers(0, 1, &previousCB);
	context->CSSetSamplers(0, 1, &previousSampler);

	if (previousCS)
		previousCS->Release();
	if (previousSRV)
		previousSRV->Release();
	if (previousUAV)
		previousUAV->Release();
	if (previousCB)
		previousCB->Release();
	if (previousSampler)
		previousSampler->Release();

	return true;
}

void Upscaling::ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
	uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight, uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY, uint32_t colorOffsetY)
{
	if (!globals::game::isVR)
		return;
	if (!colorUAV || !depthSRV || !depthWidth || !depthHeight || !colorWidth || !colorHeight)
		return;

	auto context = globals::d3d::context;
	if (!context)
		return;

	if (!vrClearHMDMaskCS || !vrClearHMDMaskCB) {
		vrClearHMDMaskCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/Upscaling/ClearHMDMaskCS.hlsl", {}, "cs_5_0"));

		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth = 32;  // 8 uints
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = 0;
		DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&cbDesc, nullptr, vrClearHMDMaskCB.put()));
	}

	if (vrClearHMDMaskCS && vrClearHMDMaskCB) {
		auto dispatchX = (colorWidth + 7) / 8;
		auto dispatchY = (colorHeight + 7) / 8;

		context->CSSetShader(vrClearHMDMaskCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { colorUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		uint32_t clearMaskParams[8] = {
			depthOffsetX,
			colorOffsetX,
			depthOffsetY,
			colorOffsetY,
			depthWidth,
			depthHeight,
			colorWidth,
			colorHeight
		};
		context->UpdateSubresource(vrClearHMDMaskCB.get(), 0, nullptr, clearMaskParams, 0, 0);

		ID3D11Buffer* cbs[1] = { vrClearHMDMaskCB.get() };
		context->CSSetConstantBuffers(0, 1, cbs);

		context->Dispatch(dispatchX, dispatchY, 1);

		// Unbind
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		ID3D11Buffer* nullCB[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
static float Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

void UpdateCameraData();

void Upscaling::ConfigureTAA()
{
	auto upscaleMethod = GetUpscaleMethod();

	// When no upscaling method is active, preserve vanilla TAA state.
	// UpdateJitter (called immediately after this hook) owns the non-upscaling path.
	if (upscaleMethod == UpscaleMethod::kNONE)
		return;

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	// CS TAA replaces vanilla TAA, so disable water TAA there.
	// FSR/DLSS keep water TAA enabled.
	bool* enableWaterTAA = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISTemporalAA) + 0x38LL);
	*enableWaterTAA = upscaleMethod != UpscaleMethod::kTAA;

	BSImagespaceShaderISTemporalAA->taaEnabled = true;
}

void Upscaling::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();
	ApplyPendingVRDLSSSettings(upscaleMethod);

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	auto screenSize = state->screenSize;

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	const bool vendorUpscalingMethod = IsVendorUpscalingMethod(upscaleMethod);
	if (globals::game::isVR && vendorUpscalingMethod && IsLoadingMenuContextActive()) {
		resolutionScale = { 1.0f, 1.0f };
		jitter = { 0.0f, 0.0f };
		PrepareFullResolutionPostProcessing();
		return;
	}

	if (vendorUpscalingMethod && IsSubmitStageDynamicResolutionActive() && g_submitStageTargetSizeKnown) {
		const float renderScale = GetSubmitStageRequestedRenderScale();
		const float internalScale = GetSubmitStageInternalDynamicResolutionScale();
		resolutionScale = { renderScale, renderScale };

		const int outputWidth = g_submitStageOutputEyeWidth > 0 ? static_cast<int>(g_submitStageOutputEyeWidth * 2u) : screenWidth;
		const int renderWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(screenWidth) * internalScale)));
		const int renderHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(screenHeight) * internalScale)));
		auto phaseCount = GetJitterPhaseCount(renderWidth, outputWidth);
		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;

		auto& runtimeData = a_viewport->GetRuntimeData();
		const bool shouldUseInternalDynamicResolution = internalScale < kDynamicResolutionUpscalingScaleThreshold;
		if (globals::game::isVR)
			SetDynamicResolutionEnabledForUpscaling(shouldUseInternalDynamicResolution);
		runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
		runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
		runtimeData.dynamicResolutionWidthRatio = internalScale;
		runtimeData.dynamicResolutionHeightRatio = internalScale;
		runtimeData.dynamicResolutionLock = shouldUseInternalDynamicResolution ? 0 : 1;
		dynamicResolutionWidthRatio = internalScale;
		dynamicResolutionHeightRatio = internalScale;
		CheckResources(upscaleMethod);
		return;
	}

	if (vendorUpscalingMethod) {
		float resolutionScaleBase = GetQualityModeResolutionScale(ClampQualityModeUInt(settings.qualityMode));

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		if (globals::game::isVR)
			a_viewport->projectionPosScaleX = -jitter.x / renderWidth;
		else
			a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		if (globals::game::isVR)
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth;
		else
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

		jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	if (!vendorUpscalingMethod) {
		if (dynamicResolutionWidthRatio != 1.0f || dynamicResolutionHeightRatio != 1.0f) {
			if (globals::game::isVR) {
				SetDynamicResolutionEnabledForUpscaling(false);
				runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
				runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
				runtimeData.dynamicResolutionWidthRatio = 1.0f;
				runtimeData.dynamicResolutionHeightRatio = 1.0f;
				runtimeData.dynamicResolutionLock = 1;
			} else {
				runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
				runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
				runtimeData.dynamicResolutionWidthRatio = 1.0f;
				runtimeData.dynamicResolutionHeightRatio = 1.0f;
				runtimeData.dynamicResolutionLock = 1;
			}
			dynamicResolutionWidthRatio = runtimeData.dynamicResolutionWidthRatio;
			dynamicResolutionHeightRatio = runtimeData.dynamicResolutionHeightRatio;
			UpdateCameraData();
		}
		CheckResources(upscaleMethod);
		return;
	}

	ApplyDynamicResolutionState(a_viewport);

	// Resource creation uses the runtime dynamic-resolution ratios via ConvertToDynamic.
	CheckResources(upscaleMethod);

	// Disable dynamic resolution unless the game explicitly enables it.
	if (!globals::game::isVR)
		runtimeData.dynamicResolutionLock = 1;
}

void Upscaling::ApplyDynamicResolutionState(RE::BSGraphics::State* a_viewport)
{
	if (!a_viewport)
		return;

	auto upscaleMethod = GetUpscaleMethod();
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return;

	auto& runtimeData = a_viewport->GetRuntimeData();
	if (IsSubmitStageDynamicResolutionActive() && g_submitStageTargetSizeKnown) {
		const float internalScale = GetSubmitStageInternalDynamicResolutionScale();
		const bool shouldUseInternalDynamicResolution = internalScale < kDynamicResolutionUpscalingScaleThreshold;
		if (globals::game::isVR)
			SetDynamicResolutionEnabledForUpscaling(shouldUseInternalDynamicResolution);
		runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
		runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
		runtimeData.dynamicResolutionWidthRatio = internalScale;
		runtimeData.dynamicResolutionHeightRatio = internalScale;
		runtimeData.dynamicResolutionLock = shouldUseInternalDynamicResolution ? 0 : 1;
		dynamicResolutionWidthRatio = internalScale;
		dynamicResolutionHeightRatio = internalScale;
		UpdateCameraData();
		return;
	}

	const bool shouldUnlockDynamicResolution = globals::game::isVR && ShouldUnlockDynamicResolutionForUpscaling(upscaleMethod, resolutionScale);

	if (globals::game::isVR) {
		SetDynamicResolutionEnabledForUpscaling(shouldUnlockDynamicResolution);
		if (shouldUnlockDynamicResolution) {
			runtimeData.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionWidthRatio;
			runtimeData.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionHeightRatio;
			runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
			runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
			runtimeData.dynamicResolutionLock = 0;
			dynamicResolutionWidthRatio = resolutionScale.x;
			dynamicResolutionHeightRatio = resolutionScale.y;
		} else {
			runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
			runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
			runtimeData.dynamicResolutionWidthRatio = 1.0f;
			runtimeData.dynamicResolutionHeightRatio = 1.0f;
			runtimeData.dynamicResolutionLock = 1;
			dynamicResolutionWidthRatio = 1.0f;
			dynamicResolutionHeightRatio = 1.0f;
		}
		UpdateCameraData();
		return;
	}

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;
	runtimeData.dynamicResolutionLock = 1;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;
}

void Upscaling::PrepareFullResolutionPostProcessing()
{
	auto viewport = globals::game::graphicsState;
	if (!viewport)
		return;

	auto& runtimeData = viewport->GetRuntimeData();
	if (globals::game::isVR)
		SetDynamicResolutionEnabledForUpscaling(false);
	runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
	runtimeData.dynamicResolutionWidthRatio = 1.0f;
	runtimeData.dynamicResolutionHeightRatio = 1.0f;
	runtimeData.dynamicResolutionLock = 1;
	dynamicResolutionWidthRatio = 1.0f;
	dynamicResolutionHeightRatio = 1.0f;
	UpdateCameraData();
}

void Upscaling::SetupResources()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		logger::warn("[Upscaling] Skipping upscaling resource setup because Open Composite has {}=true.", blocker.settingName);
		return;
	}

	QueryPerformanceFrequency(&qpf);

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)

	if (globals::game::isVR) {
		depthStencilDesc.StencilEnable = true;     // Enable stencil testing
		depthStencilDesc.StencilReadMask = 0xFF;   // Read all stencil bits
		depthStencilDesc.StencilWriteMask = 0xFF;  // Write to all stencil bits

		// Configure front-facing stencil operations
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;       // Replace on stencil fail
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;  // Replace on depth fail
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;    // Replace on pass
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;       // Always pass stencil test

		// Configure back-facing stencil operations (same as front)
		depthStencilDesc.BackFace.StencilFailOp = depthStencilDesc.FrontFace.StencilFailOp;
		depthStencilDesc.BackFace.StencilDepthFailOp = depthStencilDesc.FrontFace.StencilDepthFailOp;
		depthStencilDesc.BackFace.StencilPassOp = depthStencilDesc.FrontFace.StencilPassOp;
		depthStencilDesc.BackFace.StencilFunc = depthStencilDesc.FrontFace.StencilFunc;
	} else {
		depthStencilDesc.StencilEnable = false;  // Disable stencil testing
	}

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	// Create jitter offset constant buffer for depth upscaling
	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	// Create upscaling data constant buffer for encode textures compute shader
	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());
	dynamicResolutionStretchCB = new ConstantBuffer(ConstantBufferDesc<DynamicResolutionStretchCB>(), "Upscaling::DynamicResolutionStretchCB");
	foveatedPeripheryCB = new ConstantBuffer(ConstantBufferDesc<FoveatedPeripheryCB>());
	foveatedCenterBlendCB = new ConstantBuffer(ConstantBufferDesc<FoveatedCenterBlendCB>());
	peripheryTAACB = new ConstantBuffer(ConstantBufferDesc<PeripheryTAACB>());

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	// Create rasterizer state for fullscreen rendering
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

	CheckResources(GetUpscaleMethod());

	rcas.Initialize();

	if (d3d12SwapChainActive)
		dx12SwapChain.CreateSharedResources();

	copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));
}

void Upscaling::ClearShaderCache()
{
	for (int i = 0; i < 5; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}
	encodeTexturesCSDepthOutput = nullptr;

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	underwaterMaskUpscaleRawDepthNoStencilPS = nullptr;
	upscaleVS = nullptr;                 // com_ptr automatically releases
	foveatedPeripheryCS = nullptr;       // com_ptr automatically releases
	foveatedCenterBlendCS = nullptr;     // com_ptr automatically releases
	peripheryTAACS = nullptr;            // com_ptr automatically releases
	submitStageStretchCS = nullptr;      // com_ptr automatically releases
}

void Upscaling::CopySharedD3D12Resources()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	context->CopyResource(dx12SwapChain.motionVectorBufferShared12->resource11, motionVector.texture);

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	{
		// Set up viewport for fullscreen rendering
		auto screenSize = globals::state->screenSize;

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(views), views);

		// Set render target view for pixel shader output
		ID3D11RenderTargetView* rtvs[1] = { dx12SwapChain.depthBufferShared12->rtv };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(copyDepthToSharedBufferPS.get(), nullptr, 0);

		context->Draw(3, 0);
	}

	// Clean up
	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(views), views);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);

	globals::state->EndPerfEvent();
}

void UpdateCameraData()
{
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
}

void Upscaling::PostDisplay()
{
	auto viewport = globals::game::graphicsState;

	viewport->projectionPosScaleX = projectionPosScaleX;
	viewport->projectionPosScaleY = projectionPosScaleY;

	if (globals::game::isVR && IsVendorUpscalingMethod(GetUpscaleMethod()) && IsLoadingMenuContextActive())
		PrepareFullResolutionPostProcessing();

	if (d3d12SwapChainActive)
		SetUIBuffer();

	globals::state->UpdateSharedData(false, false);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter()
{
	if (d3d12SwapChainActive) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();

		// Wait for the next frame presentation slot
		WaitForSingleObject(waitableObject, INFINITE);

		if (settings.frameLimitMode) {
			// Fall back to the original timing method
			// Use integer arithmetic for more precise timing
			static constexpr int64_t kNanosecondsPerSecond = 1000000000LL;
			static constexpr double kFrameGenerationRateScale = 0.5;
			const double frameRateScale = ShouldUseFrameGenerationThisFrame() ? kFrameGenerationRateScale : 1.0;
			int64_t targetFrameTimeNS = int64_t(static_cast<double>(kNanosecondsPerSecond) / (refreshRate * frameRateScale));
			int64_t targetFrameTicks = (targetFrameTimeNS * qpf.QuadPart) / kNanosecondsPerSecond;

			static LARGE_INTEGER lastFrame = {};
			LARGE_INTEGER timeNow;
			QueryPerformanceCounter(&timeNow);

			int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
			if (delta < targetFrameTicks) {
				TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
			}
			QueryPerformanceCounter(&lastFrame);
		}
	}
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						// get the refresh rate
						UINT numerator = p.targetInfo.refreshRate.Numerator;
						UINT denominator = p.targetInfo.refreshRate.Denominator;
						return (double)numerator / (double)denominator;
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool Upscaling::IsFrameGenerationActive() const
{
	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && fidelityFX.isFrameGenActive;
}

bool Upscaling::IsFrameGenerationDx12PathActive() const
{
	// Frame generation in this implementation runs via the DX12 swap-chain proxy path.
	return d3d12SwapChainActive && !globals::game::isVR;
}

bool Upscaling::ShouldUseFrameGenerationThisFrame() const
{
	auto* ui = globals::game::ui;
	auto* state = globals::state;
	const bool pausedMenuOpen = ui && ui->GameIsPaused();
	const bool mainOrLoadingMenuOpen = state && state->IsMainOrLoadingMenuOpen(ui);
	const bool menuOpen = pausedMenuOpen || mainOrLoadingMenuOpen;

	return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode && (settings.frameGenerationAllowInMenus || !menuOpen);
}

bool Upscaling::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	// Only consider vendor upscalers (FSR/DLSS) as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat upscaling as inactive.
	if (!IsVendorUpscalingMethod(method)) {
		return false;
	}

	return resolutionScale.x < kDynamicResolutionUpscalingScaleThreshold ||
	       resolutionScale.y < kDynamicResolutionUpscalingScaleThreshold;
}

bool Upscaling::IsSubmitStageUpscalingActive() const
{
	const auto upscaleMethod = GetUpscaleMethod();
	return globals::game::isVR &&
	       IsVendorUpscalingMethod(upscaleMethod) &&
	       IsSubmitStageDynamicResolutionActive() &&
	       IsSubmitStageRequestedUpscalingActive() &&
	       g_submitStageTargetSizeKnown &&
	       !IsGameMenuContextActive();
}

bool Upscaling::SubmitVRUpscaledFrame(vr::EVREye a_eye, const vr::Texture_t* a_inputTexture, const vr::VRTextureBounds_t* a_inputBounds,
	vr::Texture_t& a_outputTexture, vr::VRTextureBounds_t& a_outputBounds)
{
	if (!IsSubmitStageUpscalingActive() || !a_inputTexture || !a_inputTexture->handle || a_inputTexture->eType != vr::TextureType_DirectX)
		return false;
	if (a_eye != vr::Eye_Left && a_eye != vr::Eye_Right)
		return false;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!state || !renderer || !context)
		return false;

	const auto upscaleMethod = GetUpscaleMethod();
	if (!IsVendorUpscalingMethod(upscaleMethod))
		return false;
	const auto upscaleMethodName = magic_enum::enum_name(upscaleMethod);

	const uint32_t currentFrame = state->frameCount;
	auto* sourceTexture = static_cast<ID3D11Texture2D*>(a_inputTexture->handle);
	if (submitStageHandoffFrame != currentFrame || submitStageHandoffTexture != sourceTexture) {
		static bool loggedMissingHandoff = false;
		if (!loggedMissingHandoff) {
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because the submitted texture does not match the dynamic-resolution handoff for frame {}.",
				upscaleMethodName,
				currentFrame);
			loggedMissingHandoff = true;
		}
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);
	if (sourceDesc.SampleDesc.Count != 1) {
		static bool loggedMSAA = false;
		if (!loggedMSAA) {
			logger::warn("[Upscaling] Submit-stage {} skipped because the submitted texture is MSAA.", upscaleMethodName);
			loggedMSAA = true;
		}
		return false;
	}

	const auto screenSize = state->screenSize;
	const auto renderSize = Util::ConvertToDynamic(screenSize, true);
	const bool targetScaleMode = IsSubmitStageDynamicResolutionActive() && g_submitStageTargetSizeKnown;
	const uint32_t eyeWidthOut =
		targetScaleMode && g_submitStageOutputEyeWidth > 0 ? g_submitStageOutputEyeWidth : static_cast<uint32_t>(screenSize.x / 2.0f);
	const uint32_t eyeHeightOut =
		targetScaleMode && g_submitStageOutputEyeHeight > 0 ? g_submitStageOutputEyeHeight : static_cast<uint32_t>(screenSize.y);
	const uint32_t eyeWidthIn = static_cast<uint32_t>(renderSize.x / 2.0f);
	const uint32_t eyeHeightIn = static_cast<uint32_t>(renderSize.y);
	if (!eyeWidthIn || !eyeHeightIn || !eyeWidthOut || !eyeHeightOut)
		return false;

	CheckResources(upscaleMethod);

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (!motionVector.texture || !depth.texture)
		return false;

	const uint32_t eyeIndex = a_eye == vr::Eye_Right ? 1u : 0u;
	if (submitStagePreparedFrame != currentFrame) {
		if (upscaleMethod == UpscaleMethod::kDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed)) {
			logger::debug("[Upscaling] LoadingMenu close detected - rebuilding submit-stage DLSS feature");
			UnbindUpscalingResources();
			streamline.DestroyDLSSResources();
			RequestHistoryReset();
		}

		UpdateHistoryResetState(upscaleMethod);
		LatchHistoryResetForCurrentFrame();

		if (!EncodeSubmitStageVRInputs(sourceTexture, motionVector.texture, depth.texture, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut))
			return false;

		submitStagePreparedFrame = currentFrame;
	}

	if (!vrIntermediateColorIn[eyeIndex] || !vrIntermediateColorIn[eyeIndex]->resource ||
		!vrIntermediateColorOut[eyeIndex] || !vrIntermediateColorOut[eyeIndex]->resource ||
		!vrIntermediateMotionVectors[eyeIndex] || !vrIntermediateMotionVectors[eyeIndex]->resource ||
		!vrIntermediateDepth[eyeIndex] || !vrIntermediateDepth[eyeIndex]->resource ||
		(upscaleMethod == UpscaleMethod::kFSR && (!vrIntermediateLinearDepth[eyeIndex] || !vrIntermediateLinearDepth[eyeIndex]->resource)) ||
		!vrIntermediateReactiveMask[eyeIndex] || !vrIntermediateReactiveMask[eyeIndex]->resource ||
		!vrIntermediateTransparencyMask[eyeIndex] || !vrIntermediateTransparencyMask[eyeIndex]->resource) {
		return false;
	}

	auto clampToTexture = [](int64_t value, uint32_t maxValue) {
		return static_cast<uint32_t>(std::clamp<int64_t>(value, 0, maxValue));
	};

	const bool sourceUsesCombinedStereoLayout =
		sourceDesc.ArraySize == 1 &&
		sourceDesc.Width >= eyeWidthIn * 2;

	UINT sourceSubresource = 0;
	D3D11_BOX colorBox{};
	if (sourceDesc.ArraySize > 1) {
		const UINT arraySlice = std::min<UINT>(eyeIndex, sourceDesc.ArraySize - 1);
		sourceSubresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
		colorBox = { 0, 0, 0, std::min(eyeWidthIn, sourceDesc.Width), std::min(eyeHeightIn, sourceDesc.Height), 1 };
	} else if (sourceUsesCombinedStereoLayout) {
		const uint32_t left = eyeIndex * eyeWidthIn;
		colorBox = { left, 0, 0, std::min(left + eyeWidthIn, sourceDesc.Width), std::min(eyeHeightIn, sourceDesc.Height), 1 };
	} else if (a_inputBounds) {
		const uint32_t left = clampToTexture(std::llround(a_inputBounds->uMin * static_cast<float>(sourceDesc.Width)), sourceDesc.Width);
		const uint32_t top = clampToTexture(std::llround(a_inputBounds->vMin * static_cast<float>(sourceDesc.Height)), sourceDesc.Height);
		colorBox = { left, top, 0, std::min(left + eyeWidthIn, sourceDesc.Width), std::min(top + eyeHeightIn, sourceDesc.Height), 1 };
	} else {
		colorBox = { 0, 0, 0, std::min(eyeWidthIn, sourceDesc.Width), std::min(eyeHeightIn, sourceDesc.Height), 1 };
	}

	if (colorBox.right - colorBox.left != eyeWidthIn || colorBox.bottom - colorBox.top != eyeHeightIn) {
		static bool loggedBadBounds = false;
		if (!loggedBadBounds) {
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because submit bounds do not contain the expected render area. eye={} source={}x{} box=({},{})->({},{}) expected={}x{}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				colorBox.left,
				colorBox.top,
				colorBox.right,
				colorBox.bottom,
				eyeWidthIn,
				eyeHeightIn);
			loggedBadBounds = true;
		}
		return false;
	}

	context->CopySubresourceRegion(vrIntermediateColorIn[eyeIndex]->resource.get(), 0, 0, 0, 0, sourceTexture, sourceSubresource, &colorBox);

	if (targetScaleMode)
		(void)GetSubmitStageStretchCS();

	bool vendorSucceeded = false;
	if (IsFoveatedVendorDispatchEnabled(upscaleMethod)) {
		vendorSucceeded = DispatchSubmitStageFoveatedVendorEye(
			upscaleMethod,
			eyeIndex,
			eyeWidthIn,
			eyeHeightIn,
			eyeWidthOut,
			eyeHeightOut);
		if (!vendorSucceeded) {
			static bool loggedFoveatedSubmitFallback[2] = {};
			if (!loggedFoveatedSubmitFallback[eyeIndex]) {
				logger::warn(
					"[Upscaling] Submit-stage foveated {} failed for eye {}; falling back to full-eye vendor dispatch for this frame.",
					upscaleMethodName,
					eyeIndex);
				loggedFoveatedSubmitFallback[eyeIndex] = true;
			}
		}
	}

	if (!vendorSucceeded && upscaleMethod == UpscaleMethod::kFSR) {
		vendorSucceeded = fidelityFX.UpscaleRegion(
			eyeIndex,
			vrIntermediateColorIn[eyeIndex]->resource.get(),
			vrIntermediateLinearDepth[eyeIndex]->resource.get(),
			vrIntermediateMotionVectors[eyeIndex]->resource.get(),
			vrIntermediateReactiveMask[eyeIndex]->resource.get(),
			vrIntermediateTransparencyMask[eyeIndex]->resource.get(),
			vrIntermediateColorOut[eyeIndex]->resource.get(),
			eyeWidthIn,
			eyeHeightIn,
			eyeWidthOut,
			eyeHeightOut,
			static_cast<float>(eyeWidthIn),
			static_cast<float>(eyeHeightIn),
			settings.sharpnessFSR);
	} else if (!vendorSucceeded && upscaleMethod == UpscaleMethod::kDLSS) {
		const sl::Extent extentIn{ 0u, 0u, eyeWidthIn, eyeHeightIn };
		const sl::Extent extentOut{ 0u, 0u, eyeWidthOut, eyeHeightOut };
		const sl::ViewportHandle viewport = eyeIndex == 1 ? streamline.viewportRight : streamline.viewport;
		vendorSucceeded = streamline.EvaluateDLSS(
			viewport,
			eyeIndex,
			vrIntermediateColorIn[eyeIndex]->resource.get(),
			vrIntermediateColorOut[eyeIndex]->resource.get(),
			vrIntermediateDepth[eyeIndex]->resource.get(),
			vrIntermediateMotionVectors[eyeIndex]->resource.get(),
			vrIntermediateReactiveMask[eyeIndex]->resource.get(),
			vrIntermediateTransparencyMask[eyeIndex]->resource.get(),
			extentIn,
			extentOut,
			eyeWidthOut);
	}

	if (!vendorSucceeded) {
		static bool loggedSubmitFailure[2] = {};
		if (!loggedSubmitFailure[eyeIndex]) {
			logger::warn(
				"[Upscaling] Submit-stage {} failed for eye {}; using a full-size stretch fallback for this frame.",
				upscaleMethodName,
				eyeIndex);
			loggedSubmitFailure[eyeIndex] = true;
		}

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			streamline.InvalidateDLSSOptionsCache();
			streamline.ResetFrameTracking();
		}
		RequestHistoryReset();

		if (targetScaleMode &&
			StretchSubmitStageEyeOutput(eyeIndex, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut) &&
			vrIntermediateColorOut[eyeIndex] && vrIntermediateColorOut[eyeIndex]->resource) {
			a_outputTexture = *a_inputTexture;
			a_outputTexture.handle = vrIntermediateColorOut[eyeIndex]->resource.get();
			a_outputTexture.eType = vr::TextureType_DirectX;
			a_outputBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
			return true;
		}

		return false;
	}

	if (targetScaleMode) {
		const bool canMirrorToSource =
			sourceDesc.ArraySize == 1 &&
			sourceDesc.Width >= eyeWidthOut * 2 &&
			sourceDesc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[0] && vrIntermediateColorOut[1] &&
			vrIntermediateColorOut[0]->resource && vrIntermediateColorOut[1]->resource &&
			vrIntermediateColorOut[0]->desc.Width >= eyeWidthOut &&
			vrIntermediateColorOut[0]->desc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[1]->desc.Width >= eyeWidthOut &&
			vrIntermediateColorOut[1]->desc.Height >= eyeHeightOut &&
			vrIntermediateColorOut[0]->desc.Format == sourceDesc.Format &&
			vrIntermediateColorOut[1]->desc.Format == sourceDesc.Format;

		if (canMirrorToSource) {
			if (submitStageMirrorFrame != currentFrame || submitStageMirrorSourceTexture != sourceTexture) {
				submitStageMirrorFrame = currentFrame;
				submitStageMirrorSourceTexture = sourceTexture;
				submitStageMirrorEyeReady = {};
			}

			submitStageMirrorEyeReady[eyeIndex] = true;
			if (submitStageMirrorEyeReady[0] && submitStageMirrorEyeReady[1]) {
				D3D11_BOX mirrorBox{ 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
				context->CopySubresourceRegion(sourceTexture, 0, 0, 0, 0, vrIntermediateColorOut[0]->resource.get(), 0, &mirrorBox);
				context->CopySubresourceRegion(sourceTexture, 0, eyeWidthOut, 0, 0, vrIntermediateColorOut[1]->resource.get(), 0, &mirrorBox);
				submitStageMirrorEyeReady = {};
			}
		} else {
			static bool loggedSubmitStageMirrorSkip = false;
			if (!loggedSubmitStageMirrorSkip) {
				logger::warn(
					"[Upscaling] Desktop mirror writeback skipped because the submit texture is not a compatible full stereo target. source={}x{} array={} format={} outputL={}x{} format={} outputR={}x{} format={}",
					sourceDesc.Width,
					sourceDesc.Height,
					sourceDesc.ArraySize,
					static_cast<uint32_t>(sourceDesc.Format),
					vrIntermediateColorOut[0] ? vrIntermediateColorOut[0]->desc.Width : 0,
					vrIntermediateColorOut[0] ? vrIntermediateColorOut[0]->desc.Height : 0,
					vrIntermediateColorOut[0] ? static_cast<uint32_t>(vrIntermediateColorOut[0]->desc.Format) : 0,
					vrIntermediateColorOut[1] ? vrIntermediateColorOut[1]->desc.Width : 0,
					vrIntermediateColorOut[1] ? vrIntermediateColorOut[1]->desc.Height : 0,
					vrIntermediateColorOut[1] ? static_cast<uint32_t>(vrIntermediateColorOut[1]->desc.Format) : 0);
				loggedSubmitStageMirrorSkip = true;
			}
		}

		a_outputTexture = *a_inputTexture;
		a_outputTexture.handle = vrIntermediateColorOut[eyeIndex]->resource.get();
		a_outputTexture.eType = vr::TextureType_DirectX;
		a_outputBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
		return true;
	}

	UINT outputSubresource = 0;
	uint32_t outputOffsetX = 0;
	if (sourceDesc.ArraySize > 1) {
		const UINT arraySlice = std::min<UINT>(eyeIndex, sourceDesc.ArraySize - 1);
		outputSubresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
	} else if (sourceDesc.Width >= eyeWidthOut * 2) {
		outputOffsetX = eyeIndex * eyeWidthOut;
	}
	if (outputOffsetX + eyeWidthOut > sourceDesc.Width || eyeHeightOut > sourceDesc.Height) {
		static bool loggedBadOutputBounds = false;
		if (!loggedBadOutputBounds) {
			logger::warn(
				"[Upscaling] Submit-stage {} skipped because output copy would exceed submit texture bounds. eye={} source={}x{} dst=({},0) size={}x{}",
				upscaleMethodName,
				eyeIndex,
				sourceDesc.Width,
				sourceDesc.Height,
				outputOffsetX,
				eyeWidthOut,
				eyeHeightOut);
			loggedBadOutputBounds = true;
		}
		return false;
	}

	D3D11_BOX outputBox{ 0, 0, 0, eyeWidthOut, eyeHeightOut, 1 };
	context->CopySubresourceRegion(sourceTexture, outputSubresource, outputOffsetX, 0, 0, vrIntermediateColorOut[eyeIndex]->resource.get(), 0, &outputBox);

	a_outputTexture = *a_inputTexture;
	a_outputTexture.eType = vr::TextureType_DirectX;
	a_outputBounds = a_inputBounds ? *a_inputBounds : vr::VRTextureBounds_t{ 0.0f, 0.0f, 1.0f, 1.0f };
	return true;
}

void Upscaling::RequestHistoryReset()
{
	historyResetRequested = true;
}

uint32_t Upscaling::GetEffectiveDLSSQualityMode() const
{
	const uint32_t pendingQualityMode = pendingVRDLSSQualityMode.load(std::memory_order_acquire);
	return pendingQualityMode != kPendingVRDLSSSettingUnset ? pendingQualityMode : settings.qualityMode;
}

uint32_t Upscaling::GetEffectiveDLSSPreset() const
{
	const uint32_t pendingPreset = pendingVRDLSSPreset.load(std::memory_order_acquire);
	return pendingPreset != kPendingVRDLSSSettingUnset ? pendingPreset : settings.dlssPreset;
}

void Upscaling::QueueVRDLSSQualityMode(uint32_t a_qualityMode)
{
	pendingVRDLSSQualityMode.store(std::min(a_qualityMode, kQualityModeMaxIndex), std::memory_order_release);
}

void Upscaling::QueueVRDLSSPreset(uint32_t a_dlssPreset)
{
	pendingVRDLSSPreset.store(std::min(a_dlssPreset, kDLSSPresetMaxIndex), std::memory_order_release);
}

void Upscaling::ApplyPendingVRDLSSSettings(UpscaleMethod a_upscaleMethod)
{
	if (!globals::game::isVR || a_upscaleMethod != UpscaleMethod::kDLSS)
		return;

	const uint32_t pendingQualityMode = pendingVRDLSSQualityMode.exchange(kPendingVRDLSSSettingUnset, std::memory_order_acq_rel);
	const uint32_t pendingPreset = pendingVRDLSSPreset.exchange(kPendingVRDLSSSettingUnset, std::memory_order_acq_rel);
	bool changed = false;

	if (pendingQualityMode != kPendingVRDLSSSettingUnset && settings.qualityMode != pendingQualityMode) {
		settings.qualityMode = pendingQualityMode;
		changed = true;
	}

	if (pendingPreset != kPendingVRDLSSSettingUnset && settings.dlssPreset != pendingPreset) {
		settings.dlssPreset = pendingPreset;
		changed = true;
	}

	if (changed) {
		RequestHistoryReset();
		pendingDLSSHistoryReset.store(true, std::memory_order_release);
	}
}

bool Upscaling::ShouldResetHistoryThisFrame() const
{
	return historyResetThisFrame;
}

void Upscaling::LatchHistoryResetForCurrentFrame()
{
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (historyResetLatchedFrame == frame)
		return;

	historyResetLatchedFrame = frame;
	historyResetThisFrame = historyResetRequested;
	historyResetRequested = false;
}

void Upscaling::UpdateHistoryResetState(UpscaleMethod a_upscaleMethod)
{
	auto state = globals::state;
	if (!state)
		return;

	const bool inWorld = state->inWorld;
	const bool inMapMenu = globals::game::ui ? globals::game::ui->IsMenuOpen(RE::MapMenu::MENU_NAME) : false;
	const float2 screenSize = state->screenSize;
	const bool foveatedDispatchEnabled = IsFoveatedVendorDispatchEnabled(a_upscaleMethod);
	const bool peripheryTAAEnabled = IsPeripheryTAAEnabled(a_upscaleMethod);
	const bool peripheryTAAPathActive = IsPeripheryTAAPathActive(a_upscaleMethod);
	const bool fsrRuntimePathActive = IsFSRRuntimePathActive(a_upscaleMethod);
	const bool fsrRuntimeFsr4Active = IsFSRRuntimeFsr4PathActive(a_upscaleMethod);
	const uint32_t qualityMode = ClampQualityModeUInt(settings.qualityMode);
	const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, peripheryTAAEnabled);
	const float foveatedCenterArea = foveatedProfile.centerArea;
	const float foveatedCenterHorizontalScale = foveatedProfile.centerHorizontalScale;
	const float peripheryTAACenterBlendFeather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);
	const float peripheryTAAOuterScale = ClampPeripheryTAAOuterScaleForCenter(
		settings.periphery_taa_outer_scale,
		foveatedCenterArea,
		foveatedCenterHorizontalScale,
		peripheryTAACenterBlendFeather);
	const auto foveatedCenterOffsets = GetResolvedFoveatedMaskCenterOffsets(peripheryTAAEnabled);

	auto cameraCutDetected = []() {
		constexpr float kCameraCutDistanceThreshold = 2500.0f;  // ~35m teleport/cut in Skyrim units
		const float cutDistanceSq = kCameraCutDistanceThreshold * kCameraCutDistanceThreshold;

		auto exceededThreshold = [&](uint32_t eyeIndex) {
			const auto& currentPos = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
			const auto& previousPos = globals::game::frameBufferCached.GetCameraPreviousPosAdjust(eyeIndex);
			const float dx = currentPos.x - previousPos.x;
			const float dy = currentPos.y - previousPos.y;
			const float dz = currentPos.z - previousPos.z;
			return (dx * dx + dy * dy + dz * dz) > cutDistanceSq;
		};

		if (globals::game::isVR)
			return exceededThreshold(0) || exceededThreshold(1);
		return exceededThreshold(0);
	};

	bool shouldReset = false;
	if (!historyResetTrackingInitialized) {
		shouldReset = true;
		historyResetTrackingInitialized = true;
	} else {
		const bool screenSizeChanged =
			std::abs(screenSize.x - previousHistoryScreenSize.x) > 0.5f ||
			std::abs(screenSize.y - previousHistoryScreenSize.y) > 0.5f;
		const bool scaleChanged =
			std::abs(resolutionScale.x - previousHistoryResolutionScale.x) > 1e-4f ||
			std::abs(resolutionScale.y - previousHistoryResolutionScale.y) > 1e-4f;
		const bool qualityModeChanged = qualityMode != previousHistoryQualityMode;
		const bool worldStateChanged =
			inWorld != previousHistoryInWorld ||
			inMapMenu != previousHistoryInMapMenu;
		const bool methodChanged = a_upscaleMethod != previousHistoryUpscaleMethod;
		const bool fsrRuntimePathChanged = fsrRuntimePathActive != previousHistoryFSRRuntimePathActive;
		const bool fsrRuntimeVersionChanged =
			(fsrRuntimePathActive || previousHistoryFSRRuntimePathActive) &&
			fsrRuntimeFsr4Active != previousHistoryFSRRuntimeFsr4Active;
		const bool compareFoveatedArea = foveatedDispatchEnabled || previousHistoryFoveatedDispatch;
		const bool foveatedOffsetsChanged =
			compareFoveatedArea &&
			(std::abs(foveatedCenterOffsets[0].x - previousHistoryFoveatedCenterOffsets[0].x) > 1e-4f ||
			 std::abs(foveatedCenterOffsets[0].y - previousHistoryFoveatedCenterOffsets[0].y) > 1e-4f ||
			 std::abs(foveatedCenterOffsets[1].x - previousHistoryFoveatedCenterOffsets[1].x) > 1e-4f ||
			 std::abs(foveatedCenterOffsets[1].y - previousHistoryFoveatedCenterOffsets[1].y) > 1e-4f);
		const bool foveatedChanged =
			foveatedDispatchEnabled != previousHistoryFoveatedDispatch ||
			(compareFoveatedArea && std::abs(foveatedCenterArea - previousHistoryFoveatedCenterArea) > 1e-4f) ||
			(compareFoveatedArea && std::abs(foveatedCenterHorizontalScale - previousHistoryFoveatedCenterHorizontalScale) > 1e-4f) ||
			foveatedOffsetsChanged;
		const bool longFrameGap = globals::game::deltaTime &&
								  std::isfinite(*globals::game::deltaTime) &&
								  *globals::game::deltaTime > 0.20f;
		const bool cameraCut = inWorld && cameraCutDetected();

		const bool effectivePeripheryTAAChanged =
			peripheryTAAEnabled != previousHistoryPeripheryTAA ||
			peripheryTAAPathActive != previousHistoryPeripheryTAAPathActive ||
			(peripheryTAAPathActive && (
				std::abs(peripheryTAAOuterScale - previousHistoryPeripheryTAAOuterScale) > 1e-4f ||
				std::abs(peripheryTAACenterBlendFeather - previousHistoryPeripheryTAACenterBlendFeather) > 1e-4f));

		shouldReset = screenSizeChanged || scaleChanged || qualityModeChanged || worldStateChanged || methodChanged || fsrRuntimePathChanged || fsrRuntimeVersionChanged || foveatedChanged || effectivePeripheryTAAChanged || longFrameGap || cameraCut;
	}

	if (state->pendingPostLoadRuntimeReset)
		shouldReset = true;

	if (shouldReset)
		RequestHistoryReset();

	previousHistoryScreenSize = screenSize;
	previousHistoryResolutionScale = resolutionScale;
	previousHistoryQualityMode = qualityMode;
	previousHistoryInWorld = inWorld;
	previousHistoryInMapMenu = inMapMenu;
	previousHistoryUpscaleMethod = a_upscaleMethod;
	previousHistoryFoveatedDispatch = foveatedDispatchEnabled;
	previousHistoryFoveatedCenterArea = foveatedCenterArea;
	previousHistoryFoveatedCenterHorizontalScale = foveatedCenterHorizontalScale;
	previousHistoryFoveatedCenterOffsets = foveatedCenterOffsets;
	previousHistoryPeripheryTAA = peripheryTAAEnabled;
	previousHistoryPeripheryTAAPathActive = peripheryTAAPathActive;
	previousHistoryPeripheryTAAOuterScale = peripheryTAAOuterScale;
	previousHistoryPeripheryTAACenterBlendFeather = peripheryTAACenterBlendFeather;
	previousHistoryFSRRuntimePathActive = fsrRuntimePathActive;
	previousHistoryFSRRuntimeFsr4Active = fsrRuntimeFsr4Active;
}

/**
 * @brief Retrieves the current frame time for frame generation.
 *
 * Returns the frame time from the D3D12 swap chain if frame generation is active; otherwise, returns 0.
 *
 * @return float The current frame time in seconds, or 0 if frame generation is inactive.
 */
float Upscaling::GetFrameGenerationFrameTime() const
{
	if (!IsFrameGenerationActive())
		return 0.0f;

	// Get the current frame time from D3D12 swapchain
	if (dx12SwapChain.swapChain) {
		// Get frame time from the D3D12 SwapChain
		return GetFrameTime();
	}

	return 0.0f;
}

// Unified interface methods
void Upscaling::LoadUpscalingSDKs()
{
	ApplyOpenCompositeUpscalingBlocker(true);
	const auto& blocker = GetOpenCompositeUpscalingBlocker();
	if (blocker.active) {
		if (!openCompositeUpscalingBackendSkipLogged) {
			if (blocker.configPath.empty()) {
				logger::warn(
					"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because Open Composite has {}=true.",
					blocker.settingName);
			} else {
				logger::warn(
					"[Upscaling] Skipping Community Shaders Streamline/FidelityFX backend initialization because Open Composite has {}=true in {}.",
					blocker.settingName,
					blocker.configPath);
			}
			openCompositeUpscalingBackendSkipLogged = true;
		}
		return;
	}

	// Initialize upscaling SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();
}

void Upscaling::SetUIBuffer()
{
	dx12SwapChain.SetUIBuffer();
}

HANDLE Upscaling::GetFrameLatencyWaitableObject() const
{
	return dx12SwapChain.GetFrameLatencyWaitableObject();
}

float Upscaling::GetFrameTime() const
{
	return dx12SwapChain.GetFrameTime();
}

// Backend interface methods
bool Upscaling::IsBackendInitialized() const
{
	return streamline.initialized;
}

void Upscaling::CheckBackendFeatures(IDXGIAdapter* adapter)
{
	streamline.CheckFeatures(adapter);
}

void Upscaling::UpgradeBackendInterface(void** ppInterface)
{
	streamline.slUpgradeInterface(ppInterface);
}

void Upscaling::SetBackendD3DDevice(ID3D11Device* device)
{
	streamline.slSetD3DDevice(device);
}

void Upscaling::PostBackendDevice()
{
	streamline.PostDevice();
}

// Module availability methods
bool Upscaling::HasFrameGenModule() const
{
	return fidelityFX.featureFSR3FG;
}

// Proxy interface methods
void Upscaling::SetProxyD3D11Device(ID3D11Device* device)
{
	dx12SwapChain.SetD3D11Device(device);
}

void Upscaling::SetProxyD3D11DeviceContext(ID3D11DeviceContext* context)
{
	dx12SwapChain.SetD3D11DeviceContext(context);
}

void Upscaling::CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc)
{
	dx12SwapChain.CreateSwapChain(adapter, swapChainDesc);
}

void Upscaling::CreateProxyInterop()
{
	dx12SwapChain.CreateInterop();
}

IDXGISwapChain* Upscaling::GetProxySwapChain()
{
	return dx12SwapChain.GetSwapChainProxy();
}

bool Upscaling::IsOpenCompositeUpscalingBlocked(bool a_forceRefresh) const
{
	return GetOpenCompositeUpscalingBlocker(a_forceRefresh).active;
}

void Upscaling::Upscale()
{
	ZoneScoped;
	auto upscaleMethod = GetUpscaleMethod();
	dlssUpscaleOutputInSharpenerTexture = false;

	if (globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS && pendingDLSSHistoryReset.exchange(false, std::memory_order_relaxed)) {
		logger::debug("[Upscaling] Resetting DLSS history after VR option/load transition");
		RequestHistoryReset();
	}

	UpdateHistoryResetState(upscaleMethod);
	LatchHistoryResetForCurrentFrame();

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	const bool requiresEncodedMotionVectors = upscaleMethod == UpscaleMethod::kDLSS || upscaleMethod == UpscaleMethod::kFSR;
	const bool requiresCombinedEncodedMotionVectors = requiresEncodedMotionVectors && !globals::game::isVR;
	if (requiresCombinedEncodedMotionVectors && (!motionVectorCopyTexture || !motionVectorCopyTexture->uav || !motionVectorCopyTexture->resource)) {
		logger::error("[Upscaling] Missing encoded motion-vector resources for method {}", magic_enum::enum_name(upscaleMethod));
		return;
	}

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	const bool foveatedDispatchRequested = IsFoveatedVendorDispatchEnabled(upscaleMethod);
	bool encodedVRFoveatedRegions = false;

	auto encodeUpscalingTextures = [&](bool forceFullVREncode, const char* eventName) {
		encodedVRFoveatedRegions = false;
		state->BeginPerfEvent(eventName);
		TracyD3D11Zone(globals::state->tracyCtx, "Encode Upscaling Textures");

		auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
		auto& normals = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[2]];
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
		context->CSSetShader(GetEncodeTexturesCS(), nullptr, 0);

		if (globals::game::isVR) {
			const uint32_t eyeWidthOut = static_cast<uint32_t>(state->screenSize.x / 2);
			const uint32_t eyeHeightOut = static_cast<uint32_t>(state->screenSize.y);
			const uint32_t eyeWidthIn = static_cast<uint32_t>(renderSize.x / 2);
			const uint32_t eyeHeightIn = static_cast<uint32_t>(renderSize.y);

			EnsureVRIntermediateTextures(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut,
				main.texture, motionVector.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get());

			auto dispatchEyeEncode = [&](uint32_t eye, uint32_t inputMinX, uint32_t inputMinY, uint32_t inputMaxX, uint32_t inputMaxY) {
				if (eye >= 2 || inputMaxX <= inputMinX || inputMaxY <= inputMinY)
					return;

				inputMinX = std::min(inputMinX, eyeWidthIn);
				inputMinY = std::min(inputMinY, eyeHeightIn);
				inputMaxX = std::min(inputMaxX, eyeWidthIn);
				inputMaxY = std::min(inputMaxY, eyeHeightIn);
				if (inputMaxX <= inputMinX || inputMaxY <= inputMinY)
					return;

				const uint32_t dispatchWidth = inputMaxX - inputMinX;
				const uint32_t dispatchHeight = inputMaxY - inputMinY;
				UpscalingDataCB upscalingData{};
				upscalingData.dispatchDim = { static_cast<float>(dispatchWidth), static_cast<float>(dispatchHeight) };
				upscalingData.trueSamplingDim = renderSize;
				upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
				upscalingData.seamCenterX = renderSize.x * 0.5f;
				upscalingData.seamHalfWidthPx = 2.0f;
				upscalingData.maskDepthThreshold = 1e-6f;
				upscalingData.vrSeamHardening = 1.0f;
				upscalingData.sourceOffset = { static_cast<float>(eye * eyeWidthIn + inputMinX), static_cast<float>(inputMinY) };
				upscalingData.outputOffset = { static_cast<float>(inputMinX), static_cast<float>(inputMinY) };
				upscalingDataCB->Update(upscalingData);

				ID3D11UnorderedAccessView* uavs[4] = {
					vrIntermediateReactiveMask[eye]->uav.get(),
					vrIntermediateTransparencyMask[eye]->uav.get(),
					vrIntermediateMotionVectors[eye]->uav.get(),
					(upscaleMethod == UpscaleMethod::kFSR) ? vrIntermediateLinearDepth[eye]->uav.get() : nullptr
				};
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
				context->Dispatch((dispatchWidth + 7u) >> 3, (dispatchHeight + 7u) >> 3, 1);
			};

			auto dispatchFullEyes = [&]() {
				for (uint32_t eye = 0; eye < 2; ++eye) {
					dispatchEyeEncode(eye, 0, 0, eyeWidthIn, eyeHeightIn);
				}
			};

			struct EncodeRegion
			{
				uint32_t minX = 0;
				uint32_t minY = 0;
				uint32_t maxX = 0;
				uint32_t maxY = 0;
				bool valid = false;
			};

			auto includeInputRect = [&](EncodeRegion& region, uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
				minX = std::min(minX, eyeWidthIn);
				minY = std::min(minY, eyeHeightIn);
				maxX = std::min(maxX, eyeWidthIn);
				maxY = std::min(maxY, eyeHeightIn);
				if (maxX <= minX || maxY <= minY)
					return;

				if (!region.valid) {
					region.minX = minX;
					region.minY = minY;
					region.maxX = maxX;
					region.maxY = maxY;
					region.valid = true;
				} else {
					region.minX = std::min(region.minX, minX);
					region.minY = std::min(region.minY, minY);
					region.maxX = std::max(region.maxX, maxX);
					region.maxY = std::max(region.maxY, maxY);
				}
			};

			const bool useRegionEncode = !forceFullVREncode && foveatedDispatchRequested && IsPeripheryTAAPathActive(upscaleMethod);
			bool dispatchedRegionEncode = false;
			if (useRegionEncode) {
				const bool usePeripheryTAAProfile = IsPeripheryTAAEnabled(upscaleMethod);
				const auto foveatedProfile = GetFoveatedMaskProfileParams(settings, usePeripheryTAAProfile);
				const float centerScale = foveatedProfile.centerArea;
				const float centerHorizontalScale = foveatedProfile.centerHorizontalScale;
				const float centerFeather = ClampPeripheryTAACenterBlendFeather(settings.periphery_taa_center_blend_feather);

				if (BuildFoveatedDispatchRects(eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut, true, centerScale, centerFeather, centerHorizontalScale, usePeripheryTAAProfile)) {
					std::array<EncodeRegion, 2> regions{};
					bool allRegionsValid = true;
					for (uint32_t eye = 0; eye < 2; ++eye) {
						const auto& rect = foveatedRectCache.rects[eye];
						if (!rect.inputWidth || !rect.inputHeight) {
							allRegionsValid = false;
							break;
						}

						includeInputRect(regions[eye], rect.inputOffsetX, rect.inputOffsetY, rect.inputOffsetX + rect.inputWidth, rect.inputOffsetY + rect.inputHeight);

						const float2 centerOffset = GetResolvedFoveatedMaskCenterOffset(eye, usePeripheryTAAProfile);
						const float taaOuterScale = ClampPeripheryTAAOuterScaleForCenter(
							settings.periphery_taa_outer_scale,
							centerScale,
							centerHorizontalScale,
							centerFeather);
						const auto taaOuterBounds = FoveatedCommon::BuildCenteredDispatchBounds(0, eyeWidthOut, eyeHeightOut, taaOuterScale, centerOffset.x, centerOffset.y, 0.0f, centerHorizontalScale);
						if (taaOuterBounds.maxX > taaOuterBounds.minX && taaOuterBounds.maxY > taaOuterBounds.minY) {
							constexpr uint32_t kCopyPadding = 2u;
							const auto mappedInputRect = MapOutputRectToInputRect(
								static_cast<uint32_t>(taaOuterBounds.minX),
								static_cast<uint32_t>(taaOuterBounds.minY),
								static_cast<uint32_t>(taaOuterBounds.maxX),
								static_cast<uint32_t>(taaOuterBounds.maxY),
								eyeWidthOut,
								eyeHeightOut,
								eyeWidthIn,
								eyeHeightIn,
								kCopyPadding);
							if (mappedInputRect.IsValid()) {
								includeInputRect(regions[eye], mappedInputRect.minX, mappedInputRect.minY, mappedInputRect.maxX, mappedInputRect.maxY);
							}
						}

						if (!regions[eye].valid) {
							allRegionsValid = false;
							break;
						}
					}

					if (allRegionsValid) {
						for (uint32_t eye = 0; eye < 2; ++eye) {
							dispatchEyeEncode(eye, regions[eye].minX, regions[eye].minY, regions[eye].maxX, regions[eye].maxY);
						}
						dispatchedRegionEncode = true;
						encodedVRFoveatedRegions = true;
					}
				}
			}

			if (!dispatchedRegionEncode) {
				dispatchFullEyes();
			}
		} else {
			UpscalingDataCB upscalingData{};
			upscalingData.dispatchDim = renderSize;
			upscalingData.trueSamplingDim = renderSize;
			upscalingData.invTrueSamplingDim = { renderSize.x > 0.0f ? 1.0f / renderSize.x : 0.0f, renderSize.y > 0.0f ? 1.0f / renderSize.y : 0.0f };
			upscalingData.seamCenterX = renderSize.x * 0.5f;
			upscalingData.seamHalfWidthPx = 2.0f;
			upscalingData.maskDepthThreshold = 1e-6f;
			upscalingData.vrSeamHardening = 0.0f;
			upscalingData.sourceOffset = { 0.0f, 0.0f };
			upscalingData.outputOffset = { 0.0f, 0.0f };
			upscalingDataCB->Update(upscalingData);

			ID3D11UnorderedAccessView* uavs[4] = {
				reactiveMaskTexture->uav.get(),
				transparencyCompositionMaskTexture->uav.get(),
				requiresCombinedEncodedMotionVectors ? motionVectorCopyTexture->uav.get() : nullptr,
				nullptr
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);

		ID3D11UnorderedAccessView* nullUAV[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAV), nullUAV, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
	};

	encodeUpscalingTextures(false, "Encode Upscaling Textures");

	{
		state->BeginPerfEvent("Upscaling");
		ID3D11Resource* motionVectorResource = globals::game::isVR ? motionVector.texture : motionVectorCopyTexture->resource.get();
		bool dispatched = false;
		static bool loggedFoveatedFallback = false;
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling Dispatch");

		// VR-only workaround: loading transitions can leave DLSS in a slower persistent state
		// until users manually toggle method/preset; force a one-shot feature rebuild instead.
		if (globals::game::isVR && upscaleMethod == UpscaleMethod::kDLSS && pendingDLSSReset.exchange(false, std::memory_order_relaxed)) {
			logger::debug("[Upscaling] LoadingMenu close detected - rebuilding DLSS feature");
			UnbindUpscalingResources();
			streamline.DestroyDLSSResources();
			RequestHistoryReset();
		}

		if (foveatedDispatchRequested) {
			auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			const bool foveatedOutputToSharpener =
				upscaleMethod == UpscaleMethod::kDLSS &&
				settings.sharpnessDLSS > 0.0f &&
				sharpenerTexture &&
				sharpenerTexture->resource &&
				sharpenerTexture->srv &&
				main.UAV;
			ID3D11Resource* foveatedOutput = foveatedOutputToSharpener ? sharpenerTexture->resource.get() : main.texture;
			dispatched = DispatchFoveatedVendorUpscaling(
				upscaleMethod,
				main.texture,
				depth.texture,
				motionVectorResource,
				reactiveMaskTexture->resource.get(),
				transparencyCompositionMaskTexture->resource.get(),
				foveatedOutput);
			if (dispatched && upscaleMethod == UpscaleMethod::kDLSS)
				dlssUpscaleOutputInSharpenerTexture = foveatedOutputToSharpener;
			if (!dispatched) {
				if (!loggedFoveatedFallback) {
					logger::warn("[Upscaling] Foveated vendor dispatch failed; falling back to full-frame {} dispatch.",
						magic_enum::enum_name(upscaleMethod));
					loggedFoveatedFallback = true;
				}
			} else {
				loggedFoveatedFallback = false;
			}
		} else {
			loggedFoveatedFallback = false;
		}

		if (!dispatched) {
			if (encodedVRFoveatedRegions) {
				encodeUpscalingTextures(true, "Encode Upscaling Textures (Fallback Full)");
			}
			if (upscaleMethod == UpscaleMethod::kDLSS) {
				streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource);
			} else if (upscaleMethod == UpscaleMethod::kFSR) {
				fidelityFX.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorResource, settings.sharpnessFSR);
			}
		}

		state->EndPerfEvent();
	}
}

void Upscaling::PerformUpscaling()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling");
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	// Disable dynamic resolution past this point
	runtimeData.dynamicResolutionLock = 1;

	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	UpdateCameraData();
}

void Upscaling::UpdateDepthUpscaleKernelState(JitterCB& a_jitterData, bool a_enableWideKernelLogic)
{
	if (!a_enableWideKernelLogic)
		return;

	constexpr float kEnterWideKernelRatio = 1.55f;
	constexpr float kExitWideKernelRatio = 1.45f;
	const float minScale = std::max(std::min(resolutionScale.x, resolutionScale.y), FLT_EPSILON);
	const float upscaleRatio = 1.0f / minScale;

	if (depthUpscaleUseWideKernel) {
		if (upscaleRatio < kExitWideKernelRatio) {
			depthUpscaleUseWideKernel = false;
		}
	} else {
		if (upscaleRatio > kEnterWideKernelRatio) {
			depthUpscaleUseWideKernel = true;
		}
	}

	a_jitterData.useWideKernel = depthUpscaleUseWideKernel ? 1.0f : 0.0f;
}

void Upscaling::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth");
	// Optimization overview:
	// 1) Early validation exits before issuing GPU work.
	// 2) Wide-kernel depth mode uses hysteresis to avoid frequent toggles.
	// 3) Resource copies are skipped for aliased src/dst to reduce copy churn.

	// (1) Early validation exits
	const bool depthUpscaleActive = IsUpscalingActive();
	const auto upscaleMethod = GetUpscaleMethod();
	const bool isVR = globals::game::isVR;
	const bool vendorUpscaler = upscaleMethod == UpscaleMethod::kDLSS || upscaleMethod == UpscaleMethod::kFSR;
	const bool fullResolutionMaskPath =
		upscaleMethod == UpscaleMethod::kNONE ||
		upscaleMethod == UpscaleMethod::kTAA ||
		(vendorUpscaler && settings.qualityMode == 0);
	const bool repairVRFullResolutionMask =
		isVR &&
		fullResolutionMaskPath &&
		!depthUpscaleActive;

	if (!depthUpscaleActive && !repairVRFullResolutionMask) {
		return;
	}

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState ||
		(depthUpscaleActive && !upscaleDepthStencilState)) {
		return;
	}

	auto screenSize = state->screenSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
	auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

	if (!depth.texture || !depthCopy.texture || !depthCopy.depthSRV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}
	if (depthUpscaleActive &&
		(!depth.views[0] || !refractionNormals.texture || !refractionNormals.textureCopy || !refractionNormals.SRVCopy || !refractionNormals.RTV || !saoCameraZ.RTV)) {
		return;
	}
	if (isVR && !depthCopy.stencilSRV) {
		return;
	}
	if (depthUpscaleActive && isVR && !depthCopy.views[0]) {
		return;
	}

	auto* fullscreenVS = GetUpscaleVS();
	auto* depthUpscalePS = depthUpscaleActive ? GetDepthRefractionUpscalePS() : nullptr;
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS();
	if (!fullscreenVS || !underwaterMaskPS || (depthUpscaleActive && !depthUpscalePS)) {
		return;
	}

	state->BeginPerfEvent("Render Target Upscaling");

	// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader that generates fullscreen triangle using SV_VertexID
	context->VSSetShader(fullscreenVS, nullptr, 0);

	// Set up viewport for fullscreen rendering
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set rasterizer and blend state
	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// Set up jitter/depth-kernel constant buffer for upscaling
	JitterCB jitterData{};
	jitterData.jitter = jitter;
	UpdateDepthUpscaleKernelState(jitterData, depthUpscaleActive);

	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	if (depthUpscaleActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Depth Upscale");

		// Engine copies kMAIN->kMAIN_COPY during 3D scene rendering.
		// In menu/non-3D contexts the engine path may skip this copy.
		auto* ui = globals::game::ui;
		const bool inMenuContext = state->isMapMenuOpen ||
		                           state->isMainMenuOpen ||
		                           state->isLoadingMenuOpen ||
		                           (ui && ui->GameIsPaused());
		if (inMenuContext) {
			CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
		}

		// Clear stencil to be 0xFF
		if (isVR) {
			context->ClearDepthStencilView(depthCopy.views[0], D3D11_CLEAR_STENCIL, 1.0f, 0xFF);
		}

		// Set depth stencil state to write 0x00
		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		CopyResourceIfNonAliased(context, refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		// kSAO_CAMERAZ is at quarter-stereo resolution in VR; the full-stereo viewport would
		// corrupt only the top-left quarter. The engine's ISSAOCameraZ pass populates it correctly.
		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV,
			isVR ? nullptr : saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		context->Draw(3, 0);

		// Depth copy is also used on VR.
		if (isVR) {
			CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
		}
	} else {
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Full Resolution Underwater Mask Depth Copy");

		// Full-resolution paths only need to refresh the underwater mask depth source.
		CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
	}

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Underwater Mask");

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		CopyResourceIfNonAliased(context, underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		// t0: vanilla mask copy, t1: current upscaled depth, t2: current stencil/HAM mask (VR).
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		context->Draw(3, 0);
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void Upscaling::RefreshSubmitStageUnderwaterMask()
{
	ZoneScoped;

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState) {
		return;
	}

	auto screenSize = state->screenSize;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}
	auto renderSize = Util::ConvertToDynamic(screenSize, true);
	if (renderSize.x <= 0.0f || renderSize.y <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];
	if (!depth.texture || !depthCopy.texture || !depthCopy.depthSRV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}

	auto* fullscreenVS = GetUpscaleVS();
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS(true);
	if (!fullscreenVS || !underwaterMaskPS) {
		return;
	}

	TracyD3D11Zone(state->tracyCtx, "Upscaling - Submit Stage Underwater Mask");
	state->BeginPerfEvent("Submit Stage Underwater Mask Refresh");

	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(fullscreenVS, nullptr, 0);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = renderSize.x * 0.5f;
	viewport.Height = renderSize.y * 0.5f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	JitterCB jitterData{};
	jitterData.jitter = jitter;
	UpdateDepthUpscaleKernelState(jitterData, IsUpscalingActive());
	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	CopyResourceIfNonAliased(context, depthCopy.texture, depth.texture);
	CopyResourceIfNonAliased(context, underwaterMask.textureCopy, underwaterMask.texture);

	ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
	context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);
	context->OMSetDepthStencilState(nullptr, 0x00);

	ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
	context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	context->PSSetShader(underwaterMaskPS, nullptr, 0);
	context->Draw(3, 0);

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void Upscaling::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Upscaling - Sharpening");

	if (settings.sharpnessDLSS <= 0.0f)
		return;

	if (!sharpenerTexture)
		return;

	float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
	currentSharpness = exp2(-currentSharpness);

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	context->OMSetRenderTargets(0, nullptr, nullptr);

	if (dlssUpscaleOutputInSharpenerTexture) {
		if (!main.UAV || !sharpenerTexture->srv)
			return;

		rcas.ApplySharpen(sharpenerTexture->srv.get(), main.UAV, currentSharpness);
	} else {
		if (!main.SRV || !main.texture || !sharpenerTexture->uav)
			return;

		rcas.ApplySharpen(main.SRV, sharpenerTexture->uav.get(), currentSharpness);
		context->CopyResource(main.texture, sharpenerTexture->resource.get());
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(const char* a_passName, DynamicResolutionUpsampleStage a_stage)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();
	if (IsVendorUpscalingMethod(upscaleMethod) && upscaling.IsUpscalingActive()) {
		if (IsGameMenuContextActive())
			return false;

		auto context = globals::d3d::context;
		auto renderer = globals::game::renderer;
		if (!context || !renderer)
			return false;

		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		if (!main.texture)
			return false;

		const auto state = globals::state;
		if (!state)
			return false;

		const auto screenSize = state->screenSize;
		const auto renderSize = Util::ConvertToDynamic(screenSize);
		const uint32_t inputWidth = static_cast<uint32_t>(std::max(1.0f, renderSize.x));
		const uint32_t inputHeight = static_cast<uint32_t>(std::max(1.0f, renderSize.y));
		const std::string_view passName(a_passName ? a_passName : "");

		ID3D11ShaderResourceView* psSourceSRV = nullptr;
		ID3D11ShaderResourceView* csSourceSRV = nullptr;
		context->PSGetShaderResources(0, 1, &psSourceSRV);
		context->CSGetShaderResources(0, 1, &csSourceSRV);

		const auto releaseSourceSRVs = [&]() {
			if (psSourceSRV)
				psSourceSRV->Release();
			if (csSourceSRV)
				csSourceSRV->Release();
		};

		ID3D11ShaderResourceView* sourceSRV = nullptr;
		ID3D11Resource* sourceResource = nullptr;
		ID3D11Texture2D* sourceTexture = nullptr;
		ID3D11ShaderResourceView* sourceCandidates[2] = {};
		if (a_stage == DynamicResolutionUpsampleStage::Dispatch) {
			sourceCandidates[0] = csSourceSRV;
			sourceCandidates[1] = psSourceSRV;
		} else {
			sourceCandidates[0] = psSourceSRV;
			sourceCandidates[1] = csSourceSRV;
		}

		const auto tryAcquireSource = [&](ID3D11ShaderResourceView* candidateSRV) {
			if (!candidateSRV)
				return false;

			ID3D11Resource* candidateResource = nullptr;
			candidateSRV->GetResource(&candidateResource);
			if (!candidateResource)
				return false;

			ID3D11Texture2D* candidateTexture = nullptr;
			if (FAILED(candidateResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&candidateTexture))) || !candidateTexture) {
				candidateResource->Release();
				return false;
			}

			D3D11_TEXTURE2D_DESC candidateDesc{};
			candidateTexture->GetDesc(&candidateDesc);
			if (candidateDesc.Width < inputWidth || candidateDesc.Height < inputHeight) {
				candidateTexture->Release();
				candidateResource->Release();
				return false;
			}

			sourceSRV = candidateSRV;
			sourceResource = candidateResource;
			sourceTexture = candidateTexture;
			return true;
		};

		for (uint32_t i = 0; i < 2 && !sourceTexture; ++i) {
			if (!sourceCandidates[i])
				continue;
			if (i == 1 && sourceCandidates[i] == sourceCandidates[0])
				continue;
			(void)tryAcquireSource(sourceCandidates[i]);
		}

		if (!sourceSRV || !sourceResource || !sourceTexture) {
			static bool loggedMissingSource = false;
			if (!loggedMissingSource) {
				logger::warn(
					"[Upscaling] {} replacement could not find a suitable source SRV t0 for {}x{}; falling back to vanilla pass.",
					a_passName,
					inputWidth,
					inputHeight);
				loggedMissingSource = true;
			}
			releaseSourceSRVs();
			return false;
		}

		ID3D11RenderTargetView* outputRTV = nullptr;
		ID3D11DepthStencilView* outputDSV = nullptr;
		context->OMGetRenderTargets(1, &outputRTV, &outputDSV);
		if (!outputRTV) {
			sourceTexture->Release();
			sourceResource->Release();
			releaseSourceSRVs();
			return false;
		}

		ID3D11Resource* outputResource = nullptr;
		outputRTV->GetResource(&outputResource);
		ID3D11Texture2D* outputTexture = nullptr;
		if (!outputResource || FAILED(outputResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&outputTexture))) || !outputTexture) {
			if (outputResource)
				outputResource->Release();
			outputRTV->Release();
			if (outputDSV)
				outputDSV->Release();
			sourceTexture->Release();
			sourceResource->Release();
			releaseSourceSRVs();
			return false;
		}

		const auto releaseRefs = [&]() {
			outputTexture->Release();
			outputResource->Release();
			outputRTV->Release();
			if (outputDSV)
				outputDSV->Release();
			sourceTexture->Release();
			sourceResource->Release();
			releaseSourceSRVs();
		};
		const auto unbindSourceSRV = [&]() {
			ID3D11ShaderResourceView* nullSRV = nullptr;
			context->PSSetShaderResources(0, 1, &nullSRV);
			context->CSSetShaderResources(0, 1, &nullSRV);
		};
		const auto restoreSourceSRVs = [&]() {
			context->PSSetShaderResources(0, 1, &psSourceSRV);
			context->CSSetShaderResources(0, 1, &csSourceSRV);
		};

		if (upscaling.IsSubmitStageUpscalingActive()) {
			if (passName == "ISCopyDynamicFetchDisabled" && submitStageHandoffFrame == state->frameCount) {
				releaseRefs();
				return true;
			}

			unbindSourceSRV();
			context->OMSetRenderTargets(0, nullptr, nullptr);

			auto copyDynamicRegionToTarget = [&](ID3D11Texture2D* targetTexture) {
				if (!targetTexture)
					return false;
				if (targetTexture == sourceTexture)
					return true;

				D3D11_TEXTURE2D_DESC sourceDesc{};
				D3D11_TEXTURE2D_DESC targetDesc{};
				sourceTexture->GetDesc(&sourceDesc);
				targetTexture->GetDesc(&targetDesc);
				if (sourceDesc.SampleDesc.Count != targetDesc.SampleDesc.Count ||
					sourceDesc.Format != targetDesc.Format ||
					inputWidth > sourceDesc.Width ||
					inputHeight > sourceDesc.Height ||
					inputWidth > targetDesc.Width ||
					inputHeight > targetDesc.Height) {
					static bool loggedCopyMismatch = false;
					if (!loggedCopyMismatch) {
						logger::warn(
							"[Upscaling] Submit-stage dynamic-resolution handoff could not copy source: input={}x{} source={}x{} fmt={} samples={} target={}x{} fmt={} samples={}",
							inputWidth,
							inputHeight,
							sourceDesc.Width,
							sourceDesc.Height,
							static_cast<uint32_t>(sourceDesc.Format),
							sourceDesc.SampleDesc.Count,
							targetDesc.Width,
							targetDesc.Height,
							static_cast<uint32_t>(targetDesc.Format),
							targetDesc.SampleDesc.Count);
						loggedCopyMismatch = true;
					}
					return false;
				}

				D3D11_BOX sourceBox{ 0, 0, 0, inputWidth, inputHeight, 1 };
				context->CopySubresourceRegion(targetTexture, 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
				return true;
			};

			const bool copiedToOutput = copyDynamicRegionToTarget(outputTexture);
			context->OMSetRenderTargets(1, &outputRTV, outputDSV);

			if (copiedToOutput) {
				submitStageHandoffFrame = state->frameCount;
				submitStageHandoffTexture = outputTexture;
				releaseRefs();
				if (globals::game::stateUpdateFlags)
					globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
				return true;
			}

			restoreSourceSRVs();
			releaseRefs();
			return false;
		}

		bool sourceReady = sourceTexture == main.texture;
		if (sourceTexture != main.texture) {
			D3D11_TEXTURE2D_DESC sourceDesc{};
			D3D11_TEXTURE2D_DESC mainDesc{};
			sourceTexture->GetDesc(&sourceDesc);
			main.texture->GetDesc(&mainDesc);

			if (sourceDesc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
				sourceDesc.Format == mainDesc.Format &&
				inputWidth <= sourceDesc.Width &&
				inputHeight <= sourceDesc.Height &&
				inputWidth <= mainDesc.Width &&
				inputHeight <= mainDesc.Height) {
				unbindSourceSRV();
				context->OMSetRenderTargets(0, nullptr, nullptr);
				D3D11_BOX sourceBox{ 0, 0, 0, inputWidth, inputHeight, 1 };
				context->CopySubresourceRegion(main.texture, 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
				sourceReady = true;
			} else {
				static bool loggedSourceMismatch = false;
				if (!loggedSourceMismatch) {
					logger::warn(
						"[Upscaling] Dynamic-resolution upsample replacement could not copy source to main: input={}x{} source={}x{} fmt={} samples={} main={}x{} fmt={} samples={}",
						inputWidth,
						inputHeight,
						sourceDesc.Width,
						sourceDesc.Height,
						static_cast<uint32_t>(sourceDesc.Format),
						sourceDesc.SampleDesc.Count,
						mainDesc.Width,
						mainDesc.Height,
						static_cast<uint32_t>(mainDesc.Format),
						mainDesc.SampleDesc.Count);
					loggedSourceMismatch = true;
				}
			}
		}

		if (!sourceReady) {
			releaseRefs();
			return false;
		}

		upscaling.Upscale();
		upscaling.UpscaleDepth();
		if (upscaleMethod == UpscaleMethod::kDLSS)
			upscaling.ApplySharpening();

		if (outputTexture != main.texture) {
			D3D11_TEXTURE2D_DESC outputDesc{};
			D3D11_TEXTURE2D_DESC mainDesc{};
			outputTexture->GetDesc(&outputDesc);
			main.texture->GetDesc(&mainDesc);

			if (outputDesc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
				outputDesc.Format == mainDesc.Format &&
				outputDesc.Width >= mainDesc.Width &&
				outputDesc.Height >= mainDesc.Height) {
				context->OMSetRenderTargets(0, nullptr, nullptr);
				D3D11_BOX sourceBox{ 0, 0, 0, mainDesc.Width, mainDesc.Height, 1 };
				context->CopySubresourceRegion(outputTexture, 0, 0, 0, 0, main.texture, 0, &sourceBox);
				context->OMSetRenderTargets(1, &outputRTV, outputDSV);
			} else {
				static bool loggedCopyMismatch = false;
				if (!loggedCopyMismatch) {
					logger::warn(
						"[Upscaling] Dynamic-resolution upsample replacement could not copy output: main={}x{} fmt={} samples={} target={}x{} fmt={} samples={}",
						mainDesc.Width,
						mainDesc.Height,
						static_cast<uint32_t>(mainDesc.Format),
						mainDesc.SampleDesc.Count,
						outputDesc.Width,
						outputDesc.Height,
						static_cast<uint32_t>(outputDesc.Format),
						outputDesc.SampleDesc.Count);
					loggedCopyMismatch = true;
				}
			}
		}
		context->OMSetRenderTargets(1, &outputRTV, outputDSV);

		releaseRefs();
		if (globals::game::stateUpdateFlags)
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		return true;
	}

	return false;
}

void Upscaling::UpsampleDynamicResolution_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISUpsampleDynamicResolution", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::FullScreenVR_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISFullScreenVR", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::CopyDynamicFetchDisabled_Render::thunk(void* a_imageSpaceShader, void* a_shape, void* a_param)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISCopyDynamicFetchDisabled", DynamicResolutionUpsampleStage::Render))
		return;

	func(a_imageSpaceShader, a_shape, a_param);
}

void Upscaling::UpsampleDynamicResolution_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISUpsampleDynamicResolution", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::FullScreenVR_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISFullScreenVR", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::CopyDynamicFetchDisabled_Dispatch::thunk(void* a_imageSpaceShader, uint32_t a1, uint32_t a2, uint32_t a3)
{
	if (globals::features::upscaling.TryReplaceVanillaDynamicResolutionUpsample("ISCopyDynamicFetchDisabled", DynamicResolutionUpsampleStage::Dispatch))
		return;

	func(a_imageSpaceShader, a1, a2, a3);
}

void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::features::upscaling.ConfigureTAA();
	func(a_state);
	globals::features::upscaling.ConfigureUpscaling(a_state);
}

void Upscaling::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	globals::features::upscaling.PostDisplay();
	func(a1);
}

void Upscaling::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& upscaling = globals::features::upscaling;
	auto upscaleMethod = upscaling.GetUpscaleMethod();

	if (!upscaling.ApplyPendingPostLoadRuntimeReset(upscaleMethod)) {
		func(a_this, a3, a_target, a_4, a_5);
		return;
	}

	const bool vendorMethodSelected = IsVendorUpscalingMethod(upscaleMethod);
	const bool menuPresentationContext = vendorMethodSelected && globals::game::isVR && IsGameMenuContextActive();
	const bool loadingPresentationContext = vendorMethodSelected && globals::game::isVR && IsLoadingMenuContextActive();
	const bool vendorDynamicResolutionActive = vendorMethodSelected && upscaling.IsUpscalingActive();
	if (menuPresentationContext) {
		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		if (loadingPresentationContext)
			upscaling.PrepareFullResolutionPostProcessing();
		else
			upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		if (loadingPresentationContext)
			upscaling.PrepareFullResolutionPostProcessing();
		else
			upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		return;
	}

	if (vendorDynamicResolutionActive && !upscaling.IsSubmitStageUpscalingActive()) {
		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		upscaling.UpscaleDepth();

		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;

		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		return;
	}

	if (upscaling.IsSubmitStageUpscalingActive()) {
		globals::features::vr.InstallSubmitHook();

		if (upscaling.ShouldUseFrameGenerationThisFrame())
			upscaling.CopySharedD3D12Resources();

		upscaling.UpdateHistoryResetState(upscaleMethod);
		upscaling.LatchHistoryResetForCurrentFrame();

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

		upscaling.RefreshSubmitStageUnderwaterMask();

		BSImagespaceShaderISTemporalAA->taaEnabled = false;
		func(a_this, a3, a_target, a_4, a_5);
		BSImagespaceShaderISTemporalAA->taaEnabled = false;

		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
		return;
	}

	if (upscaling.ShouldUseFrameGenerationThisFrame())
		upscaling.CopySharedD3D12Resources();

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		upscaling.PerformUpscaling();
	} else if (globals::game::isVR) {
		upscaling.UpscaleDepth();
	}

	if (upscaleMethod == UpscaleMethod::kDLSS)
		upscaling.ApplySharpening();

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

	if (upscaleMethod == UpscaleMethod::kNONE) {
		// Keep vanilla TAA/water stabilization state untouched when no upscaler is active.
		func(a_this, a3, a_target, a_4, a_5);
		return;
	}

	const bool restoreDynamicResolution = vendorDynamicResolutionActive;
	if (restoreDynamicResolution)
		upscaling.PrepareFullResolutionPostProcessing();

	BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod == UpscaleMethod::kTAA;
	func(a_this, a3, a_target, a_4, a_5);

	BSImagespaceShaderISTemporalAA->taaEnabled = false;

	if (restoreDynamicResolution)
		upscaling.ApplyDynamicResolutionState(globals::game::graphicsState);
}

void Upscaling::SetScissorRect::thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
{
	auto viewport = globals::game::graphicsState;
	auto& runtimeData = viewport->GetRuntimeData();

	if (!runtimeData.dynamicResolutionLock) {
		a_left = static_cast<int>(a_left * runtimeData.dynamicResolutionWidthRatio);
		a_right = static_cast<int>(a_right * runtimeData.dynamicResolutionWidthRatio);

		a_top = static_cast<int>(a_top * runtimeData.dynamicResolutionHeightRatio);
		a_bottom = static_cast<int>(a_bottom * runtimeData.dynamicResolutionHeightRatio);
	}

	func(This, a_left, a_top, a_right, a_bottom);
}

void Upscaling::Main_RenderPrecipitation::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}

void Upscaling::BSFaceGenManager_UpdatePendingCustomizationTextures::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}
