#pragma once

#include <array>
#include <cstdint>

namespace DLSSFrameEvaluationPolicy
{
	struct CanonicalConstants
	{
		std::array<uint32_t, 16> cameraViewToClip{};
		std::array<uint32_t, 16> clipToCameraView{};
		std::array<uint32_t, 16> clipToLensClip{};
		std::array<uint32_t, 16> clipToPrevClip{};
		std::array<uint32_t, 16> prevClipToClip{};
		std::array<uint32_t, 2> jitterOffset{};
		std::array<uint32_t, 2> motionVectorScale{};
		std::array<uint32_t, 2> cameraPinholeOffset{};
		std::array<uint32_t, 3> cameraPosition{};
		std::array<uint32_t, 3> cameraUp{};
		std::array<uint32_t, 3> cameraRight{};
		std::array<uint32_t, 3> cameraForward{};
		uint32_t cameraNear = 0;
		uint32_t cameraFar = 0;
		uint32_t cameraFOV = 0;
		uint32_t cameraAspectRatio = 0;
		uint32_t motionVectorsInvalidValue = 0;
		uint32_t minRelativeLinearDepthObjectSeparation = 0;
		uint8_t depthInverted = 0;
		uint8_t cameraMotionIncluded = 0;
		uint8_t motionVectors3D = 0;
		uint8_t reset = 0;
		uint8_t orthographicProjection = 0;
		uint8_t motionVectorsDilated = 0;
		uint8_t motionVectorsJittered = 0;

		[[nodiscard]] constexpr bool operator==(const CanonicalConstants&) const noexcept = default;
	};

	struct ResourceIdentities
	{
		std::uintptr_t colorInput = 0;
		std::uintptr_t colorOutput = 0;
		std::uintptr_t depth = 0;
		std::uintptr_t motionVectors = 0;
		std::uintptr_t reactiveMask = 0;
		std::uintptr_t transparencyMask = 0;

		[[nodiscard]] constexpr bool operator==(const ResourceIdentities&) const noexcept = default;
	};

	struct EvaluationContract
	{
		uint64_t sessionEpoch = 0;
		uint32_t frame = 0;
		uint32_t frameToken = 0;
		uint32_t viewport = UINT32_MAX;
		uint32_t eyeIndex = 0;
		uint32_t viewportRole = 0;
		uint64_t renderedInputGeneration = 0;
		uint64_t resourceGeneration = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint32_t extentInLeft = 0;
		uint32_t extentInTop = 0;
		uint32_t extentInWidth = 0;
		uint32_t extentInHeight = 0;
		uint32_t extentOutLeft = 0;
		uint32_t extentOutTop = 0;
		uint32_t extentOutWidth = 0;
		uint32_t extentOutHeight = 0;
		bool colorBuffersHDR = false;
		ResourceIdentities resources{};
		CanonicalConstants constants{};

		[[nodiscard]] constexpr bool operator==(const EvaluationContract&) const noexcept = default;
	};

	struct TicketState
	{
		bool valid = false;
		bool constantsAttempted = false;
		bool constantsSubmitted = false;
		bool evaluationAttempted = false;
		bool evaluationCompleted = false;
		bool evaluationSucceeded = false;
		bool outputReusable = false;
		EvaluationContract contract{};
	};

	enum class Admission : uint8_t
	{
		Submit,
		ReuseCompletedOutput,
		RejectIncompleteAttempt,
		RejectTemporalConflict,
		RejectOutputUnavailable
	};

	/** Identifies the Streamline temporal slot on which only one evaluation is legal. */
	[[nodiscard]] constexpr bool IsSameTemporalTuple(
		const EvaluationContract& a_left,
		const EvaluationContract& a_right) noexcept
	{
		return a_left.sessionEpoch == a_right.sessionEpoch &&
		       a_left.frameToken == a_right.frameToken &&
		       a_left.viewport == a_right.viewport;
		}

	/** Resolves a repeated request without permitting a second Streamline call. */
	[[nodiscard]] constexpr Admission ResolveAdmission(
		const TicketState* a_existing,
		const EvaluationContract& a_requested,
		bool a_outputStillOwned,
		bool a_outputReuseAllowed) noexcept
	{
		if (!a_existing || !a_existing->valid ||
			!IsSameTemporalTuple(a_existing->contract, a_requested)) {
			return Admission::Submit;
		}

		if (!(a_existing->contract == a_requested))
			return Admission::RejectTemporalConflict;

		if (!a_existing->constantsAttempted || !a_existing->constantsSubmitted ||
			!a_existing->evaluationAttempted || !a_existing->evaluationCompleted) {
			return Admission::RejectIncompleteAttempt;
		}
		if (!a_existing->evaluationSucceeded || !a_existing->outputReusable ||
			!a_outputStillOwned || !a_outputReuseAllowed) {
			return Admission::RejectOutputUnavailable;
		}

		return Admission::ReuseCompletedOutput;
	}
}
