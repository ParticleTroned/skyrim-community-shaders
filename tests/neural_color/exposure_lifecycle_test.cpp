// Exercises the same transaction-admission policy used by ExposureCapture::Bind.
// No engine hooks, D3D calls, fence completion or GPU contents are simulated here.
#include "Features/Upscaling/NeuralRendering/ExposurePolicy.h"
#include <array>
#include <cstdio>
#include <cstdlib>

using namespace NeuralRendering::Color;
static unsigned checks = 0;
static void Require(bool value, const char* message)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Lifecycle check %u: %s\n", checks, message);
		std::abort();
	}
}
int main()
{
	using Decision = ExposureLatchDecision;
	std::array<ExposureLatchPolicy, 4> routes{};
	for (std::uint32_t route = 0; route < 2; ++route) {
		for (std::uint32_t insertion = 0; insertion < 2; ++insertion) {
			auto& policy = routes[route * 2 + insertion];
			ExposureTransaction key{ 20, 17, insertion, route, 5 };
			Require(policy.Begin(key, 1, 2) == Decision::NewTransaction, "first eye creates latch");
			// No capture available: a later producer does not change the decision.
			Require(!MatchesExposure({}, key.sourceWorldFrame, 1), "no exposure is not one");
			ExposureStamp late{ 17, 1, 3, false };
			Require(MatchesExposure(late, key.sourceWorldFrame, 1), "late producer is individually frame-matched");
			for (unsigned region = 0; region < 4; ++region)
				Require(policy.Begin(key, 1, 2) == Decision::Reuse, "all regions/eyes freeze first availability");
			Require(policy.Begin(key, 3, 2) == Decision::Reject, "same frame on another context is unsafe");
			Require(policy.Begin(key, 1, 3) == Decision::Reject, "same frame on another device is unsafe");
			Require(policy.Begin(key, 1, 2) == Decision::Reuse, "failed context admission does not replace original");
			// The epoch can change between eyes; Begin intentionally does not take it.
			Require(!MatchesExposure(late, 17, 2), "old epoch not admissible for a new selection");
			Require(policy.Begin(key, 1, 2) == Decision::Reuse, "epoch toggle cannot split an active pair");
			++key.frame;
			Require(policy.Begin(key, 1, 2) == Decision::NewTransaction, "new presentation reselects exposure");
			++key.sourceWorldFrame;
			Require(policy.Begin(key, 1, 2) == Decision::NewTransaction, "changed retained source reselects exposure");
			++key.generation;
			Require(policy.Begin(key, 1, 2) == Decision::NewTransaction, "resource generation reselects exposure");
			key.frame = 0;
			Require(policy.Begin(key, 1, 2) == Decision::NewTransaction, "frame discontinuity is not an old transaction");
			Require(policy.Begin(key, 7, 8) == Decision::Reject, "new frame does not legalize another device");
			policy.Clear();
			Require(!policy.occupied, "reset and abandonment clear identity");
			Require(policy.Begin(key, 7, 8) == Decision::NewTransaction, "rebinding after explicit retirement");
		}
	}
	ExposureLatchPolicy p;
	ExposureTransaction key{ 20, 19, 0, 0, 1 };
	Require(p.Begin(key, 0, 2) == Decision::Reject, "missing context");
	Require(p.Begin(key, 1, 0) == Decision::Reject, "missing device");
	key.route = 2;
	Require(p.Begin(key, 1, 2) == Decision::Reject, "invalid route");
	key.route = 0;
	key.insertion = 2;
	Require(p.Begin(key, 1, 2) == Decision::Reject, "invalid insertion");
	key.insertion = 0;
	key.sourceWorldFrame = 0xffffffffu;
	Require(p.Begin(key, 1, 2) == Decision::Reject, "unknown source frame");
	Require(!p.occupied, "invalid admissions cannot acquire a latch");
	Require(!MatchesExposure({ 10, 0, 1, false }, 10, 0), "zero epoch is unknown");
	Require(!MatchesExposure({ 10, 1, 0, false }, 10, 1), "zero sequence is unknown");
	Require(!MatchesExposure({ 10, 1, 1, true }, 10, 1), "ambiguous source rejected");
	Require(!MatchesExposure({ 9, 1, 1, false }, 10, 1), "old frame rejected");
	Require(!MatchesExposure({ 11, 1, 1, false }, 10, 1), "future frame rejected");
	Require(MatchesExposure({ 10, 1, 1, false }, 10, 1), "exact producer admitted");
	ExposureTransaction history{ 11, 11, 0, 0, 1, ExposureSource::CapturedHDRPrevious };
	const ExposureStamp previous{ 10, 1, 1, false };
	Require(MatchesExposure(previous, history, 1), "explicit history selects exactly one source frame earlier");
	Require(!MatchesExposure(previous, history, 2), "history cannot cross a capture epoch");
	Require(!MatchesExposure({ 9, 1, 1, false }, history, 1), "two-frame history rejected");
	Require(!MatchesExposure({ 11, 1, 1, false }, history, 1), "history cannot silently upgrade to current frame");
	Require(!MatchesExposure({ 10, 1, 1, true }, history, 1), "ambiguous history rejected");
	history.sourceWorldFrame = 0;
	Require(!MatchesExposure(previous, history, 1), "history cannot underflow the source frame");
	history.sourceWorldFrame = 0xffffffffu;
	Require(!MatchesExposure(previous, history, 1), "unknown source frame cannot select history");
	history.sourceWorldFrame = 11;
	Require(p.Begin(history, 1, 2) == Decision::NewTransaction, "history starts a new stereo transaction");
	Require(p.Begin(history, 1, 2) == Decision::Reuse, "history is frozen across eyes and regions");
	history.source = ExposureSource::CapturedHDR;
	Require(!MatchesExposure(previous, history, 1), "strict source never accepts previous exposure");
	Require(p.Begin(history, 1, 2) == Decision::NewTransaction, "changing source policy cannot reuse history latch");
	std::printf("Passed %u exposure-lifecycle admission checks (not a live GPU test)\n", checks);
}
