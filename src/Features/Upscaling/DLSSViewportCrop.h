#pragma once

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
}
