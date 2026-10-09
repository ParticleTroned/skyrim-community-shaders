#include "Features/Upscaling/NeuralRendering/MemoryConservationPolicy.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

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
		recovery.Retired(1500);
		policy.Update({}, 0, true, 1500);
		Require(!policy.CanTrimColorBuffers(0), "Backend retirement reset the trim limit");
		Require(recovery.phase == MemoryRecoveryPhase::Waiting, "Recovery did not retain conservation while waiting");
		sample = healthy;
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
		policy.Retired(0);
		Require(policy.phase == MemoryRecoveryPhase::Ready && policy.retirements == 0, "Retirement without suspension changed state");
		Require(!Observe(policy, high, 0, 1000), "High pressure was admitted");
		Require(policy.phase == MemoryRecoveryPhase::Retiring, "Retirement was skipped");
		Require(!Observe(policy, healthy, 256 * mib, 9000), "Recovery preceded retirement proof");
		policy.Succeeded(9000);
		Require(policy.resumes == 0, "Incomplete retirement was marked recovered");
		policy.Retired(10000);
		policy.Retired(11000);
		Require(policy.retryAfterMs == 15000 && policy.retirements == 1, "Cooldown was not relative to completed retirement");
		for (std::uint64_t now = 10000; now < 15000; now += 250)
			Require(!Observe(policy, healthy, 256 * mib, now), "Recovery ignored the minimum off period");
		Require(Observe(policy, healthy, 256 * mib, 15000), "Stable observation could not overlap cooldown");
		Require(policy.phase == MemoryRecoveryPhase::Rebuilding && policy.resumes == 0, "Admission was reported as completed recovery");
		policy.Succeeded(15000);
		Require(policy.phase == MemoryRecoveryPhase::Probation && policy.resumes == 0, "One evaluation completed recovery");
		for (std::uint64_t now = 15250; now < 25000; now += 250) {
			Require(Observe(policy, healthy, 0, now), "Healthy probation stopped rendering");
			policy.Succeeded(now);
			Require(policy.phase == MemoryRecoveryPhase::Probation, "Probation completed prematurely");
		}
		Require(Observe(policy, healthy, 0, 25000), "Healthy probation stopped at completion");
		policy.Succeeded(25000);
		policy.Succeeded(25000);
		Require(policy.phase == MemoryRecoveryPhase::Ready && policy.resumes == 1 && policy.retryLevel == 1,
			"Probation did not complete exactly once or reset backoff prematurely");
	}

	std::uint64_t StartRecovery(MemoryRecoveryPolicy& policy, std::uint64_t start = 1000, std::uint64_t bytes = 256 * mib)
	{
		policy.Suspend(start);
		policy.Retired(start);
		const auto readyAt = policy.retryAfterMs;
		for (auto now = start; now < readyAt; now += 250)
			Require(!Observe(policy, healthy, bytes, now), "Recovery ignored its cooldown");
		Require(Observe(policy, healthy, bytes, readyAt), "Stable headroom did not admit recovery");
		return readyAt;
	}

	void SuccessfulFrames(MemoryRecoveryPolicy& policy, MemoryBudgetSample current, std::uint64_t start, std::uint64_t end)
	{
		for (auto now = start; now <= end; now += 250) {
			Require(Observe(policy, current, 0, now), "Healthy active NR was rejected");
			policy.Succeeded(now);
		}
	}

	void TestFreshConsecutiveSamples()
	{
		MemoryRecoveryPolicy policy;
		policy.Suspend(1000);
		policy.Retired(1000);
		Require(!Observe(policy, healthy, 0, 1000), "Recovery ignored the cooldown");
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
		for (std::uint64_t now = 11500; now < 14500; now += 250)
			Require(!Observe(policy, healthy, 0, now), "Recovery was premature after pressure");
		Require(Observe(policy, healthy, 0, 14500), "Consecutive fresh samples did not recover");
		Require(!Observe(policy, {}, 0, 14501), "Unknown headroom admitted allocations while rebuilding");
		Require(policy.phase == MemoryRecoveryPhase::Retiring, "Lost recovery proof retained partial resources");
	}

	void TestHeadroomAndInvalidBudgets()
	{
		MemoryRecoveryPolicy policy;
		Require(Observe(policy, {}, 0, 1), "Unavailable monitoring disabled ordinary NR");
		Require(!Observe(policy, healthy, 4000 * mib, 2), "Projected allocation ignored the safety reserve");
		policy.Retired(2);
		Require(!Observe(policy, healthy, std::numeric_limits<std::uint64_t>::max(), 1000), "Oversized allocation was admitted");
		Require(!Observe(policy, { 16000 * mib, 17000 * mib, true }, 0, 1001), "Over-budget usage underflowed headroom");
		Require(!Observe(policy, {}, 0, 1002), "Invalid sample admitted rebuilding");
		Require(!Observe(policy, { 0, 0, true }, 0, 1003), "Zero budget admitted rebuilding");
		Require(!Observe(policy, { 2048 * mib, 1536 * mib, true }, 0, 1004), "Recovery admitted the exact suspension headroom boundary");
		policy = {};
		Require(!Observe(policy, { 16000 * mib, 13500 * mib, true }, 1000 * mib, 2000), "Projected native growth crossed the pressure limit");
		policy.Retired(2000);
		for (std::uint64_t now = 2000; now <= 10000; now += 250)
			Require(!Observe(policy, { 16000 * mib, 13300 * mib, true }, 1000 * mib, now), "Recovery ignored projected growth");
		policy = {};
		Require(Observe(policy, healthy, 256 * mib, 10001), "Explicit reset did not restore admission");
	}

	void TestRelapseCannotRecoverOnItsOwnFreedMemory()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		policy.Succeeded(admittedAt);
		Require(!Observe(policy, high, 0, admittedAt + 250), "Delayed native growth was admitted");
		Require(policy.phase == MemoryRecoveryPhase::Retiring && policy.rapidRelapses == 1 && policy.resumes == 0,
			"Rapid relapse was counted as successful recovery");
		Require(policy.observedRecoveryDemandBytes >= 2128 * mib && policy.knownRecoveryFloorBytes == 256 * mib,
			"Process-wide recovery demand was not learned separately from the known allocation floor");
		policy.Retired(admittedAt + 500);
		for (auto now = admittedAt + 500; now <= 60000; now += 250)
			Require(!Observe(policy, healthy, 256 * mib, now), "NR's own released memory admitted the same failed rebuild");
		auto relieved = healthy;
		relieved.usageBytes = 11000 * mib;
		for (std::uint64_t now = 60250; now < 63250; now += 250)
			Require(!Observe(policy, relieved, 256 * mib, now), "Real relief skipped fresh observation");
		Require(Observe(policy, relieved, 256 * mib, 63250), "Materially better headroom did not admit recovery");
		relieved.usageBytes = 13000 * mib;
		SuccessfulFrames(policy, relieved, 63250, 73250);
		Require(policy.phase == MemoryRecoveryPhase::Ready && policy.resumes == 1, "NR did not remain available after actual relief");
	}

	void TestDelayedGrowthDuringProbation()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		policy.Succeeded(admittedAt + 2400);
		Require(policy.phase == MemoryRecoveryPhase::Probation && !policy.activeWindow,
			"Long initialization gave stale headroom active credit");
		SuccessfulFrames(policy, healthy, admittedAt + 2500, admittedAt + 7500);
		Require(policy.phase == MemoryRecoveryPhase::Probation, "Five healthy seconds completed probation");
		Require(!Observe(policy, high, 0, admittedAt + 7750), "Delayed growth escaped probation pressure checks");
		Require(policy.rapidRelapses == 1 && policy.retryLevel == 2, "Delayed relapse did not retain recovery history");
	}

	void TestActiveHealthNeedsSamplesAndSuccessfulTransactions()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		SuccessfulFrames(policy, healthy, admittedAt, admittedAt + 10000);
		Require(policy.phase == MemoryRecoveryPhase::Ready && policy.retryLevel == 1, "Probation did not preserve the longer retry reset window");
		for (auto now = admittedAt + 10250; now <= admittedAt + 50000; now += 250)
			Require(Observe(policy, healthy, 0, now), "Fresh inactive admission failed");
		policy.Succeeded(admittedAt + 50000);
		Require(policy.retryLevel == 1 && policy.activeSinceMs == admittedAt + 50000,
			"Budget observations without completed NR transactions reset retry history");
		SuccessfulFrames(policy, healthy, admittedAt + 50250, admittedAt + 79750);
		Require(policy.retryLevel == 1, "Retry history cleared before continuous active health");
		SuccessfulFrames(policy, healthy, admittedAt + 80000, admittedAt + 80000);
		Require(policy.retryLevel == 0, "Continuous active health did not reset retry history");
		policy.Suspend(admittedAt + 80250);
		policy.Retired(admittedAt + 80500);
		Require(policy.retryAfterMs == admittedAt + 85500, "A healthy separate pressure episode retained old backoff");
	}

	void TestMenusAndResetsPreservePressureKnowledge()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		SuccessfulFrames(policy, healthy, admittedAt, admittedAt + 10000);
		policy.BreakActiveObservation();
		Require(Observe(policy, {}, 0, admittedAt + 100000), "Missing monitoring disabled ordinary resident NR");
		policy.Succeeded(admittedAt + 100000);
		policy.Suspend(admittedAt + 100250);
		Require(policy.retryLevel == 2, "Menu time reset recovery backoff");
		policy.Retired(admittedAt + 100500);
		const auto deadline = policy.retryAfterMs;
		policy.ResourcesRetired(admittedAt + 101000);
		Require(policy.phase == MemoryRecoveryPhase::Waiting && policy.retryAfterMs == deadline && policy.retryLevel == 2,
			"Normal backend retirement erased waiting state or extended its cooldown");

		MemoryRecoveryPolicy rebuilding;
		const auto rebuildAt = StartRecovery(rebuilding);
		rebuilding.ResourcesRetired(rebuildAt + 250);
		Require(rebuilding.phase == MemoryRecoveryPhase::Waiting && rebuilding.retryAfterMs == rebuildAt + 5250 &&
					rebuilding.suspensions == 1 && rebuilding.retirements == 1 && rebuilding.retryLevel == 1,
			"Ordinary reset during rebuilding erased history or invented pressure");
		MemoryRecoveryPolicy retiring;
		retiring.Suspend(1000);
		retiring.ResourcesRetired(2500);
		Require(retiring.phase == MemoryRecoveryPhase::Waiting && retiring.retryAfterMs == 7500 && retiring.retirements == 1,
			"Normal resource release did not complete pending retirement");
		MemoryRecoveryPolicy ready;
		ready.ResourcesRetired(1000);
		Require(ready.phase == MemoryRecoveryPhase::Ready && ready.suspensions == 0, "Normal ready reset invented recovery");
	}

	void TestPendingReservationsDoNotBecomeNativeDemand()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		auto sample = healthy;
		sample.sampledAtMs = admittedAt + 250;
		sample.usageBytes += 256 * mib;
		Require(policy.Admit(sample, 0, sample.sampledAtMs, 1000 * mib), "Safe pending reservation blocked probation");
		policy.Succeeded(sample.sampledAtMs);
		policy.Suspend(sample.sampledAtMs + 1, true);
		Require(policy.observedRecoveryDemandBytes == 384 * mib && policy.knownRecoveryFloorBytes == 256 * mib,
			"Other-owner reservation was learned as NR's private allocation");
		policy.Retired(sample.sampledAtMs + 2);
		policy.InvalidateCapacityLearning();
		for (auto now = policy.retryAfterMs; now <= policy.retryAfterMs + 4000; now += 250) {
			sample = healthy;
			sample.sampledAtMs = now;
			Require(!policy.Admit(sample, 256 * mib, now, 1400 * mib), "Recovery consumed pending render-scale headroom");
		}
		sample.sampledAtMs += 250;
		Require(!policy.Admit(sample, std::numeric_limits<std::uint64_t>::max(), sample.sampledAtMs,
					std::numeric_limits<std::uint64_t>::max()),
			"Overflowed allocations bypassed admission");
	}

	void TestDemandAgingAndCapacityChanges()
	{
		MemoryRecoveryPolicy policy;
		const auto admittedAt = StartRecovery(policy);
		policy.Succeeded(admittedAt);
		Require(!Observe(policy, high, 0, admittedAt + 250), "Test relapse was not detected");
		policy.Retired(admittedAt + 500);
		const auto learned = policy.observedRecoveryDemandBytes;
		const auto probeAt = policy.demandLearnedAtMs + MemoryRecoveryPolicy::kDemandProbeIntervalMs;
		Require(!Observe(policy, {}, 0, probeAt) && policy.observedRecoveryDemandBytes == learned,
			"Unknown samples aged demand into admission");
		Require(!Observe(policy, healthy, 256 * mib, probeAt), "Uncertain-demand probe skipped stable observations");
		Require(policy.observedRecoveryDemandBytes < learned && policy.observedRecoveryDemandBytes >= 256 * mib &&
					policy.knownRecoveryFloorBytes == 256 * mib && policy.demandProbes == 1 && policy.failedAdmissionCapacityBytes == 0,
			"Demand aging erased the known floor or retained a confounded peak forever");
		for (auto now = probeAt + 250; now < probeAt + 3000; now += 250)
			Require(!Observe(policy, healthy, 256 * mib, now), "Probe skipped stable headroom");
		Require(Observe(policy, healthy, 256 * mib, probeAt + 3000), "Bounded probing could not restore NR after uncertain learning");

		policy.Suspend(probeAt + 3250);
		policy.Retired(probeAt + 3500);
		const auto retry = policy.retryLevel;
		const auto deadline = policy.retryAfterMs;
		policy.InvalidateCapacityLearning();
		Require(policy.observedRecoveryDemandBytes == 0 && policy.knownRecoveryFloorBytes == 0 &&
					!policy.failedAdmissionBarrier && policy.retryLevel == retry && policy.retryAfterMs == deadline &&
					policy.phase == MemoryRecoveryPhase::Waiting,
			"Capacity change cleared generic pressure history or retained incompatible demand");
	}

	void TestStartupKindsKeepIndependentRecoveryDemand()
	{
		MemoryRecoveryPolicy policy;
		Require(policy.startupKind == MemoryStartupKind::Cold &&
					std::string_view(ToString(MemoryStartupKind::Cold)) == "cold" &&
					std::string_view(ToString(MemoryStartupKind::Warm)) == "warm",
			"Startup identity did not default to a named cold backend");
		const auto coldAdmittedAt = StartRecovery(policy);
		SuccessfulFrames(policy, healthy, coldAdmittedAt, coldAdmittedAt + 5000);
		Require(!Observe(policy, high, 0, coldAdmittedAt + 5250), "Late cold-start growth did not suspend recovery");
		policy.Retired(coldAdmittedAt + 5500);
		const auto cold = policy;
		Require(cold.observedRecoveryDemandBytes == 2128 * mib && cold.knownRecoveryFloorBytes == 256 * mib &&
					cold.failedAdmissionBarrier,
			"Cold demand evidence was not established");

		policy.SetStartupKind(MemoryStartupKind::Warm);
		Require(policy.startupKind == MemoryStartupKind::Warm && !policy.sample.valid &&
					!policy.healthyWindow && !policy.activeWindow && policy.requiredBytes == 0 &&
					policy.admissionDemandBytes == 0 && policy.observedRecoveryDemandBytes == 0 &&
					policy.knownRecoveryFloorBytes == 0 && !policy.failedAdmissionBarrier &&
					policy.retryLevel == cold.retryLevel && policy.retryAfterMs == cold.retryAfterMs &&
					policy.suspensions == cold.suspensions && policy.retirements == cold.retirements,
			"Warm startup inherited cold allocation evidence or erased generic pressure history");
		auto warmBaseline = healthy;
		warmBaseline.usageBytes = 12500 * mib;
		const auto warmAdmittedAt = policy.retryAfterMs;
		for (auto now = coldAdmittedAt + 5500; now < warmAdmittedAt; now += 250)
			Require(!Observe(policy, warmBaseline, 128 * mib, now), "Warm startup bypassed the shared cooldown");
		Require(Observe(policy, warmBaseline, 128 * mib, warmAdmittedAt),
			"A cold-only failure prevented a fitting warm recovery");
		auto warmResident = warmBaseline;
		warmResident.usageBytes += 256 * mib;
		SuccessfulFrames(policy, warmResident, warmAdmittedAt, warmAdmittedAt + 5000);
		Require(Observe(policy, warmResident, 0, warmAdmittedAt + 5250), "Healthy late warm observation was rejected");
		policy.Suspend(warmAdmittedAt + 5251, true);
		policy.Retired(warmAdmittedAt + 5500);
		const auto warm = policy;
		Require(warm.observedRecoveryDemandBytes == 384 * mib && warm.knownRecoveryFloorBytes == 128 * mib &&
					warm.failedAdmissionBarrier && warm.failedAdmissionCapacityBytes != cold.failedAdmissionCapacityBytes,
			"Warm late growth was not learned independently");

		policy.SetStartupKind(MemoryStartupKind::Cold);
		Require(policy.observedRecoveryDemandBytes == cold.observedRecoveryDemandBytes &&
					policy.knownRecoveryFloorBytes == cold.knownRecoveryFloorBytes &&
					policy.failedAdmissionCapacityBytes == cold.failedAdmissionCapacityBytes &&
					policy.demandLearnedAtMs == cold.demandLearnedAtMs && policy.failedAdmissionBarrier &&
					policy.retryLevel == warm.retryLevel && policy.retryAfterMs == warm.retryAfterMs &&
					policy.suspensions == warm.suspensions && policy.rapidRelapses == warm.rapidRelapses,
			"Returning to cold startup lost its evidence or rolled back session history");
		for (auto now = policy.retryAfterMs; now <= policy.retryAfterMs + 3000; now += 250)
			Require(!Observe(policy, healthy, 256 * mib, now), "Cold recovery reused a smaller warm demand estimate");

		policy.SetStartupKind(MemoryStartupKind::Warm);
		Require(policy.observedRecoveryDemandBytes == warm.observedRecoveryDemandBytes &&
					policy.knownRecoveryFloorBytes == warm.knownRecoveryFloorBytes &&
					policy.failedAdmissionCapacityBytes == warm.failedAdmissionCapacityBytes &&
					policy.demandLearnedAtMs == warm.demandLearnedAtMs && policy.failedAdmissionBarrier,
			"Returning to warm startup lost its independent failure evidence");
		const auto betterAt = warm.retryAfterMs + 3500;
		Require(!Observe(policy, warmBaseline, 128 * mib, betterAt - 250), "Warm recovery ignored its own failed-admission barrier");
		for (auto now = betterAt; now < betterAt + 3000; now += 250)
			Require(!Observe(policy, healthy, 128 * mib, now), "Startup switching preserved an unrelated healthy window");
		Require(Observe(policy, healthy, 128 * mib, betterAt + 3000), "Materially better warm headroom did not recover");
		policy.Succeeded(betterAt + 3000);
		policy.SetStartupKind(MemoryStartupKind::Warm);
		Require(policy.activeWindow && policy.activeSinceMs == betterAt + 3000 && policy.sample.valid &&
					policy.requiredBytes == 128 * mib && policy.phase == MemoryRecoveryPhase::Probation,
			"An unchanged startup kind restarted observation");

		policy.ResourcesRetired(betterAt + 3250);
		auto aging = policy;
		aging.SetStartupKind(MemoryStartupKind::Cold);
		const auto probeAt = cold.demandLearnedAtMs + MemoryRecoveryPolicy::kDemandProbeIntervalMs;
		Require(!Observe(aging, healthy, 256 * mib, probeAt) &&
					aging.observedRecoveryDemandBytes < cold.observedRecoveryDemandBytes &&
					aging.knownRecoveryFloorBytes == cold.knownRecoveryFloorBytes && aging.demandProbes == 1,
			"Cold uncertainty did not age from its own observation time");
		aging.SetStartupKind(MemoryStartupKind::Warm);
		Require(aging.observedRecoveryDemandBytes == warm.observedRecoveryDemandBytes &&
					aging.demandLearnedAtMs == warm.demandLearnedAtMs && aging.failedAdmissionBarrier,
			"Aging cold evidence altered warm evidence");

		const auto deadline = policy.retryAfterMs;
		policy.InvalidateCapacityLearning();
		for (const auto kind : { MemoryStartupKind::Cold, MemoryStartupKind::Warm }) {
			policy.SetStartupKind(kind);
			Require(policy.observedRecoveryDemandBytes == 0 && policy.knownRecoveryFloorBytes == 0 &&
						policy.failedAdmissionCapacityBytes == 0 && policy.demandLearnedAtMs == 0 &&
						!policy.failedAdmissionBarrier && policy.admissionDemandBytes == 0 &&
						policy.retryLevel == warm.retryLevel && policy.retryAfterMs == deadline &&
						policy.phase == MemoryRecoveryPhase::Waiting && policy.suspensions == warm.suspensions,
				"Capacity invalidation retained another startup's evidence or erased retry history");
		}
	}

	void TestStartupSwitchDiscardsOldAttemptBaseline()
	{
		MemoryRecoveryPolicy policy;
		Require(Observe(policy, healthy, 512 * mib, 1000), "Initial cold allocation was rejected");
		policy.Succeeded(1000);
		policy.SetStartupKind(MemoryStartupKind::Cold);
		Require(policy.activeWindow && policy.sample.valid, "Repeated cold identity broke active observation");

		policy.SetStartupKind(MemoryStartupKind::Warm);
		Require(!policy.activeWindow && !policy.healthyWindow && !policy.sample.valid,
			"Startup change retained pre-retirement observation");
		auto warm = healthy;
		warm.usageBytes = 13000 * mib;
		Require(Observe(policy, warm, 128 * mib, 2000), "Warm allocation was rejected");
		policy.Succeeded(2000);
		warm.usageBytes += 500 * mib;
		Require(Observe(policy, warm, 0, 7000), "Late warm growth below the pressure threshold was rejected");
		policy.Suspend(7001, true);
		Require(policy.observedRecoveryDemandBytes == 628 * mib && policy.knownRecoveryFloorBytes == 128 * mib,
			"Late warm growth reused the pre-retirement cold baseline");
		policy.Retired(7250);
		policy.SetStartupKind(MemoryStartupKind::Cold);
		Require(policy.observedRecoveryDemandBytes == 0 && policy.knownRecoveryFloorBytes == 512 * mib &&
					!policy.failedAdmissionBarrier && policy.demandLearnedAtMs == 0 &&
					policy.admissionDemandBytes == 512 * mib && policy.outOfMemoryFailures == 1,
			"Warm failure contaminated cold evidence or removed its known allocation floor");
	}

	void TestKnownStartupFloorGatesRecovery()
	{
		MemoryRecoveryPolicy policy;
		Require(Observe(policy, healthy, 512 * mib, 1000), "Known cold allocation was not recorded");
		policy.ResourcesRetired(1250);
		policy.SetStartupKind(MemoryStartupKind::Warm);
		policy.SetStartupKind(MemoryStartupKind::Cold);
		Require(policy.knownRecoveryFloorBytes == 512 * mib && policy.observedRecoveryDemandBytes == 0,
			"Startup switching lost a known floor without uncertain growth");
		policy.Suspend(2000);
		policy.Retired(2250);
		auto narrow = healthy;
		narrow.budgetBytes = 4096 * mib;
		narrow.usageBytes = 3100 * mib;
		for (std::uint64_t now = 2250; now <= 10000; now += 250)
			Require(!Observe(policy, narrow, 128 * mib, now), "A smaller current request bypassed known startup capacity");
		Require(policy.admissionDemandBytes == 512 * mib && !policy.healthyWindow,
			"Known startup floor did not constrain admission and future recovery demand");
		for (std::uint64_t now = 10250; now < 13250; now += 250)
			Require(!Observe(policy, healthy, 128 * mib, now), "Better headroom skipped fresh observation");
		Require(Observe(policy, healthy, 128 * mib, 13250), "Sufficient capacity could not recover a known startup floor");
		policy.ResourcesRetired(13500);
		policy.InvalidateCapacityLearning();
		for (std::uint64_t now = 13750; now < 18500; now += 250)
			Require(!Observe(policy, narrow, 128 * mib, now), "Capacity invalidation bypassed the existing cooldown");
		Require(Observe(policy, narrow, 128 * mib, 18500) && policy.admissionDemandBytes == 128 * mib,
			"Changed allocation capacity retained an incompatible known floor");
	}

	void TestDlssWarningsRequireFreshRecovery()
	{
		MemoryRecoveryPolicy policy;
		policy.ReportDlssWarning(1000);
		Require(policy.phase == MemoryRecoveryPhase::Retiring && policy.dlssWarning, "DLSS warning did not request retirement");
		Require(policy.outOfMemoryFailures == 0, "Valid DLSS output was counted as an allocation failure");
		Require(!Observe(policy, healthy, 0, 1500), "DLSS warning bypassed retirement");
		policy.Retired(2000);
		Require(!Observe(policy, healthy, 0, 2000), "DLSS recovery skipped stable headroom");
		policy.ReportDlssWarning(4500);
		policy.ReportDlssWarning(4750);
		Require(policy.suspensions == 1 && policy.retirements == 1 && policy.retryLevel == 1 && policy.retryAfterMs == 9750,
			"Repeated warnings escalated the same episode or failed to postpone rebuilding");
		for (std::uint64_t now = 5000; now < 9750; now += 250)
			Require(!Observe(policy, healthy, 0, now), "Renewed warning did not extend the cooldown");
		Require(Observe(policy, healthy, 0, 9750), "DLSS recovery did not admit a rebuild");
		policy.Succeeded(9750);
		Require(policy.dlssWarning && policy.resumes == 0, "Warning cleared on the first successful frame");
		SuccessfulFrames(policy, healthy, 10000, 19750);
		Require(!policy.dlssWarning && policy.dlssWarnings == 3 && policy.resumes == 1,
			"Successful probation retained the warning or lost its diagnostic count");
	}

	void TestRepeatedPressureBackoff()
	{
		MemoryRecoveryPolicy policy;
		std::uint64_t nowMs = 1000;
		constexpr std::uint64_t delays[]{ 5000, 15000, 30000, 60000, 60000, 60000 };
		for (const auto delay : delays) {
			policy.Suspend(nowMs, true);
			nowMs += 250;
			policy.Retired(nowMs);
			Require(policy.retryAfterMs - nowMs == delay, "Retry delay did not follow bounded relapse backoff");
			policy.InvalidateCapacityLearning();
			const auto deadline = policy.retryAfterMs;
			for (; nowMs < deadline; nowMs += 250)
				Require(!Observe(policy, healthy, 128 * mib, nowMs), "Retry ignored backoff");
			Require(Observe(policy, healthy, 128 * mib, nowMs), "Retry did not resume after healthy samples");
			policy.Succeeded(nowMs);
			Require(policy.phase == MemoryRecoveryPhase::Probation, "One successful frame completed repeated recovery");
			++nowMs;
		}
		Require(policy.resumes == 0 && policy.retryLevel == 4 && policy.outOfMemoryFailures == 6,
			"Repeated failed recoveries were counted as healthy or permanently disabled NR");
	}
}

int main()
{
	try {
		TestConservationHysteresis();
		TestConservationNeedsFreshHeadroom();
		TestTrimLimitSurvivesRecoveryAndRebuilds();
		TestConservationHeadroomAndReclaimLimits();
		TestRetirementAndCompletion();
		TestFreshConsecutiveSamples();
		TestHeadroomAndInvalidBudgets();
		TestRelapseCannotRecoverOnItsOwnFreedMemory();
		TestDelayedGrowthDuringProbation();
		TestActiveHealthNeedsSamplesAndSuccessfulTransactions();
		TestMenusAndResetsPreservePressureKnowledge();
		TestPendingReservationsDoNotBecomeNativeDemand();
		TestDemandAgingAndCapacityChanges();
		TestStartupKindsKeepIndependentRecoveryDemand();
		TestStartupSwitchDiscardsOldAttemptBaseline();
		TestKnownStartupFloorGatesRecovery();
		TestDlssWarningsRequireFreshRecovery();
		TestRepeatedPressureBackoff();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
