#include "ScreenSpaceShadows.h"

#include "Features/TerrainBlending.h"
#include "FoveatedCommon.h"
#include "GpuPass.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"
#include "Utils/D3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4838 4244)
#include "ScreenSpaceShadows/bend_sss_cpu.h"
#pragma warning(pop)

using RE::RENDER_TARGETS;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceShadows::BendSettings,
	Enable,
	SampleCount,
	VRBaseSamplesAtReference,
	VRCullDistance,
	EnableFoveated,
	SurfaceThickness,
	BilinearThreshold,
	ShadowContrast)

namespace
{
	constexpr uint kSampleCountMin = 1u;
	constexpr uint kSampleCountMax = 4u;
	constexpr float kVRBaseSamplesMin = 16.0f;
	constexpr float kVRBaseSamplesMax = 96.0f;
	constexpr float kVRCullDistanceMin = 0.0f;
	constexpr float kVRCullDistanceMax = 20480.0f;
	constexpr float kSurfaceThicknessMin = 0.005f;
	constexpr float kSurfaceThicknessMax = 0.05f;
	constexpr float kBilinearThresholdMin = 0.02f;
	constexpr float kBilinearThresholdMax = 1.0f;
	constexpr float kShadowContrastMin = 0.0f;
	constexpr float kShadowContrastMax = 4.0f;

	struct FoveatedShadowState
	{
		bool available = false;
		bool active = false;
		float centerScale = FoveatedCommon::kCenterScaleMax;
		float centerHorizontalScale = 1.0f;
		std::array<float2, 2> centerOffsets{};
	};

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		if (!std::isfinite(a_value))
			return a_default;
		return std::clamp(a_value, a_min, a_max);
	}

	void SanitizeBendSettings(ScreenSpaceShadows::BendSettings& a_settings)
	{
		a_settings.Enable = a_settings.Enable ? 1u : 0u;
		a_settings.SampleCount = std::clamp(a_settings.SampleCount, kSampleCountMin, kSampleCountMax);
		a_settings.VRBaseSamplesAtReference = ClampFiniteOrDefault(a_settings.VRBaseSamplesAtReference, kVRBaseSamplesMin, kVRBaseSamplesMax, 44.0f);
		a_settings.VRCullDistance = ClampFiniteOrDefault(a_settings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax, 0.0f);
		a_settings.EnableFoveated = a_settings.EnableFoveated ? 1u : 0u;
		a_settings.SurfaceThickness = ClampFiniteOrDefault(a_settings.SurfaceThickness, kSurfaceThicknessMin, kSurfaceThicknessMax, 0.02f);
		a_settings.BilinearThreshold = ClampFiniteOrDefault(a_settings.BilinearThreshold, kBilinearThresholdMin, kBilinearThresholdMax, 0.02f);
		a_settings.ShadowContrast = ClampFiniteOrDefault(a_settings.ShadowContrast, kShadowContrastMin, kShadowContrastMax, 1.0f);
	}

	bool UseTerrainBlendingDepth()
	{
		const auto& tb = globals::features::terrainBlending;
		return tb.loaded &&
		       tb.settings.Enabled &&
		       tb.blendedDepthTexture &&
		       tb.blendedDepthTexture->srv.get();
	}

	FoveatedShadowState ResolveFoveatedShadowState(const ScreenSpaceShadows::BendSettings& a_settings)
	{
		FoveatedShadowState state{};
		if (!globals::game::isVR)
			return state;

		const auto& upscaling = globals::features::upscaling;
		const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
		state.available = profile.available;
		if (!state.available)
			return state;

		state.centerScale = FoveatedCommon::ClampCenterScale(profile.sharedVisibleScale);
		state.centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(profile.centerHorizontalScale);
		state.centerOffsets = profile.centerOffsets;
		state.available = FoveatedCommon::IsActiveCoverage(state.centerScale);
		state.active = state.available && a_settings.EnableFoveated != 0;
		return state;
	}

	FoveatedCommon::DispatchBounds BuildFoveatedBounds(
		const FoveatedShadowState& a_state,
		uint32_t a_eyeIndex,
		uint32_t a_eyeMinX,
		uint32_t a_eyeMaxX,
		uint32_t a_frameHeight)
	{
		const auto offset = a_state.centerOffsets[std::min<size_t>(a_eyeIndex, a_state.centerOffsets.size() - 1)];
		return FoveatedCommon::BuildCenteredDispatchBounds(
			a_eyeMinX,
			a_eyeMaxX,
			a_frameHeight,
			a_state.centerScale,
			offset.x,
			offset.y,
			FoveatedCommon::kCenterFeather,
			a_state.centerHorizontalScale);
	}
}

