#pragma once

#include "../DLSSViewportCrop.h"
#include "CharacterMultiRoi.h"
#include "CharacterRegionPolicy.h"
#include "CharacterSettings.h"
#include "ComputeSubrect.h"

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace NeuralRendering
{
	struct CharacterActorBound
	{
		float centerX = 0.0f;
		float centerY = 0.0f;
		float centerZ = 0.0f;
		float radius = 0.0f;
	};

	/** Stable actor/head bounds make admission independent of material draw order. */
	struct CharacterActorAdmissionArgs
	{
		std::uintptr_t actorIdentity = 0;
		CharacterActorBound faceBound{};
		CharacterActorBound actorBound{};
		std::uint32_t outputWidthPerEye = 0;
		std::uint32_t outputHeight = 0;
		CharacterSettings settings{};
	};

	struct CharacterEyeSnapshot
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t sourceWorldFrame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t contentSerial = 0;
		std::uint32_t featureSlot = 0;
		std::uint32_t evaluationWidth = 0;
		std::uint32_t evaluationHeight = 0;
		std::uint32_t visibleFaces = 0;
		std::uint32_t visibleCharacterRegions = 0;
		std::uint32_t selectedCharacterRegions = 0;
		std::uint32_t adaptivelyCulledCharacterRegions = 0;
		std::uint32_t mergedRegions = 0;
		std::uint64_t roiPixels = 0;
		ComputeSubrect computeSubrect{};
		CharacterComputeRegionPlan computeRegions{};
		std::uint64_t multiRoiPixels = 0;
		CharacterMultiRoiReason multiRoiReason = CharacterMultiRoiReason::Disabled;
		std::uint64_t computeSubrectPixels = 0;
		float computeSubrectCoveragePercent = 0.0f;
		std::uint64_t maskPixels = 0;
		std::array<std::uint64_t, 3> authoredCategoryPixels{};
		std::array<std::uint64_t, 3> visibleCategoryPixels{};
		std::uint64_t visibilityRejectedPixels = 0;
		std::uint64_t distanceRejectedPixels = 0;
		float roiCoveragePercent = 0.0f;
		float maskCoveragePercent = 0.0f;
		std::uint32_t maskCoverageFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t maskCoverageFeatureSlot = 4;
		std::uint32_t maskCoverageWidth = 0;
		std::uint32_t maskCoverageHeight = 0;
		std::uint64_t maskCoverageContentSerial = 0;
		bool maskCoverageReady = false;
		bool maskCoverageMatchesCurrentPolicy = false;
		bool zeroCoverageBypassRequested = false;
		bool zeroCoverageBypassResolved = false;
		bool zeroCoverageBypassed = false;
		CharacterFeature18Disposition feature18Disposition =
			CharacterFeature18Disposition::Unresolved;
		bool feature18EvaluationSucceeded = false;
		bool zeroCoverageCpuProven = false;
		bool fullEyeEligibilityFallback = false;
		bool depthCoordinatesValid = false;
		std::uint32_t authoredStereoWidth = 0;
		std::uint32_t authoredDepthHeight = 0;
		std::uint32_t authoredEyeBaseX = 0;
		std::uint32_t currentDepthWidth = 0;
		std::uint32_t currentDepthHeight = 0;
		std::uint32_t inputCropLeft = 0;
		std::uint32_t inputCropTop = 0;
		std::uint32_t inputCropWidth = 0;
		std::uint32_t inputCropHeight = 0;
		std::uint32_t outputCropLeft = 0;
		std::uint32_t outputCropTop = 0;
		std::uint32_t outputCropWidth = 0;
		std::uint32_t outputCropHeight = 0;
		float capturedJitterX = 0.0f;
		float capturedJitterY = 0.0f;
		bool maskPrepared = false;
		bool evaluationRequired = false;
		std::vector<CharacterRect> regions;
	};

	/** Frame-keyed mask preparation retained for asynchronous GPU attribution. */
	struct CharacterPreparedFrameSnapshot
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t preparedSlotMask = 0;
		std::uint32_t evaluationRequiredSlotMask = 0;
		std::uint32_t bypassRequestedSlotMask = 0;
		std::uint32_t resolutionRecordedSlotMask = 0;
		std::uint32_t evaluatedSlotMask = 0;
		std::uint32_t successfulSlotMask = 0;
		std::uint32_t bypassedSlotMask = 0;
		std::uint32_t abortedSlotMask = 0;
		std::array<std::uint32_t, 4> sourceWorldFrames{
			std::numeric_limits<std::uint32_t>::max(),
			std::numeric_limits<std::uint32_t>::max(),
			std::numeric_limits<std::uint32_t>::max(),
			std::numeric_limits<std::uint32_t>::max(),
		};
		std::array<std::uint64_t, 4> generations{};
		std::array<std::uint64_t, 4> contentSerials{};
		std::array<std::uint32_t, 4> widths{};
		std::array<std::uint32_t, 4> heights{};
		/** Expected physical evaluations: zero for bypass, one legacy, two split. */
		std::array<std::uint32_t, 4> computeRegionCounts{};
	};

	struct CharacterSnapshot
	{
		std::string status = "idle";
		std::string detail;
		std::string visualMaskMechanism = "csx_output_composite_r8";
		std::string computeRoiReason =
			"One per-eye enclosing compute rectangle by default; opt-in experimental multi-ROI evaluates up to two disjoint character clusters independently";
		bool enabled = false;
		bool visualMaskImplemented = true;
		bool visualMaskProviderValidated = false;
		bool computeRoiSupported = true;
		std::uint64_t observations = 0;
		std::uint64_t observationCapacityDrops = 0;
		std::uint32_t observationFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t currentObservations = 0;
		std::array<std::uint32_t, 3> currentCategoryObservations{};
		std::array<
			std::uint32_t,
			static_cast<std::size_t>(CharacterClassificationRejection::Count)>
			currentClassificationRejections{};
		std::array<
			std::uint64_t,
			static_cast<std::size_t>(CharacterClassificationRejection::Count)>
			classificationRejections{};
		std::uint64_t categoryCaptureAttempts = 0;
		std::uint64_t categoryCaptureSuccesses = 0;
		std::uint64_t categoryCaptureFailures = 0;
		std::uint64_t categoryCaptureEmptyBypasses = 0;
		std::uint64_t categoryCaptureReuses = 0;
		std::uint32_t categoryCaptureFrame =
			std::numeric_limits<std::uint32_t>::max();
		bool categoryCaptureReady = false;
		bool categoryCaptureEmpty = false;
		std::uint64_t preparationAttempts = 0;
		std::uint64_t preparationSuccesses = 0;
		std::uint64_t preparationFailures = 0;
		std::uint64_t readbackDrops = 0;
		std::uint64_t provenEmptyFeatureBypassRequests = 0;
		std::uint64_t provenEmptyFeatureBypasses = 0;
		std::array<CharacterEyeSnapshot, 2> eyes{};
		std::array<
			CharacterPreparedFrameSnapshot,
			CharacterPolicy::kPreparedFrameHistorySize>
			preparedFrames{};
	};

	struct CharacterMaskPrepareArgs
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		ID3D11ShaderResourceView* depthGuide = nullptr;
		std::uint32_t eyeIndex = 0;
		std::uint32_t featureSlot = 0;
		/** Feature 18 evaluation frame; remains monotonic during retained-menu reuse. */
		std::uint32_t frameId = std::numeric_limits<std::uint32_t>::max();
		/** Correlated world/capture frame used to build or reuse the authored mask. */
		std::uint32_t sourceWorldFrame = std::numeric_limits<std::uint32_t>::max();
		/** Resource-contract generation that owns the prepared mask. */
		std::uint64_t generation = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		UpscalingDLSS::ViewportCrop viewportCrop{};
		CharacterSettings settings{};
	};

	struct CharacterMaskPrepareResult
	{
		bool prepared = false;
		bool requiresEvaluation = true;
		/** Output-local rectangle supplied to the private Feature 18 subrect ABI. */
		ComputeSubrect computeSubrect{};
		CharacterComputeRegionPlan computeRegions{};
	};

	/** Owns character observations, stable per-eye regions, and R8 selection masks. */
	class CharacterRendering
	{
	public:
		static CharacterRendering& Instance();

		CharacterRendering(const CharacterRendering&) = delete;
		CharacterRendering& operator=(const CharacterRendering&) = delete;

		/** Returns the same admission for every material of this actor/world frame. */
		[[nodiscard]] bool ShouldAuthorActor(
			std::uint32_t a_frame,
			std::uint32_t a_actorFormId,
			const CharacterActorAdmissionArgs& a_args) noexcept;

		/** Records one actor-owned material bound; true permits semantic-ID output. */
		[[nodiscard]] bool ObserveGeometry(
			std::uint32_t a_frame,
			std::uint32_t a_actorFormId,
			std::uintptr_t a_geometryIdentity,
			CharacterCategory a_category,
			float a_centerX,
			float a_centerY,
			float a_centerZ,
			float a_radius) noexcept;
		/** Records why actor-owned geometry was deliberately excluded. */
		void ObserveClassificationRejection(
			std::uint32_t a_frame,
			CharacterClassificationRejection a_reason) noexcept;
		/** Freezes post-terrain IDs with synchronized pre-decal scene depth. */
		bool CaptureAuthoredCategories(
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			ID3D11Texture2D* a_categorySource,
			ID3D11ShaderResourceView* a_depthSource,
			std::uint32_t a_sourceEyeWidth,
			std::uint32_t a_sourceHeight,
			std::uint32_t a_frame,
			std::uint32_t a_enabledCategoryMask,
			float a_jitterX,
			float a_jitterY) noexcept;

		/** Builds an exact per-eye mask. Failure leaves Feature 18 fail-closed. */
		bool PrepareMask(
			const CharacterMaskPrepareArgs& a_args,
			CharacterMaskPrepareResult& a_result) noexcept;
		/** Records the authoritative outcome for prepared Feature 18 slots. */
		void ResolveFeature18Disposition(
			std::uint32_t a_frameId,
			std::uint32_t a_sourceWorldFrame,
			std::uint64_t a_generation,
			std::uint32_t a_preparedFeatureSlotMask,
			std::uint32_t a_evaluatedFeatureSlotMask,
			std::uint32_t a_successfulFeatureSlotMask,
			std::uint32_t a_bypassedFeatureSlotMask) noexcept;
		/** Invalidates observations, resources, compile state, and cached masks. */
		void Reset() noexcept;
		/** Invalidates region policy and cached contents without releasing resources. */
		void Invalidate() noexcept;
		/** Drops only the runtime-compiled extraction shader. */
		void ResetShaderCache() noexcept;

		[[nodiscard]] CharacterSnapshot GetSnapshot() const;
		[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
		GetDebugMaskSrv(
			std::uint32_t a_eyeIndex) const noexcept;
		/** Returns a mask only for the exact evaluation/source/generation and size. */
		[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
		GetPreparedMaskSrv(
			std::uint32_t a_featureSlot,
			std::uint32_t a_frameId,
			std::uint32_t a_sourceWorldFrame,
			std::uint64_t a_generation,
			std::uint32_t a_width,
			std::uint32_t a_height) const noexcept;
		/** Returns the matching output-local compute rectangle for a prepared mask. */
		[[nodiscard]] ComputeSubrect GetPreparedComputeSubrect(
			std::uint32_t a_featureSlot,
			std::uint32_t a_frameId,
			std::uint32_t a_sourceWorldFrame,
			std::uint64_t a_generation,
			std::uint32_t a_width,
			std::uint32_t a_height) const noexcept;
		/** Returns the exact split plan belonging to the validated prepared mask. */
		[[nodiscard]] CharacterComputeRegionPlan GetPreparedComputeRegions(
			std::uint32_t a_featureSlot,
			std::uint32_t a_frameId,
			std::uint32_t a_sourceWorldFrame,
			std::uint64_t a_generation,
			std::uint32_t a_width,
			std::uint32_t a_height) const noexcept;
		/** Current nonzero support for an already validated mask; unknown views fail open. */
		[[nodiscard]] ComputeSubrect GetMaskSupportRect(
			ID3D11ShaderResourceView* a_mask,
			std::uint32_t a_width, std::uint32_t a_height) const noexcept;

	private:
		class State;

		CharacterRendering();
		~CharacterRendering();

		std::unique_ptr<State> state_;
	};

	[[nodiscard]] constexpr const char* GetCharacterFeature18DispositionName(
		CharacterFeature18Disposition a_disposition) noexcept
	{
		switch (a_disposition) {
		case CharacterFeature18Disposition::Unresolved:
			return "unresolved";
		case CharacterFeature18Disposition::Evaluated:
			return "evaluated";
		case CharacterFeature18Disposition::EvaluationFailed:
			return "evaluation_failed";
		case CharacterFeature18Disposition::EmptyBypass:
			return "empty_bypass";
		case CharacterFeature18Disposition::Aborted:
			return "aborted";
		}
		return "unknown";
	}

	}
