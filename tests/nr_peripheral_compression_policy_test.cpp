#include "Features/Upscaling/NeuralRendering/PeripheralCompressionPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
#	include "Features/Upscaling/NeuralRendering/FaceDetailPolicy.h"
#endif

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace NeuralRendering;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void RequireNonIncreasingGrid(const PeripheralCompressionPlan& next, const PeripheralCompressionPlan& previous)
	{
		Require(next.model.width <= previous.model.width && next.model.height <= previous.model.height &&
					next.guides.width <= previous.guides.width && next.guides.height <= previous.guides.height,
			"Lower NR resolution increased native texture dimensions across compression fallback");
	}
	PeripheralCompressionPlan Plan(UpscalingDLSS::Extent source, std::uint32_t percent = 100,
		UpscalingDLSS::Extent guides = {}, PeripheralCompressionSupport support = { .requested = true })
	{
		if (guides == UpscalingDLSS::Extent{})
			guides = source;
		const auto crop = UpscalingDLSS::ViewportCrop::Identity(guides.width, guides.height, source.width, source.height);
		const auto baseline = BuildModelResolutionGeometry(crop, source, { 0, 0, source.width, source.height }, percent, guides);
		Require(baseline.has_value(), "Invalid test geometry");
		return BuildPeripheralCompressionPlan(*baseline, guides, crop, percent, support);
	}
}