void ScreenSpaceShadows::DrawSettings()
{
	bool enabled = bendSettings.Enable != 0;
	if (ImGui::Checkbox("Enable", &enabled))
		bendSettings.Enable = enabled ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Turns screen space shadows on or off.");
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("Performance");
	ImGui::Separator();

	int sampleCount = static_cast<int>(bendSettings.SampleCount);
	if (ImGui::SliderInt("Sample Count Multiplier", &sampleCount, static_cast<int>(kSampleCountMin), static_cast<int>(kSampleCountMax)))
		bendSettings.SampleCount = static_cast<uint>(std::clamp(sampleCount, static_cast<int>(kSampleCountMin), static_cast<int>(kSampleCountMax)));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Higher values improve detail but cost more performance. In VR, values >1 are not recommended.");
	}

	if (globals::game::isVR) {
		ImGui::SliderFloat("Baseline Samples", &bendSettings.VRBaseSamplesAtReference, kVRBaseSamplesMin, kVRBaseSamplesMax, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Raises or lowers VR shadow quality and GPU cost.");
		}

		ImGui::SliderFloat("Shadow Cull Distance", &bendSettings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("0 disables. Lower values improve performance but remove distant shadows.");
		}
		bendSettings.VRCullDistance = std::clamp(bendSettings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax);
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("Fine-tuning");
	ImGui::Separator();

	ImGui::SliderFloat("Surface Thickness", &bendSettings.SurfaceThickness, kSurfaceThicknessMin, kSurfaceThicknessMax);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Makes contact shadows thinner or thicker.");
	}

	ImGui::SliderFloat("Bilinear Threshold", &bendSettings.BilinearThreshold, kBilinearThresholdMin, kBilinearThresholdMax);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Balances edge sharpness versus smoothness.");
	}

	ImGui::SliderFloat("Shadow Contrast", &bendSettings.ShadowContrast, kShadowContrastMin, kShadowContrastMax);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Controls overall shadow darkness.");
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

void ScreenSpaceShadows::DrawPerformanceSettings(bool)
{
	bool enabled = bendSettings.Enable != 0;
	if (ImGui::Checkbox("Enable", &enabled))
		bendSettings.Enable = enabled ? 1u : 0u;

	int sampleCount = static_cast<int>(bendSettings.SampleCount);
	if (ImGui::SliderInt("Sample Count Multiplier", &sampleCount, static_cast<int>(kSampleCountMin), static_cast<int>(kSampleCountMax)))
		bendSettings.SampleCount = static_cast<uint>(std::clamp(sampleCount, static_cast<int>(kSampleCountMin), static_cast<int>(kSampleCountMax)));

	if (globals::game::isVR) {
		ImGui::SliderFloat("VR Baseline Samples", &bendSettings.VRBaseSamplesAtReference, kVRBaseSamplesMin, kVRBaseSamplesMax, "%.0f");
		ImGui::SliderFloat("Shadow Cull Distance", &bendSettings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax, "%.0f units");
		bendSettings.VRCullDistance = std::clamp(bendSettings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax);
	}
}

void ScreenSpaceShadows::DrawEssentialSettings()
{
	bool enabled = bendSettings.Enable != 0;
	if (ImGui::Checkbox("Enable", &enabled))
		bendSettings.Enable = enabled ? 1u : 0u;
}

json ScreenSpaceShadows::CapturePerformanceSettingsState() const
{
	return {
		{ "Settings", bendSettings },
		{ "EnableStereoSync", enableStereoSync },
		{ "UseStereoReproject", useStereoReproject }
	};
}

void ScreenSpaceShadows::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	if (a_enabled) {
		bendSettings = BendSettings{};
		enableStereoSync = false;
		useStereoReproject = false;
		return;
	}

	bendSettings.Enable = 0u;
}

json ScreenSpaceShadows::CapturePerformanceCostMeasurementState() const
{
	return CapturePerformanceSettingsState();
}

void ScreenSpaceShadows::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	if (a_state.contains("Settings"))
		bendSettings = a_state.at("Settings").get<BendSettings>();
	enableStereoSync = a_state.value("EnableStereoSync", enableStereoSync);
	useStereoReproject = a_state.value("UseStereoReproject", useStereoReproject);
}

void ScreenSpaceShadows::DrawFoveationSettings()
{
	if (!globals::game::isVR) {
		ImGui::TextDisabled("Screen Space Shadows FOV is available only in VR.");
		return;
	}

	const FoveatedShadowState foveatedState = ResolveFoveatedShadowState(bendSettings);
	const bool foveatedAvailable = foveatedState.available;
	const bool featureRuntimeActive = loaded && bendSettings.Enable != 0;
	bool foveatedEnabled = bendSettings.EnableFoveated != 0;
	{
		auto foveatedGuard = Util::DisableGuard(!featureRuntimeActive || !foveatedAvailable);
		if (ImGui::Checkbox("Screen Space Shadows FOV", &foveatedEnabled))
			bendSettings.EnableFoveated = foveatedEnabled ? 1u : 0u;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Focuses shadow detail in the clearest part of the VR view.");
		ImGui::TextUnformatted("Can improve performance, but shadows may fade near the edge of your view.");
		if (!loaded)
			ImGui::TextUnformatted("Requires Screen Space Shadows.");
		else if (bendSettings.Enable == 0)
			ImGui::TextUnformatted("Requires Screen Space Shadows to be enabled.");
		else if (!foveatedAvailable)
			ImGui::TextUnformatted("Requires active foveated upscaling.");
	}

	ImGui::TextDisabled("%s", foveatedEnabled && featureRuntimeActive && foveatedState.active ? "active" : "inactive");
}

