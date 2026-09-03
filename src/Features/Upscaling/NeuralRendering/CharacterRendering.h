#pragma once

#include "../DLSSViewportCrop.h"
#include "CharacterRegionPolicy.h"

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
	/** Semantic character categories authored by Skyrim lighting materials. */
	enum class CharacterCategory : std::uint32_t
	{
		None = 0,
		Face = 1,
		Skin = 2,
		Hair = 3,
	};

	/** Non-destructive developer visualization shown in the CS diagnostics UI. */
	enum class CharacterDebugView : std::uint32_t
	{
		Off = 0,
		CharacterMask = 1,
		RoiRectangles = 2,
		Dlss5Output = 3,
		Count,
	};

	/** Explicit experiments for validating the unpublished mask value contract. */
	enum class CharacterMaskTestMode : std::uint32_t
	{
		Authored = 0,
		ForceZero = 1,
		ForceOne = 2,
		ForceHalf = 3,
		InvertAuthored = 4,
		AuthoredWithoutVisibilityDepth = 5,
		Count,
	};

	/** Authoritative outcome after the Feature 18 execution boundary. */
	enum class CharacterFeature18Disposition : std::uint32_t
	{
		Unresolved = 0,
		Evaluated = 1,
		EvaluationFailed = 2,
		EmptyBypass = 3,
		Aborted = 4,
	};

	/** Fail-closed reasons observed while classifying actor-owned geometry. */
	enum class CharacterClassificationRejection : std::uint32_t
	{
		Player = 0,
		BlendedMaterial = 1,
		AlphaTestAndBlend = 2,
		AmbiguousFaceGen = 3,
		UnsupportedMaterial = 4,
		Count,
	};

	namespace CharacterPolicy
	{
		inline constexpr bool kDefaultEnabled = false;
		inline constexpr bool kDefaultVisualIsolation = true;
		inline constexpr bool kDefaultFaces = true;
		inline constexpr bool kDefaultSkin = true;
		inline constexpr bool kDefaultHair = false;
		inline constexpr float kDefaultFaceStrength = 1.0f;
		inline constexpr float kDefaultSkinStrength = 1.0f;
		inline constexpr float kDefaultHairStrength = 0.65f;
		inline constexpr float kDefaultMaximumDistanceMeters = 10.0f;
		inline constexpr std::uint32_t kDefaultMinimumFacePixelSize = 64;
		inline constexpr float kDefaultRoiMargin = 0.25f;
		inline constexpr std::uint32_t kDefaultMaximumRoiRegions = 4;
		inline constexpr std::uint32_t kDefaultRoiHoldFrames = 3;
		inline constexpr bool kDefaultDepthAwareFeather = false;
		inline constexpr bool kDefaultVisibilityDepthTest = true;
		inline constexpr std::uint32_t kDefaultFeatherRadius = 1;
		inline constexpr float kDefaultFeatherDepthThreshold = 0.002f;

		inline constexpr float kMinimumStrength = 0.0f;
		inline constexpr float kMaximumStrength = 1.0f;
		inline constexpr float kMinimumDistanceMeters = 0.5f;
		inline constexpr float kMaximumDistanceMeters = 100.0f;
		inline constexpr std::uint32_t kMinimumFacePixelSize = 1;
		inline constexpr std::uint32_t kMaximumFacePixelSize = 4096;
		inline constexpr float kMinimumRoiMargin = 0.0f;
		inline constexpr float kMaximumRoiMargin = 1.0f;
		inline constexpr std::uint32_t kMaximumRoiRegions = 4;
		inline constexpr std::uint32_t kMaximumRoiHoldFrames = 30;
		inline constexpr std::uint32_t kMaximumFeatherRadius = 4;
		inline constexpr float kMaximumFeatherDepthThreshold = 0.05f;
		inline constexpr std::uint32_t kMaximumObservationsPerFrame = 4096;
		inline constexpr std::uint32_t kMaximumTrackedActorsPerEye = 128;
		inline constexpr std::uint32_t kCoverageSampleIntervalFrames = 30;
		inline constexpr std::uint32_t kZeroCoverageReuseFrames = 4;
		inline constexpr std::size_t kPreparedFrameHistorySize = 8;

		[[nodiscard]] constexpr std::uint32_t CategoryBit(
			CharacterCategory a_category) noexcept
		{
			return a_category == CharacterCategory::None ?
			           0u :
			           1u << static_cast<std::uint32_t>(a_category);
		}
	}

	struct CharacterSettings
	{
		bool enabled = CharacterPolicy::kDefaultEnabled;
		bool faces = CharacterPolicy::kDefaultFaces;
		bool skin = CharacterPolicy::kDefaultSkin;
		bool hair = CharacterPolicy::kDefaultHair;
		float faceStrength = CharacterPolicy::kDefaultFaceStrength;
		float skinStrength = CharacterPolicy::kDefaultSkinStrength;
		float hairStrength = CharacterPolicy::kDefaultHairStrength;
		float maximumDistanceMeters =
			CharacterPolicy::kDefaultMaximumDistanceMeters;
		std::uint32_t minimumFacePixelSize =
			CharacterPolicy::kDefaultMinimumFacePixelSize;
		float roiMargin = CharacterPolicy::kDefaultRoiMargin;
		std::uint32_t maximumRoiRegions =
			CharacterPolicy::kDefaultMaximumRoiRegions;
		std::uint32_t roiHoldFrames = CharacterPolicy::kDefaultRoiHoldFrames;
		bool depthAwareFeather = CharacterPolicy::kDefaultDepthAwareFeather;
		bool visibilityDepthTest = CharacterPolicy::kDefaultVisibilityDepthTest;
		std::uint32_t featherRadius = CharacterPolicy::kDefaultFeatherRadius;
		float featherDepthThreshold =
			CharacterPolicy::kDefaultFeatherDepthThreshold;
		CharacterDebugView debugView = CharacterDebugView::Off;
		CharacterMaskTestMode maskTestMode = CharacterMaskTestMode::Authored;
	};

	struct CharacterEyeSnapshot
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t featureSlot = 0;
		std::uint32_t evaluationWidth = 0;
		std::uint32_t evaluationHeight = 0;
		std::uint32_t visibleFaces = 0;
		std::uint32_t visibleCharacterRegions = 0;
		std::uint32_t droppedCharacterRegions = 0;
		std::uint32_t mergedRegions = 0;
		std::uint64_t roiPixels = 0;
		std::uint64_t maskPixels = 0;
		std::array<std::uint64_t, 3> authoredCategoryPixels{};
		std::array<std::uint64_t, 3> visibleCategoryPixels{};
		std::uint64_t visibilityRejectedPixels = 0;
		float roiCoveragePercent = 0.0f;
		float maskCoveragePercent = 0.0f;
		std::uint32_t maskCoverageFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t maskCoverageFeatureSlot = 4;
		std::uint32_t maskCoverageWidth = 0;
		std::uint32_t maskCoverageHeight = 0;
		bool maskCoverageReady = false;
		bool maskCoverageMatchesCurrentPolicy = false;
		bool zeroCoverageBypassRequested = false;
		bool zeroCoverageBypassResolved = false;
		bool zeroCoverageBypassed = false;
		CharacterFeature18Disposition feature18Disposition =
			CharacterFeature18Disposition::Unresolved;
		bool feature18EvaluationSucceeded = false;
		bool zeroCoverageCpuProven = false;
		bool zeroCoverageSampleReused = false;
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
		std::array<std::uint32_t, 4> widths{};
		std::array<std::uint32_t, 4> heights{};
	};

	struct CharacterSnapshot
	{
		std::string status = "idle";
		std::string detail;
		std::string visualMaskMechanism = "csx_output_composite_r8";
		std::string computeRoiReason =
			"Multi/sparse ROI is unavailable; private single-subrect timing is not enabled";
		bool enabled = false;
		bool visualMaskImplemented = true;
		bool visualMaskProviderValidated = false;
		bool computeRoiSupported = false;
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
		std::uint64_t measuredZeroCoverageBypassRequests = 0;
		std::uint64_t provenEmptyFeatureBypasses = 0;
		std::uint64_t measuredZeroCoverageBypasses = 0;
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
		std::uint32_t frameId = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		UpscalingDLSS::ViewportCrop viewportCrop{};
		CharacterSettings settings{};
	};

	struct CharacterMaskPrepareResult
	{
		bool prepared = false;
		bool requiresEvaluation = true;
	};

	/** Owns character observations, stable per-eye regions, and R8 selection masks. */
	class CharacterRendering
	{
	public:
		static CharacterRendering& Instance();

		CharacterRendering(const CharacterRendering&) = delete;
		CharacterRendering& operator=(const CharacterRendering&) = delete;

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
		/** Returns a mask only when the requested slot matches this exact frame and size. */
		[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
		GetPreparedMaskSrv(
			std::uint32_t a_featureSlot,
			std::uint32_t a_frameId,
			std::uint32_t a_width,
			std::uint32_t a_height) const noexcept;

	private:
		class State;

		CharacterRendering();
		~CharacterRendering();

		std::unique_ptr<State> state_;
	};

	[[nodiscard]] constexpr CharacterDebugView ClampCharacterDebugView(
		std::uint32_t a_value) noexcept
	{
		const auto value = static_cast<CharacterDebugView>(a_value);
		return value < CharacterDebugView::Count ? value : CharacterDebugView::Off;
	}

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

	[[nodiscard]] constexpr CharacterMaskTestMode ClampCharacterMaskTestMode(
		std::uint32_t a_value) noexcept
	{
		const auto value = static_cast<CharacterMaskTestMode>(a_value);
		return value < CharacterMaskTestMode::Count ?
		           value :
		           CharacterMaskTestMode::Authored;
	}
}
