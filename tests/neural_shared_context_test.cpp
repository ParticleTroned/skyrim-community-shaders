#include "Features/Upscaling/NeuralRendering/SharedContextPolicy.h"

#include <array>
#include <iostream>
#include <utility>
#include <vector>

#define CHECK(condition)                                                                          \
	do {                                                                                          \
		if (!(condition)) {                                                                       \
			std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #condition << '\n'; \
			return __LINE__;                                                                      \
		}                                                                                         \
	} while (false)

namespace
{
	int TestScatter(const NeuralRendering::CharacterOutputPlan& output, UpscalingDLSS::Extent capacity)
	{
		using namespace NeuralRendering;
		using SharedContext::Mode;
		using SharedContext::Settings;
		// Coordinate-coded sources expose translation errors; untouched gaps and halos retain zero.
		auto scatterOutput = output;
		scatterOutput.regions.regions[0] = { 256, 384, 128, 128 };
		scatterOutput.regions.regions[1] = { 640, 576, 128, 128 };
		scatterOutput.enclosure = UnionCharacterComputeSubrect(scatterOutput.regions.regions[0], scatterOutput.regions.regions[1]);
		for (unsigned i = 0; i < scatterOutput.regions.count; ++i) {
			const auto& owned = scatterOutput.regions.regions[i];
			scatterOutput.regions.roi[i] = BuildRoiDescriptor(owned, owned, capacity, true);
		}
		for (const auto settings : { Settings{ Mode::Enclosing, 0 }, Settings{ Mode::Enclosing, 64 }, Settings{ Mode::FullEye, 256 } }) {
			const auto plan = SharedContext::Build(scatterOutput,
				std::span(scatterOutput.regions.roi).first(scatterOutput.regions.count), capacity,
				settings, RenderingMode::ReducedResolution, false, true);
			CHECK(plan && SharedContext::OutputCount(*plan) == 2);
			const auto& context = plan->inferenceContext;
			std::vector<std::uint32_t> source(capacity.width * capacity.height), localSource(context.Area());
			std::vector<std::uint32_t> rawResult(source.size()), localResult(source.size());
			for (std::size_t pixel = 0; pixel < source.size(); ++pixel)
				source[pixel] = static_cast<std::uint32_t>(pixel + 1);
			for (unsigned y = 0; y < context.height; ++y)
				for (unsigned x = 0; x < context.width; ++x)
					localSource[y * context.width + x] = source[(y + context.baseY) * capacity.width + x + context.baseX];
			for (unsigned owner = 0; owner < SharedContext::OutputCount(*plan); ++owner) {
				const auto copy = SharedContext::CopyDomain(*plan, owner);
				CHECK(copy && copy->output == scatterOutput.regions.regions[owner]);
				CHECK(copy->contextLocal.Fits(context.width, context.height));
				for (unsigned y = 0; y < copy->output.height; ++y) {
					for (unsigned x = 0; x < copy->output.width; ++x) {
						const auto destination = (copy->output.baseY + y) * capacity.width + copy->output.baseX + x;
						rawResult[destination] = source[destination];
						localResult[destination] = localSource[(copy->contextLocal.baseY + y) * context.width + copy->contextLocal.baseX + x];
					}
				}
			}
			CHECK(rawResult == localResult);
			for (unsigned y = 0; y < capacity.height; ++y) {
				for (unsigned x = 0; x < capacity.width; ++x) {
					const ComputeSubrect pixel{ x, y, 1, 1 };
					const bool owned = ContainsComputeSubrect(scatterOutput.regions.regions[0], pixel) ||
					                   ContainsComputeSubrect(scatterOutput.regions.regions[1], pixel);
					CHECK(rawResult[y * capacity.width + x] == (owned ? source[y * capacity.width + x] : 0u));
				}
			}
			CHECK(!SharedContext::CopyDomain(*plan, SharedContext::OutputCount(*plan)));
			CHECK(!SharedContext::CopyDomain(*plan, UINT32_MAX));
			auto invalid = *plan;
			invalid.inferenceContext = { 0, 0, 1, 1 };
			CHECK(!SharedContext::CopyDomain(invalid, 0));
			invalid = *plan;
			invalid.output.regions.count = kEnabledRegionsPerEye + 1;
			CHECK(SharedContext::OutputCount(invalid) == 0 && !SharedContext::CopyDomain(invalid, 0));
		}
		return 0;
	}
}

