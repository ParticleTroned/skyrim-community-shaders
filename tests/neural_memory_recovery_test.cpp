#include "Features/Upscaling/NeuralRendering/MemoryConservationPolicy.h"

#include <limits>
#include <stdexcept>

using namespace NeuralRendering;

namespace
{
	constexpr auto mib = MemoryRecoveryPolicy::kMiB;
	constexpr MemoryBudgetSample healthy{ 16000 * mib, 12000 * mib, true };
	constexpr MemoryBudgetSample high{ 16000 * mib, 14000 * mib, true };

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Observe(MemoryRecoveryPolicy& policy, MemoryBudgetSample sample, std::uint64_t additionalBytes, std::uint64_t nowMs)
	{
		sample.sampledAtMs = nowMs;
		return policy.Admit(sample, additionalBytes, nowMs);
	}

	void TestConservationHysteresis()
	{
		MemoryConservationPolicy policy;
		auto sample = healthy;
		sample.sampledAtMs = 1000;
		policy.Update(sample, 0, false, 1000);
		Require(!policy.active, "Healthy NR entered conservation");
		policy.Update(sample, 800 * mib, false, 1000);
		Require(policy.active && policy.entries == 1, "Projected 80 percent usage did not trigger conservation");
		MemoryRecoveryPolicy recovery;
		Require(recovery.Admit(sample, 800 * mib, 1000), "Conservation threshold also suspended NR");
		for (std::uint64_t now = 1000; now < 6000; now += 250) {
			sample.sampledAtMs = now;
			policy.Update(sample, 0, false, now);
			Require(policy.active, "Conservation exited before stable headroom");
		}
		sample.sampledAtMs = 6000;
		policy.Update(sample, 0, false, 6000);
		Require(!policy.active && policy.exits == 1, "Stable healthy samples did not restore normal caching");
		sample.usageBytes = 12500 * mib;
		sample.sampledAtMs = 6250;
		policy.Update(sample, 0, false, 6250);
		Require(!policy.active, "Hysteresis band restarted conservation");
		policy.Update(sample, 0, true, 6250);
		Require(policy.active && policy.entries == 2, "Recovery did not preserve conservation for rebuilding");
	}

	void TestTrimLimitSurvivesRecoveryAndRebuilds()
	{
		MemoryConservationPolicy policy;
		auto sample = high;
		sample.sampledAtMs = 1000;
		Require(!policy.CanTrimColorBuffers(0), "Inactive policy admitted a trim");
		policy.Update(sample, 0, false, 1000);
		policy.RecordColorTrim(0, 64 * mib);
		Require(!policy.CanTrimColorBuffers(0) && policy.CanTrimColorBuffers(1), "Trim limit did not remain per physical eye");
		policy.RecordColorTrim(0, 64 * mib);
		policy.RecordColorTrim(kPhysicalFeatureSlotCount, 64 * mib);
		Require(!policy.CanTrimColorBuffers(kPhysicalFeatureSlotCount) && policy.trimmedColorBuffers == 2 &&
					policy.reclaimedLogicalBytes == 64 * mib,
			"Duplicate or invalid trims changed the counters");

		MemoryRecoveryPolicy recovery;
		recovery.Suspend(1250);
		recovery.Retired();
		policy.Update({}, 0, true, 1500);
		Require(!policy.CanTrimColorBuffers(0), "Backend retirement reset the trim limit");
		sample = healthy;
		for (std::uint64_t now = 1750; now <= 2250; now += 250)
			Observe(recovery, sample, 0, now);
		recovery.Succeeded(2250);
		Require(recovery.phase == MemoryRecoveryPhase::Ready, "Recovery test did not finish rebuilding");
		for (std::uint64_t now = 2500; now < 7500; now += 250) {
			sample.sampledAtMs = now;
			policy.Update(sample, 0, false, now);
			Require(!policy.CanTrimColorBuffers(0), "Rebuilding within an episode allowed another trim");
		}
		sample.sampledAtMs = 7500;
		policy.Update(sample, 0, false, 7500);
		Require(!policy.active, "Healthy headroom did not end the pressure episode");
		sample = high;
		sample.sampledAtMs = 7750;
		policy.Update(sample, 0, false, 7750);
		Require(policy.CanTrimColorBuffers(0), "A new pressure episode retained the old trim limit");
		policy.RecordColorTrim(0, 32 * mib);
		Require(policy.trimmedColorBuffers == 4 && policy.reclaimedLogicalBytes == 96 * mib,
			"New-episode trim accounting was incorrect");
	}

