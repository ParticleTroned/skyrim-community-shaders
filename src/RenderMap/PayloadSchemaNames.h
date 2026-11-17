#pragma once

#include <array>

namespace CSX::RenderMap::PayloadSchemaNames
{
	// This catalogue describes current serializer outputs, including both geometry variants.
	inline constexpr char kShaderObservationV2[] = "shader-observation-v2";
	inline constexpr char kStageShaderObservationV3[] = "stage-shader-observation-v3";
	inline constexpr char kSceneObjectObservationV1[] = "scene-object-observation-v1";
	inline constexpr char kGeometryObservationV1[] = "geometry-observation-v1";
	inline constexpr char kMaterialStateObservationV1[] = "material-state-observation-v1";
	inline constexpr char kRenderTargetBindingV2[] = "render-target-binding-v2";
	inline constexpr char kRenderPassBoundaryV1[] = "render-pass-boundary-v1";
	inline constexpr char kTechniqueBoundaryV2[] = "technique-boundary-v2";
	inline constexpr char kGeometryBoundaryV1[] = "geometry-boundary-v1";
	inline constexpr char kGeometryBoundaryV2[] = "geometry-boundary-v2";
	inline constexpr char kTechniqueResolutionV1[] = "technique-resolution-v1";
	inline constexpr char kDrawCallV4[] = "draw-call-v4";
	inline constexpr char kDispatchCallV2[] = "dispatch-call-v2";
	inline constexpr char kDeviceContextObservationV2[] = "device-context-observation-v2";
	inline constexpr char kCommandRecordingObservationV1[] = "command-recording-observation-v1";
	inline constexpr char kCommandListObservationV2[] = "command-list-observation-v2";
	inline constexpr char kFinishCommandListV2[] = "finish-command-list-v2";
	inline constexpr char kExecuteCommandListV1[] = "execute-command-list-v1";
	inline constexpr char kTargetViewObservationV1[] = "target-view-observation-v1";
	inline constexpr char kResourceObservationV1[] = "resource-observation-v1";
	inline constexpr char kResourceViewBindingV2[] = "resource-view-binding-v2";
	inline constexpr char kResourceViewStateObservedV1[] = "resource-view-state-observed-v1";
	inline constexpr char kResourceFlowV1[] = "resource-flow-v1";
	inline constexpr char kResourceCpuAccessV1[] = "resource-cpu-access-v1";
	inline constexpr char kResourceVersionObservationV1[] = "resource-version-observation-v1";
	inline constexpr char kVisibilityCandidateV1[] = "visibility-candidate-v1";
	inline constexpr char kVisibilityResultReadyV1[] = "visibility-result-ready-v1";
	inline constexpr char kVisibilitySubmissionV1[] = "visibility-submission-v1";
	inline constexpr char kEyeSubmissionV1[] = "eye-submission-v1";
	inline constexpr char kCullDecisionV1[] = "cull-decision-v1";

	inline constexpr auto kAll = std::to_array<const char*>({
		kShaderObservationV2,
		kStageShaderObservationV3,
		kSceneObjectObservationV1,
		kGeometryObservationV1,
		kMaterialStateObservationV1,
		kRenderTargetBindingV2,
		kRenderPassBoundaryV1,
		kTechniqueBoundaryV2,
		kGeometryBoundaryV1,
		kGeometryBoundaryV2,
		kTechniqueResolutionV1,
		kDrawCallV4,
		kDispatchCallV2,
		kDeviceContextObservationV2,
		kCommandRecordingObservationV1,
		kCommandListObservationV2,
		kFinishCommandListV2,
		kExecuteCommandListV1,
		kTargetViewObservationV1,
		kResourceObservationV1,
		kResourceViewBindingV2,
		kResourceViewStateObservedV1,
		kResourceFlowV1,
		kResourceCpuAccessV1,
		kResourceVersionObservationV1,
		kVisibilityCandidateV1,
		kVisibilityResultReadyV1,
		kVisibilitySubmissionV1,
		kEyeSubmissionV1,
		kCullDecisionV1,
	});
}
