#include "Features/Upscaling/NeuralRendering/PeripheralCompressionPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
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
		Require(full.Active() && full.model == UpscalingDLSS::Extent{ 3456, 1944 } && full.guides == full.model,
			"Fixed profile did not reduce actual native dimensions");
		Require(full.candidatePixels * 100 == full.baselinePixels * 81, "Unexpected model area");
		Require(std::abs(full.mapping.edgeSlope[0] - 0.25f) < 1e-6f && std::abs(full.maximumFootprint[0] - 4.0f) < 1e-5f,
			"Outer density or filter bound is incorrect");
		float maximumPixelError = 0.0f;
		for (auto size : { UpscalingDLSS::Extent{ 1, 1 }, { 7, 19 }, { 639, 359 }, { 1920, 1080 }, { 3841, 2161 }, { 16384, 16384 } }) {
			for (auto percent : { 30u, 40u, 50u, 67u, 75u, 100u }) {
				const auto plan = Plan(size, percent);
				const auto ordinary = BuildModelResolutionExtent(size.width, size.height, percent);
				Require(plan.ordinaryModel == ordinary && plan.uniformPercent == percent, "Uniform scale changed");
				if (!plan.Active()) {
					Require(plan.reason == PeripheralCompressionReason::InsufficientSavings, "Unexpected refusal");
					continue;
				}
				Require(plan.model.width <= ordinary.width && plan.model.height <= ordinary.height && plan.guides == plan.model,
					"Compression expanded work or broke matching grids");
				for (std::uint32_t axis = 0; axis < 2; ++axis) {
					const auto extent = axis == 0 ? size.width : size.height;
					float previous = -std::numeric_limits<float>::infinity();
					for (int index = -4096; index <= 8192; ++index) {
						const float u = static_cast<float>(index) / 4096.0f;
						const float q = plan.mapping.Forward(u, axis);
						Require(std::isfinite(q) && q > previous, "Mapping is not finite and strictly monotone");
						previous = q;
						const float restored = plan.mapping.Inverse(q, axis);
						maximumPixelError = std::max(maximumPixelError, std::abs(restored - u) * extent);
						Require(std::abs(restored - u) * extent < 0.025f, "Roundtrip exceeded 0.025 source pixels");
					}
					for (float u : { 0.2f, 0.35f, 0.5f, 0.65f, 0.8f }) {
						const float expected = 0.5f + (u - 0.5f) / plan.mapping.work[axis];
						Require(std::abs(plan.mapping.Forward(u, axis) - expected) < 2e-7f, "Protected density differs from ordinary NR");
					}
					const auto work = axis == 0 ? plan.model.width : plan.model.height;
					for (std::uint32_t q = 0; q < work; ++q) {
						const float lower = plan.mapping.Inverse(static_cast<float>(q) / work, axis);
						const float upper = plan.mapping.Inverse(static_cast<float>(q + 1) / work, axis);
						Require(lower >= -1e-6f && upper <= 1.0f + 1e-6f && upper > lower &&
									(upper - lower) * extent <= plan.maximumFootprint[axis] + 0.015f,
							"Inverse work cell escaped source bounds or its admitted footprint");
					}
				}
				for (const auto support : { PeripheralCompressionSupport{}, PeripheralCompressionSupport{ .requested = true, .identity = true } }) {
					const auto off = Plan(size, percent, {}, support);
					Require(!off.Active() && off.model == ordinary && off.mapping == PeripheralMapping{}, "Off/identity changed the old geometry");
				}
			}
		}
		const auto mixed = Plan({ 1921, 1081 }, 67, { 1281, 721 });
		Require(mixed.Active() && ResolveFeatureUpscaling(mixed.guides.width, mixed.guides.height, mixed.model.width, mixed.model.height), "Mixed colour/guide grids rejected");
		for (auto support : { PeripheralCompressionSupport{ .requested = true, .actorRoute = true },
				 PeripheralCompressionSupport{ .requested = true, .restricted = true },
				 PeripheralCompressionSupport{ .requested = true, .compactInputs = true },
				 PeripheralCompressionSupport{ .requested = true, .guidedConflict = true } })
			Require(!Plan({ 1920, 1080 }, 50, {}, support).Active(), "Unsupported route admitted");
		auto crop = UpscalingDLSS::ViewportCrop::Identity(1920, 1080, 1920, 1080);
		const auto restricted = BuildModelResolutionGeometry(crop, { 1920, 1080 }, { 100, 100, 800, 600 }, 100);
		Require(restricted && !BuildPeripheralCompressionPlan(*restricted, { 1920, 1080 }, crop, 100, { .requested = true }).Active(), "Restricted baseline expanded to a full eye");
		crop.fullInput = crop.fullOutput = { 2000, 1200 };
		crop.input = crop.output = { 40, 60, 1960, 1140 };
		const auto offset = BuildModelResolutionGeometry(crop, { 1920, 1080 }, { 0, 0, 1920, 1080 }, 100);
		Require(offset && BuildPeripheralCompressionPlan(*offset, { 1920, 1080 }, crop, 100, { .requested = true }).reason == PeripheralCompressionReason::RestrictedDomain,
			"A viewport crop was silently treated as a full eye");
		std::array pair{ full, Plan({ 1920, 1080 }, 50, {}, { .requested = true, .guidedConflict = true }) };
		ReconcilePeripheralCompression(pair);
		Require(!pair[0].Active() && pair[0].reason == PeripheralCompressionReason::PairUnsupported && !pair[1].Active(), "Eye strategies diverged");
		for (float u : { -0.2f, 0.05f, 0.1f, 0.5f, 0.9f, 1.1f }) {
			for (float motion : { -4.0f, -0.001f, 0.0f, 0.003f, 3.0f }) {
				const float converted = full.mapping.Forward(u + motion, 0) - full.mapping.Forward(u, 0);
				Require(std::isfinite(converted) && (motion == 0.0f ? converted == 0.0f : converted * motion > 0.0f), "Endpoint motion lost direction or offscreen displacement");
				Require(std::abs(full.mapping.Inverse(full.mapping.Forward(u, 0) + converted, 0) - (u + motion)) < 3e-6f,
					"Motion endpoint applied the scale twice");
			}
		}
		const auto ordinary = ModelResolutionHistory{};
		auto compressed = ordinary;
		compressed.peripheralMapping = full.mapping;
		compressed.peripheralMappingVersion = PeripheralMapping::kVersion;
		Require(ordinary != compressed && compressed == compressed, "Mapping history identity is absent or unstable");

#ifdef DEVBENCH_BRIDGE_ENABLED
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