	void TestConservationNeedsFreshHeadroom()
	{
		MemoryConservationPolicy policy;
		policy.Update({}, 0, false, 1000);
		Require(!policy.active, "Unknown budget activated conservation");
		policy.Update({}, 0, true, 1000);
		Require(policy.active, "Recovery needs conservation even without a budget sample");
		auto sample = healthy;
		sample.sampledAtMs = 1000;
		policy.Update(sample, 0, false, 1000);
		policy.Update(sample, 0, false, 6000);
		Require(policy.active && !policy.healthyWindow, "Reused stale sample exited conservation");
		sample.sampledAtMs = 6250;
		policy.Update(sample, 0, false, 6250);
		sample.sampledAtMs = 12000;
		policy.Update(sample, 0, false, 12000);
		Require(policy.active && policy.healthySinceMs == 12000, "Sample gap was treated as continuous health");
		sample.sampledAtMs = 12500;
		policy.Update(sample, 0, false, 12250);
		Require(!policy.healthyWindow, "Future sample retained a healthy window");
		sample.sampledAtMs = 12250;
		sample.usageBytes = 12400 * mib;
		policy.Update(sample, 0, false, 12250);
		Require(policy.active && !policy.healthyWindow, "Usage above exit threshold counted as healthy");
		MemoryConservationPolicy largeEstimate;
		sample = healthy;
		sample.sampledAtMs = 13000;
		largeEstimate.Update(sample, std::numeric_limits<std::uint64_t>::max(), false, 13000);
		Require(largeEstimate.active, "Very large allocation estimate wrapped out of conservation");
	}

	void TestConservationHeadroomAndReclaimLimits()
	{
		MemoryConservationPolicy policy;
		MemoryBudgetSample sample{ 3000 * mib, 1976 * mib, true, 0, false, 1000 };
		policy.Update(sample, 0, false, 1000);
		Require(policy.active, "Low absolute headroom did not activate conservation");
		for (std::uint64_t now = 1000; now <= 7000; now += 250) {
			sample.sampledAtMs = now;
			policy.Update(sample, 0, false, now);
		}
		Require(policy.active && !policy.healthyWindow, "Low absolute headroom was mistaken for recovery");
		Require(!MemoryConservationPolicy::IsInactive(2000, 1999), "Clock regression evicted a route");
		Require(!MemoryConservationPolicy::IsInactive(2000, 3999), "Recently active route could be evicted");
		Require(MemoryConservationPolicy::IsInactive(2000, 4000), "Idle route could not be reclaimed");
		Require(!MemoryConservationPolicy::WorthTrimming(100 * mib, 69 * mib), "Small byte savings triggered a trim");
		Require(!MemoryConservationPolicy::WorthTrimming(200 * mib, 151 * mib), "Small relative savings triggered a trim");
		Require(MemoryConservationPolicy::WorthTrimming(200 * mib, 150 * mib), "Material savings could not be reclaimed");
		Require(!MemoryConservationPolicy::WorthTrimming(32 * mib, 64 * mib), "Required growth was mistaken for a trim");
	}

	void TestRetirementAndCompletion()
	{
		MemoryRecoveryPolicy policy;
		policy.Retired();
		Require(policy.phase == MemoryRecoveryPhase::Ready && policy.retirements == 0, "Retirement without suspension changed state");
		Require(Observe(policy, healthy, 256 * mib, 1000), "Healthy admission failed");
		Require(!Observe(policy, high, 0, 1100), "High pressure was admitted");
		Require(policy.phase == MemoryRecoveryPhase::Retiring, "Retirement was skipped");
		Require(!Observe(policy, healthy, 256 * mib, 10000), "Recovery preceded retirement proof");
		policy.Succeeded(10000);
		Require(policy.resumes == 0, "Incomplete retirement was marked recovered");
		policy.Retired();
		policy.Retired();
		Require(!Observe(policy, healthy, 256 * mib, 10001), "Recovery skipped stable headroom");
		Require(!Observe(policy, healthy, 256 * mib, 10251), "Recovery was premature");
		Require(Observe(policy, healthy, 256 * mib, 10501), "Automatic recovery failed");
		Require(policy.phase == MemoryRecoveryPhase::Rebuilding && policy.resumes == 0, "Admission was reported as completed recovery");
		policy.Succeeded(10502);
		policy.Succeeded(10503);
		Require(policy.resumes == 1 && policy.suspensions == 1 && policy.retirements == 1, "Recovery counters changed incorrectly");
	}

