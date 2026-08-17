#include "Utils/VRDynamicResolutionPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
	using namespace VRDynamicResolutionPolicy;

	constexpr std::array kQualityScales{
		1.0f,
		0.85f,
		1.0f / 1.3f,
		1.0f / 1.5f,
		1.0f / 1.7f,
		0.5f,
		1.0f / 3.0f,
	};

	std::uint32_t ScaleEvenRenderDimension(
		std::uint32_t a_displayExtent,
		float a_scale)
	{
		const float scaled =
			static_cast<float>(a_displayExtent) *
			std::clamp(a_scale, 0.1f, 1.0f);
		const auto bounded = std::clamp<std::uint32_t>(
			static_cast<std::uint32_t>(std::floor(scaled)),
			2u,
			a_displayExtent);
		return bounded & ~1u;
	}

	bool CoversKnownFloatReconstructionRegressions()
	{
		struct RegressionCase
		{
			std::uint32_t displayEyeExtent;
			float scale;
			std::uint32_t renderEyeExtent;
		};
		constexpr std::array cases{
			RegressionCase{ 1440u, 1.0f / 1.7f, 846u },
			RegressionCase{ 1074u, 0.85f, 912u },
		};

		for (const auto& test : cases) {
			if (ScaleEvenRenderDimension(test.displayEyeExtent, test.scale) !=
				test.renderEyeExtent) {
				return false;
			}

			const auto displayWidth = test.displayEyeExtent * 2u;
			const auto renderWidth = test.renderEyeExtent * 2u;
			const float ratio =
				static_cast<float>(renderWidth) /
				static_cast<float>(displayWidth);
			const float reconstructedEye =
				static_cast<float>(test.displayEyeExtent) * ratio;
			const float reconstructedWidth =
				static_cast<float>(displayWidth) * ratio;

			if (static_cast<std::uint32_t>(reconstructedEye) >= test.renderEyeExtent ||
				static_cast<std::uint32_t>(reconstructedWidth) >= renderWidth ||
				SnapNearIntegerPixelExtent(reconstructedEye) != static_cast<float>(test.renderEyeExtent) ||
				SnapNearIntegerPixelExtent(reconstructedWidth) != static_cast<float>(renderWidth)) {
				return false;
			}

			const StereoContract contract{
				.horizontal = { displayWidth, renderWidth },
				.vertical = { test.displayEyeExtent, test.renderEyeExtent },
			};
			if (!contract.IsValid() ||
				MapBoundaryTruncated(0, contract.horizontal) != 0 ||
				MapBoundaryTruncated(
					static_cast<std::int32_t>(test.displayEyeExtent),
					contract.horizontal) != static_cast<std::int32_t>(test.renderEyeExtent) ||
				MapBoundaryTruncated(
					static_cast<std::int32_t>(displayWidth),
					contract.horizontal) != static_cast<std::int32_t>(renderWidth)) {
				return false;
			}
		}

		return true;
	}

	bool CoversQualityModesAndStereoSeams()
	{
		for (std::uint32_t displayEyeExtent = 2u;
			displayEyeExtent <= 16384u;
			++displayEyeExtent) {
			for (const float scale : kQualityScales) {
				const auto renderEyeExtent =
					ScaleEvenRenderDimension(displayEyeExtent, scale);
				const StereoContract contract{
					.horizontal = {
						displayEyeExtent * 2u,
						renderEyeExtent * 2u,
					},
					.vertical = { displayEyeExtent, renderEyeExtent },
				};
				if (!contract.IsValid())
					return false;

				const float widthRatio =
					static_cast<float>(contract.horizontal.renderExtent) /
					static_cast<float>(contract.horizontal.displayExtent);
				const float heightRatio =
					static_cast<float>(contract.vertical.renderExtent) /
					static_cast<float>(contract.vertical.displayExtent);
				if (!MatchesPublishedRatio(widthRatio, contract.horizontal) ||
					!MatchesPublishedRatio(heightRatio, contract.vertical) ||
					SnapNearIntegerPixelExtent(
						static_cast<float>(contract.horizontal.displayExtent) * widthRatio) !=
						static_cast<float>(contract.horizontal.renderExtent) ||
					SnapNearIntegerPixelExtent(
						static_cast<float>(displayEyeExtent) * widthRatio) !=
						static_cast<float>(renderEyeExtent) ||
					SnapNearIntegerPixelExtent(
						static_cast<float>(contract.vertical.displayExtent) * heightRatio) !=
						static_cast<float>(contract.vertical.renderExtent)) {
					return false;
				}

				const auto leftMin = MapBoundaryTruncated(0, contract.horizontal);
				const auto seam = MapBoundaryTruncated(
					static_cast<std::int32_t>(displayEyeExtent),
					contract.horizontal);
				const auto rightMax = MapBoundaryTruncated(
					static_cast<std::int32_t>(contract.horizontal.displayExtent),
					contract.horizontal);
				if (leftMin != 0 ||
					seam != static_cast<std::int32_t>(renderEyeExtent) ||
					rightMax != static_cast<std::int32_t>(contract.horizontal.renderExtent) ||
					seam - leftMin != rightMax - seam) {
					return false;
				}
			}
		}

		return true;
	}

	constexpr bool CoversBoundaryAndAdmissionGuards()
	{
		constexpr PixelContract contract{ 10u, 6u };
		constexpr PixelContract missingContract{};
		constexpr PixelContract oversizedContract{ 10u, 11u };
		constexpr StereoContract stereo{
			.horizontal = { 20u, 12u },
			.vertical = { 10u, 6u },
		};
		constexpr StereoContract oddDisplay{
			.horizontal = { 21u, 12u },
			.vertical = { 10u, 6u },
		};
		constexpr StereoContract oddRender{
			.horizontal = { 20u, 11u },
			.vertical = { 10u, 6u },
		};
		return contract.IsValid() &&
		       stereo.IsValid() &&
		       !missingContract.IsValid() &&
		       !oversizedContract.IsValid() &&
		       !oddDisplay.IsValid() &&
		       !oddRender.IsValid() &&
		       MapBoundaryTruncated(10, contract) == 6 &&
		       MapBoundaryTruncated(5, contract) == 3 &&
		       MapBoundaryTruncated(-5, contract) == -3 &&
		       MapBoundaryTruncated(-1, contract) == 0 &&
		       MapBoundaryTruncated(
				   std::numeric_limits<std::int32_t>::max(),
				   PixelContract{ 1u, 1u }) ==
		           std::numeric_limits<std::int32_t>::max() &&
		       MapBoundaryTruncated(
				   std::numeric_limits<std::int32_t>::min(),
				   PixelContract{ 1u, 1u }) ==
		           std::numeric_limits<std::int32_t>::min() &&
		       MapBoundaryTruncated(7, missingContract) == 7;
	}

	bool CoversExtentAndDispatchGuards()
	{
		std::uint32_t extent = 0;
		if (!TryResolvePixelExtent(1692.0f, extent) || extent != 1692u ||
			TryResolvePixelExtent(1692.5f, extent) ||
			TryResolvePixelExtent(0.0f, extent) ||
			TryResolvePixelExtent(std::numeric_limits<float>::infinity(), extent) ||
			TryResolvePixelExtent(std::numeric_limits<float>::quiet_NaN(), extent)) {
			return false;
		}
		constexpr float genuineFraction = 912.9f;
		if (SnapNearIntegerPixelExtent(genuineFraction) != genuineFraction ||
			SnapNearIntegerPixelExtent(-1.0f) != -1.0f ||
			!std::isnan(SnapNearIntegerPixelExtent(
				std::numeric_limits<float>::quiet_NaN()))) {
			return false;
		}
		const PixelContract ratioContract{ 2880u, 1692u };
		if (!MatchesPublishedRatio(1692.0f / 2880.0f, ratioContract) ||
			MatchesPublishedRatio(0.5f, ratioContract) ||
			MatchesPublishedRatio(
				std::numeric_limits<float>::quiet_NaN(),
				ratioContract)) {
			return false;
		}

		// Snapping the reconstructed extent must not emulate an upward ratio bias:
		// a group-aligned target still dispatches exactly its required group count.
		constexpr std::uint32_t displayExtent = 2260u;
		constexpr std::uint32_t renderExtent = 1504u;
		const float ratio =
			static_cast<float>(renderExtent) /
			static_cast<float>(displayExtent);
		const float reconstructed = static_cast<float>(displayExtent) * ratio;
		const float snapped = SnapNearIntegerPixelExtent(reconstructed);
		return reconstructed > static_cast<float>(renderExtent) &&
		       static_cast<std::uint32_t>(std::ceil(reconstructed / 8.0f)) == 189u &&
		       snapped == static_cast<float>(renderExtent) &&
		       static_cast<std::uint32_t>(std::ceil(snapped / 8.0f)) ==
		           renderExtent / 8u;
	}
}

static_assert(CoversBoundaryAndAdmissionGuards());

int main()
{
	if (!CoversKnownFloatReconstructionRegressions())
		return 1;
	if (!CoversQualityModesAndStereoSeams())
		return 2;
	if (!CoversExtentAndDispatchGuards())
		return 3;
	return 0;
}