void ScreenSpaceShadows::InvalidateRaymarchShaders()
{
	for (auto& variant : raymarchShaderVariants) {
		variant.left.Reset();
		variant.right.Reset();
		variant.valid = false;
		variant.sampleCount = 0;
		variant.usesTerrainBlendingDepth = false;
		variant.lastUse = 0;
	}
	raymarchShaderUseCounter = 0;
	compiledSampleCount = 0;
}

void ScreenSpaceShadows::ClearShaderCache()
{
	InvalidateRaymarchShaders();
	stereoSyncCS.Reset();
	stereoSyncUsesTerrainBlendingDepth = false;
	stereoReprojectCS.Reset();
	stereoReprojectUsesTerrainBlendingDepth = false;
}

uint ScreenSpaceShadows::GetScaledSampleCount(bool a_dynamic)
{
	auto screenSize = globals::state->screenSize;

	if (a_dynamic)
		screenSize = Util::ConvertToDynamic(globals::state->screenSize);
	return GetScaledSampleCountForRenderSize(screenSize);
}

uint ScreenSpaceShadows::GetScaledSampleCountForRenderSize(float2 a_screenSize) const
{
	float2 renderSize = a_screenSize;
	if (globals::game::isVR) {
		// Per-eye raymarch dispatch in VR.
		renderSize.x *= 0.5f;
	}

	if (globals::game::isVR) {
		// Per-eye VR scaling against a 4.5 MP reference eye.
		constexpr float kReferencePerEyeArea = 4'500'000.0f;
		float currentArea = renderSize.x * renderSize.y;
		float areaScale = std::sqrt(currentArea / kReferencePerEyeArea);
		float baseSamples = std::max(1.0f, bendSettings.VRBaseSamplesAtReference);
		uint scaledSampleCount = static_cast<uint>(std::round(bendSettings.SampleCount * baseSamples * areaScale));
		return std::max(1u, scaledSampleCount);
	}

	// Scale sample count based on both dimensions relative to 1920x1080 reference
	float2 referenceRes = { 1920.0f, 1080.0f };
	float referenceArea = referenceRes.x * referenceRes.y;
	float currentArea = renderSize.x * renderSize.y;
	float areaScale = std::sqrt(currentArea / referenceArea);
	uint scaledSampleCount = static_cast<uint>(std::round(bendSettings.SampleCount * 60 * areaScale));

	// Quantize to steps of 8 to prevent frequent recompilation from small DRS oscillations
	scaledSampleCount = ((scaledSampleCount + 7u) / 8u) * 8u;
	scaledSampleCount = std::max(scaledSampleCount, 8u);

	return scaledSampleCount;
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarch()
{
	return GetOrCreateRaymarchShader(GetScaledSampleCount(false), false);
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarchRight()
{
	return GetOrCreateRaymarchShader(GetScaledSampleCount(false), true);
}

ID3D11ComputeShader* ScreenSpaceShadows::GetStereoReprojectCS()
{
	const bool useTerrainBlendingDepth = UseTerrainBlendingDepth();
	if (stereoReprojectUsesTerrainBlendingDepth != useTerrainBlendingDepth) {
		stereoReprojectCS.Reset();
		stereoReprojectUsesTerrainBlendingDepth = useTerrainBlendingDepth;
	}

	std::vector<std::pair<const char*, const char*>> defines{ { "VR", "" }, { "FRAMEBUFFER", "" } };
	if (useTerrainBlendingDepth)
		defines.push_back({ "TERRAIN_BLENDING", "" });
	return stereoReprojectCS.Get(L"Data\\Shaders\\ScreenSpaceShadows\\ShadowReprojectCS.hlsl", defines, "cs_5_0", "main", "ScreenSpaceShadows::StereoReprojectCS");
}

ID3D11ComputeShader* ScreenSpaceShadows::GetOrCreateRaymarchShader(
	uint a_sampleCount,
	bool a_rightEye,
	Util::ShaderCompileTiming* a_timing)
{
	const bool useTerrainBlendingDepth = UseTerrainBlendingDepth();
	const std::size_t variantCount =
		ScreenSpaceShadowsCachePolicy::ActiveVariantCount(
			globals::game::isVR,
			raymarchShaderVariants.size());
	const std::size_t selectedIndex =
		ScreenSpaceShadowsCachePolicy::SelectVariantIndex(
			raymarchShaderVariants,
			variantCount,
			a_sampleCount,
			useTerrainBlendingDepth);
	if (selectedIndex == ScreenSpaceShadowsCachePolicy::kNoVariant)
		return nullptr;
	RaymarchShaderVariant* selected =
		std::addressof(raymarchShaderVariants[selectedIndex]);
	if (!selected->valid || selected->sampleCount != a_sampleCount ||
		selected->usesTerrainBlendingDepth != useTerrainBlendingDepth) {
		selected->left.Reset();
		selected->right.Reset();
		selected->valid = true;
		selected->sampleCount = a_sampleCount;
		selected->usesTerrainBlendingDepth = useTerrainBlendingDepth;
	}
	selected->lastUse = ++raymarchShaderUseCounter;
	compiledSampleCount = a_sampleCount;
	auto& shader = a_rightEye ? selected->right : selected->left;
	switch (ScreenSpaceShadowsCachePolicy::SelectShaderLookupAction(
		shader.get() != nullptr,
		shader.HasFailed())) {
	case ScreenSpaceShadowsCachePolicy::ShaderLookupAction::ReturnCached:
		return shader.get();
	case ScreenSpaceShadowsCachePolicy::ShaderLookupAction::ReturnFailure:
		return nullptr;
	case ScreenSpaceShadowsCachePolicy::ShaderLookupAction::Compile:
		break;
	}

	std::string sampleCount = std::format("{}", a_sampleCount);
	std::vector<std::pair<const char*, const char*>> defines{ { "SAMPLE_COUNT", sampleCount.c_str() } };
	if (a_rightEye)
		defines.push_back({ "RIGHT", "" });
	if (useTerrainBlendingDepth)
		defines.push_back({ "TERRAIN_BLENDING", "" });

	return shader.Get(
		L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl",
		defines,
		"cs_5_0",
		"main",
		a_rightEye ? "ScreenSpaceShadows::RaymarchRightCS" : "ScreenSpaceShadows::RaymarchCS",
		a_timing);
}

bool ScreenSpaceShadows::PrewarmVRRenderScaleShaders(
	uint32_t a_combinedWidth,
	uint32_t a_height,
	Util::ShaderCompileTiming* a_timing)
{
	if (!globals::game::isVR || !a_combinedWidth || !a_height)
		return false;

	const uint sampleCount = GetScaledSampleCountForRenderSize({
		static_cast<float>(a_combinedWidth),
		static_cast<float>(a_height),
	});
	const bool leftReady =
		GetOrCreateRaymarchShader(sampleCount, false, a_timing) != nullptr;
	return leftReady &&
	       GetOrCreateRaymarchShader(sampleCount, true, a_timing) != nullptr;
}

void ScreenSpaceShadows::DrawShadows()
{
	ZoneScopedS(8);
	CS_GPU_PASS("ScreenSpaceShadows::DrawShadows");
	auto state = globals::state;

	auto context = globals::d3d::context;

	auto accumulator = *globals::game::currentAccumulator.get();
	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

	auto& directionNi = dirLight->GetWorldDirection();
	float3 light = { directionNi.x, directionNi.y, directionNi.z };
	light.Normalize();
	float4 lightProjection = float4(-light.x, -light.y, -light.z, 0.0f);

	// Helper lambda to calculate light projection for a given eye
	auto CalculateLightProjection = [&](uint32_t eyeIndex = 0) -> std::array<float, 4> {
		const auto& viewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex);
		auto viewProjMat = viewProj.Transpose();
		auto projectedLight = DirectX::SimpleMath::Vector4::Transform(lightProjection, viewProjMat);
		return { projectedLight.x, projectedLight.y, projectedLight.z, projectedLight.w };
	};

	auto lightProjectionF = CalculateLightProjection(0);

	float2 renderSize = Util::ConvertToDynamic(state->screenSize);
	int viewportSize[2] = { (int)renderSize.x, (int)renderSize.y };

	if (globals::game::isVR)
		viewportSize[0] /= 2;

	const FoveatedShadowState foveatedState = ResolveFoveatedShadowState(bendSettings);

	// Setup common render state.
	// SSS always uses 24/32-bit depth, never the R16_UNORM half-precision path.
	// With active TerrainBlending depth the SRV is R32_FLOAT (blendedDepthTexture);
	// otherwise, the game's kPOST_ZPREPASS_COPY (R24_UNORM_X8_TYPELESS).
	// The shader's DepthTexture declaration is conditional on TERRAIN_BLENDING:
	// `<float>` for the R32_FLOAT path, `<unorm float>` for the R24_UNORM path.
	auto* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	context->CSSetShaderResources(0, 1, &depthSRV);

	auto uav = screenSpaceShadowsTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetSamplers(0, 1, &pointBorderSampler);

	auto buffer = raymarchCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	auto ClearRaymarchState = [&] {
		ID3D11ShaderResourceView* views[1]{ nullptr };
		context->CSSetShaderResources(0, 1, views);

		ID3D11UnorderedAccessView* uavs[1]{ nullptr };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		context->CSSetShader(nullptr, nullptr, 0);

		ID3D11SamplerState* sampler = nullptr;
		context->CSSetSamplers(0, 1, &sampler);

		buffer = nullptr;
		context->CSSetConstantBuffers(1, 1, &buffer);
	};

	auto viewport = globals::game::graphicsState;

	float2 dynamicRes = { viewport->GetRuntimeData().dynamicResolutionWidthRatio, viewport->GetRuntimeData().dynamicResolutionHeightRatio };
	uint32_t depthWidth = 0;
	uint32_t depthHeight = 0;
	if (Util::TryGetDepthSrvDimensions(depthSRV, depthWidth, depthHeight)) {
		if (!globals::game::isVR) {
			dynamicRes.x = static_cast<float>(viewportSize[0]) / static_cast<float>(depthWidth);
			dynamicRes.y = static_cast<float>(viewportSize[1]) / static_cast<float>(depthHeight);
		} else {
			// Always use hardened depth-layout scaling in VR.
			const float combinedX = (static_cast<float>(viewportSize[0]) * 2.0f) / static_cast<float>(depthWidth);
			const float perEyeX = static_cast<float>(viewportSize[0]) / static_cast<float>(depthWidth);
			dynamicRes.y = static_cast<float>(viewportSize[1]) / static_cast<float>(depthHeight);

			switch (Util::DetectVRDepthLayout(depthWidth, viewportSize[0])) {
			case Util::VRDepthLayout::CombinedStereo:
				dynamicRes.x = combinedX;
				break;
			case Util::VRDepthLayout::PerEye:
				dynamicRes.x = perEyeX;
				break;
			case Util::VRDepthLayout::Unknown:
			default:
				// Ambiguous layout: pick whichever is closer to runtime DR ratio.
				dynamicRes.x = std::abs(combinedX - dynamicRes.x) <= std::abs(perEyeX - dynamicRes.x) ? combinedX : perEyeX;
				break;
			}
		}

		dynamicRes.x = std::clamp(dynamicRes.x, 0.25f, 2.0f);
		dynamicRes.y = std::clamp(dynamicRes.y, 0.25f, 2.0f);
	}

	auto* raymarchLeft = GetComputeRaymarch();
	const bool stereoReprojectResourcesReady =
		stereoSyncCopyTex &&
		stereoSyncCopyTex->resource &&
		stereoSyncCopyTex->srv &&
		stereoSyncCB;
	const bool stereoReprojectActive =
		globals::game::isVR &&
		enableStereoSync &&
		stereoReprojectResourcesReady &&
		useStereoReproject &&
		GetStereoReprojectCS();
	ID3D11ComputeShader* raymarchRight = (globals::game::isVR && !stereoReprojectActive) ? GetComputeRaymarchRight() : nullptr;
	if (!raymarchLeft || (globals::game::isVR && !stereoReprojectActive && !raymarchRight)) {
		ClearRaymarchState();
		return;
	}

	uint maxCompiledSamples = compiledSampleCount > 0 ? compiledSampleCount : GetScaledSampleCount(false);

	uint dynamicSampleCount = std::min(GetScaledSampleCount(true), maxCompiledSamples);
	dynamicSampleCount = std::max(dynamicSampleCount, 1u);
	uint dynamicReadCount = (dynamicSampleCount / 64 + 2);
	// Shared dispatch logic for both VR and non-VR
	auto DispatchEye = [&](const char* eyeName, ID3D11ComputeShader* shader, uint32_t eyeIndex, const float* lightProj,
						   float invTexSizeX, float invTexSizeY) {
		const char* profileName = eyeName ? (eyeIndex == 0 ? "ScreenSpaceShadows::RayMarch(Left Eye)" : "ScreenSpaceShadows::RayMarch(Right Eye)") : "ScreenSpaceShadows::RayMarch";
		CS_GPU_PASS_DYNAMIC(profileName);

		context->CSSetShader(shader, nullptr, 0);

		int minRenderBounds[2] = { 0, 0 };
		int maxRenderBounds[2] = { viewportSize[0], viewportSize[1] };
		if (foveatedState.active) {
			const auto bounds = BuildFoveatedBounds(foveatedState, eyeIndex, 0u, static_cast<uint32_t>(viewportSize[0]), static_cast<uint32_t>(viewportSize[1]));
			if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY)
				return;

			minRenderBounds[0] = bounds.minX;
			minRenderBounds[1] = bounds.minY;
			maxRenderBounds[0] = bounds.maxX;
			maxRenderBounds[1] = bounds.maxY;
		}

		auto dispatchList = Bend::BuildDispatchList(const_cast<float*>(lightProj), viewportSize, minRenderBounds, maxRenderBounds);

		for (int i = 0; i < dispatchList.DispatchCount; i++) {
			auto dispatchData = dispatchList.Dispatch[i];

			{
				CS_GPU_PASS("SSS::RayMarch::DispatchEyeCB");

				RaymarchCB data{};
				data.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
				data.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
				data.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
				data.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];

				data.WaveOffset[0] = dispatchData.WaveOffset_Shader[0];
				data.WaveOffset[1] = dispatchData.WaveOffset_Shader[1];

				data.FarDepthValue = 1.0f;
				data.NearDepthValue = 0.0f;

				data.DynamicRes = dynamicRes;
				data.DynamicSampleCount = dynamicSampleCount;
				data.DynamicReadCount = dynamicReadCount;
				data.FoveatedData0[0] = foveatedState.centerScale;
				data.FoveatedData0[1] = FoveatedCommon::kCenterFeather;
				data.FoveatedData0[2] = foveatedState.centerHorizontalScale;
				data.FoveatedData0[3] = foveatedState.active ? 1.0f : 0.0f;
				const auto centerOffset = foveatedState.centerOffsets[std::min<size_t>(eyeIndex, foveatedState.centerOffsets.size() - 1)];
				data.FoveatedCenterOffset[0] = centerOffset.x;
				data.FoveatedCenterOffset[1] = centerOffset.y;
				data.FoveatedCenterOffset[2] = 0.0f;
				data.FoveatedCenterOffset[3] = 0.0f;

				data.InvDepthTextureSize[0] = invTexSizeX;
				data.InvDepthTextureSize[1] = invTexSizeY;

				data.settings = bendSettings;
				if (!globals::game::isVR)
					data.settings.VRCullDistance = 0.0f;

				raymarchCB->Update(data);
			}

			{
				CS_GPU_PASS("SSS::RayMarch::DispatchEyeSweep");
				context->Dispatch(dispatchData.WaveCount[0], dispatchData.WaveCount[1], dispatchData.WaveCount[2]);
			}
		}
	};

	float InvTexSizeX = 1.0f / (float)viewportSize[0];
	float InvTexSizeY = 1.0f / (float)viewportSize[1];

	if (!globals::game::isVR) {
		DispatchEye(nullptr, raymarchLeft, 0, lightProjectionF.data(), InvTexSizeX, InvTexSizeY);
	} else {
		{
			CS_GPU_PASS("SSS::LeftEye");
			DispatchEye("Left Eye", raymarchLeft, 0, lightProjectionF.data(), InvTexSizeX, InvTexSizeY);
		}

		if (!stereoReprojectActive) {
			auto lightProjectionRightF = CalculateLightProjection(1);
			{
				CS_GPU_PASS("SSS::RightEye");
				DispatchEye("Right Eye", raymarchRight, 1, lightProjectionRightF.data(), InvTexSizeX, InvTexSizeY);
			}
		}
	}

	ClearRaymarchState();
}