int main()
{
	using namespace NeuralRendering;
	using SharedContext::Mode;
	using SharedContext::Settings;
	constexpr UpscalingDLSS::Extent capacity{ 1008, 1120 };
	constexpr ComputeSubrect left{ 192, 512, 128, 128 }, right{ 672, 512, 128, 128 };
	CharacterOutputPlan output{ UnionCharacterComputeSubrect(left, right), {} };
	output.regions.count = 2;
	output.regions.regions[0] = left;
	output.regions.regions[1] = right;
	output.regions.roi[0] = BuildRoiDescriptor(left, left, capacity, true);
	output.regions.roi[1] = BuildRoiDescriptor(right, right, capacity, true);
	output.regions.historyKeys[0] = 19;
	output.regions.historyKeys[1] = 23;
	output.regions.clusterIdentities[0] = 101;
	output.regions.regionSlots[0] = 3;
	output.regions.regionSlots[1] = 6;
	output.regions.spatialTracks = true;
	output.regions.identityConfident[1] = true;
	const auto before = output;
	const auto build = [&](const CharacterOutputPlan& source, Settings settings = { Mode::Enclosing, 256 }) {
		return SharedContext::Build(source, std::span(source.regions.roi).first(source.regions.count),
			capacity, settings, RenderingMode::ReducedResolution, false, true);
	};
	const auto expanded = build(output);
	CHECK(expanded && expanded->inferenceContext == (ComputeSubrect{ 0, 256, 1008, 640 }));
	CHECK(expanded->samplingSupport == output.enclosure);
	CHECK(GetRoiDescriptorViolation(BuildRoiDescriptor(expanded->samplingSupport, expanded->inferenceContext, capacity, false),
		expanded->inferenceContext, capacity)
			.empty());
	CHECK(expanded->output.enclosure == before.enclosure && expanded->output.regions == before.regions);
	CHECK(output.enclosure == before.enclosure && output.regions == before.regions);
	CHECK(CharacterRegionOutputPixels(expanded->output.regions, expanded->output.enclosure, 1008, 1120) == 32768);
	CHECK(expanded->inferenceContext.Area() > CharacterRegionOutputPixels(output.regions, output.enclosure, 1008, 1120));
	for (const auto [halo, expected] : std::array{
			 std::pair{ 0u, ComputeSubrect{ 192, 512, 640, 128 } },
			 std::pair{ 64u, ComputeSubrect{ 128, 448, 768, 256 } },
			 std::pair{ 128u, ComputeSubrect{ 64, 384, 896, 384 } },
			 std::pair{ 256u, ComputeSubrect{ 0, 256, 1008, 640 } } }) {
		const auto plan = build(output, { Mode::Enclosing, halo });
		CHECK(plan && plan->inferenceContext == expected);
		CHECK(plan->output.regions == before.regions);
	}
	const auto full = build(output, { Mode::FullEye, 0 });
	CHECK(full && full->inferenceContext == (ComputeSubrect{ 0, 0, 1008, 1120 }));
	CHECK(full->output.enclosure == before.enclosure && full->output.regions == before.regions);
	CHECK(!build(output, {}));
	CHECK(!build(output, { Mode::Count, 256 }));
	CHECK(!build(output, { static_cast<Mode>(UINT32_MAX), 256 }));
	for (const auto halo : { 1u, 63u, 65u, 129u, 255u, 257u, UINT32_MAX })
		CHECK(!build(output, { Mode::Enclosing, halo }));
	for (const auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated, static_cast<RenderingMode>(UINT32_MAX) })
		CHECK(!SharedContext::Build(output, std::span(output.regions.roi).first(2), capacity,
			{ Mode::Enclosing, 256 }, mode, false, true));
	CHECK(!SharedContext::Build(output, std::span(output.regions.roi).first(2), capacity,
		{ Mode::Enclosing, 256 }, RenderingMode::ReducedResolution, true, true));
	CHECK(!SharedContext::Build(output, std::span(output.regions.roi).first(2), capacity,
		{ Mode::Enclosing, 256 }, RenderingMode::ReducedResolution, false, false));
	CHECK(!SharedContext::Build(output, {}, capacity,
		{ Mode::Enclosing, 256 }, RenderingMode::ReducedResolution, false, true));
	for (const auto badCapacity : { UpscalingDLSS::Extent{ 0, 1120 }, UpscalingDLSS::Extent{ 1008, 0 },
			 UpscalingDLSS::Extent{ 16385, 1120 }, UpscalingDLSS::Extent{ 1008, 16385 } })
		CHECK(!SharedContext::Build(output, std::span(output.regions.roi).first(2), badCapacity,
			{ Mode::Enclosing, 256 }, RenderingMode::ReducedResolution, false, true));

	// Invalid ownership and descriptor provenance cannot be hidden by a larger context.
	for (unsigned change = 0; change < 10; ++change) {
		auto invalid = output;
		switch (change) {
		case 0:
			invalid.regions.regions[1] = { 200, 512, 128, 128 };
			break;
		case 1:
			invalid.regions.regions[1].baseX = UINT32_MAX;
			break;
		case 2:
			invalid.enclosure = left;
			break;
		case 3:
			invalid.regions.roi[0].ownedOutput = right;
			break;
		case 4:
			invalid.regions.roi[0].inferenceContext = right;
			break;
		case 5:
			invalid.regions.roi[0].samplingSupport = right;
			break;
		case 6:
			invalid.regions.roi[0].compactSource = left;
			break;
		case 7:
			invalid.regions.roi[0].allocationCapacity.width = 1009;
			break;
		case 8:
			invalid.regions.roi[0].temporalEnvelope = right;
			break;
		case 9:
			invalid.regions.roi[0].samplingSupport.reset();
			break;
		}
		CHECK(!build(invalid));
	}
	auto tightSupports = output;
	tightSupports.regions.roi[0].samplingSupport = ComputeSubrect{ 224, 544, 32, 32 };
	tightSupports.regions.roi[1].samplingSupport = ComputeSubrect{ 704, 576, 32, 32 };
	const auto proven = build(tightSupports);
	CHECK(proven && proven->samplingSupport == (ComputeSubrect{ 224, 544, 512, 64 }));
	CHECK(proven->samplingSupport != proven->inferenceContext && proven->output.regions == tightSupports.regions);
	CHECK(GetRoiDescriptorViolation(BuildRoiDescriptor(proven->samplingSupport, proven->inferenceContext, capacity, false),
		proven->inferenceContext, capacity)
			.empty());
	auto excessive = output;
	excessive.regions.count = kEnabledRegionsPerEye + 1;
	CHECK(!SharedContext::Build(excessive, output.regions.roi, capacity,
		{ Mode::FullEye, 256 }, RenderingMode::ReducedResolution, false, true));

	// Single-region fallback and edge clipping retain the same output coordinates.
	for (const auto owned : { ComputeSubrect{ 3, 7, 128, 128 }, ComputeSubrect{ 879, 991, 129, 129 },
			 ComputeSubrect{ 0, 0, 1008, 1120 } }) {
		const CharacterOutputPlan single{ owned, {} };
		const std::array descriptors{ BuildRoiDescriptor(owned, owned, capacity, true) };
		for (const auto halo : { 0u, 64u, 128u, 256u }) {
			const auto plan = SharedContext::Build(single, descriptors, capacity,
				{ Mode::Enclosing, halo }, RenderingMode::ReducedResolution, false, true);
			CHECK(plan && ContainsComputeSubrect(plan->inferenceContext, owned));
			CHECK(plan->inferenceContext.Fits(capacity.width, capacity.height));
			CHECK(plan->inferenceContext.baseX % 64 == 0 && plan->inferenceContext.baseY % 64 == 0);
			CHECK(plan->output.enclosure == owned && plan->output.regions == single.regions);
			CHECK(plan->inferenceContext.baseX + plan->inferenceContext.width == capacity.width ||
				  (plan->inferenceContext.baseX + plan->inferenceContext.width) % 64 == 0);
			CHECK(plan->inferenceContext.baseY + plan->inferenceContext.height == capacity.height ||
				  (plan->inferenceContext.baseY + plan->inferenceContext.height) % 64 == 0);
		}
	}
	const ComputeSubrect sliver{ 0, 0, 1, 1 };
	const CharacterOutputPlan tiny{ sliver, {} };
	const std::array tinyDescriptors{ BuildRoiDescriptor(sliver, sliver, capacity, true) };
	CHECK(!SharedContext::Build(tiny, tinyDescriptors, capacity,
		{ Mode::Enclosing, 0 }, RenderingMode::ReducedResolution, false, true));
	CHECK(SharedContext::Build(tiny, tinyDescriptors, capacity,
		{ Mode::Enclosing, 64 }, RenderingMode::ReducedResolution, false, true));

	// Touching half-open outputs remain disjoint through the maximum admitted region count.
	for (const auto count : { 1u, 2u, 4u, kEnabledRegionsPerEye }) {
		CharacterOutputPlan adjacent{ { 0, 0, 1008, 1120 }, {} };
		adjacent.regions.count = count;
		for (unsigned i = 0; i < count; ++i) {
			const ComputeSubrect owned{ i * 64, 128, 64, 128 };
			adjacent.regions.regions[i] = owned;
			adjacent.regions.roi[i] = BuildRoiDescriptor(owned, owned, capacity, true);
		}
		const auto plan = build(adjacent);
		CHECK(plan && plan->output.regions == adjacent.regions);
		CHECK(CharacterRegionOutputPixels(plan->output.regions, plan->output.enclosure, 1008, 1120) == count * 64u * 128u);
	}

	CHECK(TestScatter(output, capacity) == 0);
	const auto singleCopyPlan = SharedContext::Build(tiny, tinyDescriptors, capacity,
		{ Mode::FullEye, 256 }, RenderingMode::ReducedResolution, false, true);
	CHECK(singleCopyPlan && SharedContext::OutputCount(*singleCopyPlan) == 1);
	const auto singleCopy = SharedContext::CopyDomain(*singleCopyPlan, 0);
	CHECK(singleCopy && singleCopy->output == sliver && singleCopy->contextLocal == sliver);
	CHECK(!SharedContext::CopyDomain(*singleCopyPlan, 1));
	CHECK(SharedContext::OutputCount({}) == 0 && !SharedContext::CopyDomain({}, 0));
	static_assert(SharedContext::Valid({}));
	static_assert(!SharedContext::Valid({ Mode::Count, 256 }));
	return 0;
}
