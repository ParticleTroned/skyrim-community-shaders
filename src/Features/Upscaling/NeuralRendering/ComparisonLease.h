#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <cstdint>
#	include <limits>
#	include <optional>
#	include <utility>

namespace NeuralRendering
{
	template <class Mode>
	struct ComparisonOverrideStatus
	{
		Mode mode = Mode::Auto;
		std::uint64_t revision = 1;
		std::uint32_t remainingFrames = 0;
		bool historyResetPending = false;
	};
	/** A bounded comparison lease cannot restore over a newer request or user generation. */
	template <class Mode, Mode Maximum>
	class ComparisonLease
	{
	public:
		static constexpr std::uint32_t kMaximumFrames = 600;
		[[nodiscard]] ComparisonOverrideStatus<Mode> Status() const noexcept
		{
			auto result = status_;
			result.historyResetPending = changed_;
			return result;
		}
		bool Configure(Mode mode, std::uint32_t frames, std::uint64_t expectedRevision) noexcept
		{
			if (expectedRevision != status_.revision ||
				(mode < Mode::Auto || mode > Maximum) ||
				(mode == Mode::Auto ? frames != 0 : frames == 0 || frames > kMaximumFrames))
				return false;
			changed_ |= mode != status_.mode;
			status_ = { mode, status_.revision + 1, frames };
			frame_.reset();
			generation_.reset();
			return true;
		}
		void Cancel() noexcept
		{
			if (status_.mode != Mode::Auto)
				(void)Configure(Mode::Auto, 0, status_.revision);
		}
		[[nodiscard]] bool ConsumeChange() noexcept { return std::exchange(changed_, false); }
		/** Resolve before source admission; the caller invalidates downstream history once on change. */
		[[nodiscard]] std::pair<Mode, bool> Resolve(std::uint32_t frame, std::uint64_t generation) noexcept
		{
			if (status_.mode != Mode::Auto) {
				if (frame == std::numeric_limits<std::uint32_t>::max() || (generation_ && *generation_ != generation))
					Cancel();
				else if (frame_ && frame != *frame_) {
					const auto elapsed = frame - *frame_;
					if (elapsed >= status_.remainingFrames)
						Cancel();
					else
						status_.remainingFrames -= elapsed;
				}
				frame_ = frame;
				generation_ = generation;
			}
			return { status_.mode, ConsumeChange() };
		}

	private:
		ComparisonOverrideStatus<Mode> status_{};
		std::optional<std::uint32_t> frame_;
		std::optional<std::uint64_t> generation_;
		bool changed_ = false;
	};
}
#endif
