#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace NeuralRendering
{
	struct CharacterRect
	{
		std::uint32_t minX = 0;
		std::uint32_t minY = 0;
		std::uint32_t maxX = 0;
		std::uint32_t maxY = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return maxX > minX && maxY > minY;
		}

		[[nodiscard]] constexpr std::uint64_t Area() const noexcept
		{
			return IsValid() ?
			           static_cast<std::uint64_t>(maxX - minX) *
			               (maxY - minY) :
			           0;
		}

		bool operator==(const CharacterRect&) const = default;
	};

	/** One actor's eye-local semantic enclosure and perceptual priority inputs. */
	struct CharacterRegionCandidate
	{
		CharacterRect rect{};
		float distanceMeters = std::numeric_limits<float>::max();
		std::uint32_t facePixelSize = 0;
		std::uint32_t stableId = 0;
		bool previouslyAdaptiveSelected = false;
	};

	namespace CharacterRegionPolicy
	{
		inline constexpr float kAdaptiveDetailDistanceMeters = 8.0f;
		inline constexpr std::uint32_t kAdaptiveDetailFacePixelSize = 96;
		inline constexpr float kMaximumDistanceFadeWidthMeters = 1.0f;
		inline constexpr std::uint32_t kFaceSizeExitNumerator = 3;
		inline constexpr std::uint32_t kFaceSizeExitDenominator = 4;
		inline constexpr float kAdaptiveDistanceExitMarginMeters = 1.0f;

		[[nodiscard]] constexpr CharacterRect Union(
			const CharacterRect& a_left,
			const CharacterRect& a_right) noexcept
		{
			if (!a_left.IsValid())
				return a_right;
			if (!a_right.IsValid())
				return a_left;
			return {
				.minX = std::min(a_left.minX, a_right.minX),
				.minY = std::min(a_left.minY, a_right.minY),
				.maxX = std::max(a_left.maxX, a_right.maxX),
				.maxY = std::max(a_left.maxY, a_right.maxY),
			};
		}

		[[nodiscard]] constexpr bool IsWithinHoldWindow(
			std::uint32_t a_currentFrame,
			std::uint32_t a_lastEligibleFrame,
			std::uint32_t a_holdFrames) noexcept
		{
			return a_currentFrame >= a_lastEligibleFrame &&
			       a_currentFrame - a_lastEligibleFrame <= a_holdFrames;
		}

		/** Zero disables the explicit distance limit. */
		[[nodiscard]] inline bool IsWithinMaximumDistance(
			float a_distanceMeters,
			float a_maximumDistanceMeters) noexcept
		{
			return std::isfinite(a_distanceMeters) && a_distanceMeters >= 0.0f &&
			       std::isfinite(a_maximumDistanceMeters) &&
			       a_maximumDistanceMeters >= 0.0f &&
			       (a_maximumDistanceMeters == 0.0f ||
					a_distanceMeters <= a_maximumDistanceMeters);
		}

		/** Returns the hidden fade band immediately inside a nonzero hard cutoff. */
		[[nodiscard]] inline float ResolveDistanceFadeWidth(
			float a_maximumDistanceMeters) noexcept
		{
			return std::isfinite(a_maximumDistanceMeters) &&
			       a_maximumDistanceMeters > 0.0f ?
			           std::min(
					   a_maximumDistanceMeters,
					   kMaximumDistanceFadeWidthMeters) :
			           0.0f;
		}

		/**
		 * Smoothly conceals provider warm-up at the distance boundary while keeping
		 * the user-selected distance as the exact zero/cull point.
		 */
		[[nodiscard]] inline float ResolveDistanceWeight(
			float a_distanceMeters,
			float a_maximumDistanceMeters) noexcept
		{
			if (!std::isfinite(a_maximumDistanceMeters) ||
				a_maximumDistanceMeters < 0.0f) {
				return 0.0f;
			}
			if (a_maximumDistanceMeters == 0.0f)
				return 1.0f;
			if (!std::isfinite(a_distanceMeters) || a_distanceMeters < 0.0f)
				return 0.0f;
			const auto fadeWidth = ResolveDistanceFadeWidth(
				a_maximumDistanceMeters);
			return std::clamp(
				(a_maximumDistanceMeters - a_distanceMeters) / fadeWidth,
				0.0f,
				1.0f);
		}

		[[nodiscard]] inline bool IsDetailRelevant(
			float a_distanceMeters,
			std::uint32_t a_facePixelSize,
			float a_detailDistanceMeters = kAdaptiveDetailDistanceMeters,
			std::uint32_t a_detailFacePixelSize =
				kAdaptiveDetailFacePixelSize) noexcept
		{
			return std::isfinite(a_distanceMeters) && a_distanceMeters >= 0.0f &&
			       std::isfinite(a_detailDistanceMeters) &&
			       a_detailDistanceMeters >= 0.0f &&
			       (a_distanceMeters <= a_detailDistanceMeters ||
					a_facePixelSize >= a_detailFacePixelSize);
		}

		/** Uses a lower exit gate so projected faces cannot chatter at admission size. */
		[[nodiscard]] constexpr std::uint32_t ResolveFaceSizeExitThreshold(
			std::uint32_t a_entryThreshold) noexcept
		{
			if (a_entryThreshold == 0)
				return 0;
			return std::max(
				1u,
				static_cast<std::uint32_t>(
					(static_cast<std::uint64_t>(a_entryThreshold) *
						 kFaceSizeExitNumerator) /
					kFaceSizeExitDenominator));
		}

		[[nodiscard]] inline bool IsDetailRelevantWithHysteresis(
			float a_distanceMeters,
			std::uint32_t a_facePixelSize,
			bool a_previouslySelected,
			float a_detailDistanceMeters = kAdaptiveDetailDistanceMeters,
			std::uint32_t a_detailFacePixelSize =
				kAdaptiveDetailFacePixelSize) noexcept
		{
			if (!a_previouslySelected) {
				return IsDetailRelevant(
					a_distanceMeters,
					a_facePixelSize,
					a_detailDistanceMeters,
					a_detailFacePixelSize);
			}
			return IsDetailRelevant(
				a_distanceMeters,
				a_facePixelSize,
				a_detailDistanceMeters + kAdaptiveDistanceExitMarginMeters,
				ResolveFaceSizeExitThreshold(a_detailFacePixelSize));
		}

		/**
		 * Keeps perceptually detail-relevant actors. Input and output are sorted by
		 * player distance, projected face size, then stable actor ID so both eyes
		 * make the same decision from stereo-wide inputs.
		 */
		inline std::uint32_t SelectAdaptive(
			std::vector<CharacterRegionCandidate>& a_candidates,
			bool a_enabled,
			float a_detailDistanceMeters = kAdaptiveDetailDistanceMeters,
			std::uint32_t a_detailFacePixelSize = kAdaptiveDetailFacePixelSize)
		{
			a_candidates.erase(
				std::remove_if(
					a_candidates.begin(), a_candidates.end(),
					[](const auto& a_candidate) {
						return !a_candidate.rect.IsValid() ||
						       !std::isfinite(a_candidate.distanceMeters) ||
						       a_candidate.distanceMeters < 0.0f;
					}),
				a_candidates.end());
			std::ranges::sort(
				a_candidates, [](const auto& a_left, const auto& a_right) {
					if (a_left.distanceMeters != a_right.distanceMeters)
						return a_left.distanceMeters < a_right.distanceMeters;
					if (a_left.facePixelSize != a_right.facePixelSize)
						return a_left.facePixelSize > a_right.facePixelSize;
					return a_left.stableId < a_right.stableId;
				});
			if (!a_enabled)
				return 0;

			const auto candidateCount = a_candidates.size();
			a_candidates.erase(
				std::remove_if(
					a_candidates.begin(), a_candidates.end(),
					[&](const auto& a_candidate) {
						return !IsDetailRelevantWithHysteresis(
							a_candidate.distanceMeters,
							a_candidate.facePixelSize,
							a_candidate.previouslyAdaptiveSelected,
							a_detailDistanceMeters,
							a_detailFacePixelSize);
					}),
				a_candidates.end());
			const auto culled = static_cast<std::uint32_t>(
				candidateCount - a_candidates.size());
			return culled;
		}

		/**
		 * Compacts visual eligibility rectangles without discarding selected actors.
		 * The least-inflating pair is merged until the shader capacity is met.
		 */
		inline void CompactToCapacity(
			std::vector<CharacterRect>& a_regions,
			std::size_t a_capacity)
		{
			a_regions.erase(
				std::remove_if(
					a_regions.begin(), a_regions.end(),
					[](const auto& a_region) { return !a_region.IsValid(); }),
				a_regions.end());
			if (a_capacity == 0) {
				a_regions.clear();
				return;
			}
			std::vector<CharacterRect> compacted;
			const auto workingCapacity = a_capacity < a_regions.size() ?
			                                 a_capacity + 1 :
			                                 a_regions.size();
			compacted.reserve(workingCapacity);
			for (const auto& region : a_regions) {
				compacted.push_back(region);
				if (compacted.size() <= a_capacity)
					continue;
				std::size_t bestLeft = 0;
				std::size_t bestRight = 1;
				long double bestInflation =
					std::numeric_limits<long double>::max();
				for (std::size_t left = 0; left < compacted.size(); ++left) {
					for (std::size_t right = left + 1; right < compacted.size(); ++right) {
						const auto combined = Union(compacted[left], compacted[right]);
						const auto sourceArea =
							static_cast<long double>(compacted[left].Area()) +
							compacted[right].Area();
						const auto inflation = std::max(
							0.0L,
							static_cast<long double>(combined.Area()) - sourceArea);
						if (inflation < bestInflation) {
							bestInflation = inflation;
							bestLeft = left;
							bestRight = right;
						}
					}
				}
				compacted[bestLeft] = Union(
					compacted[bestLeft], compacted[bestRight]);
				compacted.erase(compacted.begin() + bestRight);
			}
			a_regions = std::move(compacted);
			std::ranges::sort(
				a_regions, [](const auto& a_left, const auto& a_right) {
					if (a_left.minY != a_right.minY)
						return a_left.minY < a_right.minY;
					if (a_left.minX != a_right.minX)
						return a_left.minX < a_right.minX;
					if (a_left.maxY != a_right.maxY)
						return a_left.maxY < a_right.maxY;
					return a_left.maxX < a_right.maxX;
				});
		}

		/** Returns the exact union area of a small set of screen rectangles. */
		[[nodiscard]] inline std::uint64_t CoveredArea(
			const std::vector<CharacterRect>& a_regions)
		{
			std::vector<std::uint32_t> xEdges;
			xEdges.reserve(a_regions.size() * 2);
			for (const auto& region : a_regions) {
				if (!region.IsValid())
					continue;
				xEdges.push_back(region.minX);
				xEdges.push_back(region.maxX);
			}
			std::ranges::sort(xEdges);
			xEdges.erase(std::unique(xEdges.begin(), xEdges.end()), xEdges.end());

			std::uint64_t area = 0;
			for (std::size_t edge = 1; edge < xEdges.size(); ++edge) {
				const auto left = xEdges[edge - 1];
				const auto right = xEdges[edge];
				if (right <= left)
					continue;
				std::vector<std::pair<std::uint32_t, std::uint32_t>> yIntervals;
				yIntervals.reserve(a_regions.size());
				for (const auto& region : a_regions) {
					if (region.IsValid() && region.minX < right && region.maxX > left)
						yIntervals.emplace_back(region.minY, region.maxY);
				}
				std::ranges::sort(yIntervals);
				std::uint64_t coveredY = 0;
				std::uint32_t intervalMin = 0;
				std::uint32_t intervalMax = 0;
				bool haveInterval = false;
				for (const auto& [minimum, maximum] : yIntervals) {
					if (!haveInterval) {
						intervalMin = minimum;
						intervalMax = maximum;
						haveInterval = true;
					} else if (minimum <= intervalMax) {
						intervalMax = std::max(intervalMax, maximum);
					} else {
						coveredY += intervalMax - intervalMin;
						intervalMin = minimum;
						intervalMax = maximum;
					}
				}
				if (haveInterval)
					coveredY += intervalMax - intervalMin;
				area += static_cast<std::uint64_t>(right - left) * coveredY;
			}
			return area;
		}
	}
}
