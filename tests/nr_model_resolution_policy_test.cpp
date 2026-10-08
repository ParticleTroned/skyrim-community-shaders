#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void CheckGeometry()
	{
		using namespace NeuralRendering;
		using UpscalingDLSS::Extent;
		using UpscalingDLSS::ViewportCrop;
		constexpr ViewportCrop crop{
			.fullInput = { 200, 120 },
			.input = { 30, 20, 131, 87 },
			.fullOutput = { 200, 120 },
			.output = { 30, 20, 131, 87 }
		};
		constexpr Extent source{ 101, 67 };
		constexpr ComputeSubrect region{ 7, 5, 21, 13 };
		struct Case
		{
			std::uint32_t percent;
			Extent model;
			ComputeSubrect modelRegion;
		};
		for (const auto& item : { Case{ 33, { 34, 23 }, { 2, 1, 8, 6 } },
				 Case{ 50, { 51, 34 }, { 3, 2, 12, 8 } },
				 Case{ 67, { 68, 45 }, { 4, 3, 15, 10 } },
				 Case{ 100, { 101, 67 }, region } }) {
			const auto geometry = BuildModelResolutionGeometry(crop, source, region, item.percent);
			Require(geometry.has_value(), "Valid offset FOV model geometry was rejected");
			Require(geometry->sourceSize == source && geometry->sourceRegion == region, "Projection changed the source region");
			Require(geometry->modelSize == item.model && geometry->modelRegion == item.modelRegion, "Projection lost outward ROI coverage");
			Require(geometry->nativeCrop.IsIdentity() && geometry->nativeCrop.fullInput == item.model &&
						geometry->nativeCrop.fullOutput == item.model,
				"Native evaluation must use one identity model grid");
			for (float normalizedMotion : { -0.02f, 0.0f, 0.0125f }) {
				const float expectedX = normalizedMotion * crop.fullInput.width * item.model.width / source.width;
				const float expectedY = normalizedMotion * crop.fullInput.height * item.model.height / source.height;
				const float projectedX = normalizedMotion * geometry->motionNormalization[0] * item.model.width;
				const float projectedY = normalizedMotion * geometry->motionNormalization[1] * item.model.height;
				Require(std::abs(projectedX - expectedX) < 1e-6f && std::abs(projectedY - expectedY) < 1e-6f,
					"Motion normalization converted full-eye pixel motion more than once");
			}
		}
		const auto full = BuildModelResolutionGeometry(ViewportCrop::Identity(101, 67, 101, 67), source, { 0, 0, 101, 67 }, 67);
		Require(full && full->motionNormalization == std::array{ 1.0f, 1.0f }, "Uncropped views must retain normalized motion");

		Require(!BuildModelResolutionGeometry(crop, { 100, 67 }, region, 67), "Mismatched evaluation extent was admitted");
		for (const auto invalid : { ComputeSubrect{}, ComputeSubrect{ 100, 0, 2, 1 }, ComputeSubrect{ 0, 66, 1, 2 },
				 ComputeSubrect{ std::numeric_limits<std::uint32_t>::max(), 0, 1, 1 } })
			Require(!BuildModelResolutionGeometry(crop, source, invalid, 67), "Out-of-bounds source ROI was admitted");
		auto invalidCrop = crop;
		invalidCrop.fullInput.width = 16385;
		invalidCrop.fullOutput.width = 16385;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Oversized full-eye coordinates were admitted");
		invalidCrop = crop;
		invalidCrop.fullOutput.width = 201;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Unequal input/output domains were admitted");
		invalidCrop = crop;
		++invalidCrop.output.left;
		++invalidCrop.output.right;
		Require(!BuildModelResolutionGeometry(invalidCrop, source, region, 67), "Mismatched input/output crop origins were admitted");
		for (std::uint32_t percent : { 0u, 32u, 101u, std::numeric_limits<std::uint32_t>::max() })
			Require(!BuildModelResolutionGeometry(crop, source, region, percent), "Invalid model scale reached projected geometry");
	}

}

int main()
{
	try {
		using namespace NeuralRendering;
		for (std::uint32_t percent = 0; percent <= 150; ++percent) {
			const bool valid = percent >= 33 && percent <= 100;
			Require(IsValidModelResolutionPercent(percent) == valid, "Model scale validation changed its supported interval");
			Require(EffectiveModelResolutionPercent(RenderingMode::ReducedResolution, percent) == (valid ? percent : 100),
				"Reduced-resolution mode did not retain a valid scale or reject an invalid one");
			for (auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated })
				Require(EffectiveModelResolutionPercent(mode, percent) == 100, "Model scale changed an unrelated rendering mode");
		}
		const auto maximum = std::numeric_limits<std::uint32_t>::max();
		Require(!IsValidModelResolutionPercent(maximum), "Unbounded model scale was admitted");
		Require(EffectiveModelResolutionPercent(static_cast<RenderingMode>(maximum), 50) == 100, "Unknown rendering mode applied model reduction");
		for (const auto dimensions : { std::array{ 1u, 1u }, std::array{ 101u, 67u }, std::array{ 2448u, 2448u }, std::array{ 16384u, 16384u } }) {
			for (std::uint32_t percent : { 33u, 50u, 67u, 75u, 100u }) {
				const auto extent = BuildModelResolutionExtent(dimensions[0], dimensions[1], percent);
				Require(extent.width > 0 && extent.height > 0, "Valid model extent became empty");
				Require(extent.width <= dimensions[0] && extent.height <= dimensions[1], "Model extent exceeded its source");
				Require(std::uint64_t(extent.width) * 100 >= std::uint64_t(dimensions[0]) * percent &&
							std::uint64_t(extent.width - 1) * 100 < std::uint64_t(dimensions[0]) * percent,
					"Model width does not conservatively round up");
				Require(std::uint64_t(extent.height) * 100 >= std::uint64_t(dimensions[1]) * percent &&
							std::uint64_t(extent.height - 1) * 100 < std::uint64_t(dimensions[1]) * percent,
					"Model height does not conservatively round up");
			}
		}
		for (const auto dimensions : { std::array{ 0u, 1u }, std::array{ 1u, 0u }, std::array{ 16385u, 100u }, std::array{ 100u, 16385u }, std::array{ maximum, maximum } }) {
			const auto extent = BuildModelResolutionExtent(dimensions[0], dimensions[1], 50);
			Require(extent.width == 0 && extent.height == 0, "Invalid source dimensions reached model allocation");
		}
		for (std::uint32_t percent : { 0u, 32u, 101u, maximum }) {
			const auto extent = BuildModelResolutionExtent(100, 100, percent);
			Require(extent.width == 0 && extent.height == 0, "Invalid model scale reached allocation");
		}
		CheckGeometry();
		std::cout << "PASS: NR model scale boundaries, mode isolation, conservative extents and overflow rejection\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}