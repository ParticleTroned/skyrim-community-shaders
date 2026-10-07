#include "Features/Upscaling/NeuralRendering/MemoryRecoveryPolicy.h"

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
	TestRetirementAndCompletion();
	TestDlssWarningsRequireFreshRecovery();
	TestFreshConsecutiveSamples();
	TestHeadroomAndInvalidBudgets();
	TestRepeatedPressureBackoff();
	TestFailedRebuildRetainsBackoffAfterLongWait();
}
