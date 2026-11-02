#include "Features/Upscaling/NeuralRendering/MonoPolicy.h"
#include "Features/Upscaling/NeuralRendering/PeripheralCompressionPolicy.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace NeuralRendering;
void Check(bool value)
{
	if (!value) {
		std::cerr << "Mono compression invariant failed\n";
		std::exit(1);
	}
}
int main()
{
	unsigned checks = 0;
	for (unsigned percent : { 30u, 50u, 75u, 100u })
		for (unsigned compression : { 0u, 1u, 20u, 50u, 100u }) {
			const auto plan = BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, percent, compression, false, false);
			if (!compression) {
				Check(!plan.active);
				continue;
			}
			Check(plan.active && plan.maximumFootprint[0] <= 32 && plan.maximumFootprint[1] <= 32);
			const auto& mapping = plan.mapping;
			for (unsigned y = 0; y <= 40; ++y)
				for (unsigned x = 0; x <= 40; ++x) {
					PeripheralMapping::Point input{ x / 40.0f, y / 40.0f };
					const auto target = mapping.Forward(input), restored = mapping.Inverse(target);
					for (unsigned axis = 0; axis < 2; ++axis) {
						Check(std::abs(input[axis] - restored[axis]) < 2e-6f);
						Check(target[axis] >= -1e-6f && target[axis] <= 1.000001f);
						++checks;
					}
				}
			for (unsigned y = 0; y < 8; ++y)
				for (unsigned x = 0; x < 8; ++x) {
					PeripheralMapping::Point lower{ x / 8.0f, y / 8.0f }, upper{ (x + 1) / 8.0f, (y + 1) / 8.0f };
					const auto bounds = mapping.SourceBounds(lower, upper);
					for (unsigned j = 0; j <= 4; ++j)
						for (unsigned i = 0; i <= 4; ++i) {
							const auto value = mapping.Inverse({ lower[0] + i / 32.0f, lower[1] + j / 32.0f });
							for (unsigned axis = 0; axis < 2; ++axis)
								Check(value[axis] >= bounds.first[axis] - 1e-6f && value[axis] <= bounds.second[axis] + 1e-6f);
							++checks;
						}
				}
		}
	Check(!BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 100, 50, true, false).active);
	Check(!BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 100, 50, false, true).active);
	Check(!BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 100, 101, false, false).active);
	Check(!BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 101, 20, false, false).active);
	Check(!BuildMonoCompressionPlan({ 256, 256 }, { 16384, 16384 }, 100, 100, false, false).active);
	for (const auto source : { UpscalingDLSS::Extent{ 256, 256 }, UpscalingDLSS::Extent{ 1919, 1079 }, UpscalingDLSS::Extent{ 5760, 1080 } }) {
		for (unsigned compression : { 1u, 20u, 50u, 100u }) {
			auto previous = BuildMonoCompressionPlan(source, source, 100, compression, false, false).model;
			for (unsigned percent = 99; percent >= 30; --percent) {
				const auto projected = BuildMonoCompressionPlan(source, source, percent, compression, false, false);
				Check(projected.model.width <= previous.width && projected.model.height <= previous.height);
				if (projected.active)
					Check(projected.maximumFootprint[0] <= 32 && projected.maximumFootprint[1] <= 32);
				previous = projected.model;
			}
		}
	}
	for (const auto guides : { UpscalingDLSS::Extent{ 960, 540 }, UpscalingDLSS::Extent{ 1279, 719 },
			 UpscalingDLSS::Extent{ 2880, 1620 }, UpscalingDLSS::Extent{ 960, 1080 }, UpscalingDLSS::Extent{ 1920, 540 } }) {
		for (unsigned compression : { 1u, 50u, 100u }) {
			auto previous = BuildMonoCompressionPlan({ 1919, 1079 }, guides, 100, compression, false, false).model;
			for (unsigned percent = 99; percent >= 30; --percent) {
				const auto plan = BuildMonoCompressionPlan({ 1919, 1079 }, guides, percent, compression, false, false);
				Check(plan.model.width <= previous.width && plan.model.height <= previous.height);
				previous = plan.model;
				if (!plan.active)
					continue;
				const auto nativeGuides = BuildMonoGuideExtent({ 1919, 1079 }, guides, plan.model);
				const std::array input{ guides.width, guides.height }, grid{ nativeGuides.width, nativeGuides.height };
				for (unsigned axis = 0; axis < 2; ++axis) {
					const float guideFootprint = input[axis] * (plan.mapping.work[axis] / grid[axis] + (1.0f - plan.mapping.edgeSlope) * plan.mapping.work[1u - axis] / grid[1u - axis]) / plan.mapping.edgeSlope;
					Check(guideFootprint <= plan.maximumFootprint[axis] + 1e-5f && plan.maximumFootprint[axis] <= kPeripheralMaximumFootprint);
				}
			}
		}
	}
	Check(!BuildMonoCompressionPlan({ UINT32_MAX, 1080 }, { 1280, 720 }, 100, 50, false, false).active);
	Check(!BuildMonoCompressionPlan({ 1920, 1080 }, { UINT32_MAX, 720 }, 100, 50, false, false).active);
	MonoSettings settings;
	settings.peripheralCompressionPercent = UINT32_MAX;
	Sanitize(settings);
	Check(settings.peripheralCompressionPercent == 100);
	std::cout << checks << " rectangular mapping, footprint and round-trip checks passed\n";
}