int main()
{
	try {
		const auto full = Plan({ 3840, 2160 });
		Require(full.Active() && full.model == UpscalingDLSS::Extent{ 3456, 1944 }, "Twenty percent did not retain the 80/90 dimensions");
		Require(full.candidatePixels * 100 == full.baselinePixels * 81, "Unexpected rectangular model area");
		Require(Plan({ 3840, 2160 }, 100, {}, { .requested = true, .compressionPercent = 0 }).reason == PeripheralCompressionReason::Disabled, "Zero did not bypass compression");
		const auto maximum = Plan({ 3840, 2160 }, 100, {}, { .requested = true, .compressionPercent = 100 });
		Require(maximum.Active() && maximum.mapping.protectedFraction == 0.0f && maximum.model == UpscalingDLSS::Extent{ 1920, 1080 }, "Maximum compression retained a protected centre");
		for (const auto source : { UpscalingDLSS::Extent{ 4096, 2048 }, { 2048, 4096 }, { 4096, 512 }, { 1921, 1081 } }) {
			for (const auto guides : { source, UpscalingDLSS::Extent{ source.width / 2u, source.height / 2u } }) {
				for (const auto mask : { PeripheralMask{}, PeripheralMask{ { 0.47f, 0.53f }, { 0.43f, 0.36f }, true },
						 PeripheralMask{ { 0.5f, 0.5f }, { 0.1f, 0.5f }, true } }) {
					for (const auto compression : { 1u, 20u, 50u, 100u }) {
						auto previous = Plan(source, 100, guides, { .requested = true, .compressionPercent = compression, .mask = mask });
						for (auto percent = 99u; percent >= kMinimumModelResolutionPercent; --percent) {
							const auto next = Plan(source, percent, guides, { .requested = true, .compressionPercent = compression, .mask = mask });
							RequireNonIncreasingGrid(next, previous);
							if (next.Active())
								Require(next.maximumFootprint[0] <= kPeripheralMaximumFootprint && next.maximumFootprint[1] <= kPeripheralMaximumFootprint,
									"Compression density floor exceeded the shader footprint limit");
							previous = next;
						}
					}
				}
			}
		}
		Require(Plan({ 1920, 1080 }, 100, {}, { .requested = true, .compressionPercent = 101 }).reason == PeripheralCompressionReason::InvalidGeometry, "Out-of-range slider reached allocation");
		for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f }) {
			for (unsigned field = 0; field < 4; ++field) {
				PeripheralMask mask{ .active = true };
				(field < 2 ? mask.center : mask.radii)[field % 2] = invalid;
				const auto rejected = Plan({ 1920, 1080 }, 50, {}, { .requested = true, .mask = mask });
				const auto retained = rejected;
				Require(rejected.reason == PeripheralCompressionReason::InvalidGeometry && rejected == retained,
					"Invalid mask poisoned the retained ordinary fallback plan");
				mask.active = false;
				const auto rectangular = Plan({ 1920, 1080 }, 50, {}, { .requested = true, .mask = mask });
				Require(rectangular.Active() && rectangular == Plan({ 1920, 1080 }, 50), "Inactive mask changed rectangular compression");
			}
		}
		for (const auto size : { UpscalingDLSS::Extent{ 4096, 2048 }, { 1921, 1081 }, { 13, 7 }, { 2, 2 } }) {
			for (const auto compression : { 1u, 20u, 100u }) {
				std::array<PeripheralCompressionPlan, 2> previous{};
				for (auto percent = 100u; percent >= kMinimumModelResolutionPercent; --percent) {
					std::array pair{
						Plan(size, percent, {}, { .requested = true, .compressionPercent = compression }),
						Plan({ 2049, 2049 }, percent, {}, { .requested = true, .compressionPercent = compression, .mask = { { 0.5f, 0.5f }, { 0.1f, 0.5f }, true } })
					};
					ReconcilePeripheralCompression(pair);
					for (unsigned eye = 0; eye < 2; ++eye) {
						if (percent < 100)
							RequireNonIncreasingGrid(pair[eye], previous[eye]);
					}
					previous = pair;
				}
			}
		}
		std::array<PeripheralCompressionPlan, 2> roundingPrevious{};
		for (auto percent : { 40u, 39u }) {
			std::array roundingPair{
				Plan({ 307, 202 }, percent, {}, { .requested = true, .compressionPercent = 61 }),
				Plan({ 16282, 3519 }, percent, { 8141, 1759 }, { .requested = true, .compressionPercent = 61 })
			};
			ReconcilePeripheralCompression(roundingPair);
			Require(roundingPair[0].Active() && roundingPair[1].Active(), "An ordinary-sized eye discarded its peer's compression savings");
			if (percent == 39) {
				Require(roundingPair[0].candidatePixels == roundingPair[0].baselinePixels &&
							roundingPair[1].candidatePixels < roundingPair[1].baselinePixels,
					"Rounding fixture lost its asymmetric savings boundary");
				for (unsigned eye = 0; eye < 2; ++eye)
					RequireNonIncreasingGrid(roundingPair[eye], roundingPrevious[eye]);
			}
			roundingPrevious = roundingPair;
			ReconcilePeripheralCompression(roundingPair);
			Require(roundingPair == roundingPrevious, "Stereo reconciliation changed an already reconciled plan");
		}
		std::array noSavingsPair{ Plan({ 2, 2 }, 100, {}, { .requested = true, .compressionPercent = 1 }),
			Plan({ 2, 2 }, 100, {}, { .requested = true, .compressionPercent = 1 }) };
		ReconcilePeripheralCompression(noSavingsPair);
		Require(!noSavingsPair[0].Active() && !noSavingsPair[1].Active(), "A pair without pixel savings retained compression passes");
		float maximumPixelError = 0.0f;
		for (bool shaped : { false, true }) {
			for (auto compression : { 1u, 20u, 50u, 100u }) {
				for (auto percent : { 30u, 50u, 99u, 100u }) {
					const PeripheralMask mask{ { 0.47f, 0.53f }, { 0.43f, 0.36f }, shaped };
					const auto plan = Plan({ 1921, 1081 }, percent, {}, { .requested = true, .compressionPercent = compression, .mask = mask });
					Require(plan.Active() && plan.candidatePixels < plan.baselinePixels, "Valid slider/FOV combination did not reduce work");
					const auto& mapping = plan.mapping;
					for (int y = -4; y <= 24; ++y) {
						for (int x = -4; x <= 24; ++x) {
							const PeripheralMapping::Point source{ float(x) / 20.0f, float(y) / 20.0f };
							const auto work = mapping.Forward(source), restored = mapping.Inverse(work);
							for (unsigned axis = 0; axis < 2; ++axis) {
								maximumPixelError = std::max(maximumPixelError, std::abs(restored[axis] - source[axis]) * 1921.0f);
								Require(std::isfinite(restored[axis]) && std::abs(restored[axis] - source[axis]) * 1921.0f < 0.025f, "Mask mapping lost invertibility");
							}
							if (mapping.Distance({ source[0] - mapping.center[0], source[1] - mapping.center[1] }) <= mapping.protectedFraction) {
								for (unsigned axis = 0; axis < 2; ++axis)
									Require(std::abs(mapping.origin[axis] + work[axis] * mapping.work[axis] - source[axis]) < 2e-7f, "Protected centre changed sampling density");
							}
						}
					}
					for (unsigned y = 0; y < 12; ++y) {
						for (unsigned x = 0; x < 20; ++x) {
							const PeripheralMapping::Point lower{ float(x) / 20.0f, float(y) / 12.0f }, upper{ float(x + 1) / 20.0f, float(y + 1) / 12.0f };
							const auto bounds = mapping.SourceBounds(lower, upper);
							for (unsigned sy = 0; sy <= 4; ++sy) {
								for (unsigned sx = 0; sx <= 4; ++sx) {
									const auto sample = mapping.Inverse({ lower[0] + (upper[0] - lower[0]) * float(sx) / 4.0f,
										lower[1] + (upper[1] - lower[1]) * float(sy) / 4.0f });
									for (unsigned axis = 0; axis < 2; ++axis)
										Require(sample[axis] >= bounds.first[axis] - 1e-6f && sample[axis] <= bounds.second[axis] + 1e-6f, "Inverse cell bounds missed a cross-axis extremum");
								}
							}
						}
					}
				}
			}
		}
		for (const auto support : { PeripheralCompressionSupport{}, PeripheralCompressionSupport{ .requested = true, .identity = true } }) {
			const auto off = Plan({ 1920, 1080 }, 50, {}, support);
			Require(!off.Active() && off.mapping == PeripheralMapping{} && off.model == off.ordinaryModel, "Off/identity altered geometry");
		}
		for (auto support : { PeripheralCompressionSupport{ .requested = true, .actorRoute = true },
				 PeripheralCompressionSupport{ .requested = true, .restricted = true }, PeripheralCompressionSupport{ .requested = true, .compactInputs = true } })
			Require(!Plan({ 1920, 1080 }, 50, {}, support).Active(), "Unsupported source admitted");
		const UpscalingDLSS::ViewportCrop crop{ { 2400, 1400 }, { 300, 100, 2100, 1300 }, { 2400, 1400 }, { 300, 100, 2100, 1300 } };
		const auto baseline = BuildModelResolutionGeometry(crop, { 1800, 1200 }, { 0, 0, 1800, 1200 }, 50);
		Require(baseline.has_value(), "Cropped fixture invalid");
		const PeripheralMask mask{ { 0.47f, 0.52f }, { 0.4f, 0.38f }, true };
		const auto clipped = BuildPeripheralCompressionPlan(*baseline, { 1800, 1200 }, crop, 50, { .requested = true, .mask = mask });
		Require(clipped.Active() && clipped.sourceCrop == crop && clipped.ownedOutput == baseline->sourceRegion, "FOV compression changed crop ownership");
		auto excludedCenter = mask;
		excludedCenter.center[0] = 0.01f;
		Require(BuildPeripheralCompressionPlan(*baseline, { 1800, 1200 }, crop, 50, { .requested = true, .mask = excludedCenter }).reason == PeripheralCompressionReason::InvalidGeometry,
			"A crop excluding its declared mask centre reached dispatch");
		Require(std::abs(clipped.mapping.center[0] - (0.47f * 2400.0f - 300.0f) / 1800.0f) < 1e-6f, "FOV centre was treated as crop-local");
		Require(BuildPeripheralCompressionPlan(*baseline, { 1800, 1200 }, crop, 50, { .requested = true }).reason == PeripheralCompressionReason::RestrictedDomain, "Unknown crop guessed a mask");
		std::array pair{ full, Plan({ 1920, 1080 }, 50, {}, { .requested = true, .actorRoute = true }) };
		ReconcilePeripheralCompression(pair);
		Require(!pair[0].Active() && pair[0].reason == PeripheralCompressionReason::PairUnsupported && !pair[1].Active(), "Eye strategies diverged");
		auto history = ModelResolutionHistory{};
		history.peripheralMapping = clipped.mapping;
		history.peripheralMappingVersion = PeripheralMapping::kVersion;
		Require(history != ModelResolutionHistory{}, "Mapping absent from temporal identity");