void ScreenSpaceShadows::DrawStereoSync()
{
	if (!globals::game::isVR ||
		!enableStereoSync ||
		!stereoSyncCopyTex ||
		!stereoSyncCopyTex->resource ||
		!stereoSyncCopyTex->srv ||
		!stereoSyncCB) {
		return;
	}

	const bool useTerrainBlendingDepth = UseTerrainBlendingDepth();
	if (stereoSyncUsesTerrainBlendingDepth != useTerrainBlendingDepth) {
		stereoSyncCS.Reset();
		stereoSyncUsesTerrainBlendingDepth = useTerrainBlendingDepth;
	}

	ID3D11ComputeShader* stereoCS = nullptr;
	if (useStereoReproject)
		stereoCS = GetStereoReprojectCS();
	const bool usingSync = !stereoCS;
	if (!stereoCS) {
		std::vector<std::pair<const char*, const char*>> defines{ { "VR", "" }, { "FRAMEBUFFER", "" } };
		if (useTerrainBlendingDepth)
			defines.push_back({ "TERRAIN_BLENDING", "" });
		stereoCS = stereoSyncCS.Get(L"Data\\Shaders\\ScreenSpaceShadows\\StereoSyncCS.hlsl", defines, "cs_5_0", "main", "ScreenSpaceShadows::StereoSyncCS");
	}
	if (!stereoCS)
		return;

	ZoneScoped;
	CS_GPU_PASS_SELECT(usingSync, "ScreenSpaceShadows::StereoSync", "ScreenSpaceShadows::StereoReproject");

	auto context = globals::d3d::context;

	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize);
	const uint32_t frameWidth = static_cast<uint32_t>(resolution.x);
	const uint32_t frameHeight = static_cast<uint32_t>(resolution.y);
	const FoveatedShadowState foveatedState = ResolveFoveatedShadowState(bendSettings);
	if (frameWidth == 0 || frameHeight == 0)
		return;

	const bool foveatedStereoSync = foveatedState.active && frameWidth > 1;
	std::array<FoveatedCommon::DispatchBounds, 2> syncBounds{};
	if (foveatedStereoSync) {
		const uint32_t eyeWidth = frameWidth >> 1;
		syncBounds[0] = BuildFoveatedBounds(foveatedState, 0, 0, eyeWidth, frameHeight);
		syncBounds[1] = BuildFoveatedBounds(foveatedState, 1, eyeWidth, frameWidth, frameHeight);
	} else {
		syncBounds[0].minX = 0;
		syncBounds[0].minY = 0;
		syncBounds[0].maxX = static_cast<int>(frameWidth);
		syncBounds[0].maxY = static_cast<int>(frameHeight);
	}

	auto ForEachSyncBounds = [&](auto&& a_fn) {
		a_fn(syncBounds[0], 0u);
		if (foveatedStereoSync)
			a_fn(syncBounds[1], 1u);
	};

	auto CopyStereoSyncSource = [&] {
		if (!foveatedStereoSync || !stereoSyncCopyTex->uav) {
			context->CopyResource(stereoSyncCopyTex->resource.get(), screenSpaceShadowsTexture->resource.get());
			return;
		}

		const FLOAT white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(stereoSyncCopyTex->uav.get(), white);
		ForEachSyncBounds([&](const FoveatedCommon::DispatchBounds& bounds, uint32_t) {
			if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY)
				return;

			D3D11_BOX srcBox{
				static_cast<UINT>(bounds.minX),
				static_cast<UINT>(bounds.minY),
				0u,
				static_cast<UINT>(bounds.maxX),
				static_cast<UINT>(bounds.maxY),
				1u
			};
			context->CopySubresourceRegion(
				stereoSyncCopyTex->resource.get(),
				0,
				srcBox.left,
				srcBox.top,
				0,
				screenSpaceShadowsTexture->resource.get(),
				0,
				&srcBox);
		});
	};

	// Same 24/32-bit depth path as the raymarch. SrcDepthTexture's HLSL type is
	// conditional on TERRAIN_BLENDING via the active depth source.
	auto* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	ID3D11ShaderResourceView* srvs[2]{ depthSRV, stereoSyncCopyTex->srv.get() };
	ID3D11UnorderedAccessView* uavs[1]{ screenSpaceShadowsTexture->uav.get() };

	{
		CopyStereoSyncSource();

		Util::BindGlobalConstantBuffersForCS(context);
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(stereoCS, nullptr, 0);

		auto DispatchSyncBounds = [&](const FoveatedCommon::DispatchBounds& bounds, uint32_t eyeIndex) {
			if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY)
				return;

			const uint32_t dispatchWidth = static_cast<uint32_t>(bounds.maxX - bounds.minX);
			const uint32_t dispatchHeight = static_cast<uint32_t>(bounds.maxY - bounds.minY);
			if (dispatchWidth == 0 || dispatchHeight == 0)
				return;

			StereoSyncCB cbData{};
			cbData.FrameDim[0] = resolution.x;
			cbData.FrameDim[1] = resolution.y;
			cbData.RcpFrameDim[0] = 1.0f / resolution.x;
			cbData.RcpFrameDim[1] = 1.0f / resolution.y;
			cbData.DispatchBase[0] = static_cast<float>(bounds.minX);
			cbData.DispatchBase[1] = static_cast<float>(bounds.minY);
			cbData.DispatchExtent[0] = static_cast<float>(dispatchWidth);
			cbData.DispatchExtent[1] = static_cast<float>(dispatchHeight);
			cbData.FoveatedData0[0] = foveatedState.centerScale;
			cbData.FoveatedData0[1] = FoveatedCommon::kCenterFeather;
			cbData.FoveatedData0[2] = foveatedState.centerHorizontalScale;
			cbData.FoveatedData0[3] = foveatedState.active ? 1.0f : 0.0f;
			const auto centerOffset = foveatedState.centerOffsets[std::min<size_t>(eyeIndex, foveatedState.centerOffsets.size() - 1)];
			cbData.FoveatedCenterOffset[0] = centerOffset.x;
			cbData.FoveatedCenterOffset[1] = centerOffset.y;
			cbData.FoveatedCenterOffset[2] = 0.0f;
			cbData.FoveatedCenterOffset[3] = 0.0f;

			stereoSyncCB->Update(cbData);
			auto cbPtr = stereoSyncCB->CB();
			context->CSSetConstantBuffers(1, 1, &cbPtr);

			const uint32_t groupsX = (dispatchWidth + 7u) / 8u;
			const uint32_t groupsY = (dispatchHeight + 7u) / 8u;
			context->Dispatch(groupsX, groupsY, 1);
		};

		ForEachSyncBounds(DispatchSyncBounds);
	}

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	uavs[0] = nullptr;
	ID3D11Buffer* cbPtr = nullptr;
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void ScreenSpaceShadows::Prepass()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	auto clearOutputView = [&] {
		ID3D11ShaderResourceView* view = nullptr;
		context->PSSetShaderResources(45, 1, &view);
	};
	if (!screenSpaceShadowsTexture ||
		!screenSpaceShadowsTexture->resource ||
		!screenSpaceShadowsTexture->uav ||
		!screenSpaceShadowsTexture->srv ||
		!raymarchCB ||
		!pointBorderSampler) {
		clearOutputView();
		return;
	}

	float white[4] = { 1, 1, 1, 1 };
	context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTexture->uav.get(), white);

	if (auto sky = globals::game::sky)
		if (bendSettings.Enable && sky->mode.get() == RE::Sky::Mode::kFull) {
			DrawShadows();
			DrawStereoSync();
		}

	auto view = screenSpaceShadowsTexture->srv.get();
	context->PSSetShaderResources(45, 1, &view);
}

