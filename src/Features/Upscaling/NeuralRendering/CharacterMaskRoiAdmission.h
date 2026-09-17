#pragma once

#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterMaskRoiAdmissionFreshFrames = 3;
	inline constexpr std::uint32_t kCharacterMaskRoiAdmissionRetryFrames = 30;

	/**
	 * Dampens optional readback admission, never proves mask coverage. Every
	 * admitted frame still needs its own fresh valid bounds. Explicit failures
	 * start a retry delay; merely skipping frames for a retained-world menu does
	 * not evict an established admission. Value initialization starts a new epoch.
	 */
	struct CharacterMaskRoiAdmission
	{
		[[nodiscard]] bool CanAttempt(std::uint32_t a_frame) const noexcept
		{
			if (!cooldown_)
				return true;
			const auto elapsed = a_frame - rejectedFrame_;
			// Unsigned subtraction admits the ordinary UINT32 wrap while a
			// backwards clock cannot accidentally expire an outstanding delay.
			return elapsed < kHalfFrameRange && elapsed >= kCharacterMaskRoiAdmissionRetryFrames;
		}

		[[nodiscard]] bool ObserveFresh(std::uint32_t a_frame) noexcept
		{
			if (!CanAttempt(a_frame))
				return false;
			cooldown_ = false;
			if (observed_) {
				const auto elapsed = a_frame - observedFrame_;
				if (elapsed == 0)
					return freshFrames_ >= kCharacterMaskRoiAdmissionFreshFrames;
				if (elapsed < kHalfFrameRange && freshFrames_ >= kCharacterMaskRoiAdmissionFreshFrames) {
					// A menu can retain the world for many evaluation frames. With
					// no explicit failed readback, keep the already admitted mode.
					observedFrame_ = a_frame;
					return true;
				}
				// Count validated forward samples, not contiguous renderer ticks.
				// Reprojection/retained-world menus can skip IDs even when every
				// attempted read succeeds. Actual unavailable reads call Reject.
				freshFrames_ = elapsed < kHalfFrameRange ? freshFrames_ + 1u : 1u;
			} else {
				freshFrames_ = 1u;
				observed_ = true;
			}
			observedFrame_ = a_frame;
			return freshFrames_ >= kCharacterMaskRoiAdmissionFreshFrames;
		}

		void Reject(std::uint32_t a_frame) noexcept
		{
			observed_ = false;
			freshFrames_ = 0;
			cooldown_ = true;
			rejectedFrame_ = a_frame;
		}

	private:
		static constexpr std::uint32_t kHalfFrameRange = 0x80000000u;
		std::uint32_t observedFrame_ = 0;
		std::uint32_t rejectedFrame_ = 0;
		std::uint32_t freshFrames_ = 0;
		bool observed_ = false;
		bool cooldown_ = false;
	};
}
