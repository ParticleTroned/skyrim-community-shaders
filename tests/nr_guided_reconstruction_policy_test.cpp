#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
#include "Features/Upscaling/NeuralRendering/PeripheralCompressionPolicy.h"

#include <array>
#include <stdexcept>
#include <string_view>

namespace
{
	void Require(bool value)
	{
		if (!value)
			throw std::runtime_error("Guided reconstruction contract failed");
	}

	void CheckPolicy()
	{
		using namespace NeuralRendering;
		using enum GuidedReconstructionReason;
		const auto qualified = kGuidedReconstructionQualification;
		GuidedReconstructionInput input{ true, 50, { 2001, 1001 }, { 1001, 501 }, InsertionPoint::UpscaledCenter, true };
		std::array pair{ input, input };
		const auto reason = [&](GuidedReconstructionInput value) { return SelectGuidedReconstruction(std::span(&value, 1), qualified).reason; };
		Require(qualified.maximumPercent == 100 && qualified.beforeUpscaling && qualified.afterUpscaling);
		Require(SelectGuidedReconstruction(pair).reason == Eligible);
		Require(SelectGuidedReconstruction(pair, qualified).mode == GuidedReconstructionMode::GuidedLowResolution);
		for (auto route : { InsertionPoint::UpscaledCenter, InsertionPoint::FinalLdrPreUi }) {
			input.insertion = route;
			for (unsigned percent = kMinimumModelResolutionPercent; percent <= kMaximumModelResolutionPercent; ++percent) {
				input.effectivePercent = percent;
				input.unpackedModel = BuildModelResolutionExtent(input.target.width, input.target.height, percent);
				Require(reason(input) == (percent == 100 ? NoReduction : Eligible));
			}
		}
		input = pair[0];
		input.effectivePercent = 51;
		input.unpackedModel = BuildModelResolutionExtent(input.target.width, input.target.height, input.effectivePercent);
		Require(SelectGuidedReconstruction(std::span(&input, 1), { 50, true, true }).reason == AboveCutoff);
		input = pair[0];
		input.enabled = false;
		Require(reason(input) == Disabled);
		input = pair[0];
		input.matchedReferenceDomain = false;
		Require(reason(input) == UnsupportedDomain);
		input = pair[0];
		input.insertion = static_cast<InsertionPoint>(999);
		Require(reason(input) == UnsupportedRoute);
		input = pair[0];
		input.unpackedModel.width -= 1;
		Require(reason(input) == InvalidGeometry);
		input = pair[0];
		input.effectivePercent = 0;
		Require(reason(input) == InvalidGeometry);
		input = pair[0];
		input.target = { 1, 100 };
		input.unpackedModel = { 1, 50 };
		Require(reason(input) == NoReduction);
		pair[1].matchedReferenceDomain = false;
		Require(SelectGuidedReconstruction(pair, qualified).mode == GuidedReconstructionMode::Basic);
		Require(SelectGuidedReconstruction({}, qualified).reason == InvalidGeometry);
		pair[1] = pair[0];
		Require(SelectGuidedReconstruction(pair, { 50, false, true }).reason == Unqualified);
		pair[0].insertion = pair[1].insertion = InsertionPoint::FinalLdrPreUi;
		Require(SelectGuidedReconstruction(pair, { 50, false, true }).reason == Eligible);
	}

