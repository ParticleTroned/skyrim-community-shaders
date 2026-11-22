#include "ShaderCompilationEstimate.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
	using namespace ShaderCompilationEstimate;
	constexpr Category heavy{ 100, 60'000 };
	constexpr Category light{ 2, 1000 };

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void QueueMany(Estimator& a_estimate, Category a_category, unsigned a_count)
	{
		for (unsigned i = 0; i < a_count; ++i)
			a_estimate.Queue(a_category);
	}

	void ExpensiveFirstDoesNotScaleCheapWork()
	{
		Estimator estimate;
		QueueMany(estimate, heavy, 3);
		QueueMany(estimate, light, 100);
		for (unsigned i = 0; i < 3; ++i) {
			estimate.Start(i, heavy, 0);
			estimate.MarkSourceCompile(i);
		}
		Require(!estimate.Estimate(1000, 4).calibrated, "First-run estimates need calibration");
		for (unsigned i = 0; i < 3; ++i)
			estimate.Complete(i, 60'000, Outcome::Compiled, true);
		for (unsigned i = 0; i < 4; ++i)
			estimate.Start(10 + i, light, 60'000);
		const auto result = estimate.Estimate(60'000, 4);
		Require(!result.calibrated && result.remainingMs == 25'000, "One heavy category must not calibrate or inflate unknown work");
		Require(result.workers == 4 && result.sourceSamples == 3, "Use available workers and source samples");
	}

	void HistoryAndActiveTail()
	{
		Estimator estimate;
		Require(estimate.Seed({ heavy.index, 120'000 }), "Accept bounded history");
		estimate.Queue(heavy);
		estimate.Start(1, heavy, 0);
		estimate.MarkSourceCompile(1);
		const auto halfway = estimate.Estimate(60'000, 16);
		Require(halfway.calibrated && halfway.remainingMs == 60'000 && halfway.workers == 1,
			"One running shader is not accelerated by idle workers");
		const auto overrun = estimate.Estimate(150'000, 16);
		Require(overrun.remainingMs > 0 && overrun.lowerRemainingMs > 0, "Unfinished work cannot report zero remaining");
		estimate.Complete(1, 150'000, Outcome::Compiled, true);
		Require(!estimate.Estimate(150'001, 16).active, "Completion bypasses display refresh delay");
		const auto history = estimate.History();
		Require(history.size() == 1 && history.front().milliseconds > 120'000, "Source timings refine priors");
	}

	void ReuseCannotTeachCompileCost()
	{
		Estimator estimate;
		estimate.Seed({ heavy.index, 60'000 });
		QueueMany(estimate, heavy, 14);
		for (unsigned i = 0; i < 12; ++i) {
			estimate.Start(i, heavy, 0);
			estimate.Complete(i, 10, i % 2 ? Outcome::Disk : Outcome::Shared, true);
		}
		const auto warm = estimate.Estimate(2000, 1);
		Require(warm.remainingMs < 30'000, "Reuse rate reduces predicted queue work");
		Require(estimate.History().front().milliseconds == 60'000, "Reuse must not pollute source history");
		estimate.Start(20, heavy, 2000);
		estimate.MarkSourceCompile(20);
		const auto source = estimate.Estimate(3000, 8);
		Require(source.remainingMs >= 59'000, "Known running source compile ignores cache discounts");
		estimate.CancelQueued(heavy);
		estimate.CancelRunning(20);
		Require(estimate.PendingTasks() == 0 && !estimate.Estimate(3001, 1).active, "Cancellation retires all work");
		estimate.Queue(heavy);
		estimate.Start(30, heavy, 3002);
		Require(estimate.Estimate(3002, 1).remainingMs == 60'000, "New batch retains cost history, not cache hit rates");
	}

	void CacheHitsCannotCalibrateUnknownSourceWork()
	{
		Estimator estimate;
		QueueMany(estimate, heavy, 10);
		for (unsigned i = 0; i < 3; ++i) {
			estimate.Start(i, heavy, i * 10);
			estimate.Complete(i, 10, Outcome::Disk, true);
		}
		const auto result = estimate.Estimate(2000, 1);
		Require(result.enabled && result.active && !result.calibrated,
			"A few cheap cache hits cannot calibrate unmeasured source compilation");
		Require(result.learnedFraction < 0.01 && result.sourceSamples == 0,
			"Calibration must reflect known remaining cost, not just observed hit count");
	}

	void LoweredLimitDoesNotSerializeActiveWorkers()
	{
		Estimator estimate;
		estimate.Seed({ heavy.index, 60'000 });
		QueueMany(estimate, heavy, 8);
		for (unsigned i = 0; i < 8; ++i) {
			estimate.Start(i, heavy, 0);
			estimate.MarkSourceCompile(i);
		}
		const auto running = estimate.Estimate(10'000, 2);
		Require(running.remainingMs == 50'000, "Lowered dispatch capacity cannot slow already running work");
		QueueMany(estimate, heavy, 4);
		const auto queued = estimate.Estimate(11'000, 2);
		Require(queued.remainingMs == 169'000, "Future work still uses the reduced dispatch limit");
	}

	void InvalidationAndBadSamples()
	{
		Estimator estimate;
		Require(!estimate.Seed({ kCategoryCount, 1 }), "Reject invalid category");
		Require(!estimate.Seed({ light.index, 1, 0 }) && !estimate.Seed({ light.index, 1, 9 }), "Reject invalid history sample count");
		for (double value : { 0.0, -1.0, std::numeric_limits<double>::infinity(),
				 std::numeric_limits<double>::quiet_NaN(), kMaximumSampleMs + 1 })
			Require(!estimate.Seed({ light.index, value }), "Reject invalid duration");
		estimate.Seed({ light.index, 3000 });
		QueueMany(estimate, light, 5);
		estimate.Start(1, light, 0);
		estimate.Start(2, light, 0);
		estimate.ClearLearning();
		estimate.Complete(1, 200'000, Outcome::Compiled, false);
		estimate.Complete(2, 200'000, Outcome::Failed, true);
		Require(estimate.History().empty(), "Stale contexts and failed compiles must not teach costs");
		Require(estimate.PendingTasks() == 3, "Learning invalidation retains queued work");
		estimate.ClearWork();
		estimate.Complete(1, 10, Outcome::Compiled, true);
		Require(!estimate.Estimate(3000, 0).active && estimate.History().empty(), "Detached completion cannot revive cleared work");
	}

	void RefreshAndWorkerLimits()
	{
		Estimator estimate;
		estimate.Seed({ light.index, 1000 });
		QueueMany(estimate, light, 100);
		for (unsigned i = 0; i < 8; ++i)
			estimate.Start(i, light, 0);
		const auto full = estimate.Estimate(0, 8);
		Require(!full.calibrated, "Calibration remains provisional during worker startup, even with history");
		Require(full.remainingMs == 12'500, "Parallel queue estimate");
		estimate.Complete(1, 100, Outcome::Compiled, true);
		Require(estimate.Estimate(100, 8).remainingMs == full.remainingMs, "Completions do not force a per-frame scan");
		const auto limited = estimate.Estimate(100, 2);
		Require(limited.workers == 2 && limited.remainingMs > full.remainingMs, "Changed worker limit refreshes immediately");
		const auto zero = estimate.Estimate(1200, 0);
		Require(std::isfinite(zero.remainingMs) && zero.workers == 1, "Zero capacity cannot divide by zero");
	}

	void StartupCapacityDoesNotSwingDuringDispatch()
	{
		Estimator estimate;
		QueueMany(estimate, light, 100);
		const auto queued = estimate.Estimate(0, 8);
		estimate.Start(1, light, 0);
		const auto dispatching = estimate.Estimate(1000, 8);
		Require(queued.workers == 8 && dispatching.workers == 8 && dispatching.remainingMs <= queued.remainingMs,
			"The first dispatched worker cannot multiply an immediate queue estimate");
		const auto raised = estimate.Estimate(1001, 16);
		Require(raised.workers == 16 && raised.remainingMs < dispatching.remainingMs,
			"Raising dispatch capacity must update future work before extra workers start");
	}

	constexpr std::array<Category, 3> calibrationCategories{ { { 100, 60'000 }, { 124, 30'000 }, { 148, 12'000 } } };
	constexpr Category unmeasured{ 1, 1000 };

	void LearnCategory(Estimator& a_estimate, Category a_category, double a_scale, unsigned a_samples = 3,
		Outcome a_outcome = Outcome::Compiled, bool a_learn = true)
	{
		QueueMany(a_estimate, a_category, a_samples);
		for (unsigned i = 0; i < a_samples; ++i) {
			a_estimate.Start(i, a_category, 0);
			a_estimate.Complete(i, a_category.fallbackMs * a_scale, a_outcome, a_learn);
		}
	}

	void MachineSpeedScalesUnmeasuredCategories()
	{
		for (const double speed : { 0.25, 0.5, 1.0, 2.0, 4.0 }) {
			for (const unsigned workers : { 1u, 4u, 16u }) {
				Estimator estimate;
				QueueMany(estimate, unmeasured, 960);
				for (const auto category : calibrationCategories)
					LearnCategory(estimate, category, speed);
				for (unsigned i = 0; i < workers; ++i) {
					estimate.Start(100 + i, unmeasured, 2000);
					estimate.MarkSourceCompile(100 + i);
				}
				const auto result = estimate.Estimate(2000, workers);
				Require(result.calibrated && result.remainingMs == 960'000 * speed / workers,
					"Unmeasured work must follow machine speed in both directions and respect worker capacity");
				Require(result.learnedFraction == 0 && result.upperRemainingMs == result.remainingMs * 2,
					"Inferred costs must retain broad uncertainty rather than claim direct measurement");
			}
		}
	}

	void CalibrationResistsCategoryBiasAndSeparatesStages()
	{
		Estimator estimate;
		QueueMany(estimate, unmeasured, 100);
		LearnCategory(estimate, calibrationCategories[0], 8, 80);
		LearnCategory(estimate, calibrationCategories[1], 0.5);
		Require(!estimate.Estimate(2000, 1).calibrated, "Two categories cannot establish a stage speed");
		LearnCategory(estimate, calibrationCategories[2], 0.5);
		const auto inferred = estimate.Estimate(3000, 1);
		Require(inferred.calibrated && inferred.remainingMs == 50'000,
			"A common outlier cannot outweigh the median normalized category cost");
		QueueMany(estimate, light, 10);
		const auto mostlySupported = estimate.Estimate(4000, 1);
		Require(mostlySupported.calibrated && mostlySupported.remainingMs == 60'000,
			"A small unsupported stage must not hide an otherwise calibrated estimate");
		QueueMany(estimate, light, 90);
		const auto mixedStages = estimate.Estimate(5000, 1);
		Require(mixedStages.remainingMs == 150'000 && !mixedStages.calibrated,
			"A measured stage must not scale or calibrate an unmeasured stage");
		LearnCategory(estimate, unmeasured, 0.75);
		Require(estimate.Estimate(6000, 1).remainingMs == 175'000,
			"Direct category measurements override inferred machine speed");
	}

	void CalibrationHistoryAndInvalidation()
	{
		Estimator source;
		for (const auto category : calibrationCategories)
			LearnCategory(source, category, 0.5);
		Estimator restored;
		for (const auto& entry : source.History())
			Require(restored.Seed(entry), "Measured categories round-trip through existing history");
		QueueMany(restored, unmeasured, 100);
		restored.Start(100, unmeasured, 2000);
		Require(!restored.Estimate(4000, 1).calibrated,
			"History without a known category baseline cannot infer machine speed");
		for (const auto category : calibrationCategories) {
			restored.Queue(category);
			restored.CancelQueued(category);
		}
		Require(restored.Estimate(5000, 1).calibrated && restored.Estimate(5000, 1).remainingMs < 50'000,
			"Compatible history calibrates known baselines without new source compiles");
		restored.ClearLearning();
		const auto cleared = restored.Estimate(6000, 1);
		Require(!cleared.calibrated && cleared.remainingMs > 99'000,
			"Compiler-context invalidation removes inferred speed and preserves pending work");
		for (const auto category : calibrationCategories) {
			LearnCategory(restored, category, 0.01, 8, Outcome::Disk);
			LearnCategory(restored, category, 0.01, 8, Outcome::Shared);
			LearnCategory(restored, category, 0.01, 8, Outcome::Failed);
			LearnCategory(restored, category, 0.01, 8, Outcome::Compiled, false);
		}
		Require(!restored.Estimate(7000, 1).calibrated && restored.History().empty(),
			"Reuse, failures and stale completions cannot teach machine speed");
	}
}

int main()
{
	ExpensiveFirstDoesNotScaleCheapWork();
	HistoryAndActiveTail();
	ReuseCannotTeachCompileCost();
	CacheHitsCannotCalibrateUnknownSourceWork();
	LoweredLimitDoesNotSerializeActiveWorkers();
	InvalidationAndBadSamples();
	RefreshAndWorkerLimits();
	StartupCapacityDoesNotSwingDuringDispatch();
	MachineSpeedScalesUnmeasuredCategories();
	CalibrationResistsCategoryBiasAndSeparatesStages();
	CalibrationHistoryAndInvalidation();
}
