#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ShaderCompilationEstimate
{
	inline constexpr std::size_t kStageCount = 3;
	inline constexpr std::size_t kFamilyCount = 16;
	inline constexpr std::size_t kLightingCategoryOffset = kStageCount * kFamilyCount;
	inline constexpr std::size_t kCategoryCount = kLightingCategoryOffset + kStageCount * 64 * 8;
	inline constexpr double kMaximumSampleMs = 30.0 * 60.0 * 1000.0;
	inline constexpr std::uint32_t kMinimumCostSamples = 3;
	inline constexpr std::size_t kMinimumCalibrationCategories = 3;

	struct Category
	{
		std::size_t index = 0;
		double fallbackMs = 1000.0;
	};

	enum class Outcome
	{
		Compiled,
		Disk,
		Shared,
		Failed,
	};

	struct HistoryEntry
	{
		std::size_t category = 0;
		double milliseconds = 0;
		std::uint32_t samples = 4;
	};

	struct Snapshot
	{
		bool enabled = false;
		bool active = false;
		bool calibrated = false;
		double elapsedMs = 0;
		double remainingMs = 0;
		double lowerRemainingMs = 0;
		double upperRemainingMs = 0;
		double learnedFraction = 0;
		std::uint64_t sourceSamples = 0;
		std::size_t workers = 0;
	};

	/** Estimates queued work independently of dispatch priority. The owner serializes access. */
	class Estimator
	{
	public:
		[[nodiscard]] std::uint64_t PendingTasks() const { return pendingTasks; }

		void Queue(Category a_category)
		{
			if (pendingTasks == 0)
				ClearWork();
			auto& group = groups.at(a_category.index);
			group.fallbackMs = a_category.fallbackMs;
			++group.queued;
			++pendingTasks;
		}

		void CancelQueued(Category a_category)
		{
			auto& count = groups.at(a_category.index).queued;
			if (count > 0) {
				--count;
				--pendingTasks;
			}
		}

		void Start(std::size_t a_id, Category a_category, double a_nowMs)
		{
			auto& queued = groups.at(a_category.index).queued;
			if (queued > 0)
				--queued;
			running.insert_or_assign(a_id, Running{ a_category.index, a_nowMs });
			if (firstStartMs < 0)
				firstStartMs = a_nowMs;
		}

		void MarkSourceCompile(std::size_t a_id)
		{
			if (auto it = running.find(a_id); it != running.end())
				it->second.sourceCompile = true;
		}

		void CancelRunning(std::size_t a_id)
		{
			pendingTasks -= running.erase(a_id);
		}

		/** Returns true only when a valid source completion changes persistent compile costs. */
		bool Complete(std::size_t a_id, double a_elapsedMs, Outcome a_outcome, bool a_learn)
		{
			const auto it = running.find(a_id);
			if (it == running.end())
				return false;
			bool learnedSource = false;
			auto& group = groups[it->second.category];
			if (a_learn && a_outcome != Outcome::Failed && ValidSample(a_elapsedMs)) {
				switch (a_outcome) {
				case Outcome::Compiled:
					group.compile.Add(a_elapsedMs);
					++group.compiled;
					++sourceSamples;
					learnedSource = true;
					break;
				case Outcome::Disk:
					group.disk.Add(a_elapsedMs);
					++group.diskHits;
					break;
				case Outcome::Shared:
					group.shared.Add(a_elapsedMs);
					++group.sharedHits;
					break;
				default:
					break;
				}
			}
			running.erase(it);
			--pendingTasks;
			return learnedSource;
		}

		/** Drops pending work but retains cost priors; reuse rates belong only to the current batch. */
		void ClearWork()
		{
			running.clear();
			for (auto& group : groups) {
				group.queued = 0;
				group.compiled = group.diskHits = group.sharedHits = 0;
				group.disk = group.shared = {};
			}
			sourceSamples = 0;
			pendingTasks = 0;
			firstStartMs = -1;
			lastRefreshMs = -1;
			cached = {};
		}

		/** Retains pending work when compiler settings change, without retaining incompatible measurements. */
		void ClearLearning()
		{
			for (auto& group : groups) {
				group.compile = group.disk = group.shared = {};
				group.compiled = group.diskHits = group.sharedHits = 0;
			}
			sourceSamples = 0;
			lastRefreshMs = -1;
		}

		bool Seed(const HistoryEntry& a_entry)
		{
			if (a_entry.category >= groups.size() || !ValidSample(a_entry.milliseconds) || a_entry.samples == 0 || a_entry.samples > 8)
				return false;
			groups[a_entry.category].compile = { a_entry.milliseconds, std::min(a_entry.samples, 4u) };
			return true;
		}

		[[nodiscard]] std::vector<HistoryEntry> History() const
		{
			std::vector<HistoryEntry> entries;
			for (std::size_t i = 0; i < groups.size(); ++i) {
				if (groups[i].compile.samples > 0)
					entries.push_back({ i, groups[i].compile.meanMs, groups[i].compile.samples });
			}
			return entries;
		}

		/** Refreshes at most once per second; unfinished jobs retain a nonzero tail estimate. */
		Snapshot Estimate(double a_nowMs, std::size_t a_workerLimit)
		{
			if (pendingTasks == 0) {
				cached = {};
				cached.enabled = true;
				cached.sourceSamples = sourceSamples;
				return cached;
			}
			if (lastRefreshMs >= 0 && a_nowMs - lastRefreshMs < 1000.0 && a_workerLimit == lastWorkerLimit)
				return cached;
			lastRefreshMs = a_nowMs;
			lastWorkerLimit = a_workerLimit;
			Snapshot result;
			result.enabled = true;
			result.active = true;
			result.sourceSamples = sourceSamples;
			result.workers = std::max<std::size_t>(1, std::min<std::uint64_t>(a_workerLimit, pendingTasks));
			const auto calibration = CalibrateStages();
			double workMs = 0;
			double learnedMs = 0;
			double supportedMs = 0;
			double longestMs = 0;
			for (std::size_t i = 0; i < groups.size(); ++i) {
				const auto& group = groups[i];
				if (group.queued == 0)
					continue;
				const auto cost = ExpectedCost(group, calibration[i % kStageCount]);
				workMs += cost.totalMs * static_cast<double>(group.queued);
				learnedMs += cost.learnedMs * static_cast<double>(group.queued);
				supportedMs += cost.supportedMs * static_cast<double>(group.queued);
				longestMs = std::max(longestMs, cost.totalMs);
			}
			activeCosts.clear();
			for (const auto& [id, task] : running) {
				const auto& group = groups[task.category];
				const auto& stage = calibration[task.category % kStageCount];
				const auto cost = task.sourceCompile ?
				                      CompileCost(group, stage) :
				                      ExpectedCost(group, stage);
				const auto duration = cost.totalMs;
				const auto elapsed = std::max(0.0, a_nowMs - task.startMs);
				const auto remaining = std::max(duration - elapsed, std::max(duration, elapsed) * 0.1);
				longestMs = std::max(longestMs, remaining);
				activeCosts.push_back({ remaining, duration > 0 ? remaining * cost.learnedMs / duration : 0,
					duration > 0 ? remaining * cost.supportedMs / duration : 0 });
			}
			// A lowered dispatch limit does not serialize workers already running above that limit.
			if (activeCosts.size() > result.workers)
				std::nth_element(activeCosts.begin(), activeCosts.begin() + result.workers, activeCosts.end(),
					[](const Cost& a_left, const Cost& a_right) { return a_left.totalMs > a_right.totalMs; });
			for (std::size_t i = 0; i < std::min(result.workers, activeCosts.size()); ++i) {
				workMs += activeCosts[i].totalMs;
				learnedMs += activeCosts[i].learnedMs;
				supportedMs += activeCosts[i].supportedMs;
			}
			result.remainingMs = std::max(longestMs, workMs / static_cast<double>(result.workers));
			result.learnedFraction = workMs > 0 ? std::clamp(learnedMs / workMs, 0.0, 1.0) : 0;
			result.calibrated = firstStartMs >= 0 && a_nowMs - firstStartMs >= 2000.0 && workMs > 0 && supportedMs >= workMs * 0.5;
			const bool confident = result.learnedFraction >= 0.8;
			result.lowerRemainingMs = result.remainingMs * (confident ? 0.8 : 0.5);
			result.upperRemainingMs = result.remainingMs * (confident ? 1.3 : 2.0);
			cached = result;
			return result;
		}

	private:
		struct Average
		{
			double meanMs = 0;
			std::uint32_t samples = 0;

			void Add(double a_ms)
			{
				samples = std::min(samples + 1, 8u);
				meanMs += (a_ms - meanMs) / samples;
			}
		};
		struct Group
		{
			double fallbackMs = 0;
			Average compile, disk, shared;
			std::uint64_t queued = 0;
			std::uint64_t compiled = 0;
			std::uint64_t diskHits = 0;
			std::uint64_t sharedHits = 0;
		};
		struct Running
		{
			std::size_t category;
			double startMs;
			bool sourceCompile = false;
		};
		struct Cost
		{
			double totalMs;
			double learnedMs;
			double supportedMs;
		};
		struct StageCalibration
		{
			double scale = 1;
			bool ready = false;
		};
		static bool ValidSample(double a_ms)
		{
			return std::isfinite(a_ms) && a_ms > 0 && a_ms <= kMaximumSampleMs;
		}
		std::array<StageCalibration, kStageCount> CalibrateStages() const
		{
			std::array<std::vector<double>, kStageCount> ratios;
			for (std::size_t i = 0; i < groups.size(); ++i) {
				const auto& group = groups[i];
				if (group.compile.samples >= kMinimumCostSamples && group.fallbackMs > 0) {
					const auto ratio = group.compile.meanMs / group.fallbackMs;
					if (std::isfinite(ratio) && ratio > 0)
						ratios[i % kStageCount].push_back(ratio);
				}
			}
			std::array<StageCalibration, kStageCount> result{};
			for (std::size_t i = 0; i < ratios.size(); ++i) {
				auto& stage = ratios[i];
				if (stage.size() < kMinimumCalibrationCategories)
					continue;
				// Equal category weights keep one common or unusually expensive shader from dominating.
				std::sort(stage.begin(), stage.end());
				const auto middle = stage.size() / 2;
				const auto median = stage.size() % 2 ? stage[middle] : (stage[middle - 1] + stage[middle]) * 0.5;
				result[i] = { median, true };
			}
			return result;
		}
		static Cost CompileCost(const Group& a_group, const StageCalibration& a_calibration)
		{
			if (a_group.compile.samples) {
				const auto learned = a_group.compile.samples >= kMinimumCostSamples ? a_group.compile.meanMs : 0;
				return { a_group.compile.meanMs, learned, learned };
			}
			const auto duration = std::min(a_group.fallbackMs * a_calibration.scale, kMaximumSampleMs);
			// Inferred stage costs support an initial estimate, but retain the wider uncertainty range.
			return { duration, 0, a_calibration.ready ? duration : 0 };
		}
		static Cost ExpectedCost(const Group& a_group, const StageCalibration& a_calibration)
		{
			// Two source-work priors avoid treating a few cache hits as a fully warm queue.
			const double source = static_cast<double>(a_group.compiled) + 2.0;
			const auto compile = CompileCost(a_group, a_calibration);
			const double sourceMs = source * compile.totalMs;
			const double diskMs = a_group.diskHits * a_group.disk.meanMs;
			const double sharedMs = a_group.sharedHits * a_group.shared.meanMs;
			const double observations = source + a_group.diskHits + a_group.sharedHits;
			const double learnedReuseMs = (a_group.disk.samples >= kMinimumCostSamples ? diskMs : 0) +
			                              (a_group.shared.samples >= kMinimumCostSamples ? sharedMs : 0);
			return { (sourceMs + diskMs + sharedMs) / observations,
				(source * compile.learnedMs + learnedReuseMs) / observations,
				(source * compile.supportedMs + learnedReuseMs) / observations };
		}

		std::array<Group, kCategoryCount> groups{};
		std::unordered_map<std::size_t, Running> running;
		std::vector<Cost> activeCosts;
		std::uint64_t sourceSamples = 0;
		std::uint64_t pendingTasks = 0;
		double firstStartMs = -1;
		double lastRefreshMs = -1;
		std::size_t lastWorkerLimit = 0;
		Snapshot cached;
	};
}