	void CheckCompressionPolicy()
	{
		using namespace NeuralRendering;
		using enum GuidedReconstructionReason;
		constexpr UpscalingDLSS::Extent source{ 1921, 1081 };
		const auto crop = UpscalingDLSS::ViewportCrop::Identity(source.width, source.height, source.width, source.height);
		for (bool shaped : { false, true }) {
			for (auto percent : { 30u, 90u, 99u, 100u }) {
				const auto geometry = BuildModelResolutionGeometry(crop, source, { 0, 0, source.width, source.height }, percent, source);
				Require(geometry.has_value());
				for (auto compression : { 0u, 1u, 25u, 100u }) {
					const auto plan = BuildPeripheralCompressionPlan(*geometry, source, crop, percent,
						{ .requested = true, .compressionPercent = compression, .mask = { { 0.47f, 0.53f }, { 0.43f, 0.36f }, shaped } });
					Require(plan.Active() == (compression != 0));
					for (auto route : { InsertionPoint::UpscaledCenter, InsertionPoint::FinalLdrPreUi }) {
						GuidedReconstructionInput input{ true, percent, source, plan.model, route, true, plan.Active() };
						std::array pair{ input, input };
						const auto expected = compression == 0 && percent == 100 ? NoReduction : Eligible;
						Require(SelectGuidedReconstruction(pair).reason == expected);
						Require(SelectGuidedReconstruction(std::span(&input, 1)).reason == expected);
						pair[1].enabled = false;
						if (expected == Eligible)
							Require(SelectGuidedReconstruction(pair).reason == Disabled);
					}
				}
			}
		}
		GuidedReconstructionInput input{ true, 100, source, { 961, 541 }, InsertionPoint::UpscaledCenter, true, true };
		const auto reason = [&](GuidedReconstructionInput value) { return SelectGuidedReconstruction(std::span(&value, 1)).reason; };
		Require(reason(input) == Eligible);
		input.peripheralCompression = false;
		Require(reason(input) == InvalidGeometry);
		input.peripheralCompression = true;
		input.unpackedModel = source;
		Require(reason(input) == Eligible);
		input.unpackedModel = { source.width + 1, source.height / 2 };
		Require(reason(input) == InvalidGeometry);
		input.effectivePercent = 50;
		input.unpackedModel = { 1000, 500 };
		Require(reason(input) == InvalidGeometry);
		input.unpackedModel = {};
		Require(reason(input) == InvalidGeometry);
	}