#ifdef DEVBENCH_BRIDGE_ENABLED
		FaceDetailRegions faces;
		faces.current = true;
		faces.count = 1;
		faces.bounds[0] = { 0.3f, 0.25f, 0.6f, 0.65f };
		for (const auto faceMask : { PeripheralMask{}, PeripheralMask{ { 0.47f, 0.53f }, { 0.43f, 0.36f }, true } }) {
			const auto original = Plan({ 1920, 1080 }, 90, {}, { .requested = true, .compressionPercent = 100, .mask = faceMask });
			auto protectedPlan = original;
			ApplyFaceDetailProtection(protectedPlan, faces, 100, 0.5f);
			Require(protectedPlan.mapping.faceDetail.enabled != 0, "Face protection did not redistribute a valid partial face");
			Require(protectedPlan.model == original.model && protectedPlan.guides == original.guides &&
						protectedPlan.candidatePixels == original.candidatePixels,
				"Face protection increased the model budget");
			const auto modelBounds = FaceDetailModelBounds(original.mapping, faces.bounds[0]);
			const auto& warp = protectedPlan.mapping.faceDetail;
			for (unsigned axis = 0; axis < 2; ++axis) {
				const float before = modelBounds[axis + 2] - modelBounds[axis];
				Require(warp.Forward(modelBounds[axis + 2], axis) - warp.Forward(modelBounds[axis], axis) > before,
					"Face area received no extra samples");
				for (unsigned bin = 0; bin < kFaceDetailBins; ++bin)
					Require((warp.Knot(axis, bin + 1) - warp.Knot(axis, bin)) * kFaceDetailBins >= 0.4999f,
						"Background sampling density exceeded its fixed-budget limit");
			}
			for (unsigned y = 0; y <= 20; ++y)
				for (unsigned x = 0; x <= 20; ++x) {
					const PeripheralMapping::Point source{ float(x) / 20, float(y) / 20 };
					const auto target = protectedPlan.mapping.Forward(source);
					const auto restored = protectedPlan.mapping.Inverse(target);
					Require(std::abs(restored[0] - source[0]) < 1e-5f && std::abs(restored[1] - source[1]) < 1e-5f,
						"Adaptive mapping changed coordinate ownership");
				}
			for (unsigned y = 0; y < 12; ++y)
				for (unsigned x = 0; x < 12; ++x) {
					const auto [lower, upper] = protectedPlan.mapping.SourceBounds({ float(x) / 12, float(y) / 12 },
						{ float(x + 1) / 12, float(y + 1) / 12 });
					for (unsigned sample = 0; sample < 9; ++sample) {
						const auto source = protectedPlan.mapping.Inverse({ (x + (sample % 3) * 0.5f) / 12, (y + (sample / 3) * 0.5f) / 12 });
						Require(source[0] >= lower[0] - 1e-5f && source[0] <= upper[0] + 1e-5f &&
									source[1] >= lower[1] - 1e-5f && source[1] <= upper[1] + 1e-5f,
							"Adaptive footprint excluded a source sample");
					}
				}
			for (unsigned condition = 0; condition < 10; ++condition) {
				auto rejected = original;
				auto region = faces;
				if (condition == 0)
					region.current = false;
				if (condition == 1)
					region.bounds[0] = { 0, 0, 1, 1 };
				if (condition == 2)
					region.bounds[0][0] = std::numeric_limits<float>::quiet_NaN();
				if (condition == 5)
					rejected.maximumFootprint[0] = kPeripheralMaximumFootprint;
				if (condition == 6)
					rejected.maximumFootprint[0] = std::numeric_limits<float>::infinity();
				if (condition == 7)
					region.bounds[0][0] = -0.1f;
				if (condition == 8)
					region.bounds[0][2] = std::numeric_limits<float>::max();
				if (condition == 9)
					region.count = kFaceDetailMaximumActors + 1u;
				ApplyFaceDetailProtection(rejected, region, condition == 3 ? 0u : 100u, condition == 4 ? 1.0f : 0.5f);
				Require(rejected.mapping == original.mapping && rejected.model == original.model,
					"Unavailable, full-screen, over-budget or disabled protection changed the baseline");
			}
		}
		PeripheralCompressionExperiment lease;
		using Mode = PeripheralCompressionOverride;
		Require(!lease.Configure(Mode::CompressedExperiment, 601, 1), "Unbounded lease accepted");
		Require(lease.Configure(Mode::CompressedExperiment, 2, 1), "Comparison rejected");
		Require(lease.Resolve(10, 1) == std::pair{ Mode::CompressedExperiment, true }, "Comparison did not invalidate history");
		Require(lease.Resolve(10, 1) == std::pair{ Mode::CompressedExperiment, false }, "Second eye consumed lease frame");
		Require(!lease.Configure(Mode::Ordinary, 2, 1), "Stale comparison revision accepted");
		Require(lease.Resolve(12, 1) == std::pair{ Mode::Auto, true }, "Expired lease persisted");
		Require(lease.Configure(Mode::Identity, 5, lease.Status().revision), "Identity lease rejected");
		(void)lease.Resolve(12, 1);
		Require(lease.Resolve(13, 2) == std::pair{ Mode::Auto, true }, "Generation change preserved override");
		using Json = nlohmann::json;
		Json request{ { "action", "nr_peripheral_compare" }, { "peripheralOverride", "compressed_experiment" },
			{ "peripheralOverrideFrames", 2 }, { "expectedPeripheralOverrideRevision", 1 } };
		Require(ParsePeripheralComparison(request).has_value(), "Comparison parser rejected valid lease");
		for (const auto* field : { "action", "peripheralOverride", "peripheralOverrideFrames", "expectedPeripheralOverrideRevision" }) {
			auto missing = request;
			missing.erase(field);
			Require(!ParsePeripheralComparison(missing), "Missing comparison field accepted");
			for (auto bad : { Json(nullptr), Json(true), Json(-1), Json(1.5), Json::array(), Json::object() }) {
				auto invalid = request;
				invalid[field] = bad;
				Require(!ParsePeripheralComparison(invalid), "Malformed comparison field accepted");
			}
		}
		request["peripheralOverride"] = "identity";
		Require(ParsePeripheralComparison(request).has_value(), "Identity parser rejected");
		request["peripheralOverrideFrames"] = 0;
		Require(!ParsePeripheralComparison(request), "Zero experiment lease accepted");
		request["peripheralOverride"] = "auto";
		Require(ParsePeripheralComparison(request).has_value(), "Auto cancellation rejected");
#endif
		std::cout << "PASS: peripheral geometry, pair policy and endpoint motion; maximum fp32 roundtrip error " << maximumPixelError << " source pixels\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
