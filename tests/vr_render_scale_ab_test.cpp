#include "Features/Upscaling/VRRenderScaleABTest.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Require(bool a_value, const char* a_message)
	{
		if (!a_value) {
			std::cerr << a_message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}
}

int main()
{
	using namespace VRRenderScaleABTest;
	State test;
	Require(test.Requested() == Condition::Baseline, "restart must default to baseline");
	Require(!test.ObserveMenuHook(0), "unlatched menu draw must pass through");
	Require(!test.Request(static_cast<Condition>(99)), "invalid conditions must reject");
	for (std::uint32_t mode = 0; mode < 4; ++mode) {
		const auto condition = static_cast<Condition>(mode);
		const auto frame = 10 + mode;
		Require(test.Request(condition), "condition request failed");
		test.BeginFrame(frame, true, test.SuspensionEpoch());
		Require(test.Eligible(frame) && test.Applied(frame) == condition, "condition did not latch");
		Require(test.ObserveMenuHook(frame) == ((mode & 1) != 0), "menu switch changed wrong condition");
		test.ObserveMenuHook(frame);
		Require(test.ObserveNativeCandidate(frame) == ((mode & 2) != 0), "native switch changed wrong condition");
		if (!(mode & 2))
			test.ObserveNativeReplacement(frame);
		const auto counts = test.Read(condition);
		Require(counts.frames == 1 && counts.menuHookFrames == 1, "multiple draw hooks overcounted frames");
		Require(counts.nativeCandidates == 1, "native candidate was not recorded");
		Require(counts.nativeRestored == ((mode & 2) ? 1u : 0u), "native restoration count is wrong");
		Require(counts.nativeReplaced == ((mode & 2) ? 0u : 1u), "native replacement count is wrong");
		Require(!test.ObserveMenuHook(frame + 1), "old frame selection leaked into a new frame");
	}

	Require(test.Request(Condition::Baseline), "baseline request failed");
	test.BeginFrame(20, true, test.SuspensionEpoch());
	Require(test.Request(Condition::Both), "combined request failed");
	test.BeginFrame(20, true, test.SuspensionEpoch());
	Require(test.Applied(20) == Condition::Baseline, "request changed an in-progress frame");
	test.BeginFrame(21, true, test.SuspensionEpoch());
	Require(test.Applied(21) == Condition::Both, "next frame did not apply request");
	test.Suspend();
	test.BeginFrame(21, true, test.SuspensionEpoch());
	Require(!test.Eligible(21) && test.Applied(21) == Condition::Baseline, "menu suspension rearmed in the same frame");
	Require(!test.ObserveMenuHook(21) && !test.ObserveNativeCandidate(21), "suspended test changed rendering");
	const auto before = test.Read(Condition::Both);
	test.BeginFrame(22, false, test.SuspensionEpoch());
	test.ObserveMenuHook(22);
	test.ObserveNativeCandidate(22);
	test.ObserveNativeReplacement(22);
	const auto after = test.Read(Condition::Both);
	Require(before.frames == after.frames && before.menuHookFrames == after.menuHookFrames &&
				before.nativeCandidates == after.nativeCandidates && before.nativeReplaced == after.nativeReplaced,
		"ineligible gameplay contaminated counters");
	Require(test.Requested() == Condition::Both, "suspension discarded the user's selection");
	test.BeginFrame(23, true, test.SuspensionEpoch());
	Require(test.Applied(23) == Condition::Both, "settled gameplay did not resume the selection");
	const auto epochBeforeMenu = test.SuspensionEpoch();
	test.Suspend();
	test.BeginFrame(24, true, epochBeforeMenu);
	Require(!test.Eligible(24), "stale eligibility rearmed after a concurrent menu event");
	return EXIT_SUCCESS;
}