	void CheckAsymmetricCompression()
	{
		using namespace NeuralRendering;
		const auto plan = [](UpscalingDLSS::Extent source, UpscalingDLSS::Extent guides, unsigned percent, unsigned compression) {
			const auto crop = UpscalingDLSS::ViewportCrop::Identity(guides.width, guides.height, source.width, source.height);
			const auto geometry = BuildModelResolutionGeometry(crop, source, { 0, 0, source.width, source.height }, percent, guides);
			Require(geometry.has_value());
			return BuildPeripheralCompressionPlan(*geometry, guides, crop, percent, { .requested = true, .compressionPercent = compression });
		};
		for (const auto percent : { 39u, 100u }) {
			const std::array<UpscalingDLSS::Extent, 2> sources = percent == 39 ?
			                                                         std::array<UpscalingDLSS::Extent, 2>{ { { 307, 202 }, { 16282, 3519 } } } :
			                                                         std::array<UpscalingDLSS::Extent, 2>{ { { 2, 2 }, { 1921, 1081 } } };
			const auto secondGuides = percent == 39 ? UpscalingDLSS::Extent{ 8141, 1759 } : sources[1];
			const auto compression = percent == 39 ? 61u : 1u;
			std::array plans{ plan(sources[0], sources[0], percent, compression), plan(sources[1], secondGuides, percent, compression) };
			ReconcilePeripheralCompression(plans);
			Require(plans[0].Active() && plans[1].Active());
			Require(plans[0].candidatePixels == plans[0].baselinePixels && plans[1].candidatePixels < plans[1].baselinePixels);
			for (auto route : { InsertionPoint::UpscaledCenter, InsertionPoint::FinalLdrPreUi }) {
				std::array<GuidedReconstructionInput, 2> inputs{};
				for (unsigned eye = 0; eye < inputs.size(); ++eye)
					inputs[eye] = { true, percent, sources[eye], plans[eye].model, route, true, plans[eye].Active() };
				Require(SelectGuidedReconstruction(inputs).reason == GuidedReconstructionReason::Eligible);
				inputs[1].matchedReferenceDomain = false;
				Require(SelectGuidedReconstruction(inputs).reason == GuidedReconstructionReason::UnsupportedDomain);
			}
		}
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void CheckComparison()
	{
		using namespace NeuralRendering;
		using enum GuidedReconstructionOverride;
		GuidedReconstructionExperiment lease;
		Require(!lease.Configure(Basic, 601, 1) && !lease.Configure(Basic, 0, 1));
		Require(!lease.Configure(Auto, 1, 1) && !lease.Configure(Basic, 2, 0));
		Require(lease.Configure(Auto, 0, 1));
		Require(!lease.Status().historyResetPending);
		Require(lease.Configure(GuidedExperiment, 2, lease.Status().revision));
		Require(lease.Status().historyResetPending);
		Require(lease.Resolve(10, 1) == std::pair{ GuidedExperiment, true });
		Require(!lease.Status().historyResetPending);
		Require(lease.Resolve(10, 1) == std::pair{ GuidedExperiment, false });
		Require(lease.Resolve(11, 1) == std::pair{ GuidedExperiment, false });
		Require(lease.Resolve(12, 1) == std::pair{ Auto, true });
		Require(lease.Resolve(13, 1) == std::pair{ Auto, false });
		Require(!lease.Configure(Auto, 0, 2));
		Require(!lease.Status().historyResetPending);
		Require(std::string_view(GetGuidedReconstructionModeName(GuidedReconstructionMode::Basic)) == "basic");
		Require(std::string_view(GetGuidedReconstructionModeName(GuidedReconstructionMode::GuidedLowResolution)) == "guided_low_resolution");
		Require(lease.Configure(Basic, 4, lease.Status().revision));
		Require(lease.Resolve(14, 1) == std::pair{ Basic, true });
		Require(lease.Configure(Basic, 4, lease.Status().revision));
		Require(!lease.Status().historyResetPending);
		Require(lease.Resolve(14, 1) == std::pair{ Basic, false });
		Require(lease.Resolve(15, 2) == std::pair{ Auto, true });
		Require(lease.Configure(Basic, 4, lease.Status().revision));
		Require(lease.Resolve(20, 2) == std::pair{ Basic, true });
		lease.Cancel();
		Require(lease.Resolve(20, 2) == std::pair{ Auto, true });
		Require(lease.Configure(Basic, 4, lease.Status().revision));
		Require(lease.Resolve(0xffffffffu, 2) == std::pair{ Auto, true });
		using Json = nlohmann::json;
		Json request{ { "action", "nr_reconstruction_compare" }, { "reconstructionOverride", "guided_experiment" },
			{ "reconstructionOverrideFrames", 2 }, { "expectedReconstructionOverrideRevision", 1 } };
		Require(ParseReconstructionComparison(request).has_value());
		for (const auto* field : { "action", "reconstructionOverride", "reconstructionOverrideFrames", "expectedReconstructionOverrideRevision" }) {
			auto missing = request;
			missing.erase(field);
			Require(!ParseReconstructionComparison(missing));
			for (auto bad : { Json(nullptr), Json(true), Json(-1), Json(1.5), Json::array(), Json::object() }) {
				auto invalid = request;
				invalid[field] = bad;
				Require(!ParseReconstructionComparison(invalid));
			}
		}
		for (auto invalid : { Json(0), Json(0xffffffffffffffffULL), Json("1") }) {
			request["expectedReconstructionOverrideRevision"] = invalid;
			Require(!ParseReconstructionComparison(request));
		}
		request["expectedReconstructionOverrideRevision"] = 2;
		request["reconstructionOverride"] = "auto";
		Require(!ParseReconstructionComparison(request));
		request["reconstructionOverrideFrames"] = 0;
		Require(ParseReconstructionComparison(request).has_value());
		request["guidedReconstruction"] = true;
		Require(!ParseReconstructionComparison(request));
	}
#endif
}

int main()
{
	CheckPolicy();
	CheckCompressionPolicy();
	CheckAsymmetricCompression();
#ifdef DEVBENCH_BRIDGE_ENABLED
	CheckComparison();
#endif
}
