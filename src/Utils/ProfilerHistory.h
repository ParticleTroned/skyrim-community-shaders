#pragma once

#include "ProfilerTiming.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Util
{
	struct ProfilerHistory
	{
		static constexpr uint32_t kCapacity = 300;
		float history[kCapacity]{};
		uint32_t head = 0;
		uint32_t count = 0;
		float lastMs = 0.0f;

		void PushSample(float ms)
		{
			history[head] = ms;
			head = (head + 1) % kCapacity;
			if (count < kCapacity)
				count++;
			lastMs = ms;
		}

		float GetAverage() const { return GetAverage(count); }
		float GetAverage(uint32_t maxSamples) const
		{
			if (count == 0 || maxSamples == 0)
				return lastMs;

			const uint32_t sampleCount = std::min(count, maxSamples);
			float sum = 0.0f;
			const uint32_t firstSample = count - sampleCount;
			for (uint32_t i = 0; i < sampleCount; i++)
				sum += GetSample(firstSample + i);
			return sum / static_cast<float>(sampleCount);
		}
		float GetPercentile(float p) const
		{
			if (count == 0)
				return lastMs;

			thread_local std::vector<float> sorted;
			sorted.resize(count);
			for (uint32_t i = 0; i < count; i++)
				sorted[i] = history[i];
			std::sort(sorted.begin(), sorted.end());

			float idx = (p / 100.0f) * static_cast<float>(count - 1);
			uint32_t lo = static_cast<uint32_t>(idx);
			uint32_t hi = std::min(lo + 1, count - 1);
			float frac = idx - static_cast<float>(lo);
			return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
		}
		ProfilerTiming::Statistics GetStatistics() const
		{
			return count ? ProfilerTiming::CalculateStatistics({ history, count }) : ProfilerTiming::Statistics{ lastMs, lastMs, lastMs };
		}

		float GetSample(uint32_t index) const
		{
			if (index >= count)
				return 0.0f;
			return history[(head - count + index + kCapacity) % kCapacity];
		}
	};
}