void ScreenSpaceShadows::LoadSettings(json& o_json)
{
	bendSettings = o_json;
	enableStereoSync = o_json.is_object() ? o_json.value("EnableStereoSync", false) : false;
	useStereoReproject = o_json.is_object() ? o_json.value("UseStereoReproject", false) : false;
	SanitizeBendSettings(bendSettings);
}

void ScreenSpaceShadows::SaveSettings(json& o_json)
{
	o_json = bendSettings;
	o_json["EnableStereoSync"] = enableStereoSync;
	o_json["UseStereoReproject"] = useStereoReproject;
}

void ScreenSpaceShadows::RestoreDefaultSettings()
{
	bendSettings = {};
	enableStereoSync = false;
	useStereoReproject = false;
}

bool ScreenSpaceShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

void ScreenSpaceShadows::SetupResources()
{
	auto device = globals::d3d::device;
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != device) {
		ClearShaderCache();
		shaderDevice = device;
	}

	delete raymarchCB;
	raymarchCB = nullptr;
	raymarchCB = new ConstantBuffer(ConstantBufferDesc<RaymarchCB>());

	delete stereoSyncCB;
	stereoSyncCB = nullptr;
	if (globals::game::isVR) {
		stereoSyncCB = new ConstantBuffer(ConstantBufferDesc<StereoSyncCB>());
	}

	if (pointBorderSampler) {
		pointBorderSampler->Release();
		pointBorderSampler = nullptr;
	}
	delete screenSpaceShadowsTexture;
	screenSpaceShadowsTexture = nullptr;
	delete stereoSyncCopyTex;
	stereoSyncCopyTex = nullptr;

	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		samplerDesc.BorderColor[0] = 1.0f;
		samplerDesc.BorderColor[1] = 1.0f;
		samplerDesc.BorderColor[2] = 1.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointBorderSampler));
	}

	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return;

		static bool loggedMissingShadowMask = false;
		static bool loggedShadowResourceAllocationFailure = false;
		auto shadowMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK];
		if (!shadowMask.texture || !shadowMask.SRV) {
			if (!loggedMissingShadowMask) {
				logger::warn("[ScreenSpaceShadows] Skipping setup because kSHADOW_MASK is unavailable after render-target recreation.");
				loggedMissingShadowMask = true;
			}
			return;
		}
		loggedMissingShadowMask = false;

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		shadowMask.texture->GetDesc(&texDesc);
		shadowMask.SRV->GetDesc(&srvDesc);

		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		srvDesc.Format = texDesc.Format;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		try {
			auto screenSpaceShadowsReplacement = std::make_unique<Texture2D>(texDesc);
			screenSpaceShadowsReplacement->CreateSRV(srvDesc);
			screenSpaceShadowsReplacement->CreateUAV(uavDesc);

			std::unique_ptr<Texture2D> stereoSyncCopyReplacement;
			if (globals::game::isVR) {
				stereoSyncCopyReplacement = std::make_unique<Texture2D>(texDesc);
				stereoSyncCopyReplacement->CreateSRV(srvDesc);
				stereoSyncCopyReplacement->CreateUAV(uavDesc);
			}

			screenSpaceShadowsTexture = screenSpaceShadowsReplacement.release();
			stereoSyncCopyTex = stereoSyncCopyReplacement.release();
			loggedShadowResourceAllocationFailure = false;
		} catch (const std::exception& e) {
			if (!loggedShadowResourceAllocationFailure) {
				logger::warn("[ScreenSpaceShadows] Skipping setup because shadow resources could not be allocated after render-target recreation: {}", e.what());
				loggedShadowResourceAllocationFailure = true;
			}
			return;
		} catch (...) {
			if (!loggedShadowResourceAllocationFailure) {
				logger::warn("[ScreenSpaceShadows] Skipping setup because shadow resources could not be allocated after render-target recreation.");
				loggedShadowResourceAllocationFailure = true;
			}
			return;
		}
	}
}

void ScreenSpaceShadows::SetupRenderTargetResources()
{
	SetupResources();
}
