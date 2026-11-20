#pragma once

#include <array>
#include <cstdint>

namespace CSX::Diagnostics::ColourPipelineProbe
{
	enum class Route : std::uint8_t
	{
		MainPass,
		CompositorSubmit
	};

	enum class Stage : std::uint8_t
	{
		FsrInput,
		FsrOutput,
		CombinedMain,
		ImageSpaceInput,
		ImageSpaceOutput
	};
}

namespace CSX::Diagnostics::ColourPipelineProbe::Policy
{
	inline constexpr std::uint32_t kEyeCount = 2;
	inline constexpr std::uint32_t kStageCount = static_cast<std::uint32_t>(Stage::ImageSpaceOutput) + 1;
	inline constexpr std::uint32_t kSlotCount = kStageCount * kEyeCount;
	inline constexpr std::uint32_t kMaximumDimension = 8192;
	inline constexpr std::uint32_t kGridSize = 17;
	inline constexpr std::uint64_t kMaximumSlotBytes = std::uint64_t{ kMaximumDimension } * kGridSize * 16;
	inline constexpr std::uint64_t kMaximumCaptureBytes = kMaximumSlotBytes * kSlotCount;
	inline constexpr std::uint32_t kMaximumCaptureIdBytes = 128;
	inline constexpr std::uint32_t kMaximumMetadataBytes = 16 * 1024;
	inline constexpr std::uint32_t kTimeoutSeconds = 15;

	inline constexpr std::uint32_t RequiredSlotCount(Route a_route) noexcept
	{
		return a_route == Route::CompositorSubmit ? 2 * kEyeCount : kSlotCount;
	}

	/** Keep main-source and VR ImageSpace destination observations distinct. */
	inline constexpr bool ValidImageSpaceTarget(Stage a_stage, std::uint32_t a_target,
		bool a_matchesMain, std::uint32_t a_mainTarget, std::uint32_t a_vrDestination) noexcept
	{
		return (a_stage == Stage::CombinedMain && a_target == a_mainTarget && a_matchesMain) ||
		       ((a_stage == Stage::ImageSpaceInput || a_stage == Stage::ImageSpaceOutput) &&
				   a_target == a_vrDestination);
	}

	struct StageEyeSlot
	{
		Stage stage;
		std::uint32_t eye;
		bool queued;
		bool mapped;
	};

	/** Derive every required identity from its slot index, including absent slots. */
	template <class Slot>
	std::array<StageEyeSlot, kSlotCount> DescribeStageEyeSlots(const std::array<Slot, kSlotCount>& a_slots) noexcept
	{
		std::array<StageEyeSlot, kSlotCount> result{};
		for (std::uint32_t index = 0; index < kSlotCount; ++index)
			result[index] = { static_cast<Stage>(index / kEyeCount), index % kEyeCount,
				a_slots[index].queued, a_slots[index].mapped };
		return result;
	}

	/** Both capture routes retain one nonzero vendor evaluation attempt. */
	inline constexpr bool MatchesVendorBatch(std::uint64_t a_retained, std::uint64_t a_observed) noexcept
	{
		return a_retained != 0 && a_retained == a_observed;
	}

	/** Attribute retained input bytes only to the successful dispatch for that eye. */
	template <class Dispatch>
	bool BindInputDispatch(Dispatch& a_input, const Dispatch& a_dispatch,
		std::uint32_t a_frame, std::uint64_t a_revision, std::uint32_t a_eye)
	{
		if (!a_dispatch.dispatchSerial || a_dispatch.frame != a_frame ||
			a_dispatch.colourContractRevision != a_revision ||
			a_dispatch.contextIndex != a_eye || a_dispatch.path.empty() ||
			!MatchesVendorBatch(a_input.vendorBatch, a_dispatch.vendorBatch))
			return false;
		a_input = a_dispatch;
		return true;
	}

	/** Check source bounds before addition or staging allocation. */
	inline constexpr bool ValidRectangle(
		std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight,
		std::uint32_t a_x, std::uint32_t a_y,
		std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		return a_width && a_height && a_width <= kMaximumDimension &&
		       a_height <= kMaximumDimension && a_x <= a_sourceWidth &&
		       a_y <= a_sourceHeight && a_width <= a_sourceWidth - a_x &&
		       a_height <= a_sourceHeight - a_y;
	}

	/** Select the same inclusive, uniformly spaced source pixels for every format. */
	inline constexpr std::uint32_t SampleCoordinate(std::uint32_t a_extent, std::uint32_t a_gridIndex) noexcept
	{
		return a_extent ? static_cast<std::uint32_t>((std::uint64_t{ a_extent - 1 } * a_gridIndex) / (kGridSize - 1)) : 0;
	}

	/** Stage only the sampled rows, retaining native bytes across the full row. */
	inline constexpr std::uint32_t ReadbackHeight(std::uint32_t a_height) noexcept
	{
		return a_height < kGridSize ? a_height : kGridSize;
	}

	inline constexpr std::uint32_t SourceRow(std::uint32_t a_height, std::uint32_t a_readbackRow) noexcept
	{
		return a_height <= kGridSize ? a_readbackRow : SampleCoordinate(a_height, a_readbackRow);
	}

	inline constexpr std::uint32_t SampleRow(std::uint32_t a_height, std::uint32_t a_gridIndex) noexcept
	{
		return a_height <= kGridSize ? SampleCoordinate(a_height, a_gridIndex) : a_gridIndex;
	}

	/** Bound sampled-row payload bytes; driver allocation overhead is not measured. */
	inline constexpr bool CanAllocate(
		std::uint32_t a_width, std::uint32_t a_height,
		std::uint32_t a_bytesPerPixel, std::uint64_t a_allocated) noexcept
	{
		if (!a_width || !a_height || a_width > kMaximumDimension ||
			a_height > kMaximumDimension || !a_bytesPerPixel ||
			a_bytesPerPixel > 16 || a_allocated > kMaximumCaptureBytes)
			return false;
		const auto bytes = std::uint64_t{ a_width } * ReadbackHeight(a_height) * a_bytesPerPixel;
		return bytes <= kMaximumSlotBytes && bytes <= kMaximumCaptureBytes - a_allocated;
	}
}