	void TestFreshConsecutiveSamples()
	{
		MemoryRecoveryPolicy policy;
		policy.Suspend(100);
		policy.Retired();
		Require(!Observe(policy, healthy, 0, 599), "Recovery ignored the cooldown");
		Require(!Observe(policy, healthy, 0, 1000), "Recovery skipped observation");
		auto cached = healthy;
		cached.sampledAtMs = 1000;
		Require(!policy.Admit(cached, 0, 1500), "Repeated cached sample advanced stability");
		Require(!policy.Admit(cached, 0, 1501) && !policy.healthyWindow, "Stale sample retained a healthy window");
		Require(!Observe(policy, healthy, 0, 2000), "Fresh sample skipped stability after staleness");
		Require(!Observe(policy, healthy, 0, 10000), "Time without NR was counted as healthy observation");
		cached.sampledAtMs = 11000;
		Require(!policy.Admit(cached, 0, 10250), "Future sample was accepted");
		Require(!Observe(policy, healthy, 0, 11000), "Invalid sample retained stability");
		Require(!Observe(policy, high, 0, 11250), "Renewed pressure admitted recovery");
		Require(!Observe(policy, healthy, 0, 11500), "Pressure did not restart stability");
		Require(!Observe(policy, healthy, 0, 11750), "Recovery was premature after pressure");
		Require(Observe(policy, healthy, 0, 12000), "Consecutive fresh samples did not recover");
		Require(!Observe(policy, {}, 0, 12001), "Unknown headroom admitted replacement allocations while rebuilding");
		Require(policy.phase == MemoryRecoveryPhase::Retiring, "Lost recovery proof retained partial resources");
	}

	void TestHeadroomAndInvalidBudgets()
	{
		MemoryRecoveryPolicy policy;
		Require(Observe(policy, {}, 0, 1), "Unavailable monitoring disabled ordinary NR");
		Require(!Observe(policy, healthy, 4000 * mib, 2), "Projected allocation ignored the safety reserve");
		policy.Retired();
		Require(!Observe(policy, healthy, std::numeric_limits<std::uint64_t>::max(), 1000), "Oversized allocation was admitted");
		Require(!Observe(policy, { 16000 * mib, 17000 * mib, true }, 0, 1001), "Over-budget usage underflowed headroom");
		Require(!Observe(policy, {}, 0, 1002), "Invalid sample admitted rebuilding");
		Require(!Observe(policy, { 0, 0, true }, 0, 1003), "Zero budget admitted rebuilding");
		Require(!Observe(policy, { 2048 * mib, 1536 * mib, true }, 0, 1004), "Recovery admitted the exact suspension headroom boundary");
		policy = {};
		Require(!Observe(policy, { 16000 * mib, 13500 * mib, true }, 1000 * mib, 2000), "Projected native growth crossed the pressure limit");
		policy.Retired();
		Require(!Observe(policy, { 16000 * mib, 13300 * mib, true }, 1000 * mib, 5000), "Recovery ignored projected growth");
		policy = {};
		Require(Observe(policy, healthy, 256 * mib, 6000), "Explicit reset did not restore admission");
	}

	void TestFailedRebuildRetainsBackoffAfterLongWait()
	{
		MemoryRecoveryPolicy policy;
		policy.Suspend(1000);
		policy.Retired();
		Require(!Observe(policy, healthy, 0, 1500), "Recovery skipped stability");
		Require(Observe(policy, healthy, 0, 2000), "Initial recovery was not admitted");
		policy.Succeeded(2000);
		policy.Suspend(2100, true);
		policy.Retired();
		Require(!Observe(policy, healthy, 0, 100000), "Long wait skipped stability");
		Require(Observe(policy, healthy, 0, 100500), "Fresh samples after long wait did not admit retry");
		policy.Suspend(100501, true);
		Require(policy.retryLevel == 3, "Time spent waiting reset the failed-rebuild backoff");
	}

