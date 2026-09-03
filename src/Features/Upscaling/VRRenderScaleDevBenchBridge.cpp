#include "Features/Upscaling/VRRenderScaleDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/Upscaling.h"
#	include "Features/Upscaling/NeuralRendering/CharacterRendering.h"
#	include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#	include "Features/Upscaling/NeuralRendering/Renderer.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "Profiler.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <array>
#	include <chrono>
#	include <cmath>
#	include <cstdint>
#	include <format>
#	include <functional>
#	include <future>
#	include <limits>
#	include <memory>
#	include <optional>
#	include <stdexcept>
#	include <string>
#	include <string_view>
#	include <vector>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr auto kMainThreadCompletionGrace = std::chrono::milliseconds(1000);
	std::atomic_bool g_registered{ false };

	const char* GetUpscaleMethodName(Upscaling::UpscaleMethod a_method)
	{
		switch (a_method) {
		case Upscaling::UpscaleMethod::kNONE:
			return "none";
		case Upscaling::UpscaleMethod::kTAA:
			return "taa";
		case Upscaling::UpscaleMethod::kFSR:
			return "fsr";
		case Upscaling::UpscaleMethod::kDLSS:
			return "dlss";
		default:
			return "unknown";
		}
	}

	const char* GetBackendName(Upscaling::VRRenderScaleBackendKind a_backend)
	{
		switch (a_backend) {
		case Upscaling::VRRenderScaleBackendKind::None:
			return "none";
		case Upscaling::VRRenderScaleBackendKind::DLSS:
			return "dlss";
		case Upscaling::VRRenderScaleBackendKind::FSRHost:
			return "fsr_host";
		case Upscaling::VRRenderScaleBackendKind::FSRRuntime:
			return "fsr_runtime";
		case Upscaling::VRRenderScaleBackendKind::FSR4Runtime:
			return "fsr4_runtime";
		default:
			return "unknown";
		}
	}

	const char* GetOriginName(Upscaling::VRUpscalingTransitionOrigin a_origin)
	{
		switch (a_origin) {
		case Upscaling::VRUpscalingTransitionOrigin::CSMenu:
			return "cs_menu";
		case Upscaling::VRUpscalingTransitionOrigin::VRAPI:
			return "vrapi";
		case Upscaling::VRUpscalingTransitionOrigin::RecoveryRelatch:
			return "recovery_relatch";
		case Upscaling::VRUpscalingTransitionOrigin::PostLoadSync:
			return "post_load_sync";
		default:
			return "unknown";
		}
	}

	json ProfileJson(const Upscaling::VRRenderScaleProfileSnapshot& a_profile)
	{
		return {
			{ "valid", a_profile.valid },
			{ "active", a_profile.active },
			{ "requestID", a_profile.requestID },
			{ "transitionEpoch", a_profile.transitionEpoch },
			{ "contractGeneration", a_profile.contractGeneration },
			{ "method", GetUpscaleMethodName(a_profile.method) },
			{ "qualityMode", a_profile.qualityMode },
			{ "dlssPreset", a_profile.dlssPreset },
			{ "renderScale", a_profile.renderScale },
			{ "renderScaleMode", a_profile.renderScaleModeEnabled },
			{ "displayEyeWidth", a_profile.displayEyeWidth },
			{ "displayEyeHeight", a_profile.displayEyeHeight },
			{ "renderEyeWidth", a_profile.renderEyeWidth },
			{ "renderEyeHeight", a_profile.renderEyeHeight },
			{ "queuedFrame", a_profile.queuedFrame },
			{ "origin", GetOriginName(a_profile.origin) },
			{ "backend", GetBackendName(a_profile.resources.backend) },
		};
	}

	json LifecycleJson(const Upscaling::VRVendorRuntimeLifecycleSnapshot& a_lifecycle)
	{
		return {
			{ "method", GetUpscaleMethodName(a_lifecycle.method) },
			{ "backend", GetBackendName(a_lifecycle.backend) },
			{ "phase", Upscaling::GetVRVendorRuntimeLifecyclePhaseName(a_lifecycle.phase) },
			{ "transitionEpoch", a_lifecycle.transitionEpoch },
			{ "requestedGeneration", a_lifecycle.requestedGeneration },
			{ "runtimeGeneration", a_lifecycle.runtimeGeneration },
			{ "stateFrame", a_lifecycle.stateFrame },
			{ "attempts", a_lifecycle.attempts },
			{ "deferrals", a_lifecycle.deferrals },
			{ "failures", a_lifecycle.failures },
			{ "resourcesPresent", a_lifecycle.resourcesPresent },
			{ "readyForContract", a_lifecycle.readyForContract },
		};
	}

	json NeuralImplementationJson(bool a_batchedStereo, bool a_directCommit)
	{
		return {
			{ "id", NeuralRendering::GetImplementationName(a_batchedStereo, a_directCommit) },
			{ "displayName", NeuralRendering::GetImplementationDisplayName(a_batchedStereo, a_directCommit) },
			{ "purpose", NeuralRendering::GetImplementationPurposeName(a_batchedStereo, a_directCommit) },
			{ "purposeDescription", NeuralRendering::GetImplementationPurpose(a_batchedStereo, a_directCommit) },
			{ "stereoSubmission", NeuralRendering::GetStereoSubmissionName(a_batchedStereo) },
			{ "outputCommit", NeuralRendering::GetOutputCommitName(a_directCommit) },
			{ "batchedStereo", a_batchedStereo },
			{ "directCommit", a_directCommit },
		};
	}

	json NeuralImplementationMatrixJson()
	{
		json matrix = json::array();
		for (uint32_t index = 0;
			index < NeuralRendering::kPipelineImplementations.size(); ++index) {
			const auto implementation =
				NeuralRendering::kPipelineImplementations[index];
			auto lane = NeuralImplementationJson(
				implementation.batchedStereo, implementation.directCommit);
			lane["index"] = index;
			matrix.push_back(std::move(lane));
		}
		return matrix;
	}

	json NeuralInsertionPointJson(NeuralRendering::InsertionPoint a_insertionPoint)
	{
		return {
			{ "id", NeuralRendering::GetInsertionPointName(a_insertionPoint) },
			{ "displayName", NeuralRendering::GetInsertionPointDisplayName(a_insertionPoint) },
			{ "value", static_cast<uint32_t>(a_insertionPoint) },
			{ "experimental", a_insertionPoint == NeuralRendering::InsertionPoint::FinalLdrPreUi },
		};
	}

	json NeuralInsertionPointMatrixJson()
	{
		json matrix = json::array();
		for (std::size_t index = 0;
			index < NeuralRendering::kInsertionPointCount; ++index) {
			matrix.push_back(NeuralInsertionPointJson(
				static_cast<NeuralRendering::InsertionPoint>(index)));
		}
		return matrix;
	}

	json NeuralInsertionPointPerformanceJson(
		const NeuralRendering::RendererPerformanceTelemetry& a_performance)
	{
		json telemetry = json::array();
		for (std::size_t index = 0;
			index < NeuralRendering::kInsertionPointCount; ++index) {
			const auto insertionPoint =
				static_cast<NeuralRendering::InsertionPoint>(index);
			telemetry.push_back({
				{ "id", NeuralRendering::GetInsertionPointName(insertionPoint) },
				{ "value", index },
				{ "featureGpuSamples", a_performance.featureGpuSamplesByInsertionPoint[index] },
				{ "featureGpuMicroseconds", a_performance.featureGpuMicrosecondsByInsertionPoint[index] },
			});
		}
		return telemetry;
	}

	enum class FoveationCycleControl : uint8_t
	{
		Master,
		PeripheryTAA,
		CenterOrigin,
		HorizontalAnchor,
		FovOnlyCenterScale,
		PeripheryTAACenterScale,
		PeripheryTAAOuterScale,
		CenterHorizontalScale,
		LeftEyeOffsetX,
		LeftEyeOffsetY,
		RightEyeOffsetX,
		RightEyeOffsetY,
		FovOnlyBlendFeather,
		PeripheryTAABlendFeather,
		NeuralFinalLdrBlendFeather,
		ReconstructionGuardBandPixels,
		MaskVisualization,
	};

	struct FoveationControlDescriptor
	{
		FoveationCycleControl control;
		std::string_view name;
	};

	constexpr std::array kFoveationControlDescriptors{
		FoveationControlDescriptor{ FoveationCycleControl::Master, "master" },
		FoveationControlDescriptor{ FoveationCycleControl::PeripheryTAA, "periphery_taa" },
		FoveationControlDescriptor{ FoveationCycleControl::CenterOrigin, "center_origin" },
		FoveationControlDescriptor{ FoveationCycleControl::HorizontalAnchor, "horizontal_anchor" },
		FoveationControlDescriptor{ FoveationCycleControl::FovOnlyCenterScale, "fov_only_center_scale" },
		FoveationControlDescriptor{ FoveationCycleControl::PeripheryTAACenterScale, "periphery_taa_center_scale" },
		FoveationControlDescriptor{ FoveationCycleControl::PeripheryTAAOuterScale, "periphery_taa_outer_scale" },
		FoveationControlDescriptor{ FoveationCycleControl::CenterHorizontalScale, "center_horizontal_scale" },
		FoveationControlDescriptor{ FoveationCycleControl::LeftEyeOffsetX, "left_eye_offset_x" },
		FoveationControlDescriptor{ FoveationCycleControl::LeftEyeOffsetY, "left_eye_offset_y" },
		FoveationControlDescriptor{ FoveationCycleControl::RightEyeOffsetX, "right_eye_offset_x" },
		FoveationControlDescriptor{ FoveationCycleControl::RightEyeOffsetY, "right_eye_offset_y" },
		FoveationControlDescriptor{ FoveationCycleControl::FovOnlyBlendFeather, "fov_only_blend_feather" },
		FoveationControlDescriptor{ FoveationCycleControl::PeripheryTAABlendFeather, "periphery_taa_blend_feather" },
		FoveationControlDescriptor{ FoveationCycleControl::NeuralFinalLdrBlendFeather, "neural_final_ldr_blend_feather" },
		FoveationControlDescriptor{ FoveationCycleControl::ReconstructionGuardBandPixels, "reconstruction_guard_band_pixels" },
		FoveationControlDescriptor{ FoveationCycleControl::MaskVisualization, "mask_visualization" },
	};

	constexpr std::array<float, 3> kCenterScaleCycleValues{
		FoveatedCenterAlignment::kCenterScaleMin,
		0.60f,
		FoveatedCenterAlignment::kCenterScaleMax,
	};
	constexpr std::array<float, 2> kHorizontalScaleCycleValues{
		FoveatedCenterAlignment::kCenterHorizontalScaleMin,
		FoveatedCenterAlignment::kCenterHorizontalScaleMax,
	};
	constexpr std::array<float, 3> kManualOffsetCycleValues{
		FoveatedCenterAlignment::kManualOffsetMin,
		0.0f,
		FoveatedCenterAlignment::kManualOffsetMax,
	};
	constexpr std::array<float, 3> kBlendFeatherCycleValues{
		Upscaling::kFoveatedBlendFeatherMin,
		0.05f,
		Upscaling::kFoveatedBlendFeatherMax,
	};
	constexpr std::array<uint32_t, 3> kGuardBandCycleValues{
		0u,
		Upscaling::kFoveatedReconstructionGuardBandMax / 2u,
		Upscaling::kFoveatedReconstructionGuardBandMax,
	};
	constexpr double kCenterScaleRequestMin = 0.25;
	constexpr double kCenterScaleRequestMax = 1.0;
	constexpr double kCenterHorizontalScaleRequestMin = 1.0;
	constexpr double kCenterHorizontalScaleRequestMax = 2.0;
	constexpr double kManualOffsetRequestMin = -0.3;
	constexpr double kManualOffsetRequestMax = 0.3;
	constexpr double kBlendFeatherRequestMin = 0.0;
	constexpr double kBlendFeatherRequestMax = 0.1;
	constexpr double kPeripheryTAAOuterScaleRequestMin = 0.3;
	constexpr double kPeripheryTAAOuterScaleRequestMax = 1.0;
	constexpr bool RequestBoundMatchesRuntime(double a_request, float a_runtime)
	{
		const auto difference = a_request - static_cast<double>(a_runtime);
		constexpr auto tolerance =
			static_cast<double>(std::numeric_limits<float>::epsilon());
		return difference >= -tolerance && difference <= tolerance;
	}

	static_assert(RequestBoundMatchesRuntime(kCenterScaleRequestMin,
		FoveatedCenterAlignment::kCenterScaleMin));
	static_assert(RequestBoundMatchesRuntime(kCenterScaleRequestMax,
		FoveatedCenterAlignment::kCenterScaleMax));
	static_assert(RequestBoundMatchesRuntime(kCenterHorizontalScaleRequestMin,
		FoveatedCenterAlignment::kCenterHorizontalScaleMin));
	static_assert(RequestBoundMatchesRuntime(kCenterHorizontalScaleRequestMax,
		FoveatedCenterAlignment::kCenterHorizontalScaleMax));
	static_assert(RequestBoundMatchesRuntime(kManualOffsetRequestMin,
		FoveatedCenterAlignment::kManualOffsetMin));
	static_assert(RequestBoundMatchesRuntime(kManualOffsetRequestMax,
		FoveatedCenterAlignment::kManualOffsetMax));
	static_assert(RequestBoundMatchesRuntime(kBlendFeatherRequestMin,
		Upscaling::kFoveatedBlendFeatherMin));
	static_assert(RequestBoundMatchesRuntime(kBlendFeatherRequestMax,
		Upscaling::kFoveatedBlendFeatherMax));
	static_assert(RequestBoundMatchesRuntime(kPeripheryTAAOuterScaleRequestMin,
		Upscaling::kPeripheryTAAOuterScaleMin));
	static_assert(RequestBoundMatchesRuntime(kPeripheryTAAOuterScaleRequestMax,
		Upscaling::kPeripheryTAAOuterScaleMax));

	const char* GetFoveationCycleControlName(FoveationCycleControl a_control)
	{
		for (const auto& descriptor : kFoveationControlDescriptors) {
			if (descriptor.control == a_control)
				return descriptor.name.data();
		}
		return "unknown";
	}

	std::optional<FoveationCycleControl> ParseFoveationCycleControl(
		const std::string& a_name)
	{
		for (const auto& descriptor : kFoveationControlDescriptors) {
			if (a_name == descriptor.name)
				return descriptor.control;
		}
		return std::nullopt;
	}

	const char* GetFoveatedInputValidityName(
		FoveatedCenterAlignment::InputValidity a_validity)
	{
		switch (a_validity) {
		case FoveatedCenterAlignment::InputValidity::Unavailable:
			return "unavailable";
		case FoveatedCenterAlignment::InputValidity::Invalid:
			return "invalid";
		case FoveatedCenterAlignment::InputValidity::Valid:
			return "valid";
		default:
			return "unknown";
		}
	}

	template <class T>
	json FoveatedPointJson(const T& a_point)
	{
		return { { "x", a_point.x }, { "y", a_point.y } };
	}

	json FoveatedEyeAlignmentJson(
		const FoveatedCenterAlignment::EyeDiagnostics& a_eye)
	{
		return {
			{ "eye", a_eye.eyeIndex },
			{ "source", FoveatedCenterAlignment::GetOpticalCenterSourceName(a_eye.source) },
			{ "fallbackReason", FoveatedCenterAlignment::GetOpticalFallbackReasonName(a_eye.fallbackReason) },
			{ "projectionValidity", GetFoveatedInputValidityName(a_eye.projectionValidity) },
			{ "tangentValidity", GetFoveatedInputValidityName(a_eye.tangentValidity) },
			{ "projectionCenterUV", FoveatedPointJson(a_eye.projectionCenterUV) },
			{ "tangentCenterUV", FoveatedPointJson(a_eye.tangentCenterUV) },
			{ "baseCenterUV", FoveatedPointJson(a_eye.baseCenterUV) },
			{ "baseOffset", FoveatedPointJson(a_eye.baseOffset) },
			{ "anchorOffset", FoveatedPointJson(a_eye.anchorOffset) },
			{ "requestedManualOffset", FoveatedPointJson(a_eye.requestedManualOffset) },
			{ "manualOffset", FoveatedPointJson(a_eye.manualOffset) },
			{ "unclampedOffset", FoveatedPointJson(a_eye.unclampedOffset) },
			{ "finalOffset", FoveatedPointJson(a_eye.finalOffset) },
			{ "manualOffsetClampedX", a_eye.manualOffsetClampedX },
			{ "manualOffsetClampedY", a_eye.manualOffsetClampedY },
			{ "finalOffsetClampedX", a_eye.finalOffsetClampedX },
			{ "finalOffsetClampedY", a_eye.finalOffsetClampedY },
		};
	}

	void AppendDistinctFoveationCycleValue(
		std::vector<float>& a_values,
		float a_value)
	{
		if (a_values.empty() ||
			std::abs(a_values.back() - a_value) > 1.0e-6f) {
			a_values.push_back(a_value);
		}
	}

	std::vector<float> GetPeripheryTAAOuterScaleCycleValues(
		const Upscaling::Settings& a_settings)
	{
		const float requestedCenter =
			std::isfinite(a_settings.periphery_taa_center_area) ?
				a_settings.periphery_taa_center_area :
				Upscaling::kPeripheryTAAOuterScaleMin;
		const float minimum = std::clamp(
			requestedCenter,
			Upscaling::kPeripheryTAAOuterScaleMin,
			Upscaling::kPeripheryTAAOuterScaleMax);
		std::vector<float> values;
		values.reserve(3u);
		AppendDistinctFoveationCycleValue(values, minimum);
		AppendDistinctFoveationCycleValue(
			values,
			minimum +
				(Upscaling::kPeripheryTAAOuterScaleMax - minimum) * 0.5f);
		AppendDistinctFoveationCycleValue(
			values, Upscaling::kPeripheryTAAOuterScaleMax);
		return values;
	}

	json FoveationCycleMatrixJson(const Upscaling::Settings& a_settings)
	{
		const auto originImage = FoveatedCenterAlignment::GetCenterOriginName(
			FoveatedCenterAlignment::CenterOrigin::ImageCenter);
		const auto originOptical = FoveatedCenterAlignment::GetCenterOriginName(
			FoveatedCenterAlignment::CenterOrigin::OpticalCenter);
		const auto anchorSymmetric = FoveatedCenterAlignment::GetHorizontalAnchorName(
			FoveatedCenterAlignment::HorizontalAnchor::Symmetric);
		const auto anchorOutward = FoveatedCenterAlignment::GetHorizontalAnchorName(
			FoveatedCenterAlignment::HorizontalAnchor::Outward);
		return json::array({
			{ { "control", "master" }, { "values", json::array({ false, true }) } },
			{ { "control", "periphery_taa" }, { "values", json::array({ false, true }) } },
			{ { "control", "center_origin" }, { "values", json::array({ originImage, originOptical }) } },
			{ { "control", "horizontal_anchor" }, { "values", json::array({ anchorSymmetric, anchorOutward }) } },
			{ { "control", "fov_only_center_scale" }, { "values", kCenterScaleCycleValues } },
			{ { "control", "periphery_taa_center_scale" }, { "values", kCenterScaleCycleValues } },
			{ { "control", "periphery_taa_outer_scale" }, { "values", GetPeripheryTAAOuterScaleCycleValues(a_settings) } },
			{ { "control", "center_horizontal_scale" }, { "values", kHorizontalScaleCycleValues } },
			{ { "control", "left_eye_offset_x" }, { "values", kManualOffsetCycleValues } },
			{ { "control", "left_eye_offset_y" }, { "values", kManualOffsetCycleValues } },
			{ { "control", "right_eye_offset_x" }, { "values", kManualOffsetCycleValues } },
			{ { "control", "right_eye_offset_y" }, { "values", kManualOffsetCycleValues } },
			{ { "control", "fov_only_blend_feather" }, { "values", kBlendFeatherCycleValues } },
			{ { "control", "periphery_taa_blend_feather" }, { "values", kBlendFeatherCycleValues } },
			{ { "control", "neural_final_ldr_blend_feather" }, { "values", kBlendFeatherCycleValues } },
			{ { "control", "reconstruction_guard_band_pixels" }, { "values", kGuardBandCycleValues } },
			{ { "control", "mask_visualization" }, { "values", json::array({ false, true }) } },
		});
	}

	const char* GetFoveatedModeId(Upscaling::FoveatedUpscalingMode a_mode)
	{
		switch (a_mode) {
		case Upscaling::FoveatedUpscalingMode::CenterOnly:
			return "fov_only";
		case Upscaling::FoveatedUpscalingMode::PeripheralTAA:
			return "periphery_taa";
		case Upscaling::FoveatedUpscalingMode::Disabled:
		default:
			return "disabled";
		}
	}

	json FoveatedRectJson(const FoveatedRegionPlan::Rect& a_rect)
	{
		return {
			{ "valid", a_rect.IsValid() },
			{ "minX", a_rect.minX },
			{ "minY", a_rect.minY },
			{ "maxX", a_rect.maxX },
			{ "maxY", a_rect.maxY },
			{ "width", a_rect.Width() },
			{ "height", a_rect.Height() },
		};
	}

	json FoveatedPlanEyeJson(
		const FoveatedRegionPlan::Eye& a_eye,
		uint32_t a_eyeIndex,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		uint32_t a_guardPixels)
	{
		const uint32_t sourceOffsetX =
			a_eye.visibleOutput.minX >= a_eye.output.minX ?
				a_eye.visibleOutput.minX - a_eye.output.minX :
				0u;
		const uint32_t sourceOffsetY =
			a_eye.visibleOutput.minY >= a_eye.output.minY ?
				a_eye.visibleOutput.minY - a_eye.output.minY :
				0u;
		return {
			{ "eye", a_eyeIndex },
			{ "centerOffset", FoveatedPointJson(a_eye.centerOffset) },
			{ "pinholeOffset", FoveatedPointJson(a_eye.pinholeOffset) },
			{ "visibleOutput", FoveatedRectJson(a_eye.visibleOutput) },
			{ "output", FoveatedRectJson(a_eye.output) },
			{ "input", FoveatedRectJson(a_eye.input) },
			{ "encodeInput", FoveatedRectJson(a_eye.encodeInput) },
			{ "centerInteriorOutput", FoveatedRectJson(a_eye.centerInteriorOutput) },
			{ "centerUnderlayHoleOutput", FoveatedRectJson(a_eye.centerUnderlayHoleOutput) },
			{ "peripheryTaaOuterOutput", FoveatedRectJson(a_eye.peripheryTAAOuterOutput) },
			{ "peripheryTaaHistoryOutput", FoveatedRectJson(a_eye.peripheryTAAHistoryOutput) },
			{ "peripheryTaaOuterInput", FoveatedRectJson(a_eye.peripheryTAAOuterInput) },
			{ "sourceOffset", { { "x", sourceOffsetX }, { "y", sourceOffsetY } } },
			{ "guardClipped", {
								  { "left", a_guardPixels > a_eye.visibleOutput.minX },
								  { "top", a_guardPixels > a_eye.visibleOutput.minY },
								  { "right", a_guardPixels > a_outputWidth - std::min(a_eye.visibleOutput.maxX, a_outputWidth) },
								  { "bottom", a_guardPixels > a_outputHeight - std::min(a_eye.visibleOutput.maxY, a_outputHeight) },
							  } },
		};
	}

	float ClampFoveationFeatherForStatus(float a_value)
	{
		return std::clamp(
			std::isfinite(a_value) ? a_value : FoveatedCommon::kCenterFeather,
			Upscaling::kFoveatedBlendFeatherMin,
			Upscaling::kFoveatedBlendFeatherMax);
	}

	bool NearlyEqualFoveationValue(float a_left, float a_right)
	{
		return std::abs(a_left - a_right) <= 0.00001f;
	}

	bool FoveatedPlanMatchesSettings(
		const Upscaling& a_upscaling,
		const Upscaling::RuntimeResolutionPlan& a_resolutionPlan,
		const Upscaling::ActiveUpscalingFoveatedProfile& a_profile,
		bool a_refreshPending,
		float a_expectedSupportFeather)
	{
		if (a_refreshPending ||
			a_resolutionPlan.upscaleMethod != a_upscaling.GetRuntimeUpscaleMethod() ||
			a_resolutionPlan.foveatedActive != a_profile.available) {
			return false;
		}

		const auto& plan = a_resolutionPlan.foveatedRegion;
		if (!a_profile.available)
			return !plan.IsValid();
		if (!plan.IsValid() ||
			a_resolutionPlan.peripheryTAAActive !=
				a_profile.usesPeripheryTAAOuterMask ||
			plan.reconstructionGuardBandPixels != std::min(
													  a_upscaling.settings.foveatedReconstructionGuardBandPixels,
													  Upscaling::kFoveatedReconstructionGuardBandMax) ||
			!NearlyEqualFoveationValue(plan.centerScale, a_profile.vendorCenterScale) ||
			!NearlyEqualFoveationValue(
				plan.centerHorizontalScale, a_profile.centerHorizontalScale) ||
			!NearlyEqualFoveationValue(
				plan.centerFeather, a_expectedSupportFeather)) {
			return false;
		}

		const float expectedOuterScale =
			a_profile.usesPeripheryTAAOuterMask ?
				a_profile.sharedVisibleScale :
				0.0f;
		if (!NearlyEqualFoveationValue(
				plan.peripheryTAAOuterScale, expectedOuterScale)) {
			return false;
		}

		const uint32_t eyeCount = globals::game::isVR ? 2u : 1u;
		for (uint32_t eye = 0; eye < eyeCount; ++eye) {
			if (!NearlyEqualFoveationValue(
					plan.eyes[eye].centerOffset.x,
					a_profile.centerOffsets[eye].x) ||
				!NearlyEqualFoveationValue(
					plan.eyes[eye].centerOffset.y,
					a_profile.centerOffsets[eye].y)) {
				return false;
			}
		}
		return true;
	}

	json FoveatedPlanJson(
		const Upscaling& a_upscaling,
		const Upscaling::ActiveUpscalingFoveatedProfile& a_profile)
	{
		const auto& resolutionPlan = a_upscaling.GetRuntimeResolutionPlan();
		const auto& plan = resolutionPlan.foveatedRegion;
		const uint32_t observedFrame =
			globals::state ? globals::state->frameCount : 0u;
		const uint32_t latchedFrame =
			a_upscaling.GetRuntimeResolutionPlanFrame();
		const uint32_t currentWorkFrame =
			a_upscaling.GetRuntimeResolutionWorkFrame();
		const bool refreshPending =
			currentWorkFrame == std::numeric_limits<uint32_t>::max() ||
			latchedFrame != currentWorkFrame;
		const bool peripheryPathActive = a_upscaling.IsPeripheryTAAPathActive(
			a_upscaling.GetRuntimeUpscaleMethod());
		const float normalBlendFeather = ClampFoveationFeatherForStatus(
			peripheryPathActive ?
				a_upscaling.settings.periphery_taa_center_blend_feather :
				a_upscaling.settings.foveatedCenterBlendFeather);
		const bool finalLdrNeuralSupportRequested =
			a_upscaling.GetRuntimeUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
			a_upscaling.settings.neuralRenderingEnabled &&
			!a_upscaling.settings.foveatedPeripheryMaskVisualization &&
			NeuralRendering::ClampInsertionPoint(
				a_upscaling.settings.neuralRenderingInsertionPoint) ==
				NeuralRendering::InsertionPoint::FinalLdrPreUi;
		const float finalLdrBlendFeather = ClampFoveationFeatherForStatus(
			a_upscaling.settings.neuralRenderingBlendFeather);
		const float expectedSupportFeather = finalLdrNeuralSupportRequested ?
		                                         std::max(normalBlendFeather, finalLdrBlendFeather) :
		                                         normalBlendFeather;
		const bool finalLdrNeuralSupportLatched =
			!refreshPending && a_profile.available &&
			resolutionPlan.foveatedActive && plan.IsValid() &&
			finalLdrNeuralSupportRequested &&
			NearlyEqualFoveationValue(
				plan.centerFeather, expectedSupportFeather);

		json eyes = json::array();
		for (uint32_t eye = 0; eye < 2u; ++eye) {
			eyes.push_back(FoveatedPlanEyeJson(
				plan.eyes[eye],
				eye,
				plan.outputWidthPerEye,
				plan.outputHeight,
				plan.reconstructionGuardBandPixels));
		}

		json latchedFrameJson = nullptr;
		if (latchedFrame != std::numeric_limits<uint32_t>::max())
			latchedFrameJson = latchedFrame;
		json currentWorkFrameJson = nullptr;
		if (currentWorkFrame != std::numeric_limits<uint32_t>::max())
			currentWorkFrameJson = currentWorkFrame;
		json effectiveNotBeforeFrame = nullptr;
		json measurementSafeFromFrame = nullptr;
		if (refreshPending) {
			effectiveNotBeforeFrame = observedFrame;
			measurementSafeFromFrame = observedFrame ==
			                                   std::numeric_limits<uint32_t>::max() ?
			                               observedFrame :
			                               observedFrame + 1u;
		}

		return {
			{ "observedFrame", observedFrame },
			{ "latchedFrame", std::move(latchedFrameJson) },
			{ "currentWorkFrame", std::move(currentWorkFrameJson) },
			{ "refreshPending", refreshPending },
			{ "effectiveNotBeforeFrame", std::move(effectiveNotBeforeFrame) },
			{ "measurementSafeFromFrame", std::move(measurementSafeFromFrame) },
			{ "matchesRequestedSettings", FoveatedPlanMatchesSettings(
											  a_upscaling,
											  resolutionPlan,
											  a_profile,
											  refreshPending,
											  expectedSupportFeather) },
			{ "upscaleMethod", GetUpscaleMethodName(resolutionPlan.upscaleMethod) },
			{ "foveatedActive", resolutionPlan.foveatedActive },
			{ "peripheryTaaActive", resolutionPlan.peripheryTAAActive },
			{ "valid", plan.IsValid() },
			{ "inputWidthPerEye", plan.inputWidthPerEye },
			{ "inputHeight", plan.inputHeight },
			{ "outputWidthPerEye", plan.outputWidthPerEye },
			{ "outputHeight", plan.outputHeight },
			{ "centerScale", plan.centerScale },
			{ "normalBlendFeather", normalBlendFeather },
			{ "finalLdrNeuralSupportRequested", finalLdrNeuralSupportRequested },
			{ "finalLdrNeuralSupportLatched", finalLdrNeuralSupportLatched },
			{ "finalLdrBlendFeather", finalLdrBlendFeather },
			{ "expectedSupportFeather", expectedSupportFeather },
			{ "effectiveSupportFeather", plan.centerFeather },
			{ "reconstructionGuardBandPixels", plan.reconstructionGuardBandPixels },
			{ "centerHorizontalScale", plan.centerHorizontalScale },
			{ "peripheryTaaOuterScale", plan.peripheryTAAOuterScale },
			{ "eyes", std::move(eyes) },
		};
	}

	json FoveationStatusJson(const Upscaling& a_upscaling)
	{
		const auto& settings = a_upscaling.settings;
		const auto alignment = a_upscaling.GetResolvedFoveatedCenterAlignment(
			settings.periphery_taa_enable);
		const auto activeProfile = a_upscaling.GetActiveUpscalingFoveatedProfile();
		json eyes = json::array();
		for (const auto& eye : alignment.eyes)
			eyes.push_back(FoveatedEyeAlignmentJson(eye));

		json activeOffsets = json::array();
		for (const auto& offset : activeProfile.centerOffsets) {
			activeOffsets.push_back({ { "x", offset.x }, { "y", offset.y } });
		}

		return {
			{ "settings", {
							  { "foveatedEnabled", settings.foveatedVendorDispatch },
							  { "peripheryTaaEnabled", settings.periphery_taa_enable },
							  { "centerOrigin", FoveatedCenterAlignment::GetCenterOriginName(alignment.origin) },
							  { "centerOriginValue", static_cast<uint32_t>(alignment.origin) },
							  { "horizontalAnchor", FoveatedCenterAlignment::GetHorizontalAnchorName(alignment.anchor) },
							  { "horizontalAnchorValue", static_cast<uint32_t>(alignment.anchor) },
							  { "fovOnlyCenterScale", settings.foveatedCenterArea },
							  { "peripheryTaaCenterScale", settings.periphery_taa_center_area },
							  { "peripheryTaaOuterScale", settings.periphery_taa_outer_scale },
							  { "centerHorizontalScale", settings.foveatedCenterHorizontalScale },
							  { "leftEyeOffsetX", settings.foveatedLeftEyeMaskOffsetX },
							  { "leftEyeOffsetY", settings.foveatedLeftEyeMaskOffsetY },
							  { "rightEyeOffsetX", settings.foveatedRightEyeMaskOffsetX },
							  { "rightEyeOffsetY", settings.foveatedRightEyeMaskOffsetY },
							  { "fovOnlyBlendFeather", settings.foveatedCenterBlendFeather },
							  { "peripheryTaaBlendFeather", settings.periphery_taa_center_blend_feather },
							  { "neuralFinalLdrBlendFeather", settings.neuralRenderingBlendFeather },
							  { "reconstructionGuardBandPixels", settings.foveatedReconstructionGuardBandPixels },
							  { "maskVisualization", settings.foveatedPeripheryMaskVisualization },
						  } },
			{ "active", {
							{ "available", activeProfile.available },
							{ "mode", Upscaling::GetFoveatedUpscalingModeName(activeProfile.mode) },
							{ "modeId", GetFoveatedModeId(activeProfile.mode) },
							{ "displayName", Upscaling::GetFoveatedUpscalingModeName(activeProfile.mode) },
							{ "modeValue", static_cast<uint32_t>(activeProfile.mode) },
							{ "usesPeripheryTaa", activeProfile.usesPeripheryTAAOuterMask },
							{ "vendorCenterScale", activeProfile.vendorCenterScale },
							{ "sharedVisibleScale", activeProfile.sharedVisibleScale },
							{ "centerHorizontalScale", activeProfile.centerHorizontalScale },
							{ "centerOffsets", std::move(activeOffsets) },
							{ "maskVisualization", settings.foveatedPeripheryMaskVisualization },
							{ "neuralSuppressedByMaskVisualization", settings.neuralRenderingEnabled && settings.foveatedPeripheryMaskVisualization },
						} },
			{ "alignment", {
							   { "frame", globals::state ? globals::state->frameCount : 0u },
							   { "profile", settings.periphery_taa_enable ? "periphery_taa" : "fov_only" },
							   { "origin", FoveatedCenterAlignment::GetCenterOriginName(alignment.origin) },
							   { "originValue", static_cast<uint32_t>(alignment.origin) },
							   { "horizontalAnchor", FoveatedCenterAlignment::GetHorizontalAnchorName(alignment.anchor) },
							   { "horizontalAnchorValue", static_cast<uint32_t>(alignment.anchor) },
							   { "centerScale", alignment.centerScale },
							   { "centerHorizontalScale", alignment.centerHorizontalScale },
							   { "eyes", std::move(eyes) },
						   } },
			{ "ranges", {
							{ "centerScale", { { "minimum", FoveatedCenterAlignment::kCenterScaleMin }, { "maximum", FoveatedCenterAlignment::kCenterScaleMax } } },
							{ "centerHorizontalScale", { { "minimum", FoveatedCenterAlignment::kCenterHorizontalScaleMin }, { "maximum", FoveatedCenterAlignment::kCenterHorizontalScaleMax } } },
							{ "manualOffset", { { "minimum", FoveatedCenterAlignment::kManualOffsetMin }, { "maximum", FoveatedCenterAlignment::kManualOffsetMax } } },
							{ "blendFeather", { { "minimum", Upscaling::kFoveatedBlendFeatherMin }, { "maximum", Upscaling::kFoveatedBlendFeatherMax } } },
							{ "peripheryTaaOuterScale", { { "minimum", Upscaling::kPeripheryTAAOuterScaleMin }, { "maximum", Upscaling::kPeripheryTAAOuterScaleMax } } },
							{ "reconstructionGuardBandPixels", { { "minimum", 0u }, { "maximum", Upscaling::kFoveatedReconstructionGuardBandMax } } },
						} },
			{ "plan", FoveatedPlanJson(a_upscaling, activeProfile) },
			{ "cycleMatrix", FoveationCycleMatrixJson(settings) },
		};
	}

	const char* GetCharacterDebugViewName(NeuralRendering::CharacterDebugView a_view)
	{
		switch (a_view) {
		case NeuralRendering::CharacterDebugView::Off:
			return "off";
		case NeuralRendering::CharacterDebugView::CharacterMask:
			return "character_mask";
		case NeuralRendering::CharacterDebugView::RoiRectangles:
			return "roi_rectangles";
		case NeuralRendering::CharacterDebugView::Dlss5Output:
			return "dlss5_output";
		default:
			return "unknown";
		}
	}

	const char* GetCharacterMaskTestModeName(
		NeuralRendering::CharacterMaskTestMode a_mode)
	{
		switch (a_mode) {
		case NeuralRendering::CharacterMaskTestMode::Authored:
			return "authored";
		case NeuralRendering::CharacterMaskTestMode::ForceZero:
			return "force_zero";
		case NeuralRendering::CharacterMaskTestMode::ForceOne:
			return "force_one";
		case NeuralRendering::CharacterMaskTestMode::ForceHalf:
			return "force_half";
		case NeuralRendering::CharacterMaskTestMode::InvertAuthored:
			return "invert_authored";
		case NeuralRendering::CharacterMaskTestMode::AuthoredWithoutVisibilityDepth:
			return "authored_without_visibility_depth";
		default:
			return "unknown";
		}
	}

	json ProfileTimerJson(std::string_view a_name)
	{
		if (!globals::profiler) {
			return {
				{ "available", false },
				{ "reason", "profiler_unavailable" },
			};
		}

		const auto& results = globals::profiler->GetResults();
		const auto result = std::ranges::find_if(
			results, [a_name](const Profiler::TimerResult& a_result) {
				return a_result.name == a_name;
			});
		if (result == results.end()) {
			return {
				{ "available", false },
				{ "reason", "timer_not_observed" },
			};
		}

		return {
			{ "available", true },
			{ "valid", result->valid },
			{ "activeGpu", result->activeGpu },
			{ "activeCpu", result->activeCpu },
			{ "gpuMilliseconds", result->hasGpu ? json(result->gpuTimeMs) : json(nullptr) },
			{ "gpuAverageMilliseconds", result->hasGpu ? json(result->avgMs) : json(nullptr) },
			{ "gpuP95Milliseconds", result->hasGpu ? json(result->p95Ms) : json(nullptr) },
			{ "cpuMilliseconds", result->hasCpu ? json(result->cpuTimeMs) : json(nullptr) },
			{ "cpuAverageMilliseconds", result->hasCpu ? json(result->cpuAvgMs) : json(nullptr) },
			{ "cpuP95Milliseconds", result->hasCpu ? json(result->cpuP95Ms) : json(nullptr) },
		};
	}

	json CharacterRenderingStatusJson(const Upscaling& a_upscaling)
	{
		const auto& settings = a_upscaling.settings;
		const auto snapshot =
			NeuralRendering::CharacterRendering::Instance().GetSnapshot();
		const auto rendererSnapshot =
			NeuralRendering::Renderer::Instance().GetSnapshot();
		const auto debugView = NeuralRendering::ClampCharacterDebugView(
			settings.neuralCharacterDebugView);
		const auto maskTestMode = NeuralRendering::ClampCharacterMaskTestMode(
			settings.neuralCharacterMaskTestMode);

		json eyes = json::array();
		std::uint32_t currentPreparedCharacterSlotMask = 0;
		const std::uint32_t observedFrame =
			globals::state ? globals::state->frameCount : 0u;
		for (std::size_t eyeIndex = 0; eyeIndex < snapshot.eyes.size(); ++eyeIndex) {
			const auto& eye = snapshot.eyes[eyeIndex];
			if (eye.maskPrepared && eye.frame == observedFrame &&
				eye.featureSlot < 32u) {
				currentPreparedCharacterSlotMask |= 1u << eye.featureSlot;
			}
			const auto coveragePreparation = std::ranges::find_if(
				snapshot.preparedFrames, [&](const auto& a_prepared) {
					return eye.maskCoverageReady &&
				           a_prepared.frame == eye.maskCoverageFrame;
				});
			const bool coverageMatchesPreparation =
				coveragePreparation != snapshot.preparedFrames.end() &&
				eye.maskCoverageFeatureSlot < coveragePreparation->widths.size() &&
				(coveragePreparation->preparedSlotMask &
					(1u << eye.maskCoverageFeatureSlot)) != 0u &&
				coveragePreparation->widths[eye.maskCoverageFeatureSlot] ==
					eye.maskCoverageWidth &&
				coveragePreparation->heights[eye.maskCoverageFeatureSlot] ==
					eye.maskCoverageHeight;
			json regions = json::array();
			for (const auto& region : eye.regions) {
				regions.push_back({
					{ "minX", region.minX },
					{ "minY", region.minY },
					{ "maxX", region.maxX },
					{ "maxY", region.maxY },
					{ "width", region.maxX > region.minX ? region.maxX - region.minX : 0u },
					{ "height", region.maxY > region.minY ? region.maxY - region.minY : 0u },
					{ "pixels", region.Area() },
				});
			}
			eyes.push_back({
				{ "eye", eyeIndex },
				{ "frame", eye.frame },
				{ "featureSlot", eye.featureSlot },
				{ "evaluationWidth", eye.evaluationWidth },
				{ "evaluationHeight", eye.evaluationHeight },
				{ "evaluationPixels", static_cast<std::uint64_t>(eye.evaluationWidth) * eye.evaluationHeight },
				{ "eligibleFaceActors", eye.visibleFaces },
				{ "eligibleCharacterActors", eye.visibleCharacterRegions },
				{ "droppedCharacterActors", eye.droppedCharacterRegions },
				{ "mergedEligibilityRegions", eye.mergedRegions },
				{ "eligibilityPixels", eye.roiPixels },
				{ "eligibilityCoveragePercent", eye.roiCoveragePercent },
				{ "maskPixels", eye.maskCoverageReady ? json(eye.maskPixels) : json(nullptr) },
				{ "maskCoveragePercent", eye.maskCoverageReady ? json(eye.maskCoveragePercent) : json(nullptr) },
				{ "maskCoverageSampleFrame", eye.maskCoverageReady ? json(eye.maskCoverageFrame) : json(nullptr) },
				{ "maskCoverageSampleFeatureSlot", eye.maskCoverageReady ? json(eye.maskCoverageFeatureSlot) : json(nullptr) },
				{ "maskCoverageSampleWidth", eye.maskCoverageReady ? json(eye.maskCoverageWidth) : json(nullptr) },
				{ "maskCoverageSampleHeight", eye.maskCoverageReady ? json(eye.maskCoverageHeight) : json(nullptr) },
				{ "maskCoverageSampleAgeFrames", eye.maskCoverageReady ? json(observedFrame - eye.maskCoverageFrame) : json(nullptr) },
				{ "maskCoverageMatchesEvaluation", coverageMatchesPreparation },
				{ "maskCoverageReady", eye.maskCoverageReady },
				{ "maskCoverageMatchesCurrentPolicy", eye.maskCoverageMatchesCurrentPolicy },
				{ "categoryPixelCountSpace", "feature18_evaluation_pixels_before_visibility" },
				{ "authoredCategoryPixels", {
												{ "face", eye.maskCoverageReady ? json(eye.authoredCategoryPixels[0]) : json(nullptr) },
												{ "skin", eye.maskCoverageReady ? json(eye.authoredCategoryPixels[1]) : json(nullptr) },
												{ "hair", eye.maskCoverageReady ? json(eye.authoredCategoryPixels[2]) : json(nullptr) },
											} },
				{ "visibleCategoryPixels", {
											   { "face", eye.maskCoverageReady ? json(eye.visibleCategoryPixels[0]) : json(nullptr) },
											   { "skin", eye.maskCoverageReady ? json(eye.visibleCategoryPixels[1]) : json(nullptr) },
											   { "hair", eye.maskCoverageReady ? json(eye.visibleCategoryPixels[2]) : json(nullptr) },
										   } },
				{ "visibilityRejectedPixels", eye.maskCoverageReady ? json(eye.visibilityRejectedPixels) : json(nullptr) },
				{ "zeroCoverageBypassedFeature18", eye.zeroCoverageBypassed },
				{ "depthCoordinates", {
										  { "valid", eye.depthCoordinatesValid },
										  { "authoredStereoWidth", eye.authoredStereoWidth },
										  { "authoredDepthHeight", eye.authoredDepthHeight },
										  { "authoredEyeBaseX", eye.authoredEyeBaseX },
										  { "authoredCropGlobalLeft", eye.authoredEyeBaseX + eye.inputCropLeft },
										  { "authoredCropGlobalRight", eye.authoredEyeBaseX + eye.inputCropLeft + eye.inputCropWidth },
										  { "authoredCropTop", eye.inputCropTop },
										  { "authoredCropBottom", eye.inputCropTop + eye.inputCropHeight },
										  { "currentDepthWidth", eye.currentDepthWidth },
										  { "currentDepthHeight", eye.currentDepthHeight },
										  { "inputCropLeft", eye.inputCropLeft },
										  { "inputCropTop", eye.inputCropTop },
										  { "inputCropWidth", eye.inputCropWidth },
										  { "inputCropHeight", eye.inputCropHeight },
										  { "capturedJitterX", eye.capturedJitterX },
										  { "capturedJitterY", eye.capturedJitterY },
										  { "mapping", "output_uv_to_eye_local_crop_then_global_eye_base" },
									  } },
				{ "outputCoordinates", {
										   { "cropLeft", eye.outputCropLeft },
										   { "cropTop", eye.outputCropTop },
										   { "cropWidth", eye.outputCropWidth },
										   { "cropHeight", eye.outputCropHeight },
										   { "maskIsCropLocal", true },
									   } },
				{ "maskPrepared", eye.maskPrepared },
				{ "evaluationRequired", eye.evaluationRequired },
				{ "eligibilityRegions", std::move(regions) },
			});
		}
		const std::uint32_t lastFeatureSlotMask =
			rendererSnapshot.performance.lastFeatureSlotMask;
		const auto attributedPreparation = std::ranges::find_if(
			snapshot.preparedFrames, [&](const auto& a_prepared) {
				return a_prepared.frame ==
			           rendererSnapshot.performance.lastFeatureFrameId;
			});
		const bool attributedPreparationFound =
			attributedPreparation != snapshot.preparedFrames.end();
		const std::uint32_t preparedCharacterSlotMask =
			attributedPreparationFound ?
				attributedPreparation->preparedSlotMask :
				0u;
		const std::uint32_t evaluationRequiredCharacterSlotMask =
			attributedPreparationFound ?
				attributedPreparation->evaluationRequiredSlotMask :
				0u;
		auto expandStereoEvaluationMask = [](
											  std::uint32_t a_preparedMask,
											  std::uint32_t a_requiredMask) noexcept {
			std::uint32_t expectedMask = a_preparedMask & a_requiredMask;
			for (const std::uint32_t pairMask :
				std::array{ 0b0011u, 0b1100u }) {
				if ((a_preparedMask & pairMask) == pairMask &&
					(a_requiredMask & pairMask) != 0u) {
					expectedMask |= pairMask;
				}
			}
			return expectedMask;
		};
		const std::uint32_t expectedFeatureSlotMask =
			expandStereoEvaluationMask(
				preparedCharacterSlotMask,
				evaluationRequiredCharacterSlotMask);
		const std::uint32_t missingPreparedFeatureSlots =
			preparedCharacterSlotMask & ~lastFeatureSlotMask;
		const std::uint32_t missingExpectedFeatureSlots =
			expectedFeatureSlotMask & ~lastFeatureSlotMask;
		const std::uint32_t unexpectedFeatureSlots =
			lastFeatureSlotMask & ~expectedFeatureSlotMask;
		const bool featureTimingIsStereoPair =
			rendererSnapshot.performance.lastFeatureEvaluationCount == 2u;
		const bool featureTimingIsEyeSample =
			rendererSnapshot.performance.lastFeatureEvaluationCount == 1u;
		const bool featureTimingSlotsMatch = featureTimingIsStereoPair ?
		                                         lastFeatureSlotMask == expectedFeatureSlotMask :
		                                         featureTimingIsEyeSample && lastFeatureSlotMask != 0u &&
		                                             unexpectedFeatureSlots == 0u;
		const bool featureTimingMatchesPreparedMask =
			attributedPreparationFound && expectedFeatureSlotMask != 0 &&
			featureTimingSlotsMatch;
		const bool visualIsolationConfigured =
			settings.neuralRenderingEnabled &&
			settings.neuralCharacterRenderingEnabled &&
			settings.neuralCharacterVisualIsolationEnabled;
		const bool visualIsolationEffective =
			visualIsolationConfigured && featureTimingIsStereoPair &&
			featureTimingMatchesPreparedMask &&
			lastFeatureSlotMask == expectedFeatureSlotMask;

		return {
			{ "settings", {
							  { "enabled", settings.neuralCharacterRenderingEnabled },
							  { "characterVisualIsolationEnabled", settings.neuralCharacterVisualIsolationEnabled },
							  { "faces", settings.neuralCharacterFacesEnabled },
							  { "skin", settings.neuralCharacterSkinEnabled },
							  { "hair", settings.neuralCharacterHairEnabled },
							  { "faceStrength", settings.neuralCharacterFaceStrength },
							  { "skinStrength", settings.neuralCharacterSkinStrength },
							  { "hairStrength", settings.neuralCharacterHairStrength },
							  { "maximumDistanceMeters", settings.neuralCharacterMaximumDistanceMeters },
							  { "minimumFacePixelSize", settings.neuralCharacterMinimumFacePixelSize },
							  { "roiMargin", settings.neuralCharacterRoiMargin },
							  { "maximumRoiRegions", settings.neuralCharacterMaxRoiRegions },
							  { "roiHoldFrames", settings.neuralCharacterRoiHoldFrames },
							  { "depthAwareFeather", settings.neuralCharacterDepthAwareFeatherEnabled },
							  { "visibilityDepthTest", settings.neuralCharacterVisibilityDepthTestEnabled },
							  { "featherRadius", settings.neuralCharacterFeatherRadius },
							  { "featherDepthThreshold", settings.neuralCharacterDepthThreshold },
							  { "debugView", GetCharacterDebugViewName(debugView) },
							  { "debugViewValue", static_cast<std::uint32_t>(debugView) },
							  { "maskTestMode", GetCharacterMaskTestModeName(maskTestMode) },
							  { "maskTestModeValue", static_cast<std::uint32_t>(maskTestMode) },
						  } },
			{ "capability", {
								{ "visualMasking", {
													   { "implemented", snapshot.visualMaskImplemented },
													   { "providerValidated", snapshot.visualMaskProviderValidated },
													   { "status", "experimental_private_contract" },
													   { "publicProductSemanticsDescribed", true },
													   { "exactBindingContractPublished", false },
													   { "mechanism", snapshot.visualMaskMechanism },
													   { "format", {
																	   { "value", "R8_UNORM" },
																	   { "experimental", true },
																	   { "providerDeclared", false },
																   } },
													   { "valueRange", {
																		   { "value", "0..1" },
																		   { "experimental", true },
																		   { "providerDeclared", false },
																	   } },
													   { "scope", "per_eye_evaluation_image" },
												   } },
								{ "visualIsolation", {
														 { "requested", settings.neuralCharacterVisualIsolationEnabled },
														 { "configured", visualIsolationConfigured },
														 { "active", visualIsolationEffective },
														 { "guarantee", "provider_output_isolated_to_nonzero_mask_support" },
														 { "finalVisibilityClassificationGuaranteed", false },
														 { "upscaledCenterCommitPolicy", "configured_commit_lane_preserved" },
														 { "mainNonFloatDirectCommitBaseline", "one_normal_dlss_center_copy" },
														 { "submitFloatBaseline", "existing_normal_dlss_center" },
														 { "disabledPurpose", "provider_control_mask_only_calibration" },
														 { "emptyStereoPairPolicy", "bypass_feature18" },
													 } },
								{ "computeRoi", {
													{ "supported", snapshot.computeRoiSupported },
													{ "multiSparseSupported", false },
													{ "privateSingleSubrectCandidate", true },
													{ "privateSingleSubrectEnabled", false },
													{ "privateSingleSubrectValidation", "pending_contract_and_timing_validation" },
													{ "reason", snapshot.computeRoiReason },
													{ "resolvedMode", "masked_full_frame_inference" },
													{ "inferenceRestrictedToRois", false },
												} },
								{ "categoryProvenance", {
															{ "vrAttachmentFormat", "R8G8_UNORM" },
															{ "legacyAttachmentFormat", "R16_UNORM" },
															{ "vrBytesPerPixel", 2 },
															{ "legacyBytesPerPixel", 2 },
															{ "additionalVrBytesPerPixel", 0 },
															{ "inverseVertexAoBits", 8 },
															{ "attachmentAllocatedWhenFeatureDisabled", true },
															{ "frozenDepthSnapshotConditional", true },
														} },
							} },
			{ "runtime", {
							 { "status", snapshot.status },
							 { "detail", snapshot.detail },
							 { "observations", snapshot.observations },
							 { "observationFrame", snapshot.observationFrame != std::numeric_limits<std::uint32_t>::max() ? json(snapshot.observationFrame) : json(nullptr) },
							 { "currentObservations", snapshot.currentObservations },
							 { "observationCapacityDrops", snapshot.observationCapacityDrops },
							 { "currentCategoryObservations", {
																  { "face", snapshot.currentCategoryObservations[0] },
																  { "skin", snapshot.currentCategoryObservations[1] },
																  { "hair", snapshot.currentCategoryObservations[2] },
															  } },
							 { "currentClassificationRejections", {
																	  { "player", snapshot.currentClassificationRejections[0] },
																	  { "blendedMaterial", snapshot.currentClassificationRejections[1] },
																	  { "alphaTestAndBlend", snapshot.currentClassificationRejections[2] },
																	  { "ambiguousFaceGen", snapshot.currentClassificationRejections[3] },
																	  { "unsupportedMaterial", snapshot.currentClassificationRejections[4] },
																  } },
							 { "classificationRejections", {
															   { "player", snapshot.classificationRejections[0] },
															   { "blendedMaterial", snapshot.classificationRejections[1] },
															   { "alphaTestAndBlend", snapshot.classificationRejections[2] },
															   { "ambiguousFaceGen", snapshot.classificationRejections[3] },
															   { "unsupportedMaterial", snapshot.classificationRejections[4] },
														   } },
							 { "categoryCaptureAttempts", snapshot.categoryCaptureAttempts },
							 { "categoryCaptureSuccesses", snapshot.categoryCaptureSuccesses },
							 { "categoryCaptureFailures", snapshot.categoryCaptureFailures },
							 { "categoryCaptureEmptyBypasses", snapshot.categoryCaptureEmptyBypasses },
							 { "categoryCaptureFrame", snapshot.categoryCaptureReady ? json(snapshot.categoryCaptureFrame) : json(nullptr) },
							 { "categoryCaptureReady", snapshot.categoryCaptureReady },
							 { "categoryCaptureEmpty", snapshot.categoryCaptureEmpty },
							 { "authoredMaskCoverageSampleIntervalFrames", 1 },
							 { "forcedMaskCoverageSampleIntervalFrames", NeuralRendering::CharacterPolicy::kCoverageSampleIntervalFrames },
							 { "preparationAttempts", snapshot.preparationAttempts },
							 { "preparationSuccesses", snapshot.preparationSuccesses },
							 { "preparationFailures", snapshot.preparationFailures },
							 { "readbackDrops", snapshot.readbackDrops },
							 { "measuredZeroCoverageBypasses", snapshot.measuredZeroCoverageBypasses },
							 { "currentPreparedCharacterSlotMask", currentPreparedCharacterSlotMask },
							 { "eyes", std::move(eyes) },
						 } },
			{ "profiling", {
							   { "categoryCapture", ProfileTimerJson("Upscaling::DLSS5CharacterCategoryCapture") },
							   { "maskGeneration", ProfileTimerJson("Upscaling::DLSS5CharacterMask") },
							   { "roiSetup", ProfileTimerJson("Upscaling::DLSS5CharacterRoiSetup") },
							   { "baselineCopy", ProfileTimerJson("Upscaling::DLSS5CharacterBaselineCopy") },
							   { "dlss5Evaluation", {
														{ "perEye", ProfileTimerJson("Upscaling::DLSSNeuralRendering") },
														{ "batchedStereo", ProfileTimerJson("Upscaling::DLSSNeuralRenderingStereo") },
														{ "sequentialStereo", ProfileTimerJson("Upscaling::DLSSNeuralRenderingSequentialStereo") },
													} },
							   { "lastFeature18GpuSample", {
															   { "available", rendererSnapshot.performance.lastFeatureFrameId != std::numeric_limits<std::uint32_t>::max() },
															   { "frame", rendererSnapshot.performance.lastFeatureFrameId != std::numeric_limits<std::uint32_t>::max() ? json(rendererSnapshot.performance.lastFeatureFrameId) : json(nullptr) },
															   { "gpuMicroseconds", rendererSnapshot.performance.lastFeatureGpuMicroseconds },
															   { "pixelCount", rendererSnapshot.performance.lastFeaturePixelCount },
															   { "evaluationCount", rendererSnapshot.performance.lastFeatureEvaluationCount },
															   { "slotMask", lastFeatureSlotMask },
															   { "insertionPoint", NeuralRendering::GetInsertionPointName(rendererSnapshot.performance.lastInsertionPoint) },
															   { "preparedCharacterSlotMask", preparedCharacterSlotMask },
															   { "evaluationRequiredCharacterSlotMask", evaluationRequiredCharacterSlotMask },
															   { "expectedFeatureSlotMask", expectedFeatureSlotMask },
															   { "preparedFrameFound", attributedPreparationFound },
															   { "missingPreparedFeatureSlotMask", missingPreparedFeatureSlots },
															   { "missingExpectedFeatureSlotMask", missingExpectedFeatureSlots },
															   { "unexpectedFeatureSlotMask", unexpectedFeatureSlots },
															   { "correlationScope", featureTimingIsStereoPair ? "stereo_pair" : (featureTimingIsEyeSample ? "eye_sample" : "invalid") },
															   { "coversPreparedStereoPair", featureTimingIsStereoPair && lastFeatureSlotMask == expectedFeatureSlotMask },
															   { "matchesPreparedCharacterMask", featureTimingMatchesPreparedMask },
														   } },
							   { "composite", ProfileTimerJson("Upscaling::DLSS5CharacterComposite") },
							   { "total", {
											  { "available", false },
											  { "reason", "D3D11 profile components and asynchronous D3D12 Feature 18 samples are exposed separately; no uncorrelated sum is reported" },
										  } },
						   } },
		};
	}

	json NeuralRenderingStatusJson(const Upscaling& a_upscaling)
	{
		const auto snapshot = NeuralRendering::Renderer::Instance().GetSnapshot();
		json failuresByStage = json::array();
		for (std::size_t index = 0;
			index < static_cast<std::size_t>(NeuralRendering::RendererStage::Count);
			++index) {
			const auto stage = static_cast<NeuralRendering::RendererStage>(index);
			failuresByStage.push_back({
				{ "stage", NeuralRendering::ToString(stage) },
				{ "stageValue", index },
				{ "failures", snapshot.counters.failuresByStage[index] },
			});
		}

		json slots = json::array();
		for (std::size_t index = 0;
			index < NeuralRendering::Runtime::kFeatureSlotCount;
			++index) {
			slots.push_back({
				{ "slot", index },
				{ "successes", snapshot.counters.slotSuccesses[index] },
				{ "failures", snapshot.counters.slotFailures[index] },
			});
		}

		const auto routeSnapshots = a_upscaling.GetNeuralStereoRouteSnapshot();
		const auto submitCycle = a_upscaling.GetLatestNeuralSubmitCycleSnapshot();
		const uint32_t observedFrame = globals::state ? globals::state->frameCount : 0u;
		const bool submitCycleCurrentFrame =
			submitCycle.entryObserved && submitCycle.frame == observedFrame;
		uint64_t latestRouteSequence = 0;
		json routes = json::array();
		for (const auto& route : routeSnapshots) {
			latestRouteSequence = std::max(latestRouteSequence, route.sequence);
			const bool submitRole =
				route.role == Upscaling::NeuralStereoRouteRole::Submit;
			const bool freshForCurrentFrame = route.valid && route.frame == observedFrame;
			const bool freshForCurrentCycle =
				!submitRole ||
				(submitCycleCurrentFrame && submitCycle.compositorCycle != 0 &&
					route.valid && route.compositorCycle == submitCycle.compositorCycle);
			json eyes = json::array();
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const uint32_t eyeBit = 1u << eye;
				eyes.push_back({
					{ "eye", eye },
					{ "prepared", (route.preparedEyeMask & eyeBit) != 0 },
					{ "attempted", (route.attemptedEyeMask & eyeBit) != 0 },
					{ "applied", (route.appliedEyeMask & eyeBit) != 0 },
					{ "neuralCommitted", (route.committedEyeMask & eyeBit) != 0 },
					{ "dlssEvaluated", (route.dlssEyeMask & eyeBit) != 0 },
					{ "dlssEvaluationAttempts", route.dlssEvaluationAttemptCount[eye] },
					{ "dlssEvaluationSuccesses", route.dlssEvaluationSuccessCount[eye] },
					{ "feature18EvaluationAttempts", route.featureEvaluationAttemptCount[eye] },
					{ "feature18EvaluationSuccesses", route.featureEvaluationSuccessCount[eye] },
					{ "centerBlendAttempts", route.centerBlendAttemptCount[eye] },
					{ "centerBlendSuccesses", route.centerBlendSuccessCount[eye] },
					{ "lateNeuralBlendAttempts", route.lateNeuralBlendAttemptCount[eye] },
					{ "lateNeuralBlendSuccesses", route.lateNeuralBlendSuccessCount[eye] },
					{ "unexpectedPassCountDetected", (route.unexpectedPassEyeMask & eyeBit) != 0 },
				});
			}
			const auto routeInsertionPoint =
				static_cast<NeuralRendering::InsertionPoint>(route.insertionPoint);
			const auto& temporalAdmission = route.temporalAdmission;
			routes.push_back({
				{ "valid", route.valid },
				{ "role", Upscaling::GetNeuralStereoRouteRoleName(route.role) },
				{ "roleValue", static_cast<uint32_t>(route.role) },
				{ "sequence", route.sequence },
				{ "compositorCycle", route.compositorCycle },
				{ "frame", route.frame },
				{ "fresh", freshForCurrentFrame && freshForCurrentCycle },
				{ "freshForCurrentFrame", freshForCurrentFrame },
				{ "freshForCurrentCycle", freshForCurrentCycle },
				{ "generation", route.generation },
				{ "arrangement", NeuralRendering::GetPipelineArrangementName(
									 static_cast<NeuralRendering::PipelineArrangement>(route.arrangement)) },
				{ "arrangementValue", route.arrangement },
				{ "insertionPoint", NeuralRendering::GetInsertionPointName(routeInsertionPoint) },
				{ "insertionPointValue", route.insertionPoint },
				{ "requested", route.requested },
				{ "eligible", route.eligible },
				{ "sourceBatchEligible", route.sourceBatchEligible },
				{ "sourceSignatureProven", route.sourceSignatureProven },
				{ "pairComplete", route.pairComplete },
				{ "gates", {
							   { "colorBuffersHDRKnown", route.colorBuffersHDRKnown },
							   { "colorBuffersHDR", route.colorBuffersHDRKnown ? json(route.colorBuffersHDR) : json(nullptr) },
							   { "hdrRequired", route.hdrRequired },
							   { "hdrClassification", route.colorBuffersHDRKnown ? (route.colorBuffersHDR ? "hdr" : "ldr") : "unknown" },
							   { "frameGenerationActive", route.frameGenerationActive },
							   { "frameGenerationPassed", route.frameGenerationGatePassed },
							   { "knownMenuContext", temporalAdmission.menuContextActive },
							   { "hardMenuBlocked", route.hardMenuBlocked },
							   { "lateMenuCompositeReady", route.lateMenuCompositeReady },
							   { "csOverlayOpen", route.csOverlayOpen },
							   { "menuContinuityAllowed", route.menuContinuityAllowed },
							   { "gamePaused", temporalAdmission.gamePaused },
							   { "pausedSubmitContinuityAllowed", temporalAdmission.pausedSubmitContinuityAllowed },
							   { "worldFrameStateAvailable", temporalAdmission.worldFrameStateAvailable },
							   { "worldFrameStarted", temporalAdmission.worldFrameStarted },
							   { "worldFrameCompleted", temporalAdmission.worldFrameCompleted },
							   { "temporalSourceFresh", temporalAdmission.temporalSourceFresh },
						   } },
				{ "temporalAdmission", {
										   { "admitted", temporalAdmission.admitted },
										   { "blockReason", NeuralRendering::GetTemporalAdmissionBlockReasonName(temporalAdmission.blockReason) },
										   { "currentFrame", temporalAdmission.currentFrame },
										   { "lastWorldRenderFrame", temporalAdmission.lastWorldRenderFrame },
										   { "lastCompletedWorldRenderFrame", temporalAdmission.lastCompletedWorldRenderFrame },
									   } },
				{ "pairDisposition", Upscaling::GetNeuralStereoPairDispositionName(route.disposition) },
				{ "fallbackReason", Upscaling::GetNeuralStereoFallbackReasonName(route.fallbackReason) },
				{ "unexpectedPassEyeMask", route.unexpectedPassEyeMask },
				{ "eyeMasks", {
								  { "prepared", route.preparedEyeMask },
								  { "attempted", route.attemptedEyeMask },
								  { "applied", route.appliedEyeMask },
								  { "neuralCommitted", route.committedEyeMask },
								  { "dlss", route.dlssEyeMask },
							  } },
				{ "eyes", std::move(eyes) },
			});
		}

		const bool batchedStereo = a_upscaling.settings.neuralRenderingBatchedStereo;
		const bool directCommit = a_upscaling.settings.neuralRenderingDirectCommit;
		const auto insertionPoint = NeuralRendering::ClampInsertionPoint(
			a_upscaling.settings.neuralRenderingInsertionPoint);
		return {
			{ "apiVersion", 9 },
			{ "arrangement", NeuralRendering::GetPipelineArrangementName() },
			{ "implementationMatrix", NeuralImplementationMatrixJson() },
			{ "insertionPointMatrix", NeuralInsertionPointMatrixJson() },
			{ "foveation", FoveationStatusJson(a_upscaling) },
			{ "characterRendering", CharacterRenderingStatusJson(a_upscaling) },
			{ "settings", {
							  { "enabled", a_upscaling.settings.neuralRenderingEnabled },
							  { "insertionPoint", NeuralRendering::GetInsertionPointName(insertionPoint) },
							  { "insertionPointValue", static_cast<uint32_t>(insertionPoint) },
							  { "selectedInsertionPoint", NeuralInsertionPointJson(insertionPoint) },
							  { "batchedStereo", batchedStereo },
							  { "directCommit", directCommit },
							  { "stereoSubmission", NeuralRendering::GetStereoSubmissionName(batchedStereo) },
							  { "outputCommit", NeuralRendering::GetOutputCommitName(directCommit) },
							  { "implementation", NeuralRendering::GetImplementationName(batchedStereo, directCommit) },
							  { "comparisonPurpose", NeuralRendering::GetImplementationPurposeName(batchedStereo, directCommit) },
							  { "comparisonPurposeLabel", NeuralRendering::GetImplementationPurpose(batchedStereo, directCommit) },
							  { "selectedImplementation", NeuralImplementationJson(batchedStereo, directCommit) },
							  { "preset", a_upscaling.settings.neuralRenderingPreset },
							  { "intensity", a_upscaling.settings.neuralRenderingIntensity },
							  { "localToneStrength", a_upscaling.settings.neuralRenderingLocalTone },
							  { "localStructureStrength", a_upscaling.settings.neuralRenderingLocalStructure },
							  { "skinStructureStrength", a_upscaling.settings.neuralRenderingSkinStructure },
							  { "style", a_upscaling.settings.neuralRenderingStyle },
							  { "useAutoMask", a_upscaling.settings.neuralRenderingAutoMask },
							  { "uiCorrection", a_upscaling.settings.neuralRenderingUICorrection },
						  } },
			{ "safeControlContract", {
										 { "useAutoMask", {
															  { "fixed", true },
															  { "requiredValue", !a_upscaling.settings.neuralCharacterRenderingEnabled },
															  { "policy", "automatic_without_character_mask_manual_with_character_mask" },
														  } },
										 { "uiCorrection", { { "fixed", true }, { "requiredValue", false } } },
									 } },
			{ "routeObservation", {
									  { "currentFrame", observedFrame },
									  { "currentSubmitCycle", submitCycleCurrentFrame ? submitCycle.compositorCycle : 0u },
									  { "submitCycleSource", "submit_entry" },
									  { "submitEntryObserved", submitCycle.entryObserved },
									  { "submitCycleCurrentFrame", submitCycleCurrentFrame },
									  { "latestSubmitFrame", submitCycle.frame },
									  { "latestSubmitCycle", submitCycle.compositorCycle },
									  { "latestSequence", latestRouteSequence },
								  } },
			{ "eyeMaskSemantics", {
									  { "prepared", "complete renderer arguments were prepared for the eye" },
									  { "attempted", "NVIDIA Feature 18 evaluation was entered for the eye" },
									  { "applied", "the renderer committed a successful eye output before pair-level fallback" },
									  { "neuralCommitted", "the final coherent stereo pair retained the neural output" },
									  { "unexpectedPassCountDetected", "one or more pass kinds executed more than once for the eye" },
								  } },
			{ "stereoRoutes", std::move(routes) },
			{ "runtime", {
							 { "status", snapshot.status },
							 { "trust", snapshot.trust },
							 { "identityClassification", snapshot.trust },
							 { "publisherTrustEstablished", false },
							 { "admissionPolicy", "version_exports_and_loaded_image_identity" },
							 { "developerModeRequired", false },
							 { "streamlineLogLevelAffectsAdmission", false },
							 { "allowUnlistedPatchedOrUnsignedRuntime", true },
							 { "streamlineLogLevel", a_upscaling.settings.streamlineLogLevel },
							 { "failureStage", snapshot.runtimeFailureStage },
							 { "detail", snapshot.detail },
							 { "path", snapshot.runtimePath },
							 { "sha256", snapshot.runtimeHash },
							 { "version", snapshot.runtimeVersion },
							 { "proxyHits", snapshot.runtimeProxyHits },
							 { "proxyInstalled", snapshot.runtimeProxyInstalled },
							 { "successfulFrames", snapshot.runtimeSuccessfulFrames },
							 { "ngxResult", snapshot.ngxResult },
							 { "parameterCore", {
													{ "path", snapshot.parameterCorePath },
													{ "sha256", snapshot.parameterCoreHash },
													{ "trust", snapshot.parameterCoreTrust },
													{ "source", snapshot.parameterCoreSource },
												} },
						 } },
			{ "renderer", {
							  { "lastCompletedStage", NeuralRendering::ToString(snapshot.lastCompletedStage) },
							  { "failureStage", NeuralRendering::ToString(snapshot.failureStage) },
							  { "lastResult", snapshot.lastResult },
							  { "featureSlot", snapshot.featureSlot },
							  { "failureFeatureSlot", snapshot.failureFeatureSlot },
							  { "frame", snapshot.frameId },
							  { "generation", snapshot.generation },
							  { "insertionPoint", NeuralRendering::GetInsertionPointName(snapshot.insertionPoint) },
							  { "insertionPointValue", static_cast<uint32_t>(snapshot.insertionPoint) },
							  { "successes", snapshot.successes },
							  { "failures", snapshot.failures },
							  { "featureUpscaling", snapshot.featureUpscaling },
							  { "failureLatched", snapshot.failureLatched },
							  { "quarantined", snapshot.quarantined },
							  { "outputCommitted", snapshot.outputCommitted },
							  { "controlMaskPresent", snapshot.controlMaskPresent },
						  } },
			{ "dimensions", {
								{ "color", { { "width", snapshot.colorWidth }, { "height", snapshot.colorHeight } } },
								{ "guide", { { "width", snapshot.guideWidth }, { "height", snapshot.guideHeight } } },
								{ "output", { { "width", snapshot.outputWidth }, { "height", snapshot.outputHeight } } },
								{ "controlMask", { { "width", snapshot.controlMaskWidth }, { "height", snapshot.controlMaskHeight } } },
							} },
			{ "resources", { { "formats", {
											  { "color", snapshot.colorFormat },
											  { "depthSource", snapshot.depthSourceFormat },
											  { "depthView", snapshot.depthViewFormat },
											  { "motionVectors", snapshot.motionVectorFormat },
											  { "output", snapshot.outputFormat },
											  { "controlMask", snapshot.controlMaskFormat },
										  } } } },
			{ "counters", {
							  { "attempts", snapshot.counters.attempts },
							  { "successes", snapshot.counters.successes },
							  { "failures", snapshot.counters.failures },
							  { "validationFailures", snapshot.counters.validationFailures },
							  { "interopInitializations", snapshot.counters.interopInitializations },
							  { "runtimeInitializations", snapshot.counters.runtimeInitializations },
							  { "resourceRebuilds", snapshot.counters.resourceRebuilds },
							  { "depthGuideCopies", snapshot.counters.depthGuideCopies },
							  { "controlMaskCopies", snapshot.counters.controlMaskCopies },
							  { "featureEvaluations", snapshot.counters.featureEvaluations },
							  { "outputCommits", snapshot.counters.outputCommits },
							  { "stereoAttempts", snapshot.counters.stereoAttempts },
							  { "stereoSuccesses", snapshot.counters.stereoSuccesses },
							  { "stereoFailures", snapshot.counters.stereoFailures },
							  { "callerHistoryResets", snapshot.counters.callerHistoryResets },
							  { "forcedHistoryResets", snapshot.counters.forcedHistoryResets },
							  { "discontinuousHistoryResets", snapshot.counters.discontinuousHistoryResets },
							  { "resetAttempts", snapshot.counters.resetAttempts },
							  { "resetSuccesses", snapshot.counters.resetSuccesses },
							  { "resetFailures", snapshot.counters.resetFailures },
							  { "deviceRemovals", snapshot.counters.deviceRemovals },
							  { "quarantines", snapshot.counters.quarantines },
							  { "latchedBypasses", snapshot.counters.latchedBypasses },
							  { "quarantinedBypasses", snapshot.counters.quarantinedBypasses },
							  { "failuresByStage", std::move(failuresByStage) },
						  } },
			{ "performance", {
								 { "d3d11PreparationCpuEnqueueSamples", snapshot.performance.d3d11PreparationCpuEnqueueSamples },
								 { "d3d11PreparationCpuEnqueueMicroseconds", snapshot.performance.d3d11PreparationCpuEnqueueMicroseconds },
								 { "lastD3D11PreparationCpuEnqueueMicroseconds", snapshot.performance.lastD3D11PreparationCpuEnqueueMicroseconds },
								 { "maximumD3D11PreparationCpuEnqueueMicroseconds", snapshot.performance.maximumD3D11PreparationCpuEnqueueMicroseconds },
								 { "outputCommitCpuEnqueueSamples", snapshot.performance.outputCommitCpuEnqueueSamples },
								 { "outputCommitCpuEnqueueMicroseconds", snapshot.performance.outputCommitCpuEnqueueMicroseconds },
								 { "lastOutputCommitCpuEnqueueMicroseconds", snapshot.performance.lastOutputCommitCpuEnqueueMicroseconds },
								 { "maximumOutputCommitCpuEnqueueMicroseconds", snapshot.performance.maximumOutputCommitCpuEnqueueMicroseconds },
								 { "commandSubmissions", snapshot.performance.commandSubmissions },
								 { "mainCommandSubmissions", snapshot.performance.mainCommandSubmissions },
								 { "submitCommandSubmissions", snapshot.performance.submitCommandSubmissions },
								 { "stereoCommandSubmissions", snapshot.performance.stereoCommandSubmissions },
								 { "mainStereoCommandSubmissions", snapshot.performance.mainStereoCommandSubmissions },
								 { "submitStereoCommandSubmissions", snapshot.performance.submitStereoCommandSubmissions },
								 { "backpressureWaits", snapshot.performance.backpressureWaits },
								 { "backpressureWaitMicroseconds", snapshot.performance.backpressureWaitMicroseconds },
								 { "maximumBackpressureWaitMicroseconds", snapshot.performance.maximumBackpressureWaitMicroseconds },
								 { "featureGpuSamples", snapshot.performance.featureGpuSamples },
								 { "featureGpuReadbackFailures", snapshot.performance.featureGpuReadbackFailures },
								 { "featureGpuMicroseconds", snapshot.performance.featureGpuMicroseconds },
								 { "mainFeatureGpuSamples", snapshot.performance.mainFeatureGpuSamples },
								 { "mainFeatureGpuMicroseconds", snapshot.performance.mainFeatureGpuMicroseconds },
								 { "submitFeatureGpuSamples", snapshot.performance.submitFeatureGpuSamples },
								 { "submitFeatureGpuMicroseconds", snapshot.performance.submitFeatureGpuMicroseconds },
								 { "byInsertionPoint", NeuralInsertionPointPerformanceJson(snapshot.performance) },
								 { "unexpectedFeatureSlotMaskSamples", snapshot.performance.unexpectedFeatureSlotMaskSamples },
								 { "invalidInsertionPointSamples", snapshot.performance.invalidInsertionPointSamples },
								 { "lastFeatureGpuMicroseconds", snapshot.performance.lastFeatureGpuMicroseconds },
								 { "maximumFeatureGpuMicroseconds", snapshot.performance.maximumFeatureGpuMicroseconds },
								 { "lastFeaturePixelCount", snapshot.performance.lastFeaturePixelCount },
								 { "lastFeatureFrameId", snapshot.performance.lastFeatureFrameId },
								 { "lastFeatureEvaluationCount", snapshot.performance.lastFeatureEvaluationCount },
								 { "lastFeatureSlotMask", snapshot.performance.lastFeatureSlotMask },
								 { "lastInsertionPoint", NeuralRendering::GetInsertionPointName(snapshot.performance.lastInsertionPoint) },
								 { "lastInsertionPointValue", static_cast<uint32_t>(snapshot.performance.lastInsertionPoint) },
							 } },
			{ "slots", std::move(slots) },
		};
	}

	json BuildStatus(Upscaling& a_upscaling)
	{
		const auto controller = a_upscaling.GetVRRenderScaleTransitionSnapshot();
		const auto session = a_upscaling.GetVRRenderScaleStressSessionSnapshot();
		const uint32_t frame = globals::state ? globals::state->frameCount : 0u;

		json eyes = json::array();
		for (const auto& eye : controller.fidelity.eyes) {
			eyes.push_back({
				{ "frame", eye.frame },
				{ "generation", eye.generation },
				{ "inputWidth", eye.inputWidth },
				{ "inputHeight", eye.inputHeight },
				{ "outputWidth", eye.outputWidth },
				{ "outputHeight", eye.outputHeight },
				{ "evaluated", eye.evaluated },
				{ "valid", eye.valid },
			});
		}
		json presentationEyes = json::array();
		for (const auto& eye : controller.presentation.eyes) {
			presentationEyes.push_back({
				{ "valid", eye.valid },
				{ "path", Upscaling::GetVRRenderScalePresentationPathName(eye.path) },
				{ "frame", eye.frame },
				{ "transitionEpoch", eye.transitionEpoch },
				{ "contractGeneration", eye.contractGeneration },
				{ "method", GetUpscaleMethodName(eye.method) },
				{ "inputWidth", eye.inputWidth },
				{ "inputHeight", eye.inputHeight },
				{ "expectedInputWidth", eye.expectedInputWidth },
				{ "expectedInputHeight", eye.expectedInputHeight },
				{ "outputWidth", eye.outputWidth },
				{ "outputHeight", eye.outputHeight },
				{ "consecutiveFrames", eye.consecutiveFrames },
				{ "loadingOrMenuContext", eye.loadingOrMenuContext },
				{ "transitionCooldown", eye.transitionCooldown },
			});
		}
		const auto observationsSinceStart = [](uint64_t a_current, uint64_t a_baseline) {
			return a_current >= a_baseline ? a_current - a_baseline : a_current;
		};

		return {
			{ "frame", frame },
			{ "modeStatus", Upscaling::GetVRRenderScaleModeStatusName(a_upscaling.GetVRRenderScaleModeStatus()) },
			{ "loadPresentationProbe", a_upscaling.BuildVRLoadPresentationProbeStatus() },
			{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) },
			{ "session", {
							 { "id", session.sessionID },
							 { "active", session.active },
							 { "startFrame", session.startFrame },
							 { "endFrame", session.endFrame },
							 { "retainedEvents", session.count },
							 { "overwrittenEvents", session.overwrittenEvents },
						 } },
			{ "controller", {
								{ "state", Upscaling::GetVRRenderScaleTransitionStateName(controller.state) },
								{ "targetEpoch", controller.targetEpoch },
								{ "revision", controller.revision },
								{ "requested", ProfileJson(controller.requested) },
								{ "applying", ProfileJson(controller.applying) },
								{ "applied", ProfileJson(controller.applied) },
								{ "stable", ProfileJson(controller.stable) },
								{ "memory", {
												{ "valid", controller.memory.valid },
												{ "sampleFrame", controller.memory.sampleFrame },
												{ "usageBytes", controller.memory.currentUsageBytes },
												{ "budgetBytes", controller.memory.budgetBytes },
												{ "headroomBytes", controller.memory.headroomBytes },
												{ "usageRatio", controller.memory.usageRatio },
												{ "systemCommitValid", controller.memory.systemCommitValid },
												{ "systemCommitBytes", controller.memory.systemCommitBytes },
												{ "systemCommitLimitBytes", controller.memory.systemCommitLimitBytes },
												{ "systemCommitHeadroomBytes", controller.memory.systemCommitHeadroomBytes },
												{ "systemCommitRatio", controller.memory.systemCommitRatio },
												{ "processPrivateUsageValid", controller.memory.processPrivateUsageValid },
												{ "processPrivateUsageBytes", controller.memory.processPrivateUsageBytes },
												{ "pressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.memory.pressure) },
												{ "recoverySamples", controller.memory.recoverySamples },
											} },
								{ "resourcePlan", {
													  { "valid", controller.relatchPlan.valid },
													  { "transitionEpoch", controller.relatchPlan.transitionEpoch },
													  { "previousVendorMethod", GetUpscaleMethodName(controller.relatchPlan.previousVendorMethod) },
													  { "memoryPressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.relatchPlan.memoryPressure) },
													  { "estimatedAdditionalBytes", controller.relatchPlan.estimatedAdditionalBytes },
													  { "projectedAdditionalBytes", controller.relatchPlan.projectedAdditionalBytes },
													  { "projectedUsageBytes", controller.relatchPlan.projectedUsageBytes },
													  { "admissionUsageLimitBytes", controller.relatchPlan.admissionUsageLimitBytes },
													  { "postTrimAdmissionUsageLimitBytes", controller.relatchPlan.postTrimAdmissionUsageLimitBytes },
													  { "projectedSystemCommitAdditionalBytes", controller.relatchPlan.projectedSystemCommitAdditionalBytes },
													  { "projectedSystemCommitBytes", controller.relatchPlan.projectedSystemCommitBytes },
													  { "systemCommitAdmissionLimitBytes", controller.relatchPlan.systemCommitAdmissionLimitBytes },
													  { "pressureCleanupRequired", controller.relatchPlan.pressureCleanupRequired },
													  { "projectedResidencyGuardActive", controller.relatchPlan.projectedResidencyGuardActive },
													  { "projectedResidencyPostTrimRelaxed", controller.relatchPlan.projectedResidencyPostTrimRelaxed },
													  { "projectedResidencyDeferred", controller.relatchPlan.projectedResidencyDeferred },
													  { "systemCommitGuardActive", controller.relatchPlan.systemCommitGuardActive },
													  { "doorHandoffHardReserveOnly", controller.relatchPlan.doorHandoffHardReserveOnly },
													  { "systemCommitDeferred", controller.relatchPlan.systemCommitDeferred },
													  { "pressureDeferred", controller.relatchPlan.pressureDeferred },
												  } },
								{ "memoryTrim", {
													{ "pending", controller.memoryTrim.pending },
													{ "reason", Upscaling::GetVRRenderScaleMemoryTrimReasonName(controller.memoryTrim.reason) },
													{ "ownerEpoch", controller.memoryTrim.ownerEpoch },
													{ "requestedFrame", controller.memoryTrim.requestedFrame },
													{ "completedFrame", controller.memoryTrim.completedFrame },
													{ "fenceFailures", controller.memoryTrim.fenceFailures },
													{ "completedCount", controller.memoryTrim.completedCount },
													{ "failures", controller.memoryTrim.failures },
													{ "lastSucceeded", controller.memoryTrim.lastSucceeded },
													{ "preRecreateDrainCount", controller.memoryTrim.preRecreateDrainCount },
													{ "preRecreateDrainFailures", controller.memoryTrim.preRecreateDrainFailures },
													{ "lastOfferedResourceCount", controller.memoryTrim.lastOfferedResourceCount },
													{ "lastOfferUsedDecommit", controller.memoryTrim.lastOfferUsedDecommit },
												} },
								{ "retirement", {
													{ "pendingSets", controller.retirement.pendingSets },
													{ "fencePending", controller.retirement.fencePending },
													{ "capacityBlocked", controller.retirement.capacityBlocked },
													{ "nextCleanupFrame", controller.retirement.nextCleanupFrame },
												} },
								{ "engineTargetRetirement", {
																{ "supported", controller.engineTargetRetirement.supported },
																{ "pending", controller.engineTargetRetirement.pending },
																{ "oldestEpoch", controller.engineTargetRetirement.oldestEpoch },
																{ "newestEpoch", controller.engineTargetRetirement.newestEpoch },
																{ "pendingGenerations", controller.engineTargetRetirement.pendingGenerations },
																{ "capturedPointerCount", controller.engineTargetRetirement.capturedPointerCount },
																{ "aliasedPointerCount", controller.engineTargetRetirement.aliasedPointerCount },
																{ "provenPointerCount", controller.engineTargetRetirement.provenPointerCount },
																{ "retainedUnprovenPointerCount", controller.engineTargetRetirement.retainedUnprovenPointerCount },
																{ "replacedPointerCount", controller.engineTargetRetirement.replacedPointerCount },
																{ "restoredPointerCount", controller.engineTargetRetirement.restoredPointerCount },
																{ "pendingReleaseCount", controller.engineTargetRetirement.pendingReleaseCount },
																{ "lastReleasedPointerCount", controller.engineTargetRetirement.lastReleasedPointerCount },
																{ "totalReleasedPointerCount", controller.engineTargetRetirement.totalReleasedPointerCount },
																{ "completedCount", controller.engineTargetRetirement.completedCount },
																{ "fenceFailures", controller.engineTargetRetirement.fenceFailures },
																{ "fencePending", controller.engineTargetRetirement.fencePending },
																{ "capacityBlocked", controller.engineTargetRetirement.capacityBlocked },
															} },
								{ "postLoadRecovery", {
														  { "active", controller.postLoadRecovery.active },
														  { "recoveryEpoch", controller.postLoadRecovery.recoveryEpoch },
														  { "settledSamples", controller.postLoadRecovery.settledSamples },
														  { "baselineUsageBytes", controller.postLoadRecovery.baselineUsageBytes },
														  { "peakUsageBytes", controller.postLoadRecovery.peakUsageBytes },
														  { "baselineSystemCommitBytes", controller.postLoadRecovery.baselineSystemCommitBytes },
														  { "peakSystemCommitBytes", controller.postLoadRecovery.peakSystemCommitBytes },
														  { "baselineProcessPrivateUsageBytes", controller.postLoadRecovery.baselineProcessPrivateUsageBytes },
														  { "peakProcessPrivateUsageBytes", controller.postLoadRecovery.peakProcessPrivateUsageBytes },
														  { "peakPressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.postLoadRecovery.peakPressure) },
														  { "cleanupDrained", controller.postLoadRecovery.cleanupDrained },
														  { "trimArmed", controller.postLoadRecovery.trimArmed },
														  { "trimCompleted", controller.postLoadRecovery.trimCompleted },
														  { "trimSucceeded", controller.postLoadRecovery.trimSucceeded },
														  { "relatchAdmitted", controller.postLoadRecovery.relatchAdmitted },
													  } },
								{ "fidelity", {
												  { "active", controller.fidelity.active },
												  { "transitionEpoch", controller.fidelity.transitionEpoch },
												  { "contractGeneration", controller.fidelity.contractGeneration },
												  { "method", GetUpscaleMethodName(controller.fidelity.method) },
												  { "backend", GetBackendName(controller.fidelity.backend) },
												  { "bothEyesValid", controller.fidelity.bothEyesValid },
												  { "evaluationEyeMask", controller.fidelity.evaluationEyeMask },
												  { "invariantEyeMask", controller.fidelity.invariantEyeMask },
												  { "lastMismatchMask", controller.fidelity.lastMismatchMask },
												  { "mismatchCount", controller.fidelity.mismatchCount },
												  { "eyes", std::move(eyes) },
											  } },
								{ "presentation", {
													  { "lastBothEyesVendorFrame", controller.presentation.lastBothEyesVendorFrame },
													  { "consecutiveBothEyesVendorFrames", controller.presentation.consecutiveBothEyesVendorFrames },
													  { "lastFallbackFrame", controller.presentation.lastFallbackFrame },
													  { "maximumConsecutivePresentationStretchFrames", controller.presentation.maximumConsecutivePresentationStretchFrames },
													  { "vendorEvaluatedEyeObservations", controller.presentation.vendorEvaluatedEyeObservations },
													  { "presentationStretchEyeObservations", controller.presentation.presentationStretchEyeObservations },
													  { "vendorFailureStretchEyeObservations", controller.presentation.vendorFailureStretchEyeObservations },
													  { "boundsMismatchOriginalFallbackEyeObservations", controller.presentation.boundsMismatchOriginalFallbackEyeObservations },
													  { "sessionVendorEvaluatedEyeObservations", observationsSinceStart(controller.presentation.vendorEvaluatedEyeObservations, session.baselineVendorEvaluatedEyeObservations) },
													  { "sessionPresentationStretchEyeObservations", observationsSinceStart(controller.presentation.presentationStretchEyeObservations, session.baselinePresentationStretchEyeObservations) },
													  { "sessionVendorFailureStretchEyeObservations", observationsSinceStart(controller.presentation.vendorFailureStretchEyeObservations, session.baselineVendorFailureStretchEyeObservations) },
													  { "sessionBoundsMismatchOriginalFallbackEyeObservations", observationsSinceStart(controller.presentation.boundsMismatchOriginalFallbackEyeObservations, session.baselineBoundsMismatchOriginalFallbackEyeObservations) },
													  { "eyes", std::move(presentationEyes) },
												  } },
								{ "currentMetrics", {
														{ "valid", controller.metrics.current.valid },
														{ "transitionEpoch", controller.metrics.current.transitionEpoch },
														{ "retries", controller.metrics.current.retries },
														{ "pressureDeferrals", controller.metrics.current.pressureDeferrals },
														{ "retirementDeferrals", controller.metrics.current.retirementDeferrals },
														{ "backendDeferrals", controller.metrics.current.backendDeferrals },
														{ "failures", controller.metrics.current.failures },
														{ "fidelityMismatches", controller.metrics.current.fidelityMismatches },
														{ "memoryTrimCount", controller.metrics.current.memoryTrimCount },
														{ "memoryTrimFailures", controller.metrics.current.memoryTrimFailures },
														{ "memoryPreRecreateDrainCount", controller.metrics.current.memoryPreRecreateDrainCount },
														{ "memoryPreRecreateDrainFailures", controller.metrics.current.memoryPreRecreateDrainFailures },
													} },
								{ "dlssLifecycle", LifecycleJson(controller.dlssLifecycle) },
								{ "fsrLifecycle", LifecycleJson(controller.fsrLifecycle) },
							} },
		};
	}

	void RunHandler(
		json (*a_build)(const json&),
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = a_build(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		try {
			const std::string serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			const char* fallback = R"({"error":"response serialization failed"})";
			a_write(a_sink, fallback);
		}
	}

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return {
				{ "error", "SKSE task interface unavailable" },
				{ "errorCode", "main_thread_unavailable" },
			};

		enum class TaskClaim : uint8_t
		{
			Pending,
			Running,
			Cancelled,
		};
		auto promise = std::make_shared<std::promise<json>>();
		auto claim = std::make_shared<std::atomic<TaskClaim>>(TaskClaim::Pending);
		auto future = promise->get_future();
		try {
			taskInterface->AddTask([promise, claim, run = std::move(a_run)]() mutable {
				auto expected = TaskClaim::Pending;
				if (!claim->compare_exchange_strong(
						expected,
						TaskClaim::Running,
						std::memory_order_acq_rel,
						std::memory_order_acquire)) {
					return;
				}

				json result;
				try {
					result = run();
				} catch (const std::exception& e) {
					result = {
						{ "error", "main-thread task failed" },
						{ "errorCode", "main_thread_task_failed" },
						{ "detail", e.what() },
					};
				} catch (...) {
					result = {
						{ "error", "main-thread task failed" },
						{ "errorCode", "main_thread_task_failed" },
					};
				}
				try {
					promise->set_value(std::move(result));
				} catch (const std::exception& e) {
					logger::error(
						"VRRenderScaleDevBenchBridge: could not publish main-thread result: {}",
						e.what());
				} catch (...) {
					logger::error(
						"VRRenderScaleDevBenchBridge: could not publish main-thread result");
				}
			});
		} catch (const std::exception& e) {
			return {
				{ "error", "could not queue main-thread task" },
				{ "errorCode", "main_thread_queue_failed" },
				{ "detail", e.what() },
			};
		} catch (...) {
			return {
				{ "error", "could not queue main-thread task" },
				{ "errorCode", "main_thread_queue_failed" },
			};
		}

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			auto expected = TaskClaim::Pending;
			if (claim->compare_exchange_strong(
					expected,
					TaskClaim::Cancelled,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
				return {
					{ "error", "main thread did not run within 5000ms" },
					{ "errorCode", "main_thread_timeout" },
					{ "mainThreadTaskClaimed", false },
					{ "mutationOutcome", "not_started" },
				};
			}

			// A running task cannot be cancelled; bound the wait and report uncertainty.
			if (future.wait_for(kMainThreadCompletionGrace) !=
				std::future_status::ready) {
				return {
					{ "error", "main-thread task is still running after timeout" },
					{ "errorCode", "main_thread_running_timeout" },
					{ "mainThreadTaskClaimed", true },
					{ "mutationOutcome", "indeterminate" },
				};
			}
		}
		return future.get();
	}

	json EnsureFoveationMutationEnvelope(
		json a_response,
		std::string_view a_action,
		std::optional<std::string_view> a_control = std::nullopt)
	{
		if (!a_response.is_object()) {
			a_response = {
				{ "error", "invalid main-thread response" },
				{ "errorCode", "main_thread_response_invalid" },
			};
		}
		if (!a_response.contains("action"))
			a_response["action"] = std::string(a_action);
		if (a_control && !a_response.contains("control"))
			a_response["control"] = std::string(*a_control);
		if (!a_response.contains("mutationFrame"))
			a_response["mutationFrame"] = nullptr;
		a_response["executionClaimed"] = false;
		return a_response;
	}

	template <std::size_t N>
	bool TryValidateActionFields(
		const json& a_args,
		const std::array<std::string_view, N>& a_allowedFields,
		std::string_view a_actionName,
		std::string_view a_errorCode,
		json& a_error)
	{
		for (const auto& item : a_args.items()) {
			const auto found = std::find(
				a_allowedFields.begin(),
				a_allowedFields.end(),
				std::string_view(item.key()));
			if (found != a_allowedFields.end())
				continue;

			a_error = {
				{ "error", std::format("{} is not valid for {}", item.key(), a_actionName) },
				{ "errorCode", std::string(a_errorCode) },
				{ "field", item.key() },
			};
			return false;
		}
		return true;
	}

	constexpr std::array kNeuralRenderingConfigureFields{
		std::string_view{ "action" },
		std::string_view{ "enabled" },
		std::string_view{ "insertionPoint" },
		std::string_view{ "preset" },
		std::string_view{ "intensity" },
		std::string_view{ "localToneStrength" },
		std::string_view{ "localStructureStrength" },
		std::string_view{ "skinStructureStrength" },
		std::string_view{ "style" },
		std::string_view{ "batchedStereo" },
		std::string_view{ "directCommit" },
		std::string_view{ "implementation" },
		std::string_view{ "optimizedStereoPath" },
		std::string_view{ "useAutoMask" },
		std::string_view{ "uiCorrection" },
		std::string_view{ "characterEnabled" },
		std::string_view{ "characterVisualIsolationEnabled" },
		std::string_view{ "characterFaces" },
		std::string_view{ "characterSkin" },
		std::string_view{ "characterHair" },
		std::string_view{ "characterFaceStrength" },
		std::string_view{ "characterSkinStrength" },
		std::string_view{ "characterHairStrength" },
		std::string_view{ "characterMaximumDistanceMeters" },
		std::string_view{ "characterMinimumFacePixelSize" },
		std::string_view{ "characterRoiMargin" },
		std::string_view{ "characterMaximumRoiRegions" },
		std::string_view{ "characterRoiHoldFrames" },
		std::string_view{ "characterDepthAwareFeather" },
		std::string_view{ "characterVisibilityDepthTest" },
		std::string_view{ "characterFeatherRadius" },
		std::string_view{ "characterFeatherDepthThreshold" },
		std::string_view{ "characterDebugView" },
		std::string_view{ "characterMaskTestMode" },
	};

	struct NeuralRenderingConfigurationRequest
	{
		std::optional<bool> enabled;
		std::optional<NeuralRendering::InsertionPoint> insertionPoint;
		std::optional<uint32_t> preset;
		std::optional<float> intensity;
		std::optional<float> localToneStrength;
		std::optional<float> localStructureStrength;
		std::optional<float> skinStructureStrength;
		std::optional<uint32_t> style;
		std::optional<bool> batchedStereo;
		std::optional<bool> directCommit;
		std::optional<std::string> implementation;
		std::optional<bool> legacyOptimizedStereoPath;
		std::optional<bool> useAutoMask;
		std::optional<bool> uiCorrection;
		std::optional<bool> characterEnabled;
		std::optional<bool> characterVisualIsolationEnabled;
		std::optional<bool> characterFaces;
		std::optional<bool> characterSkin;
		std::optional<bool> characterHair;
		std::optional<float> characterFaceStrength;
		std::optional<float> characterSkinStrength;
		std::optional<float> characterHairStrength;
		std::optional<float> characterMaximumDistanceMeters;
		std::optional<std::uint32_t> characterMinimumFacePixelSize;
		std::optional<float> characterRoiMargin;
		std::optional<std::uint32_t> characterMaximumRoiRegions;
		std::optional<std::uint32_t> characterRoiHoldFrames;
		std::optional<bool> characterDepthAwareFeather;
		std::optional<bool> characterVisibilityDepthTest;
		std::optional<std::uint32_t> characterFeatherRadius;
		std::optional<float> characterFeatherDepthThreshold;
		std::optional<NeuralRendering::CharacterDebugView> characterDebugView;
		std::optional<NeuralRendering::CharacterMaskTestMode> characterMaskTestMode;

		[[nodiscard]] bool HasImageTuningOverrides() const noexcept
		{
			return intensity || localToneStrength || localStructureStrength ||
			       skinStructureStrength || style;
		}

		[[nodiscard]] bool HasCharacterControls() const noexcept
		{
			return characterEnabled || characterVisualIsolationEnabled ||
			       characterFaces || characterSkin ||
			       characterHair || characterFaceStrength ||
			       characterSkinStrength || characterHairStrength ||
			       characterMaximumDistanceMeters ||
			       characterMinimumFacePixelSize || characterRoiMargin ||
			       characterMaximumRoiRegions || characterRoiHoldFrames ||
			       characterDepthAwareFeather || characterVisibilityDepthTest ||
			       characterFeatherRadius ||
			       characterFeatherDepthThreshold ||
			       characterDebugView || characterMaskTestMode;
		}

		[[nodiscard]] bool HasAnyControl() const noexcept
		{
			return enabled || insertionPoint || preset || HasImageTuningOverrides() ||
			       batchedStereo || directCommit || implementation ||
			       legacyOptimizedStereoPath || useAutoMask || uiCorrection ||
			       HasCharacterControls();
		}
	};

	bool TryParseNeuralRenderingConfiguration(
		const json& a_args,
		NeuralRenderingConfigurationRequest& a_request,
		json& a_error)
	{
		if (!TryValidateActionFields(
				a_args,
				kNeuralRenderingConfigureFields,
				"nr_configure",
				"nr_request_field_unknown",
				a_error)) {
			return false;
		}

		if (const auto enabled = a_args.find("enabled"); enabled != a_args.end()) {
			if (!enabled->is_boolean()) {
				a_error = { { "error", "enabled must be a boolean" } };
				return false;
			}
			a_request.enabled = enabled->get<bool>();
		}

		if (const auto insertionPoint = a_args.find("insertionPoint");
			insertionPoint != a_args.end()) {
			if (!insertionPoint->is_string()) {
				a_error = {
					{ "error", "insertionPoint must be a string" },
					{ "errorCode", "nr_insertion_point_type_invalid" },
					{ "field", "insertionPoint" },
				};
				return false;
			}
			const auto requested = insertionPoint->get<std::string>();
			const auto parsed = NeuralRendering::ParseInsertionPointName(requested);
			if (!parsed) {
				a_error = {
					{ "error", "insertionPoint is not a supported Neural Rendering insertion point" },
					{ "errorCode", "nr_insertion_point_unknown" },
					{ "field", "insertionPoint" },
					{ "requested", requested },
				};
				return false;
			}
			a_request.insertionPoint = *parsed;
		}

		if (const auto preset = a_args.find("preset"); preset != a_args.end()) {
			if (preset->is_number_unsigned()) {
				const auto requested = preset->get<uint64_t>();
				if (requested > 4u) {
					a_error = {
						{ "error", "preset is outside 0..4" },
						{ "errorCode", "nr_preset_out_of_range" },
						{ "field", "preset" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.preset = static_cast<uint32_t>(requested);
			} else if (preset->is_number_integer()) {
				const auto requested = preset->get<int64_t>();
				if (requested < 0 || requested > 4) {
					a_error = {
						{ "error", "preset is outside 0..4" },
						{ "errorCode", "nr_preset_out_of_range" },
						{ "field", "preset" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.preset = static_cast<uint32_t>(requested);
			} else {
				a_error = {
					{ "error", "preset must be an integer" },
					{ "errorCode", "nr_preset_type_invalid" },
					{ "field", "preset" },
				};
				return false;
			}
		}

		const auto parseStrength =
			[&](const char* a_name, std::optional<float>& a_output) {
				const auto value = a_args.find(a_name);
				if (value == a_args.end())
					return true;
				if (!value->is_number()) {
					a_error = {
						{ "error", std::format("{} must be a number", a_name) },
						{ "errorCode", "nr_tuning_type_invalid" },
						{ "field", a_name },
					};
					return false;
				}
				const double requested = value->get<double>();
				if (!std::isfinite(requested)) {
					a_error = {
						{ "error", std::format("{} must be finite", a_name) },
						{ "errorCode", "nr_tuning_non_finite" },
						{ "field", a_name },
					};
					return false;
				}
				if (requested < 0.0 || requested > 2.0) {
					a_error = {
						{ "error", std::format("{} is outside 0..2", a_name) },
						{ "errorCode", "nr_tuning_out_of_range" },
						{ "field", a_name },
						{ "requested", requested },
					};
					return false;
				}
				a_output = static_cast<float>(requested);
				return true;
			};
		if (!parseStrength("intensity", a_request.intensity) ||
			!parseStrength("localToneStrength", a_request.localToneStrength) ||
			!parseStrength("localStructureStrength", a_request.localStructureStrength) ||
			!parseStrength("skinStructureStrength", a_request.skinStructureStrength)) {
			return false;
		}

		if (const auto style = a_args.find("style"); style != a_args.end()) {
			if (style->is_number_unsigned()) {
				const auto requested = style->get<uint64_t>();
				if (requested > 3u) {
					a_error = {
						{ "error", "style is outside 0..3" },
						{ "errorCode", "nr_style_out_of_range" },
						{ "field", "style" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.style = static_cast<uint32_t>(requested);
			} else if (style->is_number_integer()) {
				const auto requested = style->get<int64_t>();
				if (requested < 0 || requested > 3) {
					a_error = {
						{ "error", "style is outside 0..3" },
						{ "errorCode", "nr_style_out_of_range" },
						{ "field", "style" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.style = static_cast<uint32_t>(requested);
			} else {
				a_error = {
					{ "error", "style must be an integer" },
					{ "errorCode", "nr_style_type_invalid" },
					{ "field", "style" },
				};
				return false;
			}
		}

		const auto parseBoolean =
			[&](const char* a_name, std::optional<bool>& a_output) {
				const auto value = a_args.find(a_name);
				if (value == a_args.end())
					return true;
				if (!value->is_boolean()) {
					a_error = { { "error", std::format("{} must be a boolean", a_name) } };
					return false;
				}
				a_output = value->get<bool>();
				return true;
			};
		if (!parseBoolean("batchedStereo", a_request.batchedStereo) ||
			!parseBoolean("directCommit", a_request.directCommit) ||
			!parseBoolean("optimizedStereoPath", a_request.legacyOptimizedStereoPath) ||
			!parseBoolean("useAutoMask", a_request.useAutoMask) ||
			!parseBoolean("uiCorrection", a_request.uiCorrection) ||
			!parseBoolean("characterEnabled", a_request.characterEnabled) ||
			!parseBoolean(
				"characterVisualIsolationEnabled",
				a_request.characterVisualIsolationEnabled) ||
			!parseBoolean("characterFaces", a_request.characterFaces) ||
			!parseBoolean("characterSkin", a_request.characterSkin) ||
			!parseBoolean("characterHair", a_request.characterHair) ||
			!parseBoolean(
				"characterDepthAwareFeather",
				a_request.characterDepthAwareFeather) ||
			!parseBoolean(
				"characterVisibilityDepthTest",
				a_request.characterVisibilityDepthTest)) {
			return false;
		}

		const auto parseCharacterFloat =
			[&](
				const char* a_name,
				double a_minimum,
				double a_maximum,
				std::optional<float>& a_output) {
				const auto value = a_args.find(a_name);
				if (value == a_args.end())
					return true;
				if (!value->is_number()) {
					a_error = {
						{ "error", std::format("{} must be a number", a_name) },
						{ "errorCode", "nr_character_number_type_invalid" },
						{ "field", a_name },
					};
					return false;
				}
				const double requested = value->get<double>();
				if (!std::isfinite(requested)) {
					a_error = {
						{ "error", std::format("{} must be finite", a_name) },
						{ "errorCode", "nr_character_number_non_finite" },
						{ "field", a_name },
					};
					return false;
				}
				if (requested < a_minimum || requested > a_maximum) {
					a_error = {
						{ "error", std::format("{} is outside {}..{}", a_name, a_minimum, a_maximum) },
						{ "errorCode", "nr_character_number_out_of_range" },
						{ "field", a_name },
						{ "requested", requested },
						{ "minimum", a_minimum },
						{ "maximum", a_maximum },
					};
					return false;
				}
				a_output = static_cast<float>(requested);
				return true;
			};
		const auto parseCharacterUint =
			[&](
				const char* a_name,
				std::uint32_t a_minimum,
				std::uint32_t a_maximum,
				std::optional<std::uint32_t>& a_output) {
				const auto value = a_args.find(a_name);
				if (value == a_args.end())
					return true;
				std::uint64_t requested = 0;
				if (value->is_number_unsigned()) {
					requested = value->get<std::uint64_t>();
				} else if (value->is_number_integer()) {
					const auto signedValue = value->get<std::int64_t>();
					if (signedValue < 0) {
						a_error = {
							{ "error", std::format("{} is outside {}..{}", a_name, a_minimum, a_maximum) },
							{ "errorCode", "nr_character_integer_out_of_range" },
							{ "field", a_name },
							{ "requested", signedValue },
							{ "minimum", a_minimum },
							{ "maximum", a_maximum },
						};
						return false;
					}
					requested = static_cast<std::uint64_t>(signedValue);
				} else {
					a_error = {
						{ "error", std::format("{} must be an integer", a_name) },
						{ "errorCode", "nr_character_integer_type_invalid" },
						{ "field", a_name },
					};
					return false;
				}
				if (requested < a_minimum || requested > a_maximum) {
					a_error = {
						{ "error", std::format("{} is outside {}..{}", a_name, a_minimum, a_maximum) },
						{ "errorCode", "nr_character_integer_out_of_range" },
						{ "field", a_name },
						{ "requested", requested },
						{ "minimum", a_minimum },
						{ "maximum", a_maximum },
					};
					return false;
				}
				a_output = static_cast<std::uint32_t>(requested);
				return true;
			};

		if (!parseCharacterFloat(
				"characterFaceStrength",
				NeuralRendering::CharacterPolicy::kMinimumStrength,
				NeuralRendering::CharacterPolicy::kMaximumStrength,
				a_request.characterFaceStrength) ||
			!parseCharacterFloat(
				"characterSkinStrength",
				NeuralRendering::CharacterPolicy::kMinimumStrength,
				NeuralRendering::CharacterPolicy::kMaximumStrength,
				a_request.characterSkinStrength) ||
			!parseCharacterFloat(
				"characterHairStrength",
				NeuralRendering::CharacterPolicy::kMinimumStrength,
				NeuralRendering::CharacterPolicy::kMaximumStrength,
				a_request.characterHairStrength) ||
			!parseCharacterFloat(
				"characterMaximumDistanceMeters",
				NeuralRendering::CharacterPolicy::kMinimumDistanceMeters,
				NeuralRendering::CharacterPolicy::kMaximumDistanceMeters,
				a_request.characterMaximumDistanceMeters) ||
			!parseCharacterFloat(
				"characterRoiMargin",
				NeuralRendering::CharacterPolicy::kMinimumRoiMargin,
				NeuralRendering::CharacterPolicy::kMaximumRoiMargin,
				a_request.characterRoiMargin) ||
			!parseCharacterFloat(
				"characterFeatherDepthThreshold", 0.0,
				NeuralRendering::CharacterPolicy::kMaximumFeatherDepthThreshold,
				a_request.characterFeatherDepthThreshold) ||
			!parseCharacterUint(
				"characterMinimumFacePixelSize",
				NeuralRendering::CharacterPolicy::kMinimumFacePixelSize,
				NeuralRendering::CharacterPolicy::kMaximumFacePixelSize,
				a_request.characterMinimumFacePixelSize) ||
			!parseCharacterUint(
				"characterMaximumRoiRegions", 1u,
				NeuralRendering::CharacterPolicy::kMaximumRoiRegions,
				a_request.characterMaximumRoiRegions) ||
			!parseCharacterUint(
				"characterRoiHoldFrames", 0u,
				NeuralRendering::CharacterPolicy::kMaximumRoiHoldFrames,
				a_request.characterRoiHoldFrames) ||
			!parseCharacterUint(
				"characterFeatherRadius", 0u,
				NeuralRendering::CharacterPolicy::kMaximumFeatherRadius,
				a_request.characterFeatherRadius)) {
			return false;
		}

		if (const auto debugView = a_args.find("characterDebugView");
			debugView != a_args.end()) {
			if (!debugView->is_string()) {
				a_error = {
					{ "error", "characterDebugView must be a string" },
					{ "errorCode", "nr_character_debug_view_type_invalid" },
					{ "field", "characterDebugView" },
				};
				return false;
			}
			const auto requested = debugView->get<std::string>();
			if (requested == "off") {
				a_request.characterDebugView = NeuralRendering::CharacterDebugView::Off;
			} else if (requested == "character_mask") {
				a_request.characterDebugView =
					NeuralRendering::CharacterDebugView::CharacterMask;
			} else if (requested == "roi_rectangles") {
				a_request.characterDebugView =
					NeuralRendering::CharacterDebugView::RoiRectangles;
			} else if (requested == "dlss5_output") {
				a_request.characterDebugView =
					NeuralRendering::CharacterDebugView::Dlss5Output;
			} else {
				a_error = {
					{ "error", "characterDebugView is not supported" },
					{ "errorCode", "nr_character_debug_view_unknown" },
					{ "field", "characterDebugView" },
					{ "requested", requested },
				};
				return false;
			}
		}

		if (const auto maskTestMode = a_args.find("characterMaskTestMode");
			maskTestMode != a_args.end()) {
			if (!maskTestMode->is_string()) {
				a_error = {
					{ "error", "characterMaskTestMode must be a string" },
					{ "errorCode", "nr_character_mask_test_mode_type_invalid" },
					{ "field", "characterMaskTestMode" },
				};
				return false;
			}
			const auto requested = maskTestMode->get<std::string>();
			if (requested == "authored") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::Authored;
			} else if (requested == "force_zero") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::ForceZero;
			} else if (requested == "force_one") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::ForceOne;
			} else if (requested == "force_half") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::ForceHalf;
			} else if (requested == "invert_authored") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::InvertAuthored;
			} else if (requested == "authored_without_visibility_depth") {
				a_request.characterMaskTestMode =
					NeuralRendering::CharacterMaskTestMode::AuthoredWithoutVisibilityDepth;
			} else {
				a_error = {
					{ "error", "characterMaskTestMode is not supported" },
					{ "errorCode", "nr_character_mask_test_mode_unknown" },
					{ "field", "characterMaskTestMode" },
					{ "requested", requested },
				};
				return false;
			}
		}

		if (const auto implementation = a_args.find("implementation");
			implementation != a_args.end()) {
			if (!implementation->is_string()) {
				a_error = { { "error", "implementation must be a string" } };
				return false;
			}
			a_request.implementation = implementation->get<std::string>();
			const auto parsed =
				NeuralRendering::ParseImplementationName(*a_request.implementation);
			if (!parsed) {
				a_error = {
					{ "error", "implementation is not a supported Neural Rendering lane" },
					{ "errorCode", "nr_implementation_unknown" },
				};
				return false;
			}
			a_request.batchedStereo = parsed->batchedStereo;
			a_request.directCommit = parsed->directCommit;
		}
		if (a_request.implementation &&
			(a_args.contains("batchedStereo") ||
				a_args.contains("directCommit") ||
				a_args.contains("optimizedStereoPath"))) {
			a_error = {
				{ "error", "implementation cannot be combined with batchedStereo, directCommit, or optimizedStereoPath" },
				{ "errorCode", "nr_implementation_controls_ambiguous" },
			};
			return false;
		}
		if (a_request.legacyOptimizedStereoPath &&
			(a_request.batchedStereo || a_request.directCommit)) {
			a_error = {
				{ "error", "optimizedStereoPath cannot be combined with batchedStereo or directCommit" },
				{ "errorCode", "nr_implementation_controls_ambiguous" },
			};
			return false;
		}
		if (a_request.uiCorrection && *a_request.uiCorrection) {
			a_error = {
				{ "error", "neural UI correction requires unimplemented UI resources" },
				{ "errorCode", "nr_ui_correction_unsupported" },
			};
			return false;
		}
		if (!a_request.HasAnyControl()) {
			a_error = {
				{ "error", "nr_configure requires at least one Neural Rendering control" },
				{ "errorCode", "nr_configure_empty" },
			};
			return false;
		}
		return true;
	}

	struct FoveationConfigurationRequest
	{
		std::optional<bool> foveatedEnabled;
		std::optional<bool> peripheryTaaEnabled;
		std::optional<FoveatedCenterAlignment::CenterOrigin> centerOrigin;
		std::optional<FoveatedCenterAlignment::HorizontalAnchor> horizontalAnchor;
		std::optional<float> fovOnlyCenterScale;
		std::optional<float> peripheryTaaCenterScale;
		std::optional<float> peripheryTaaOuterScale;
		std::optional<float> centerHorizontalScale;
		std::optional<float> leftEyeOffsetX;
		std::optional<float> leftEyeOffsetY;
		std::optional<float> rightEyeOffsetX;
		std::optional<float> rightEyeOffsetY;
		std::optional<float> fovOnlyBlendFeather;
		std::optional<float> peripheryTaaBlendFeather;
		std::optional<float> neuralFinalLdrBlendFeather;
		std::optional<uint32_t> reconstructionGuardBandPixels;
		std::optional<bool> maskVisualization;

		[[nodiscard]] bool HasAnyControl() const noexcept
		{
			return foveatedEnabled || peripheryTaaEnabled || centerOrigin ||
			       horizontalAnchor || fovOnlyCenterScale ||
			       peripheryTaaCenterScale || peripheryTaaOuterScale ||
			       centerHorizontalScale ||
			       leftEyeOffsetX || leftEyeOffsetY || rightEyeOffsetX ||
			       rightEyeOffsetY || fovOnlyBlendFeather ||
			       peripheryTaaBlendFeather || neuralFinalLdrBlendFeather ||
			       reconstructionGuardBandPixels || maskVisualization;
		}
	};

	constexpr std::array kFoveationConfigureFields{
		std::string_view{ "action" },
		std::string_view{ "foveatedEnabled" },
		std::string_view{ "peripheryTaaEnabled" },
		std::string_view{ "centerOrigin" },
		std::string_view{ "horizontalAnchor" },
		std::string_view{ "fovOnlyCenterScale" },
		std::string_view{ "peripheryTaaCenterScale" },
		std::string_view{ "peripheryTaaOuterScale" },
		std::string_view{ "centerHorizontalScale" },
		std::string_view{ "leftEyeOffsetX" },
		std::string_view{ "leftEyeOffsetY" },
		std::string_view{ "rightEyeOffsetX" },
		std::string_view{ "rightEyeOffsetY" },
		std::string_view{ "fovOnlyBlendFeather" },
		std::string_view{ "peripheryTaaBlendFeather" },
		std::string_view{ "neuralFinalLdrBlendFeather" },
		std::string_view{ "reconstructionGuardBandPixels" },
		std::string_view{ "maskVisualization" },
	};
	constexpr std::array kFoveationCycleFields{
		std::string_view{ "action" },
		std::string_view{ "control" },
		std::string_view{ "valueIndex" },
	};

	template <std::size_t N>
	bool TryValidateFoveationActionFields(
		const json& a_args,
		const std::array<std::string_view, N>& a_allowedFields,
		json& a_error)
	{
		return TryValidateActionFields(
			a_args,
			a_allowedFields,
			"this foveation action",
			"foveation_request_field_unknown",
			a_error);
	}

	bool TryParseFoveationBoolean(
		const json& a_args,
		const char* a_name,
		std::optional<bool>& a_output,
		json& a_error)
	{
		const auto value = a_args.find(a_name);
		if (value == a_args.end())
			return true;
		if (!value->is_boolean()) {
			a_error = {
				{ "error", std::format("{} must be a boolean", a_name) },
				{ "errorCode", "foveation_boolean_type_invalid" },
				{ "field", a_name },
			};
			return false;
		}
		a_output = value->get<bool>();
		return true;
	}

	bool TryParseFoveationFloat(
		const json& a_args,
		const char* a_name,
		double a_minimum,
		double a_maximum,
		std::optional<float>& a_output,
		json& a_error)
	{
		const auto value = a_args.find(a_name);
		if (value == a_args.end())
			return true;
		if (!value->is_number()) {
			a_error = {
				{ "error", std::format("{} must be a number", a_name) },
				{ "errorCode", "foveation_number_type_invalid" },
				{ "field", a_name },
			};
			return false;
		}

		const double requested = value->get<double>();
		if (!std::isfinite(requested)) {
			a_error = {
				{ "error", std::format("{} must be finite", a_name) },
				{ "errorCode", "foveation_number_non_finite" },
				{ "field", a_name },
			};
			return false;
		}
		if (requested < a_minimum || requested > a_maximum) {
			a_error = {
				{ "error", std::format("{} is outside {}..{}", a_name, a_minimum, a_maximum) },
				{ "errorCode", "foveation_number_out_of_range" },
				{ "field", a_name },
				{ "requested", requested },
				{ "minimum", a_minimum },
				{ "maximum", a_maximum },
			};
			return false;
		}

		a_output = static_cast<float>(requested);
		return true;
	}

	bool TryParseFoveationGuardBand(
		const json& a_args,
		std::optional<uint32_t>& a_output,
		json& a_error)
	{
		constexpr const char* name = "reconstructionGuardBandPixels";
		const auto value = a_args.find(name);
		if (value == a_args.end())
			return true;

		uint64_t requested = 0;
		if (value->is_number_unsigned()) {
			requested = value->get<uint64_t>();
		} else if (value->is_number_integer()) {
			const auto signedValue = value->get<int64_t>();
			if (signedValue < 0) {
				a_error = {
					{ "error", std::format("{} is outside 0..{}", name, Upscaling::kFoveatedReconstructionGuardBandMax) },
					{ "errorCode", "foveation_guard_band_out_of_range" },
					{ "field", name },
					{ "requested", signedValue },
				};
				return false;
			}
			requested = static_cast<uint64_t>(signedValue);
		} else {
			a_error = {
				{ "error", std::format("{} must be an integer", name) },
				{ "errorCode", "foveation_guard_band_type_invalid" },
				{ "field", name },
			};
			return false;
		}

		if (requested > Upscaling::kFoveatedReconstructionGuardBandMax) {
			a_error = {
				{ "error", std::format("{} is outside 0..{}", name, Upscaling::kFoveatedReconstructionGuardBandMax) },
				{ "errorCode", "foveation_guard_band_out_of_range" },
				{ "field", name },
				{ "requested", requested },
			};
			return false;
		}

		a_output = static_cast<uint32_t>(requested);
		return true;
	}

	bool TryParseFoveationCenterOrigin(
		const json& a_args,
		std::optional<FoveatedCenterAlignment::CenterOrigin>& a_output,
		json& a_error)
	{
		constexpr const char* name = "centerOrigin";
		const auto value = a_args.find(name);
		if (value == a_args.end())
			return true;
		if (!value->is_string()) {
			a_error = {
				{ "error", std::format("{} must be a string", name) },
				{ "errorCode", "foveation_center_origin_type_invalid" },
				{ "field", name },
			};
			return false;
		}

		const auto requested = value->get<std::string>();
		constexpr std::array values{
			FoveatedCenterAlignment::CenterOrigin::ImageCenter,
			FoveatedCenterAlignment::CenterOrigin::OpticalCenter,
		};
		for (const auto candidate : values) {
			if (requested == FoveatedCenterAlignment::GetCenterOriginName(candidate)) {
				a_output = candidate;
				return true;
			}
		}

		a_error = {
			{ "error", "centerOrigin is not supported" },
			{ "errorCode", "foveation_center_origin_unknown" },
			{ "field", name },
			{ "requested", requested },
		};
		return false;
	}

	bool TryParseFoveationHorizontalAnchor(
		const json& a_args,
		std::optional<FoveatedCenterAlignment::HorizontalAnchor>& a_output,
		json& a_error)
	{
		constexpr const char* name = "horizontalAnchor";
		const auto value = a_args.find(name);
		if (value == a_args.end())
			return true;
		if (!value->is_string()) {
			a_error = {
				{ "error", std::format("{} must be a string", name) },
				{ "errorCode", "foveation_horizontal_anchor_type_invalid" },
				{ "field", name },
			};
			return false;
		}

		const auto requested = value->get<std::string>();
		constexpr std::array values{
			FoveatedCenterAlignment::HorizontalAnchor::Symmetric,
			FoveatedCenterAlignment::HorizontalAnchor::Outward,
		};
		for (const auto candidate : values) {
			if (requested == FoveatedCenterAlignment::GetHorizontalAnchorName(candidate)) {
				a_output = candidate;
				return true;
			}
		}

		a_error = {
			{ "error", "horizontalAnchor is not supported" },
			{ "errorCode", "foveation_horizontal_anchor_unknown" },
			{ "field", name },
			{ "requested", requested },
		};
		return false;
	}

	bool TryParseFoveationConfiguration(
		const json& a_args,
		FoveationConfigurationRequest& a_request,
		json& a_error)
	{
		if (!TryValidateFoveationActionFields(
				a_args, kFoveationConfigureFields, a_error) ||
			!TryParseFoveationBoolean(a_args, "foveatedEnabled", a_request.foveatedEnabled, a_error) ||
			!TryParseFoveationBoolean(a_args, "peripheryTaaEnabled", a_request.peripheryTaaEnabled, a_error) ||
			!TryParseFoveationCenterOrigin(a_args, a_request.centerOrigin, a_error) ||
			!TryParseFoveationHorizontalAnchor(a_args, a_request.horizontalAnchor, a_error) ||
			!TryParseFoveationFloat(a_args, "fovOnlyCenterScale", kCenterScaleRequestMin, kCenterScaleRequestMax, a_request.fovOnlyCenterScale, a_error) ||
			!TryParseFoveationFloat(a_args, "peripheryTaaCenterScale", kCenterScaleRequestMin, kCenterScaleRequestMax, a_request.peripheryTaaCenterScale, a_error) ||
			!TryParseFoveationFloat(a_args, "peripheryTaaOuterScale", kPeripheryTAAOuterScaleRequestMin, kPeripheryTAAOuterScaleRequestMax, a_request.peripheryTaaOuterScale, a_error) ||
			!TryParseFoveationFloat(a_args, "centerHorizontalScale", kCenterHorizontalScaleRequestMin, kCenterHorizontalScaleRequestMax, a_request.centerHorizontalScale, a_error) ||
			!TryParseFoveationFloat(a_args, "leftEyeOffsetX", kManualOffsetRequestMin, kManualOffsetRequestMax, a_request.leftEyeOffsetX, a_error) ||
			!TryParseFoveationFloat(a_args, "leftEyeOffsetY", kManualOffsetRequestMin, kManualOffsetRequestMax, a_request.leftEyeOffsetY, a_error) ||
			!TryParseFoveationFloat(a_args, "rightEyeOffsetX", kManualOffsetRequestMin, kManualOffsetRequestMax, a_request.rightEyeOffsetX, a_error) ||
			!TryParseFoveationFloat(a_args, "rightEyeOffsetY", kManualOffsetRequestMin, kManualOffsetRequestMax, a_request.rightEyeOffsetY, a_error) ||
			!TryParseFoveationFloat(a_args, "fovOnlyBlendFeather", kBlendFeatherRequestMin, kBlendFeatherRequestMax, a_request.fovOnlyBlendFeather, a_error) ||
			!TryParseFoveationFloat(a_args, "peripheryTaaBlendFeather", kBlendFeatherRequestMin, kBlendFeatherRequestMax, a_request.peripheryTaaBlendFeather, a_error) ||
			!TryParseFoveationFloat(a_args, "neuralFinalLdrBlendFeather", kBlendFeatherRequestMin, kBlendFeatherRequestMax, a_request.neuralFinalLdrBlendFeather, a_error) ||
			!TryParseFoveationGuardBand(a_args, a_request.reconstructionGuardBandPixels, a_error) ||
			!TryParseFoveationBoolean(a_args, "maskVisualization", a_request.maskVisualization, a_error)) {
			return false;
		}

		if (!a_request.HasAnyControl()) {
			a_error = {
				{ "error", "foveation_configure requires at least one foveation control" },
				{ "errorCode", "foveation_configure_empty" },
			};
			return false;
		}
		return true;
	}

	void ApplyFoveationRequest(
		Upscaling::Settings& a_settings,
		const FoveationConfigurationRequest& a_request)
	{
		if (a_request.foveatedEnabled)
			a_settings.foveatedVendorDispatch = *a_request.foveatedEnabled;
		if (a_request.peripheryTaaEnabled)
			a_settings.periphery_taa_enable = *a_request.peripheryTaaEnabled;
		if (a_request.centerOrigin)
			a_settings.foveatedCenterOrigin = static_cast<uint>(*a_request.centerOrigin);
		if (a_request.horizontalAnchor)
			a_settings.foveatedHorizontalAnchor = static_cast<uint>(*a_request.horizontalAnchor);
		if (a_request.fovOnlyCenterScale)
			a_settings.foveatedCenterArea = *a_request.fovOnlyCenterScale;
		if (a_request.peripheryTaaCenterScale)
			a_settings.periphery_taa_center_area = *a_request.peripheryTaaCenterScale;
		if (a_request.peripheryTaaOuterScale)
			a_settings.periphery_taa_outer_scale = *a_request.peripheryTaaOuterScale;
		if (a_request.centerHorizontalScale)
			a_settings.foveatedCenterHorizontalScale = *a_request.centerHorizontalScale;
		if (a_request.leftEyeOffsetX)
			a_settings.foveatedLeftEyeMaskOffsetX = *a_request.leftEyeOffsetX;
		if (a_request.leftEyeOffsetY)
			a_settings.foveatedLeftEyeMaskOffsetY = *a_request.leftEyeOffsetY;
		if (a_request.rightEyeOffsetX)
			a_settings.foveatedRightEyeMaskOffsetX = *a_request.rightEyeOffsetX;
		if (a_request.rightEyeOffsetY)
			a_settings.foveatedRightEyeMaskOffsetY = *a_request.rightEyeOffsetY;
		if (a_request.fovOnlyBlendFeather)
			a_settings.foveatedCenterBlendFeather = *a_request.fovOnlyBlendFeather;
		if (a_request.peripheryTaaBlendFeather)
			a_settings.periphery_taa_center_blend_feather = *a_request.peripheryTaaBlendFeather;
		if (a_request.neuralFinalLdrBlendFeather)
			a_settings.neuralRenderingBlendFeather = *a_request.neuralFinalLdrBlendFeather;
		if (a_request.reconstructionGuardBandPixels)
			a_settings.foveatedReconstructionGuardBandPixels = *a_request.reconstructionGuardBandPixels;
		if (a_request.maskVisualization)
			a_settings.foveatedPeripheryMaskVisualization = *a_request.maskVisualization;
	}

	bool HasSameFoveationControls(
		const Upscaling::Settings& a_left,
		const Upscaling::Settings& a_right)
	{
		return a_left.foveatedVendorDispatch == a_right.foveatedVendorDispatch &&
		       a_left.periphery_taa_enable == a_right.periphery_taa_enable &&
		       a_left.foveatedCenterOrigin == a_right.foveatedCenterOrigin &&
		       a_left.foveatedHorizontalAnchor == a_right.foveatedHorizontalAnchor &&
		       a_left.foveatedCenterArea == a_right.foveatedCenterArea &&
		       a_left.periphery_taa_center_area == a_right.periphery_taa_center_area &&
		       a_left.periphery_taa_outer_scale == a_right.periphery_taa_outer_scale &&
		       a_left.foveatedCenterHorizontalScale == a_right.foveatedCenterHorizontalScale &&
		       a_left.foveatedLeftEyeMaskOffsetX == a_right.foveatedLeftEyeMaskOffsetX &&
		       a_left.foveatedLeftEyeMaskOffsetY == a_right.foveatedLeftEyeMaskOffsetY &&
		       a_left.foveatedRightEyeMaskOffsetX == a_right.foveatedRightEyeMaskOffsetX &&
		       a_left.foveatedRightEyeMaskOffsetY == a_right.foveatedRightEyeMaskOffsetY &&
		       a_left.foveatedCenterBlendFeather == a_right.foveatedCenterBlendFeather &&
		       a_left.periphery_taa_center_blend_feather == a_right.periphery_taa_center_blend_feather &&
		       a_left.neuralRenderingBlendFeather == a_right.neuralRenderingBlendFeather &&
		       a_left.foveatedReconstructionGuardBandPixels == a_right.foveatedReconstructionGuardBandPixels &&
		       a_left.foveatedPeripheryMaskVisualization == a_right.foveatedPeripheryMaskVisualization;
	}

	json ApplyFoveationConfiguration(
		Upscaling& a_upscaling,
		const FoveationConfigurationRequest& a_request,
		const char* a_action,
		bool a_rejectNoOp,
		json a_response = json::object())
	{
		const auto previousSettings = a_upscaling.settings;
		auto requestedSettings = previousSettings;
		ApplyFoveationRequest(requestedSettings, a_request);
		const bool requestedSettingsChanged = !HasSameFoveationControls(
			previousSettings, requestedSettings);
		const uint32_t mutationFrame = globals::state ?
		                                   globals::state->frameCount :
		                                   0u;
		const uint32_t measurementSafeFromFrame = mutationFrame ==
		                                                  std::numeric_limits<uint32_t>::max() ?
		                                              mutationFrame :
		                                              mutationFrame + 1u;

		a_response["action"] = a_action;
		a_response["mutationFrame"] = mutationFrame;
		a_response["executionClaimed"] = false;
		a_response["requestedSettingsChanged"] = requestedSettingsChanged;
		a_response["settingsChanged"] = false;
		a_response["noOp"] = !requestedSettingsChanged;
		a_response["historyResetRequested"] = false;
		a_response["frameScopedStateInvalidated"] = false;
		a_response["effectiveNotBeforeFrame"] = nullptr;
		a_response["measurementSafeFromFrame"] = nullptr;

		if (requestedSettings.periphery_taa_outer_scale <
			requestedSettings.periphery_taa_center_area) {
			a_response["error"] =
				"peripheryTaaOuterScale must be at least peripheryTaaCenterScale";
			a_response["errorCode"] = "foveation_outer_scale_below_center";
			a_response["field"] = "peripheryTaaOuterScale";
			a_response["requested"] = requestedSettings.periphery_taa_outer_scale;
			a_response["minimum"] = requestedSettings.periphery_taa_center_area;
			a_response["neuralSettingsTransitionAttempted"] = false;
			a_response["transitionSucceeded"] = true;
			a_response["neuralRendering"] = NeuralRenderingStatusJson(a_upscaling);
			return a_response;
		}

		if (!requestedSettingsChanged) {
			if (a_rejectNoOp) {
				a_response["error"] = "requested foveation settings already match the active settings";
				a_response["errorCode"] = "foveation_configure_noop";
			}
			a_response["neuralSettingsTransitionAttempted"] = false;
			a_response["transitionSucceeded"] = true;
			a_response["neuralRendering"] = NeuralRenderingStatusJson(a_upscaling);
			return a_response;
		}

		const bool neuralSettingsChanged =
			!Upscaling::HasSameNeuralRenderingSettingsKey(
				previousSettings, requestedSettings);
		a_upscaling.settings = requestedSettings;
		a_upscaling.InvalidateFrameScopedUpscalingState();
		a_upscaling.RequestHistoryReset();
		a_response["settingsChanged"] = true;
		a_response["historyResetRequested"] = true;
		a_response["frameScopedStateInvalidated"] = true;
		a_response["effectiveNotBeforeFrame"] = mutationFrame;
		a_response["measurementSafeFromFrame"] = measurementSafeFromFrame;
		const bool transitionSucceeded = !neuralSettingsChanged ||
		                                 a_upscaling.HandleNeuralRenderingSettingsTransition(
											 previousSettings,
											 "DevBench foveation configuration");
		a_response["neuralSettingsTransitionAttempted"] = neuralSettingsChanged;
		a_response["transitionSucceeded"] = transitionSucceeded;
		a_response["neuralRendering"] = NeuralRenderingStatusJson(a_upscaling);
		if (!transitionSucceeded) {
			a_response["error"] = "neural-rendering transition did not complete";
			a_response["errorCode"] = "foveation_neural_transition_failed";
		}
		return a_response;
	}

	template <class T, std::size_t N>
	bool SelectExactCycleValue(
		const T& a_current,
		const std::array<T, N>& a_values,
		const std::optional<uint32_t>& a_requestedIndex,
		T& a_selected,
		uint32_t& a_selectedIndex,
		json& a_error)
	{
		if (a_requestedIndex && *a_requestedIndex >= N) {
			a_error = {
				{ "error", std::format("valueIndex is outside 0..{}", N - 1u) },
				{ "errorCode", "foveation_cycle_index_out_of_range" },
				{ "requested", *a_requestedIndex },
				{ "maximum", N - 1u },
			};
			return false;
		}

		if (a_requestedIndex) {
			a_selectedIndex = *a_requestedIndex;
		} else {
			std::size_t currentIndex = N;
			for (std::size_t index = 0; index < N; ++index) {
				if (a_values[index] == a_current) {
					currentIndex = index;
					break;
				}
			}
			a_selectedIndex = currentIndex < N ?
			                      static_cast<uint32_t>((currentIndex + 1u) % N) :
			                      0u;
		}
		a_selected = a_values[a_selectedIndex];
		return true;
	}

	template <class TValues>
	bool SelectFloatCycleValue(
		float a_current,
		const TValues& a_values,
		const std::optional<uint32_t>& a_requestedIndex,
		float& a_selected,
		uint32_t& a_selectedIndex,
		json& a_error)
	{
		const std::size_t valueCount = a_values.size();
		if (valueCount == 0u) {
			a_error = {
				{ "error", "foveation cycle has no values" },
				{ "errorCode", "foveation_cycle_values_empty" },
			};
			return false;
		}
		if (a_requestedIndex && *a_requestedIndex >= valueCount) {
			a_error = {
				{ "error", std::format("valueIndex is outside 0..{}", valueCount - 1u) },
				{ "errorCode", "foveation_cycle_index_out_of_range" },
				{ "requested", *a_requestedIndex },
				{ "maximum", valueCount - 1u },
			};
			return false;
		}

		if (a_requestedIndex) {
			a_selectedIndex = *a_requestedIndex;
		} else {
			std::size_t currentIndex = valueCount;
			for (std::size_t index = 0; index < valueCount; ++index) {
				if (std::abs(a_values[index] - a_current) <= 1.0e-6f) {
					currentIndex = index;
					break;
				}
			}
			if (currentIndex < valueCount) {
				a_selectedIndex = static_cast<uint32_t>(
					(currentIndex + 1u) % valueCount);
			} else {
				a_selectedIndex = 0u;
				for (std::size_t index = 0; index < valueCount; ++index) {
					if (a_values[index] > a_current) {
						a_selectedIndex = static_cast<uint32_t>(index);
						break;
					}
				}
			}
		}
		a_selected = a_values[a_selectedIndex];
		return true;
	}

	bool TryParseFoveationCycleRequest(
		const json& a_args,
		FoveationCycleControl& a_control,
		std::optional<uint32_t>& a_valueIndex,
		json& a_error)
	{
		if (!TryValidateFoveationActionFields(
				a_args, kFoveationCycleFields, a_error)) {
			return false;
		}
		const auto control = a_args.find("control");
		if (control == a_args.end() || !control->is_string()) {
			a_error = {
				{ "error", "control must be a supported foveation control name" },
				{ "errorCode", "foveation_cycle_control_invalid" },
				{ "field", "control" },
			};
			return false;
		}
		const auto parsedControl = ParseFoveationCycleControl(
			control->get<std::string>());
		if (!parsedControl) {
			a_error = {
				{ "error", "control is not a supported foveation cycle axis" },
				{ "errorCode", "foveation_cycle_control_unknown" },
				{ "field", "control" },
				{ "requested", control->get<std::string>() },
			};
			return false;
		}
		a_control = *parsedControl;

		const auto valueIndex = a_args.find("valueIndex");
		if (valueIndex == a_args.end())
			return true;
		uint64_t requested = 0;
		if (valueIndex->is_number_unsigned()) {
			requested = valueIndex->get<uint64_t>();
		} else if (valueIndex->is_number_integer()) {
			const auto signedIndex = valueIndex->get<int64_t>();
			if (signedIndex < 0) {
				a_error = {
					{ "error", "valueIndex must be a non-negative integer" },
					{ "errorCode", "foveation_cycle_index_invalid" },
					{ "field", "valueIndex" },
				};
				return false;
			}
			requested = static_cast<uint64_t>(signedIndex);
		} else {
			a_error = {
				{ "error", "valueIndex must be a non-negative integer" },
				{ "errorCode", "foveation_cycle_index_invalid" },
				{ "field", "valueIndex" },
			};
			return false;
		}
		if (requested > std::numeric_limits<uint32_t>::max()) {
			a_error = {
				{ "error", "valueIndex is too large" },
				{ "errorCode", "foveation_cycle_index_invalid" },
				{ "field", "valueIndex" },
			};
			return false;
		}
		a_valueIndex = static_cast<uint32_t>(requested);
		return true;
	}

	json ApplyFoveationCycle(
		Upscaling& a_upscaling,
		FoveationCycleControl a_control,
		const std::optional<uint32_t>& a_requestedIndex)
	{
		const auto& settings = a_upscaling.settings;
		FoveationConfigurationRequest request;
		json response{
			{ "control", GetFoveationCycleControlName(a_control) },
		};
		json error;
		uint32_t selectedIndex = 0;
		uint32_t matrixSize = 0;
		auto recordSelection =
			[&](json a_previousValue, json a_selectedValue, uint32_t a_size) {
				matrixSize = a_size;
				response["previousValue"] = std::move(a_previousValue);
				response["currentValue"] = std::move(a_selectedValue);
			};

		switch (a_control) {
		case FoveationCycleControl::Master:
			{
				constexpr std::array values{ false, true };
				bool selected = false;
				if (!SelectExactCycleValue(
						settings.foveatedVendorDispatch,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.foveatedEnabled = selected;
				recordSelection(
					settings.foveatedVendorDispatch, selected,
					static_cast<uint32_t>(values.size()));
				break;
			}
		case FoveationCycleControl::PeripheryTAA:
			{
				constexpr std::array values{ false, true };
				bool selected = false;
				if (!SelectExactCycleValue(
						settings.periphery_taa_enable,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.peripheryTaaEnabled = selected;
				recordSelection(
					settings.periphery_taa_enable, selected,
					static_cast<uint32_t>(values.size()));
				break;
			}
		case FoveationCycleControl::CenterOrigin:
			{
				constexpr std::array values{
					FoveatedCenterAlignment::CenterOrigin::ImageCenter,
					FoveatedCenterAlignment::CenterOrigin::OpticalCenter,
				};
				const auto current =
					static_cast<FoveatedCenterAlignment::CenterOrigin>(
						settings.foveatedCenterOrigin);
				FoveatedCenterAlignment::CenterOrigin selected{};
				if (!SelectExactCycleValue(
						current,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.centerOrigin = selected;
				recordSelection(
					FoveatedCenterAlignment::GetCenterOriginName(current),
					FoveatedCenterAlignment::GetCenterOriginName(selected),
					static_cast<uint32_t>(values.size()));
				break;
			}
		case FoveationCycleControl::HorizontalAnchor:
			{
				constexpr std::array values{
					FoveatedCenterAlignment::HorizontalAnchor::Symmetric,
					FoveatedCenterAlignment::HorizontalAnchor::Outward,
				};
				const auto current =
					static_cast<FoveatedCenterAlignment::HorizontalAnchor>(
						settings.foveatedHorizontalAnchor);
				FoveatedCenterAlignment::HorizontalAnchor selected{};
				if (!SelectExactCycleValue(
						current,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.horizontalAnchor = selected;
				recordSelection(
					FoveatedCenterAlignment::GetHorizontalAnchorName(current),
					FoveatedCenterAlignment::GetHorizontalAnchorName(selected),
					static_cast<uint32_t>(values.size()));
				break;
			}
		case FoveationCycleControl::FovOnlyCenterScale:
			{
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						settings.foveatedCenterArea,
						kCenterScaleCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.fovOnlyCenterScale = selected;
				recordSelection(
					settings.foveatedCenterArea, selected,
					static_cast<uint32_t>(kCenterScaleCycleValues.size()));
				break;
			}
		case FoveationCycleControl::PeripheryTAACenterScale:
			{
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						settings.periphery_taa_center_area,
						kCenterScaleCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.peripheryTaaCenterScale = selected;
				if (settings.periphery_taa_outer_scale < selected) {
					request.peripheryTaaOuterScale = selected;
					response["dependentOuterScale"] = selected;
				}
				recordSelection(
					settings.periphery_taa_center_area, selected,
					static_cast<uint32_t>(kCenterScaleCycleValues.size()));
				break;
			}
		case FoveationCycleControl::PeripheryTAAOuterScale:
			{
				const auto values = GetPeripheryTAAOuterScaleCycleValues(settings);
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						settings.periphery_taa_outer_scale,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.peripheryTaaOuterScale = selected;
				recordSelection(
					settings.periphery_taa_outer_scale, selected,
					static_cast<uint32_t>(values.size()));
				break;
			}
		case FoveationCycleControl::CenterHorizontalScale:
			{
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						settings.foveatedCenterHorizontalScale,
						kHorizontalScaleCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.centerHorizontalScale = selected;
				recordSelection(
					settings.foveatedCenterHorizontalScale, selected,
					static_cast<uint32_t>(kHorizontalScaleCycleValues.size()));
				break;
			}
		case FoveationCycleControl::LeftEyeOffsetX:
		case FoveationCycleControl::LeftEyeOffsetY:
		case FoveationCycleControl::RightEyeOffsetX:
		case FoveationCycleControl::RightEyeOffsetY:
			{
				float current = 0.0f;
				switch (a_control) {
				case FoveationCycleControl::LeftEyeOffsetX:
					current = settings.foveatedLeftEyeMaskOffsetX;
					break;
				case FoveationCycleControl::LeftEyeOffsetY:
					current = settings.foveatedLeftEyeMaskOffsetY;
					break;
				case FoveationCycleControl::RightEyeOffsetX:
					current = settings.foveatedRightEyeMaskOffsetX;
					break;
				case FoveationCycleControl::RightEyeOffsetY:
					current = settings.foveatedRightEyeMaskOffsetY;
					break;
				default:
					break;
				}
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						current,
						kManualOffsetCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				switch (a_control) {
				case FoveationCycleControl::LeftEyeOffsetX:
					request.leftEyeOffsetX = selected;
					break;
				case FoveationCycleControl::LeftEyeOffsetY:
					request.leftEyeOffsetY = selected;
					break;
				case FoveationCycleControl::RightEyeOffsetX:
					request.rightEyeOffsetX = selected;
					break;
				case FoveationCycleControl::RightEyeOffsetY:
					request.rightEyeOffsetY = selected;
					break;
				default:
					break;
				}
				recordSelection(
					current, selected,
					static_cast<uint32_t>(kManualOffsetCycleValues.size()));
				break;
			}
		case FoveationCycleControl::FovOnlyBlendFeather:
		case FoveationCycleControl::PeripheryTAABlendFeather:
		case FoveationCycleControl::NeuralFinalLdrBlendFeather:
			{
				float current = settings.foveatedCenterBlendFeather;
				if (a_control == FoveationCycleControl::PeripheryTAABlendFeather)
					current = settings.periphery_taa_center_blend_feather;
				else if (a_control == FoveationCycleControl::NeuralFinalLdrBlendFeather)
					current = settings.neuralRenderingBlendFeather;
				float selected = 0.0f;
				if (!SelectFloatCycleValue(
						current,
						kBlendFeatherCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				if (a_control == FoveationCycleControl::FovOnlyBlendFeather)
					request.fovOnlyBlendFeather = selected;
				else if (a_control == FoveationCycleControl::PeripheryTAABlendFeather)
					request.peripheryTaaBlendFeather = selected;
				else
					request.neuralFinalLdrBlendFeather = selected;
				recordSelection(
					current, selected,
					static_cast<uint32_t>(kBlendFeatherCycleValues.size()));
				break;
			}
		case FoveationCycleControl::ReconstructionGuardBandPixels:
			{
				uint32_t selected = 0;
				if (!SelectExactCycleValue(
						settings.foveatedReconstructionGuardBandPixels,
						kGuardBandCycleValues,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.reconstructionGuardBandPixels = selected;
				recordSelection(
					settings.foveatedReconstructionGuardBandPixels, selected,
					static_cast<uint32_t>(kGuardBandCycleValues.size()));
				break;
			}
		case FoveationCycleControl::MaskVisualization:
			{
				constexpr std::array values{ false, true };
				bool selected = false;
				if (!SelectExactCycleValue(
						settings.foveatedPeripheryMaskVisualization,
						values,
						a_requestedIndex,
						selected,
						selectedIndex,
						error)) {
					break;
				}
				request.maskVisualization = selected;
				recordSelection(
					settings.foveatedPeripheryMaskVisualization, selected,
					static_cast<uint32_t>(values.size()));
				break;
			}
		default:
			error = {
				{ "error", "control is not a supported foveation cycle axis" },
				{ "errorCode", "foveation_cycle_control_unknown" },
			};
			break;
		}

		if (!error.empty()) {
			error["action"] = "foveation_cycle";
			error["control"] = GetFoveationCycleControlName(a_control);
			error["mutationFrame"] = globals::state ? globals::state->frameCount : 0u;
			error["executionClaimed"] = false;
			return error;
		}
		if (!request.HasAnyControl() || matrixSize == 0) {
			return {
				{ "error", "foveation cycle did not select a value" },
				{ "errorCode", "foveation_cycle_selection_failed" },
				{ "action", "foveation_cycle" },
				{ "control", GetFoveationCycleControlName(a_control) },
				{ "mutationFrame", globals::state ? globals::state->frameCount : 0u },
				{ "executionClaimed", false },
			};
		}

		response["currentIndex"] = selectedIndex;
		response["nextIndex"] = (selectedIndex + 1u) % matrixSize;
		response["matrixSize"] = matrixSize;
		return ApplyFoveationConfiguration(
			a_upscaling,
			request,
			"foveation_cycle",
			false,
			std::move(response));
	}

	json BuildRenderScaleResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "foveation_configure") {
			FoveationConfigurationRequest request;
			json error;
			if (!TryParseFoveationConfiguration(a_args, request, error)) {
				error["action"] = "foveation_configure";
				error["mutationFrame"] = nullptr;
				error["executionClaimed"] = false;
				return error;
			}

			return EnsureFoveationMutationEnvelope(
				RunOnMainThread([request]() {
					if (!globals::game::isVR) {
						return json{
							{ "error", "foveation configuration requires Skyrim VR" },
							{ "errorCode", "unsupported_runtime" },
							{ "action", "foveation_configure" },
							{ "mutationFrame", globals::state ? globals::state->frameCount : 0u },
							{ "executionClaimed", false },
						};
					}
					return ApplyFoveationConfiguration(
						globals::features::upscaling,
						request,
						"foveation_configure",
						true);
				}),
				"foveation_configure");
		}

		if (action == "foveation_cycle") {
			FoveationCycleControl control{};
			std::optional<uint32_t> requestedIndex;
			json error;
			if (!TryParseFoveationCycleRequest(
					a_args, control, requestedIndex, error)) {
				error["action"] = "foveation_cycle";
				error["mutationFrame"] = nullptr;
				error["executionClaimed"] = false;
				return error;
			}

			return EnsureFoveationMutationEnvelope(
				RunOnMainThread([control, requestedIndex]() {
					if (!globals::game::isVR) {
						return json{
							{ "error", "foveation cycling requires Skyrim VR" },
							{ "errorCode", "unsupported_runtime" },
							{ "action", "foveation_cycle" },
							{ "control", GetFoveationCycleControlName(control) },
							{ "mutationFrame", globals::state ? globals::state->frameCount : 0u },
							{ "executionClaimed", false },
						};
					}
					return ApplyFoveationCycle(
						globals::features::upscaling, control, requestedIndex);
				}),
				"foveation_cycle",
				GetFoveationCycleControlName(control));
		}

		if (action == "nr_status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR) {
					return json{
						{ "error", "neural-rendering diagnostics require Skyrim VR" },
						{ "errorCode", "unsupported_runtime" },
					};
				}
				auto& upscaling = globals::features::upscaling;
				return json{
					{ "action", "nr_status" },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
			});
		}

		if (action == "nr_configure") {
			NeuralRenderingConfigurationRequest request;
			json error;
			if (!TryParseNeuralRenderingConfiguration(a_args, request, error))
				return error;

			return RunOnMainThread([request]() {
				if (!globals::game::isVR) {
					return json{
						{ "error", "neural-rendering configuration requires Skyrim VR" },
						{ "errorCode", "unsupported_runtime" },
					};
				}
				auto& upscaling = globals::features::upscaling;
				auto& settings = upscaling.settings;
				const auto previousSettings = settings;
				auto requestedSettings = previousSettings;
				if (request.enabled)
					requestedSettings.neuralRenderingEnabled = *request.enabled;
				if (request.insertionPoint) {
					requestedSettings.neuralRenderingInsertionPoint =
						static_cast<uint32_t>(*request.insertionPoint);
				}
				if (request.preset)
					(void)Upscaling::ApplyNeuralRenderingPreset(requestedSettings, *request.preset);
				if (request.legacyOptimizedStereoPath) {
					requestedSettings.neuralRenderingBatchedStereo =
						*request.legacyOptimizedStereoPath;
					requestedSettings.neuralRenderingDirectCommit =
						*request.legacyOptimizedStereoPath;
				}
				if (request.batchedStereo)
					requestedSettings.neuralRenderingBatchedStereo = *request.batchedStereo;
				if (request.directCommit)
					requestedSettings.neuralRenderingDirectCommit = *request.directCommit;
				if (request.intensity)
					requestedSettings.neuralRenderingIntensity = *request.intensity;
				if (request.localToneStrength)
					requestedSettings.neuralRenderingLocalTone = *request.localToneStrength;
				if (request.localStructureStrength)
					requestedSettings.neuralRenderingLocalStructure = *request.localStructureStrength;
				if (request.skinStructureStrength)
					requestedSettings.neuralRenderingSkinStructure = *request.skinStructureStrength;
				if (request.style)
					requestedSettings.neuralRenderingStyle = *request.style;
				if (request.useAutoMask)
					requestedSettings.neuralRenderingAutoMask = *request.useAutoMask;
				if (request.uiCorrection)
					requestedSettings.neuralRenderingUICorrection = *request.uiCorrection;
				if (request.characterEnabled)
					requestedSettings.neuralCharacterRenderingEnabled = *request.characterEnabled;
				if (request.characterVisualIsolationEnabled) {
					requestedSettings.neuralCharacterVisualIsolationEnabled =
						*request.characterVisualIsolationEnabled;
				}
				if (request.characterFaces)
					requestedSettings.neuralCharacterFacesEnabled = *request.characterFaces;
				if (request.characterSkin)
					requestedSettings.neuralCharacterSkinEnabled = *request.characterSkin;
				if (request.characterHair)
					requestedSettings.neuralCharacterHairEnabled = *request.characterHair;
				if (request.characterFaceStrength)
					requestedSettings.neuralCharacterFaceStrength = *request.characterFaceStrength;
				if (request.characterSkinStrength)
					requestedSettings.neuralCharacterSkinStrength = *request.characterSkinStrength;
				if (request.characterHairStrength)
					requestedSettings.neuralCharacterHairStrength = *request.characterHairStrength;
				if (request.characterMaximumDistanceMeters) {
					requestedSettings.neuralCharacterMaximumDistanceMeters =
						*request.characterMaximumDistanceMeters;
				}
				if (request.characterMinimumFacePixelSize) {
					requestedSettings.neuralCharacterMinimumFacePixelSize =
						*request.characterMinimumFacePixelSize;
				}
				if (request.characterRoiMargin)
					requestedSettings.neuralCharacterRoiMargin = *request.characterRoiMargin;
				if (request.characterMaximumRoiRegions) {
					requestedSettings.neuralCharacterMaxRoiRegions =
						*request.characterMaximumRoiRegions;
				}
				if (request.characterRoiHoldFrames) {
					requestedSettings.neuralCharacterRoiHoldFrames =
						*request.characterRoiHoldFrames;
				}
				if (request.characterDepthAwareFeather) {
					requestedSettings.neuralCharacterDepthAwareFeatherEnabled =
						*request.characterDepthAwareFeather;
				}
				if (request.characterVisibilityDepthTest) {
					requestedSettings.neuralCharacterVisibilityDepthTestEnabled =
						*request.characterVisibilityDepthTest;
				}
				if (request.characterFeatherRadius) {
					requestedSettings.neuralCharacterFeatherRadius =
						*request.characterFeatherRadius;
				}
				if (request.characterFeatherDepthThreshold) {
					requestedSettings.neuralCharacterDepthThreshold =
						*request.characterFeatherDepthThreshold;
				}
				if (request.characterDebugView) {
					requestedSettings.neuralCharacterDebugView =
						static_cast<std::uint32_t>(*request.characterDebugView);
				}
				if (request.characterMaskTestMode) {
					requestedSettings.neuralCharacterMaskTestMode =
						static_cast<std::uint32_t>(*request.characterMaskTestMode);
				}
				if (request.HasImageTuningOverrides())
					requestedSettings.neuralRenderingPreset = 0;

				const bool requiredAutoMask =
					!requestedSettings.neuralCharacterRenderingEnabled;
				if (request.useAutoMask &&
					*request.useAutoMask != requiredAutoMask) {
					return json{
						{ "error", "useAutoMask conflicts with the requested character-mask mode" },
						{ "errorCode", "nr_control_mask_mode_conflict" },
						{ "action", "nr_configure" },
						{ "field", "useAutoMask" },
						{ "requiredValue", requiredAutoMask },
						{ "characterEnabled", requestedSettings.neuralCharacterRenderingEnabled },
					};
				}
				requestedSettings.neuralRenderingAutoMask = requiredAutoMask;

				const bool runtimeSettingsChanged =
					!Upscaling::HasSameNeuralRenderingSettingsKey(
						previousSettings, requestedSettings);

				const bool enableStateChanged =
					previousSettings.neuralRenderingEnabled !=
					requestedSettings.neuralRenderingEnabled;
				const bool insertionPointChanged =
					NeuralRendering::ClampInsertionPoint(
						previousSettings.neuralRenderingInsertionPoint) !=
					NeuralRendering::ClampInsertionPoint(
						requestedSettings.neuralRenderingInsertionPoint);
				const bool stereoSubmissionChanged =
					previousSettings.neuralRenderingBatchedStereo !=
					requestedSettings.neuralRenderingBatchedStereo;
				const bool outputCommitChanged =
					previousSettings.neuralRenderingDirectCommit !=
					requestedSettings.neuralRenderingDirectCommit;
				const bool implementationChanged =
					stereoSubmissionChanged || outputCommitChanged;
				const bool characterMaskTestModeChanged =
					previousSettings.neuralCharacterMaskTestMode !=
					requestedSettings.neuralCharacterMaskTestMode;
				const bool characterDebugViewChanged =
					previousSettings.neuralCharacterDebugView !=
					requestedSettings.neuralCharacterDebugView;
				const bool characterVisualIsolationChanged =
					previousSettings.neuralCharacterVisualIsolationEnabled !=
					requestedSettings.neuralCharacterVisualIsolationEnabled;
				const bool characterSettingsChanged =
					previousSettings.neuralCharacterRenderingEnabled != requestedSettings.neuralCharacterRenderingEnabled ||
					characterVisualIsolationChanged ||
					previousSettings.neuralCharacterFacesEnabled != requestedSettings.neuralCharacterFacesEnabled ||
					previousSettings.neuralCharacterSkinEnabled != requestedSettings.neuralCharacterSkinEnabled ||
					previousSettings.neuralCharacterHairEnabled != requestedSettings.neuralCharacterHairEnabled ||
					previousSettings.neuralCharacterFaceStrength != requestedSettings.neuralCharacterFaceStrength ||
					previousSettings.neuralCharacterSkinStrength != requestedSettings.neuralCharacterSkinStrength ||
					previousSettings.neuralCharacterHairStrength != requestedSettings.neuralCharacterHairStrength ||
					previousSettings.neuralCharacterMaximumDistanceMeters != requestedSettings.neuralCharacterMaximumDistanceMeters ||
					previousSettings.neuralCharacterMinimumFacePixelSize != requestedSettings.neuralCharacterMinimumFacePixelSize ||
					previousSettings.neuralCharacterRoiMargin != requestedSettings.neuralCharacterRoiMargin ||
					previousSettings.neuralCharacterMaxRoiRegions != requestedSettings.neuralCharacterMaxRoiRegions ||
					previousSettings.neuralCharacterRoiHoldFrames != requestedSettings.neuralCharacterRoiHoldFrames ||
					previousSettings.neuralCharacterDepthAwareFeatherEnabled != requestedSettings.neuralCharacterDepthAwareFeatherEnabled ||
					previousSettings.neuralCharacterVisibilityDepthTestEnabled != requestedSettings.neuralCharacterVisibilityDepthTestEnabled ||
					previousSettings.neuralCharacterFeatherRadius != requestedSettings.neuralCharacterFeatherRadius ||
					previousSettings.neuralCharacterDepthThreshold != requestedSettings.neuralCharacterDepthThreshold ||
					characterDebugViewChanged || characterMaskTestModeChanged;
				const bool settingsChanged =
					previousSettings.neuralRenderingEnabled != requestedSettings.neuralRenderingEnabled ||
					previousSettings.neuralRenderingInsertionPoint != requestedSettings.neuralRenderingInsertionPoint ||
					previousSettings.neuralRenderingBatchedStereo != requestedSettings.neuralRenderingBatchedStereo ||
					previousSettings.neuralRenderingDirectCommit != requestedSettings.neuralRenderingDirectCommit ||
					previousSettings.neuralRenderingPreset != requestedSettings.neuralRenderingPreset ||
					previousSettings.neuralRenderingIntensity != requestedSettings.neuralRenderingIntensity ||
					previousSettings.neuralRenderingLocalTone != requestedSettings.neuralRenderingLocalTone ||
					previousSettings.neuralRenderingLocalStructure != requestedSettings.neuralRenderingLocalStructure ||
					previousSettings.neuralRenderingSkinStructure != requestedSettings.neuralRenderingSkinStructure ||
					previousSettings.neuralRenderingStyle != requestedSettings.neuralRenderingStyle ||
					previousSettings.neuralRenderingAutoMask != requestedSettings.neuralRenderingAutoMask ||
					previousSettings.neuralRenderingUICorrection != requestedSettings.neuralRenderingUICorrection ||
					characterSettingsChanged;
				if (!settingsChanged) {
					return json{
						{ "error", "requested Neural Rendering settings already match the active settings" },
						{ "errorCode", "nr_configure_noop" },
						{ "action", "nr_configure" },
						{ "settingsChanged", false },
						{ "runtimeSettingsChanged", false },
						{ "historyResetRequested", false },
						{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
					};
				}
				const bool characterMaskModeChanged =
					previousSettings.neuralCharacterRenderingEnabled !=
					requestedSettings.neuralCharacterRenderingEnabled;
				const char* transition =
					!runtimeSettingsChanged ?
						(characterDebugViewChanged ? "character_debug_view" : "settings_staged") :
					enableStateChanged ?
						(previousSettings.neuralRenderingEnabled ? "disable" : "enable") :
					insertionPointChanged    ? "insertion_point" :
					implementationChanged    ? "implementation" :
					characterSettingsChanged ? "character_rendering" :
											   "tuning";
				const bool resetAttempted =
					runtimeSettingsChanged &&
					(enableStateChanged || insertionPointChanged);

				settings = requestedSettings;
				const bool transitionSucceeded =
					!runtimeSettingsChanged ||
					upscaling.HandleNeuralRenderingSettingsTransition(
						previousSettings,
						"DevBench neural-rendering configuration");
				json response{
					{ "action", "nr_configure" },
					{ "settingsChanged", true },
					{ "runtimeSettingsChanged", runtimeSettingsChanged },
					{ "insertionPointChanged", insertionPointChanged },
					{ "implementationChanged", implementationChanged },
					{ "characterSettingsChanged", characterSettingsChanged },
					{ "characterMaskModeChanged", characterMaskModeChanged },
					{ "characterMaskTestModeChanged", characterMaskTestModeChanged },
					{ "characterDebugViewChanged", characterDebugViewChanged },
					{ "characterVisualIsolationChanged", characterVisualIsolationChanged },
					{ "stereoSubmissionChanged", stereoSubmissionChanged },
					{ "outputCommitChanged", outputCommitChanged },
					{ "transition", transition },
					{ "transitionSucceeded", transitionSucceeded },
					{ "historyResetRequested", runtimeSettingsChanged },
					{ "resetAttempted", resetAttempted },
					{ "resetSucceeded", resetAttempted ? json(transitionSucceeded) : json(nullptr) },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
				if (!transitionSucceeded) {
					response["error"] = "neural-rendering backend reset did not complete";
					response["errorCode"] = "nr_transition_reset_failed";
				}
				return response;
			});
		}

		if (action == "nr_cycle_modes") {
			std::optional<uint32_t> requestedIndex;
			if (const auto matrixIndex = a_args.find("matrixIndex");
				matrixIndex != a_args.end()) {
				uint64_t parsedIndex = 0;
				if (matrixIndex->is_number_unsigned()) {
					parsedIndex = matrixIndex->get<uint64_t>();
				} else if (matrixIndex->is_number_integer()) {
					const auto signedIndex = matrixIndex->get<int64_t>();
					if (signedIndex < 0) {
						return {
							{ "error", "matrixIndex must be an integer in range 0..3" },
							{ "errorCode", "nr_matrix_index_invalid" },
						};
					}
					parsedIndex = static_cast<uint64_t>(signedIndex);
				} else {
					return {
						{ "error", "matrixIndex must be an integer in range 0..3" },
						{ "errorCode", "nr_matrix_index_invalid" },
					};
				}
				if (parsedIndex >= NeuralRendering::kPipelineImplementations.size()) {
					return {
						{ "error", "matrixIndex must be an integer in range 0..3" },
						{ "errorCode", "nr_matrix_index_invalid" },
					};
				}
				requestedIndex = static_cast<uint32_t>(parsedIndex);
			}

			return RunOnMainThread([requestedIndex]() {
				if (!globals::game::isVR) {
					return json{
						{ "error", "neural-rendering mode cycling requires Skyrim VR" },
						{ "errorCode", "unsupported_runtime" },
					};
				}
				auto& upscaling = globals::features::upscaling;
				auto& settings = upscaling.settings;
				const auto previousSettings = settings;
				const uint32_t previousIndex =
					(previousSettings.neuralRenderingBatchedStereo ? 1u : 0u) |
					(previousSettings.neuralRenderingDirectCommit ? 2u : 0u);
				const uint32_t matrixSize = static_cast<uint32_t>(
					NeuralRendering::kPipelineImplementations.size());
				const uint32_t currentIndex = requestedIndex.value_or(
					(previousIndex + 1u) % matrixSize);
				const auto implementation =
					NeuralRendering::kPipelineImplementations[currentIndex];
				auto requestedSettings = previousSettings;
				requestedSettings.neuralRenderingBatchedStereo =
					implementation.batchedStereo;
				requestedSettings.neuralRenderingDirectCommit =
					implementation.directCommit;
				const bool settingsChanged = previousIndex != currentIndex;
				if (settingsChanged)
					settings = requestedSettings;
				const bool transitionSucceeded = !settingsChanged ||
				                                 upscaling.HandleNeuralRenderingSettingsTransition(
													 previousSettings,
													 "DevBench neural-rendering matrix cycle");
				return json{
					{ "action", "nr_cycle_modes" },
					{ "previousIndex", previousIndex },
					{ "currentIndex", currentIndex },
					{ "nextIndex", (currentIndex + 1u) % matrixSize },
					{ "settingsChanged", settingsChanged },
					{ "noOp", !settingsChanged },
					{ "transitionSucceeded", transitionSucceeded },
					{ "historyResetRequested", settingsChanged },
					{ "selectedLane", NeuralImplementationJson(
										  implementation.batchedStereo,
										  implementation.directCommit) },
					{ "executionClaimed", false },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
			});
		}

		if (action == "nr_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR) {
					return json{
						{ "error", "neural-rendering reset requires Skyrim VR" },
						{ "errorCode", "unsupported_runtime" },
					};
				}
				auto& upscaling = globals::features::upscaling;
				const bool resetSucceeded =
					NeuralRendering::Renderer::Instance().Reset();
				NeuralRendering::CharacterRendering::Instance().Reset();
				upscaling.RequestHistoryReset();
				json response{
					{ "action", "nr_reset" },
					{ "resetSucceeded", resetSucceeded },
					{ "characterStateReset", true },
					{ "historyResetRequested", true },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
				if (!resetSucceeded) {
					response["error"] = "neural-rendering backend reset did not complete";
					response["errorCode"] = "nr_reset_failed";
				}
				return response;
			});
		}

		if (action == "status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				return json{ { "action", "status" }, { "status", BuildStatus(globals::features::upscaling) } };
			});
		}

		if (action == "record") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				return json{ { "action", "record" }, { "record", globals::features::upscaling.BuildVRRenderScaleIterationRecord() } };
			});
		}

		if (action == "start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to start a stress capture" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "a stress capture is already active" }, { "status", BuildStatus(upscaling) } };
				upscaling.StartVRRenderScaleStressSession();
				return json{ { "action", "start" }, { "status", BuildStatus(upscaling) } };
			});
		}

		if (action == "stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "no stress capture is active" }, { "status", BuildStatus(upscaling) } };
				upscaling.StopVRRenderScaleStressSession();
				return json{
					{ "action", "stop" },
					{ "record", upscaling.BuildVRRenderScaleIterationRecord() },
				};
			});
		}

		if (action == "reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "stop the active capture before resetting it" }, { "status", BuildStatus(upscaling) } };
				upscaling.ResetVRRenderScaleStressSession();
				return json{ { "action", "reset" }, { "status", BuildStatus(upscaling) } };
			});
		}

		if (action == "probe_start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to start a load presentation probe" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (status.value("active", false))
					return json{ { "error", "a load presentation probe is already active" }, { "status", status } };
				if (!globals::features::vr.InstallSubmitHook(false)) {
					return json{
						{ "error", "OpenVR submit interception is not available; load presentation probe was not started" },
						{ "status", status }
					};
				}
				upscaling.StartVRLoadPresentationProbe();
				return json{ { "action", "probe_start" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "probe_stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (!status.value("active", false))
					return json{ { "error", "no load presentation probe is active" }, { "status", status } };
				upscaling.StopVRLoadPresentationProbe();
				return json{ { "action", "probe_stop" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "probe_record") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				return json{
					{ "action", "probe_record" },
					{ "record", globals::features::upscaling.BuildVRLoadPresentationProbeRecord() },
				};
			});
		}

		if (action == "probe_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (status.value("active", false))
					return json{ { "error", "stop the load presentation probe before resetting it" }, { "status", status } };
				upscaling.ResetVRLoadPresentationProbe();
				return json{ { "action", "probe_reset" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "apply") {
			if (!a_args.contains("method") || !a_args["method"].is_string())
				return { { "error", "apply requires string parameter 'method'" } };
			if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())
				return { { "error", "apply requires boolean parameter 'enabled'" } };
			if (!a_args.contains("qualityMode") || !a_args["qualityMode"].is_number_integer())
				return { { "error", "apply requires integer parameter 'qualityMode'" } };

			const std::string methodName = a_args["method"].get<std::string>();
			Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
			if (methodName == "dlss")
				method = Upscaling::UpscaleMethod::kDLSS;
			else if (methodName == "fsr")
				method = Upscaling::UpscaleMethod::kFSR;
			else
				return { { "error", "method must be 'dlss' or 'fsr'" }, { "method", methodName } };

			const bool enabled = a_args["enabled"].get<bool>();
			const int64_t qualityValue = a_args["qualityMode"].get<int64_t>();
			if (qualityValue < 0 || qualityValue > static_cast<int64_t>(Upscaling::kQualityModeMaxIndex))
				return { { "error", "qualityMode is outside 0..6" }, { "qualityMode", qualityValue } };
			if (enabled && qualityValue == 0)
				return { { "error", "enabled render scale requires qualityMode 1..6" } };

			std::optional<uint32_t> requestedPreset;
			if (a_args.contains("dlssPreset")) {
				if (!a_args["dlssPreset"].is_number_integer())
					return { { "error", "dlssPreset must be an integer" } };
				const int64_t presetValue = a_args["dlssPreset"].get<int64_t>();
				if (presetValue < 0 || presetValue > static_cast<int64_t>(Upscaling::kDLSSPresetMaxIndex))
					return { { "error", "dlssPreset is outside 0..5" }, { "dlssPreset", presetValue } };
				requestedPreset = static_cast<uint32_t>(presetValue);
			}

			return RunOnMainThread([method,
									   methodName,
									   enabled,
									   qualityMode = static_cast<uint32_t>(qualityValue),
									   requestedPreset]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to apply an iteration profile" } };

				auto& upscaling = globals::features::upscaling;
				const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
				if (!session.active)
					return json{ { "error", "start a stress capture before applying an iteration profile" }, { "status", BuildStatus(upscaling) } };

				const auto before = upscaling.GetPendingVRRenderScaleDesiredProfile();
				const uint32_t dlssPreset = requestedPreset.value_or(before.dlssPreset);
				upscaling.ApplyCSMenuUpscalingTransition(
					method,
					enabled,
					qualityMode,
					dlssPreset,
					"devbench render-scale iteration",
					Upscaling::VRUpscalingTransitionOrigin::CSMenu);

				const auto after = upscaling.GetPendingVRRenderScaleDesiredProfile();
				const bool queued =
					after.pending &&
					after.requestID != 0 &&
					after.requestID != before.requestID &&
					after.origin == Upscaling::VRUpscalingTransitionOrigin::CSMenu;
				json response{
					{ "action", "apply" },
					{ "method", methodName },
					{ "enabled", enabled },
					{ "qualityMode", qualityMode },
					{ "dlssPreset", dlssPreset },
					{ "queued", queued },
					{ "requestID", queued ? after.requestID : 0 },
					{ "transitionEpoch", queued ? after.transitionEpoch : 0 },
					{ "status", BuildStatus(upscaling) },
				};
				if (!queued)
					response["note"] = "request was unchanged, applied synchronously, or rejected by transition ownership";
				return response;
			});
		}

		return {
			{ "error", "unknown action" },
			{ "action", action },
			{ "supported", json::array({ "status", "record", "start", "apply", "stop", "reset", "probe_start", "probe_stop", "probe_record", "probe_reset", "nr_status", "nr_configure", "nr_cycle_modes", "nr_reset", "foveation_configure", "foveation_cycle" }) },
		};
	}

	void RenderScaleToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(&BuildRenderScaleResult, a_argsJson, a_sink, a_write);
	}
}

namespace VRRenderScaleDevBenchBridge
{
	void Install()
	{
		g_registered.store(false, std::memory_order_release);
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("VRRenderScaleDevBenchBridge: devbench host not present; iteration tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({
  "description":"Control and inspect Community Shaders VR render-scale stress iterations, DLSS Neural Rendering, character masking, and foveated-center tuning. nr_status returns the API-v9 NR runtime, routes, temporal admission, GPU telemetry, and frame-attributed per-eye character diagnostics. NR runtime admission is independent of Developer Mode and Streamline logging: any 310.8 runtime with the required exports and stable loaded-image identity is accepted, while SHA-256 is informational. Character diagnostics count authored face, skin, and hair pixels before visibility rejection, report visible/rejected pixels and exact frozen/current depth coordinates, and retain frame-keyed preparation history for asynchronous Feature 18 attribution. Expected timing masks expand a one-eye nonempty result to both slots when the stereo evaluator must process the complete pair. Authored masks with measured zero coverage bypass Feature 18 while GPU sampling continues. The authored_without_visibility_depth mode bypasses only core frozen/current visibility rejection; characterVisibilityDepthTest controls that rejection independently of depth-aware edge feathering. The R8_UNORM 0..1 mask representation is an experimental implementation contract, not a provider-declared format or value semantic. Multi/sparse compute ROI is unsupported. ROI rectangles constrain visual masking, not inference dimensions. characterVisualIsolationEnabled=true isolates provider output to the mask's nonzero support without applying category strength twice. Upscaled Center preserves the configured commit lane. The main/non-float Direct route retains one normal-DLSS center copy as its composite baseline; the submit float route already keeps normal DLSS separate. nr_configure strictly accepts one or more NR or character controls through the in-game reset/history contract; an empty, unknown, invalid, conflicting, or unchanged request is rejected. Debug-view-only changes are applied without a history reset. nr_cycle_modes preserves the four-lane stereo implementation cycle. foveation_configure atomically applies one or more validated foveation controls on the main thread. foveation_cycle selects or advances one documented foveation axis. Existing render-scale mutations require Skyrim VR and developer mode; apply additionally requires an active stress capture.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "action":{"type":"string","enum":["status","record","start","apply","stop","reset","probe_start","probe_stop","probe_record","probe_reset","nr_status","nr_configure","nr_cycle_modes","nr_reset","foveation_configure","foveation_cycle"]},
      "method":{"type":"string","enum":["dlss","fsr"]},
      "enabled":{"type":"boolean"},
      "insertionPoint":{"type":"string","enum":["upscaled_center","final_ldr_pre_ui"]},
      "qualityMode":{"type":"integer","minimum":0,"maximum":6},
      "dlssPreset":{"type":"integer","minimum":0,"maximum":5},
      "implementation":{"type":"string","enum":["per_eye_staged_commit","stereo_batched_staged_commit","per_eye_direct_commit","stereo_batched_direct_commit"]},
      "matrixIndex":{"type":"integer","minimum":0,"maximum":3},
      "preset":{"type":"integer","minimum":0,"maximum":4},
      "intensity":{"type":"number","minimum":0,"maximum":2},
      "localToneStrength":{"type":"number","minimum":0,"maximum":2},
      "localStructureStrength":{"type":"number","minimum":0,"maximum":2},
      "skinStructureStrength":{"type":"number","minimum":0,"maximum":2},
      "style":{"type":"integer","minimum":0,"maximum":3},
      "batchedStereo":{"type":"boolean"},
      "directCommit":{"type":"boolean"},
      "optimizedStereoPath":{"type":"boolean"},
      "useAutoMask":{"type":"boolean"},
      "uiCorrection":{"type":"boolean"},
      "characterEnabled":{"type":"boolean"},
      "characterVisualIsolationEnabled":{"type":"boolean"},
      "characterFaces":{"type":"boolean"},
      "characterSkin":{"type":"boolean"},
      "characterHair":{"type":"boolean"},
      "characterFaceStrength":{"type":"number","minimum":0.0,"maximum":1.0},
      "characterSkinStrength":{"type":"number","minimum":0.0,"maximum":1.0},
      "characterHairStrength":{"type":"number","minimum":0.0,"maximum":1.0},
      "characterMaximumDistanceMeters":{"type":"number","minimum":0.5,"maximum":100.0},
      "characterMinimumFacePixelSize":{"type":"integer","minimum":1,"maximum":4096},
      "characterRoiMargin":{"type":"number","minimum":0.0,"maximum":1.0},
      "characterMaximumRoiRegions":{"type":"integer","minimum":1,"maximum":4},
      "characterRoiHoldFrames":{"type":"integer","minimum":0,"maximum":30},
      "characterDepthAwareFeather":{"type":"boolean"},
      "characterVisibilityDepthTest":{"type":"boolean"},
      "characterFeatherRadius":{"type":"integer","minimum":0,"maximum":4},
      "characterFeatherDepthThreshold":{"type":"number","minimum":0.0,"maximum":0.05},
      "characterDebugView":{"type":"string","enum":["off","character_mask","roi_rectangles","dlss5_output"]},
      "characterMaskTestMode":{"type":"string","enum":["authored","force_zero","force_one","force_half","invert_authored","authored_without_visibility_depth"]},
      "foveatedEnabled":{"type":"boolean"},
      "peripheryTaaEnabled":{"type":"boolean"},
      "centerOrigin":{"type":"string","enum":["image_center","optical_center"]},
      "horizontalAnchor":{"type":"string","enum":["symmetric","outward"]},
      "fovOnlyCenterScale":{"type":"number","minimum":0.25,"maximum":1.0},
      "peripheryTaaCenterScale":{"type":"number","minimum":0.25,"maximum":1.0},
      "peripheryTaaOuterScale":{"type":"number","minimum":0.3,"maximum":1.0},
      "centerHorizontalScale":{"type":"number","minimum":1.0,"maximum":2.0},
      "leftEyeOffsetX":{"type":"number","minimum":-0.3,"maximum":0.3},
      "leftEyeOffsetY":{"type":"number","minimum":-0.3,"maximum":0.3},
      "rightEyeOffsetX":{"type":"number","minimum":-0.3,"maximum":0.3},
      "rightEyeOffsetY":{"type":"number","minimum":-0.3,"maximum":0.3},
      "fovOnlyBlendFeather":{"type":"number","minimum":0.0,"maximum":0.1},
      "peripheryTaaBlendFeather":{"type":"number","minimum":0.0,"maximum":0.1},
      "neuralFinalLdrBlendFeather":{"type":"number","minimum":0.0,"maximum":0.1},
      "reconstructionGuardBandPixels":{"type":"integer","minimum":0,"maximum":64},
      "maskVisualization":{"type":"boolean"},
      "control":{"type":"string","enum":["master","periphery_taa","center_origin","horizontal_anchor","fov_only_center_scale","periphery_taa_center_scale","periphery_taa_outer_scale","center_horizontal_scale","left_eye_offset_x","left_eye_offset_y","right_eye_offset_x","right_eye_offset_y","fov_only_blend_feather","periphery_taa_blend_feather","neural_final_ldr_blend_feather","reconstruction_guard_band_pixels","mask_visualization"]},
      "valueIndex":{"type":"integer","minimum":0,"maximum":2}
    },
    "required":["action"],
    "allOf":[
      {"if":{"properties":{"action":{"const":"nr_configure"}},"required":["action"]},"then":{"minProperties":2,"propertyNames":{"enum":["action","enabled","insertionPoint","preset","intensity","localToneStrength","localStructureStrength","skinStructureStrength","style","batchedStereo","directCommit","implementation","optimizedStereoPath","useAutoMask","uiCorrection","characterEnabled","characterVisualIsolationEnabled","characterFaces","characterSkin","characterHair","characterFaceStrength","characterSkinStrength","characterHairStrength","characterMaximumDistanceMeters","characterMinimumFacePixelSize","characterRoiMargin","characterMaximumRoiRegions","characterRoiHoldFrames","characterDepthAwareFeather","characterVisibilityDepthTest","characterFeatherRadius","characterFeatherDepthThreshold","characterDebugView","characterMaskTestMode"]}}},
      {"if":{"properties":{"action":{"const":"foveation_configure"}},"required":["action"]},"then":{"propertyNames":{"enum":["action","foveatedEnabled","peripheryTaaEnabled","centerOrigin","horizontalAnchor","fovOnlyCenterScale","peripheryTaaCenterScale","peripheryTaaOuterScale","centerHorizontalScale","leftEyeOffsetX","leftEyeOffsetY","rightEyeOffsetX","rightEyeOffsetY","fovOnlyBlendFeather","peripheryTaaBlendFeather","neuralFinalLdrBlendFeather","reconstructionGuardBandPixels","maskVisualization"]},"anyOf":[{"required":["foveatedEnabled"]},{"required":["peripheryTaaEnabled"]},{"required":["centerOrigin"]},{"required":["horizontalAnchor"]},{"required":["fovOnlyCenterScale"]},{"required":["peripheryTaaCenterScale"]},{"required":["peripheryTaaOuterScale"]},{"required":["centerHorizontalScale"]},{"required":["leftEyeOffsetX"]},{"required":["leftEyeOffsetY"]},{"required":["rightEyeOffsetX"]},{"required":["rightEyeOffsetY"]},{"required":["fovOnlyBlendFeather"]},{"required":["peripheryTaaBlendFeather"]},{"required":["neuralFinalLdrBlendFeather"]},{"required":["reconstructionGuardBandPixels"]},{"required":["maskVisualization"]}]}},
      {"if":{"properties":{"action":{"const":"foveation_cycle"}},"required":["action"]},"then":{"required":["control"],"propertyNames":{"enum":["action","control","valueIndex"]}}},
      {"if":{"properties":{"action":{"const":"foveation_cycle"},"control":{"enum":["master","periphery_taa","center_origin","horizontal_anchor","center_horizontal_scale","mask_visualization"]}},"required":["action","control"]},"then":{"properties":{"valueIndex":{"maximum":1}}}}
    ]
  }
})";
		devBench->RegisterTool(
			"communityshaders.renderscale",
			descriptor,
			&RenderScaleToolHandler,
			nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"VRRenderScaleDevBenchBridge: registered communityshaders.renderscale with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt()
	{
		return true;
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace VRRenderScaleDevBenchBridge
{
	void Install() {}

	bool IsBuilt()
	{
		return false;
	}

	bool IsRegistered()
	{
		return false;
	}
}

#endif
