#pragma once

#include "ProfilerTiming.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>

namespace Util::FlatFrameTiming
{
	inline constexpr uint32_t kRetainedFrames = 64;

	inline bool IsValid(double ms) { return ProfilerTiming::IsValidSample(ms) && ms > 0.0; }

	struct Sample
	{
		uint64_t presentId = 0;
		uint32_t frame = 0;
		float cpuMs = 0.0f;
		float gpuMs = 0.0f;
		bool hasCpu = false;
		bool hasGpu = false;
		bool resolved = true;
	};

	/** @brief Associates delayed GPU queries with accepted game-produced Presents. */
	struct History
	{
		std::deque<Sample> samples;
		uint64_t presentId = 0;
		uint64_t epoch = 0;
		double lastPresentStartMs = 0.0;

		void Reset()
		{
			samples.clear();
			lastPresentStartMs = 0.0;
			++epoch;
		}

		uint64_t Complete(Sample sample, double startMs, bool accepted)
		{
			if (!accepted || !std::isfinite(startMs) || startMs <= 0.0) {
				Reset();
				return 0;
			}
			const double interval = startMs - lastPresentStartMs;
			if (lastPresentStartMs > 0.0 && !IsValid(interval))
				Reset();
			sample.presentId = ++presentId;
			lastPresentStartMs = startMs;
			samples.push_back(sample);
			while (samples.size() > kRetainedFrames)
				samples.pop_front();
			return presentId;
		}

		void Resolve(uint64_t id, float gpuMs)
		{
			for (auto& sample : samples) {
				if (sample.presentId != id)
					continue;
				sample.hasGpu = IsValid(gpuMs);
				sample.gpuMs = sample.hasGpu ? gpuMs : 0.0f;
				sample.resolved = true;
				break;
			}
		}
	};

	template <std::size_t BlockCount>
	struct PendingSample
	{
		uint64_t presentId = 0;
		uint64_t epoch = 0;
		std::array<double, BlockCount> weights{};
	};

	/** @brief Resolves CPU/GPU into their original blocks without extending a measurement phase. */
	template <std::size_t BlockCount, class Consumer>
	void ResolvePending(std::deque<PendingSample<BlockCount>>& pendingSamples, std::span<const Sample> samples,
		uint64_t latestPresentId, uint64_t epoch, bool finalize, Consumer&& consume)
	{
		while (!pendingSamples.empty()) {
			const auto& pending = pendingSamples.front();
			const auto match = std::ranges::find(samples, pending.presentId, &Sample::presentId);
			const bool sameEpoch = pending.epoch == epoch;
			const bool found = sameEpoch && match != samples.end();
			const bool expired = latestPresentId > pending.presentId && latestPresentId - pending.presentId >= kRetainedFrames;
			if (!finalize && sameEpoch && !expired && (!found || !match->resolved))
				break;
			consume(pending, found ? &*match : nullptr);
			pendingSamples.pop_front();
		}
	}
}
