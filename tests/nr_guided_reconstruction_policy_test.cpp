#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"

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
		const GuidedReconstructionQualification qualified{ 50, true, true };
		GuidedReconstructionInput input{ true, 50, { 2001, 1001 }, { 1001, 501 }, InsertionPoint::UpscaledCenter, true };
		std::array pair{ input, input };
		const auto reason = [&](GuidedReconstructionInput value) { return SelectGuidedReconstruction(std::span(&value, 1), qualified).reason; };
		Require(SelectGuidedReconstruction(pair).reason == Unqualified);
		Require(SelectGuidedReconstruction(pair, qualified).mode == GuidedReconstructionMode::GuidedLowResolution);
		for (auto route : { InsertionPoint::UpscaledCenter, InsertionPoint::FinalLdrPreUi }) {
			input.insertion = route;
			for (unsigned percent : { 30u, 33u, 50u, 51u, 67u, 99u, 100u }) {
				input.effectivePercent = percent;
				input.unpackedModel = BuildModelResolutionExtent(input.target.width, input.target.height, percent);
				Require(reason(input) == (percent == 100 ? NoReduction : percent > 50 ? AboveCutoff :
																						Eligible));
			}
		}
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
#ifdef DEVBENCH_BRIDGE_ENABLED
	CheckComparison();
#endif
}