	void TestDlssWarningsRequireFreshRecovery()
	{
		MemoryRecoveryPolicy policy;
		policy.ReportDlssWarning(1000);
		Require(policy.phase == MemoryRecoveryPhase::Retiring && policy.dlssWarning, "DLSS warning did not request retirement");
		Require(policy.outOfMemoryFailures == 0, "Valid DLSS output was counted as an allocation failure");
		Require(!Observe(policy, healthy, 0, 1500), "DLSS warning bypassed retirement");
		policy.Retired();
		Require(!Observe(policy, healthy, 0, 1500), "DLSS recovery skipped stable headroom");
		policy.ReportDlssWarning(1750);
		policy.ReportDlssWarning(1800);
		Require(policy.suspensions == 1 && policy.retirements == 1 && policy.retryLevel == 1,
			"Repeated warnings retired resources again or escalated the same recovery episode");
		Require(!Observe(policy, healthy, 0, 2250), "Renewed warning did not extend the cooldown");
		Require(!Observe(policy, {}, 0, 2300), "DLSS recovery accepted unknown headroom");
		Require(!Observe(policy, healthy, 0, 2300), "Renewed warning kept the previous healthy window");
		Require(!Observe(policy, healthy, 0, 2550), "DLSS recovery was premature");
		Require(Observe(policy, healthy, 0, 2800), "DLSS recovery did not admit a rebuild");
		Require(policy.dlssWarning, "Warning cleared before successful rebuilding");
		policy.ReportDlssWarning(2801);
		policy.Succeeded(2802);
		Require(policy.phase == MemoryRecoveryPhase::Retiring && policy.resumes == 0,
			"Warning during a rebuild was marked recovered");
		policy.Retired();
		Require(!Observe(policy, healthy, 0, 4000), "Second recovery skipped stable headroom");
		Require(Observe(policy, healthy, 0, 4500), "Second recovery did not admit rebuilding");
		policy.Succeeded(4501);
		Require(!policy.dlssWarning && policy.dlssWarnings == 4 && policy.resumes == 1,
			"Successful recovery retained the warning or lost its diagnostic count");
	}

	void TestRepeatedPressureBackoff()
	{
		MemoryRecoveryPolicy policy;
		std::uint64_t nowMs = 1000, previousDelay = 0;
		for (int attempt = 0; attempt < 12; ++attempt) {
			policy.Suspend(nowMs, attempt % 2 != 0);
			const auto delay = policy.retryAfterMs - nowMs;
			Require(delay >= previousDelay && delay <= 10000, "Retry delay shrank or exceeded its bound");
			if (attempt % 2 != 0)
				Require(delay >= 2000, "Out-of-memory retry was immediate");
			previousDelay = delay;
			policy.Retired();
			Require(!Observe(policy, healthy, 0, policy.retryAfterMs - 1), "Retry ignored backoff");
			nowMs = policy.retryAfterMs;
			Require(!Observe(policy, healthy, 0, nowMs), "Retry skipped stability");
			Require(!Observe(policy, healthy, 0, nowMs + 250), "Retry skipped the stable interval");
			nowMs += 500;
			Require(Observe(policy, healthy, 0, nowMs), "Retry did not resume after healthy samples");
			policy.Succeeded(nowMs);
			++nowMs;
		}
		Require(previousDelay == 10000 && policy.resumes == 12, "Backoff did not saturate without disabling NR");
		nowMs += MemoryRecoveryPolicy::kRetryResetMs;
		policy.Suspend(nowMs);
		Require(policy.retryAfterMs - nowMs == 500, "A separated pressure episode retained the old backoff");
	}
}

int main()
{
	TestConservationHysteresis();
	TestConservationNeedsFreshHeadroom();
	TestTrimLimitSurvivesRecoveryAndRebuilds();
	TestConservationHeadroomAndReclaimLimits();
	TestRetirementAndCompletion();
	TestDlssWarningsRequireFreshRecovery();
	TestFreshConsecutiveSamples();
	TestHeadroomAndInvalidBudgets();
	TestRepeatedPressureBackoff();
	TestFailedRebuildRetainsBackoffAfterLongWait();
}
