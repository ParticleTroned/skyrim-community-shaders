#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace VRSubmitTemporalSnapshot
{
	inline constexpr std::uint64_t MaxCompositorCycle = std::numeric_limits<std::uint64_t>::max() >> 1u;

	struct Key
	{
		std::uint32_t frame = 0;
		std::uint32_t generation = 0;
		std::uint32_t method = 0;
		std::uint32_t inputWidth = 0;
		std::uint32_t inputHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint64_t compositorCycle = 0;

		bool operator==(const Key&) const = default;
	};

	struct Scalars
	{
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		float cameraNear = 0.0f;
		float cameraFar = 0.0f;
		float verticalFov = 0.0f;
		float frameTimeMilliseconds = 0.0f;
		bool historyReset = false;
	};

	[[nodiscard]] constexpr bool IsValid(const Key& a_key) noexcept
	{
		constexpr std::uint32_t maxTextureDimension = 16384;
		const auto validDimension = [](std::uint32_t a_dimension) {
			return a_dimension != 0 && a_dimension <= maxTextureDimension;
		};
		return a_key.frame != std::numeric_limits<std::uint32_t>::max() && a_key.method != 0 &&
		       a_key.compositorCycle <= MaxCompositorCycle &&
		       validDimension(a_key.inputWidth) && validDimension(a_key.inputHeight) &&
		       validDimension(a_key.outputWidth) && validDimension(a_key.outputHeight);
	}

	[[nodiscard]] inline bool IsValid(const Scalars& a_scalars) noexcept
	{
		return std::isfinite(a_scalars.jitterX) && std::isfinite(a_scalars.jitterY) &&
		       std::isfinite(a_scalars.cameraNear) && a_scalars.cameraNear > 0.0f &&
		       std::isfinite(a_scalars.cameraFar) && a_scalars.cameraFar > a_scalars.cameraNear &&
		       std::isfinite(a_scalars.verticalFov) && a_scalars.verticalFov > 0.0f && a_scalars.verticalFov < 3.141592654f &&
		       std::isfinite(a_scalars.frameTimeMilliseconds) && a_scalars.frameTimeMilliseconds >= 0.0f;
	}

	/** A late recovery reset can strengthen the producer's decision, never clear it. */
	[[nodiscard]] constexpr bool ResolveHistoryReset(bool a_capturedReset, bool a_lateReset, bool a_pendingReset = false) noexcept
	{
		return a_capturedReset || a_lateReset || a_pendingReset;
	}

	[[nodiscard]] constexpr bool MatchesProducer(std::uint32_t a_cachedFrame, std::uint64_t a_cachedCycle, std::uint32_t a_frame, std::uint64_t a_cycle) noexcept
	{
		return a_cachedFrame != std::numeric_limits<std::uint32_t>::max() &&
		       a_frame != std::numeric_limits<std::uint32_t>::max() &&
		       a_cachedCycle == a_cycle && (a_cycle != 0 || a_cachedFrame == a_frame);
	}

	[[nodiscard]] constexpr bool IsSameProducer(const Key& a_left, const Key& a_right) noexcept
	{
		return MatchesProducer(a_left.frame, a_left.compositorCycle, a_right.frame, a_right.compositorCycle);
	}

	[[nodiscard]] constexpr bool IsNewerCompositorCycle(std::uint64_t a_candidate, std::uint64_t a_published) noexcept
	{
		if (a_candidate == 0 || a_published == 0 || a_candidate > MaxCompositorCycle || a_published > MaxCompositorCycle)
			return false;
		const auto distance = a_candidate >= a_published ? a_candidate - a_published : MaxCompositorCycle - a_published + a_candidate;
		return distance != 0 && distance <= MaxCompositorCycle / 2u;
	}

	/** Owns one producer's immutable stereo constants; the caller validates its camera payload. */
	template <class EyeCamera>
	struct Snapshot
	{
		Key key{};
		Scalars scalars{};
		std::array<EyeCamera, 2> eyes{};
		bool valid = false;

		[[nodiscard]] bool Matches(const Key& a_key) const noexcept
		{
			return valid && key == a_key;
		}

		/** A desktop Present may advance the engine frame before the same compositor cycle submits. */
		[[nodiscard]] bool MatchesForDispatch(const Key& a_key) const noexcept
		{
			return valid && IsValid(a_key) && IsSameProducer(key, a_key) &&
			       key.generation == a_key.generation && key.method == a_key.method &&
			       key.inputWidth == a_key.inputWidth && key.inputHeight == a_key.inputHeight &&
			       key.outputWidth == a_key.outputWidth && key.outputHeight == a_key.outputHeight;
		}

		/** Repeated producer visits retain the first capture; a changed producer contract invalidates it. */
		[[nodiscard]] bool Publish(const Key& a_key, const Scalars& a_scalars, const std::array<EyeCamera, 2>& a_eyes)
		{
			if (attempted && IsSameProducer(key, a_key)) {
				if (a_key != key)
					valid = false;
				return Matches(a_key);
			}
			if (attempted) {
				if (key.compositorCycle == 0 && a_key.compositorCycle == 0) {
					if (key.frame - a_key.frame < 0x80000000u)
						return false;
				} else if (a_key.compositorCycle == 0 ||
						   (key.compositorCycle != 0 && !IsNewerCompositorCycle(a_key.compositorCycle, key.compositorCycle))) {
					return false;
				}
			}

			attempted = true;
			valid = false;
			key = a_key;
			if (!IsValid(a_key) || !IsValid(a_scalars))
				return false;

			scalars = a_scalars;
			eyes = a_eyes;
			valid = true;
			return true;
		}

		/** Prevents reuse or recapture until a later producer cycle, or frame before pose tracking starts. */
		void Invalidate() noexcept { valid = false; }

	private:
		bool attempted = false;
	};
}
