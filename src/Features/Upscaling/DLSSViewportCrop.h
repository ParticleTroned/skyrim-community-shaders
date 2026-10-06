#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace UpscalingDLSS
{
	struct Extent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return width != 0 && height != 0;
		}

		friend constexpr bool operator==(const Extent&, const Extent&) noexcept = default;
	};

	struct Rect
	{
		std::uint32_t left = 0;
		std::uint32_t top = 0;
		std::uint32_t right = 0;
		std::uint32_t bottom = 0;

		[[nodiscard]] constexpr std::uint32_t Width() const noexcept
		{
			return right > left ? right - left : 0;
		}

		[[nodiscard]] constexpr std::uint32_t Height() const noexcept
		{
			return bottom > top ? bottom - top : 0;
		}

		[[nodiscard]] constexpr bool IsValidFor(const Extent& a_extent) const noexcept
		{
			return a_extent.IsValid() && Width() != 0 && Height() != 0 &&
			       right <= a_extent.width && bottom <= a_extent.height;
		}

		friend constexpr bool operator==(const Rect&, const Rect&) noexcept = default;
	};

	/** Exact input and output subrects evaluated by one DLSS viewport. */
	struct ViewportCrop
	{
		Extent fullInput{};
		Rect input{};
		Extent fullOutput{};
		Rect output{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return input.IsValidFor(fullInput) && output.IsValidFor(fullOutput);
		}

		[[nodiscard]] constexpr bool IsIdentity() const noexcept
		{
			return IsValid() &&
			       input == Rect{ 0, 0, fullInput.width, fullInput.height } &&
			       output == Rect{ 0, 0, fullOutput.width, fullOutput.height };
		}

		[[nodiscard]] constexpr bool MatchesEvaluationExtents(
			std::uint32_t a_inputWidth,
			std::uint32_t a_inputHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight) const noexcept
		{
			return IsValid() && input.Width() == a_inputWidth &&
			       input.Height() == a_inputHeight &&
			       output.Width() == a_outputWidth &&
			       output.Height() == a_outputHeight;
		}

		[[nodiscard]] static constexpr ViewportCrop Identity(
			std::uint32_t a_inputWidth,
			std::uint32_t a_inputHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight) noexcept
		{
			return {
				.fullInput = { a_inputWidth, a_inputHeight },
				.input = { 0, 0, a_inputWidth, a_inputHeight },
				.fullOutput = { a_outputWidth, a_outputHeight },
				.output = { 0, 0, a_outputWidth, a_outputHeight },
			};
		}

		friend constexpr bool operator==(const ViewportCrop&, const ViewportCrop&) noexcept = default;
	};

	struct Matrix4x4
	{
		std::array<std::array<float, 4>, 4> values{};

		[[nodiscard]] constexpr std::array<float, 4>& operator[](std::size_t a_row) noexcept
		{
			return values[a_row];
		}

		[[nodiscard]] constexpr const std::array<float, 4>& operator[](std::size_t a_row) const noexcept
		{
			return values[a_row];
		}

		[[nodiscard]] static constexpr Matrix4x4 Identity() noexcept
		{
			Matrix4x4 result{};
			for (std::size_t diagonal = 0; diagonal < 4; ++diagonal)
				result[diagonal][diagonal] = 1.0f;
			return result;
		}

		friend constexpr bool operator==(const Matrix4x4&, const Matrix4x4&) noexcept = default;
	};

	/** Multiplies row-major matrices used with row-vector transforms. */
	[[nodiscard]] constexpr Matrix4x4 Multiply(
		const Matrix4x4& a_left,
		const Matrix4x4& a_right) noexcept
	{
		Matrix4x4 result{};
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				for (std::size_t inner = 0; inner < 4; ++inner)
					result[row][column] += a_left[row][inner] * a_right[inner][column];
			}
		}
		return result;
	}

	struct ClipCropAffine
	{
		bool valid = false;
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		float centerX = 0.0f;
		float centerY = 0.0f;
		Matrix4x4 fullClipToCrop = Matrix4x4::Identity();
		Matrix4x4 cropClipToFull = Matrix4x4::Identity();
	};

	/** Builds the exact full-clip/cropped-clip basis change from the input rect. */
	[[nodiscard]] constexpr ClipCropAffine BuildClipCropAffine(
		const ViewportCrop& a_crop) noexcept
	{
		ClipCropAffine result{};
		if (!a_crop.IsValid())
			return result;

		const float fullWidth = static_cast<float>(a_crop.fullInput.width);
		const float fullHeight = static_cast<float>(a_crop.fullInput.height);
		result.scaleX = static_cast<float>(a_crop.input.Width()) / fullWidth;
		result.scaleY = static_cast<float>(a_crop.input.Height()) / fullHeight;
		result.centerX =
			(static_cast<float>(a_crop.input.left) + static_cast<float>(a_crop.input.right)) /
				fullWidth -
			1.0f;
		result.centerY =
			1.0f -
			(static_cast<float>(a_crop.input.top) + static_cast<float>(a_crop.input.bottom)) /
				fullHeight;

		result.fullClipToCrop = Matrix4x4::Identity();
		result.fullClipToCrop[0][0] = 1.0f / result.scaleX;
		result.fullClipToCrop[1][1] = 1.0f / result.scaleY;
		result.fullClipToCrop[3][0] = -result.centerX / result.scaleX;
		result.fullClipToCrop[3][1] = -result.centerY / result.scaleY;

		result.cropClipToFull = Matrix4x4::Identity();
		result.cropClipToFull[0][0] = result.scaleX;
		result.cropClipToFull[1][1] = result.scaleY;
		result.cropClipToFull[3][0] = result.centerX;
		result.cropClipToFull[3][1] = result.centerY;
		result.valid = true;
		return result;
	}

	struct MotionVectorScale
	{
		bool valid = false;
		float x = 1.0f;
		float y = 1.0f;
	};

	/** Converts full-input normalized motion vectors to cropped-input pixel motion. */
	[[nodiscard]] constexpr MotionVectorScale BuildMotionVectorScale(
		const ViewportCrop& a_crop) noexcept
	{
		if (!a_crop.IsValid())
			return {};

		return {
			.valid = true,
			.x = static_cast<float>(a_crop.fullInput.width) /
			     static_cast<float>(a_crop.input.Width()),
			.y = static_cast<float>(a_crop.fullInput.height) /
			     static_cast<float>(a_crop.input.Height()),
		};
	}

	/** Converts full-eye normalized motion into Feature 18 pixel displacement. */
	[[nodiscard]] constexpr MotionVectorScale BuildMotionVectorPixelScale(
		const ViewportCrop& a_crop) noexcept
	{
		if (!a_crop.IsValid())
			return {};

		return {
			.valid = true,
			.x = static_cast<float>(a_crop.fullInput.width),
			.y = static_cast<float>(a_crop.fullInput.height),
		};
	}

	enum class CropHistoryResetReason : std::uint8_t
	{
		None,
		InvalidDescriptor,
		InvalidGeneration,
		NoSuccessfulHistory,
		GenerationChanged,
		NonSequentialFrame,
		DescriptorChanged,
	};

	[[nodiscard]] constexpr const char* GetCropHistoryResetReasonName(
		CropHistoryResetReason a_reason) noexcept
	{
		switch (a_reason) {
		case CropHistoryResetReason::None:
			return "none";
		case CropHistoryResetReason::InvalidDescriptor:
			return "invalid_descriptor";
		case CropHistoryResetReason::InvalidGeneration:
			return "invalid_generation";
		case CropHistoryResetReason::NoSuccessfulHistory:
			return "no_successful_history";
		case CropHistoryResetReason::GenerationChanged:
			return "generation_changed";
		case CropHistoryResetReason::NonSequentialFrame:
			return "non_sequential_frame";
		case CropHistoryResetReason::DescriptorChanged:
			return "descriptor_changed";
		default:
			return "unknown";
		}
	}

	struct SuccessfulCropHistory
	{
		bool valid = false;
		std::uint32_t frame = 0;
		std::uint64_t generation = 0;
		ViewportCrop crop{};
		ViewportCrop previousCrop{};
		bool continuous = false;
		bool reset = true;
		CropHistoryResetReason reason =
			CropHistoryResetReason::NoSuccessfulHistory;
	};

	struct CropContinuityDecision
	{
		bool currentDescriptorValid = false;
		bool continuous = false;
		bool reset = true;
		bool sameFrameReplay = false;
		CropHistoryResetReason reason = CropHistoryResetReason::InvalidDescriptor;
		ViewportCrop previousCrop{};
	};

	/** Admits history only from the immediately preceding successful evaluation. */
	[[nodiscard]] constexpr CropContinuityDecision EvaluateCropContinuity(
		const SuccessfulCropHistory& a_history,
		std::uint32_t a_currentFrame,
		std::uint64_t a_currentGeneration,
		const ViewportCrop& a_currentCrop) noexcept
	{
		CropContinuityDecision result{};
		result.currentDescriptorValid = a_currentCrop.IsValid();
		result.previousCrop = a_currentCrop;
		if (!result.currentDescriptorValid)
			return result;

		if (a_currentGeneration == 0) {
			result.reason = CropHistoryResetReason::InvalidGeneration;
			return result;
		}
		if (!a_history.valid || !a_history.crop.IsValid()) {
			result.reason = CropHistoryResetReason::NoSuccessfulHistory;
			return result;
		}
		if (a_history.generation != a_currentGeneration) {
			result.reason = CropHistoryResetReason::GenerationChanged;
			return result;
		}
		if (a_history.frame == a_currentFrame && a_history.crop == a_currentCrop) {
			result.continuous = a_history.continuous;
			result.reset = a_history.reset;
			result.sameFrameReplay = true;
			result.reason = a_history.reason;
			result.previousCrop = a_history.previousCrop.IsValid() ?
			                          a_history.previousCrop :
			                          a_currentCrop;
			return result;
		}
		if (static_cast<std::uint32_t>(a_history.frame + 1u) != a_currentFrame) {
			result.reason = CropHistoryResetReason::NonSequentialFrame;
			return result;
		}
		if (a_history.crop != a_currentCrop) {
			result.reason = CropHistoryResetReason::DescriptorChanged;
			return result;
		}

		result.continuous = true;
		result.reset = false;
		result.reason = CropHistoryResetReason::None;
		result.previousCrop = a_history.crop;
		return result;
	}

	/** Creates history only after the corresponding DLSS evaluation succeeds. */
	[[nodiscard]] constexpr SuccessfulCropHistory MakeSuccessfulCropHistory(
		std::uint32_t a_frame,
		std::uint64_t a_generation,
		const ViewportCrop& a_crop,
		const CropContinuityDecision& a_decision) noexcept
	{
		if (!a_crop.IsValid() || a_generation == 0)
			return {};

		return {
			.valid = true,
			.frame = a_frame,
			.generation = a_generation,
			.crop = a_crop,
			.previousCrop = a_decision.previousCrop.IsValid() ?
			                    a_decision.previousCrop :
			                    a_crop,
			.continuous = a_decision.continuous,
			.reset = a_decision.reset,
			.reason = a_decision.reason,
		};
	}

	[[nodiscard]] constexpr SuccessfulCropHistory MakeSuccessfulCropHistory(
		std::uint32_t a_frame,
		std::uint64_t a_generation,
		const ViewportCrop& a_crop) noexcept
	{
		CropContinuityDecision firstUse{};
		firstUse.currentDescriptorValid = a_crop.IsValid();
		firstUse.previousCrop = a_crop;
		firstUse.reason = CropHistoryResetReason::NoSuccessfulHistory;
		return MakeSuccessfulCropHistory(
			a_frame, a_generation, a_crop, firstUse);
	}
}
