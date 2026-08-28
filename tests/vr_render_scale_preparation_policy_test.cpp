#include "Features/Upscaling/VRRenderScalePreparationPolicy.h"

namespace
{
	namespace Policy = VRRenderScalePreparationPolicy;
	constexpr uintptr_t kDevice = 0x1234;

	constexpr Policy::Key MakeKey()
	{
		return {
			.vrRuntime = true,
			.requestID = 7,
			.transitionEpoch = 11,
			.optionsGeneration = 13,
			.shaderDefinesGeneration = 17,
			.deviceIdentity = kDevice,
			.method = 3,
			.qualityMode = 4,
			.dlssPreset = 1,
			.renderEyeWidth = 1600,
			.renderEyeHeight = 1800,
			.displayEyeWidth = 2000,
			.displayEyeHeight = 2200,
			.renderScaleEnabled = true,
			.fsr4RuntimeEnabled = false,
		};
	}

	constexpr Policy::CompletionFacts ReadyFacts()
	{
		constexpr auto key = MakeKey();
		return {
			.candidate = key,
			.latest = key,
			.currentDeviceIdentity = kDevice,
			.shadersReady = true,
			.providerReady = true,
		};
	}

	constexpr bool CoversPreparationOwnership()
	{
		if (!Policy::ShouldAttempt(7, 0) || Policy::ShouldAttempt(7, 7))
			return false;

		auto facts = ReadyFacts();
		facts.latest.requestID = 8;
		if (Policy::SelectCompletionDisposition(facts) !=
			Policy::CompletionDisposition::Superseded) {
			return false;
		}

		facts = ReadyFacts();
		facts.currentDeviceIdentity = 0x5678;
		if (Policy::SelectCompletionDisposition(facts) !=
			Policy::CompletionDisposition::DeviceChanged) {
			return false;
		}

		facts = ReadyFacts();
		facts.shadersReady = false;
		auto disposition = Policy::SelectCompletionDisposition(facts);
		if (disposition != Policy::CompletionDisposition::ShaderFailure ||
			!Policy::RetainsProtectedFallback(disposition)) {
			return false;
		}

		facts = ReadyFacts();
		facts.providerReady = false;
		disposition = Policy::SelectCompletionDisposition(facts);
		if (disposition != Policy::CompletionDisposition::ProviderFailure ||
			!Policy::RetainsProtectedFallback(disposition)) {
			return false;
		}

		facts = ReadyFacts();
		facts.cancelled = true;
		if (Policy::SelectCompletionDisposition(facts) !=
			Policy::CompletionDisposition::Cancelled) {
			return false;
		}

		facts = ReadyFacts();
		facts.latest.optionsGeneration++;
		if (Policy::SelectCompletionDisposition(facts) !=
			Policy::CompletionDisposition::Superseded) {
			return false;
		}

		facts = ReadyFacts();
		if (Policy::SelectCompletionDisposition(facts) !=
				Policy::CompletionDisposition::Publish ||
			!Policy::CanUsePreparedResult(
				facts.candidate,
				facts.latest,
				kDevice)) {
			return false;
		}
		facts.latest.requestID++;
		if (Policy::CanUsePreparedResult(
				facts.candidate,
				facts.latest,
				kDevice)) {
			return false;
		}
		facts.latest = facts.candidate;
		if (Policy::CanUsePreparedResult(
				facts.candidate,
				facts.latest,
				0x5678)) {
			return false;
		}

		facts = ReadyFacts();
		facts.candidate.vrRuntime = false;
		facts.latest = facts.candidate;
		return Policy::SelectCompletionDisposition(facts) ==
		       Policy::CompletionDisposition::NotApplicable;
	}

	static_assert(CoversPreparationOwnership());
}

int main() {}
