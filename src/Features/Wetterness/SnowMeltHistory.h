#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace WetternessSnowMelt
{
	inline constexpr uint32_t MaxSamples = 32;
	inline constexpr uint32_t MaxFires = 8;
	inline constexpr double MaximumDryingHours = 24.0;

	struct Sample
	{
		// Snow accumulation, precipitation density, seasonal altitude, age in game hours.
		std::array<float, 4> state{};
		uint32_t fireCount = 0;
		float fireStrength = 0.0f;
		float fireRadiusScale = 1.0f;
		uint32_t padding = 0;
		std::array<std::array<float, 4>, MaxFires> fires{};
		std::array<float, 4> lightDryingAge{};
		std::array<float, 4> heavyDryingAge{};
	};
	static_assert(sizeof(Sample) == 192);

	struct PerFrame
	{
		uint32_t count = 0;
		uint32_t enabled = 0;
		uint32_t padding[2]{};
		std::array<float, 4> lightDryingHours{};
		std::array<float, 4> heavyDryingHours{};
		std::array<Sample, MaxSamples> samples{};
	};
	static_assert(sizeof(PerFrame) == 48 + MaxSamples * sizeof(Sample));

	/** Bounded world history; the shader evaluates each sample on the original snow footprint. */
	class History
	{
	public:
		void Clear() { *this = {}; }
		bool HasMelt() const
		{
			if (count < 2 || now - lastMelt >= MaximumDryingHours)
				return false;
			for (uint32_t i = 0; i < dryingClock.size(); ++i)
				if (dryingClock[i] - lastMeltClock[i] < 1.0)
					return true;
			return false;
		}
		bool HasSamples() const { return count != 0; }
		double LastHours() const { return now; }
		/** Requires HasSamples(); the last observation anchors travel catch-up. */
		const Sample& Latest() const { return nodes[count - 1].sample; }

		void Observe(Sample sample, double hours, float snowRate,
			const std::array<float, 4>& lightHours, const std::array<float, 4>& heavyHours)
		{
			if (!Valid(sample) || !std::isfinite(hours) || !std::isfinite(snowRate)) {
				Clear();
				return;
			}
			if (count && hours < now)
				Clear();
			if (count && hours == now)
				return;
			for (uint32_t i = 0; i < 4; ++i) {
				durations[i] = std::isfinite(lightHours[i]) ? std::clamp(lightHours[i], 1.0f, 24.0f) : 1.0f;
				durations[i + 4] = std::isfinite(heavyHours[i]) ? std::clamp(heavyHours[i], 1.0f, 24.0f) : 1.0f;
			}
			if (count && snowRate != 0.0f) {
				const auto previous = nodes[count - 1];
				const double movingHours = (sample.state[0] - previous.sample.state[0]) / snowRate;
				const double elapsed = hours - now;
				// Wait/sleep must preserve when snow finished melting, rather than move it to wake-up.
				if (movingHours > 0.0 && movingHours < elapsed) {
					auto stopped = sample;
					stopped.state[2] = std::lerp(previous.sample.state[2], sample.state[2], static_cast<float>(movingHours / elapsed));
					Append(stopped, now + movingHours);
				}
			}
			Append(sample, hours);
			while (count > 2 && nodes[1].hours < now - MaximumDryingHours)
				RemoveFirst();
		}

		PerFrame GetData() const
		{
			PerFrame data{};
			if (!HasMelt())
				return data;
			data.enabled = 1;
			data.count = count;
			for (uint32_t i = 0; i < count; ++i) {
				data.samples[i] = nodes[i].sample;
				data.samples[i].state[3] = static_cast<float>(now - nodes[i].hours);
				for (uint32_t material = 0; material < 4; ++material) {
					data.samples[i].lightDryingAge[material] = static_cast<float>(dryingClock[material] - nodes[i].dryingClock[material]);
					data.samples[i].heavyDryingAge[material] = static_cast<float>(dryingClock[material + 4] - nodes[i].dryingClock[material + 4]);
				}
			}
			return data;
		}

	private:
		struct Node
		{
			Sample sample;
			double hours = 0.0;
			std::array<double, 8> dryingClock{};
		};
		std::array<Node, MaxSamples> nodes{};
		uint32_t count = 0;
		double now = 0.0;
		double lastMelt = -MaximumDryingHours;
		std::array<double, 8> dryingClock{};
		std::array<double, 8> lastMeltClock{};
		std::array<float, 8> durations{};

		static bool Valid(const Sample& sample)
		{
			if (sample.fireCount > MaxFires || !std::isfinite(sample.fireStrength) ||
				!std::isfinite(sample.fireRadiusScale) || sample.fireRadiusScale <= 0.0f)
				return false;
			for (float value : sample.state)
				if (!std::isfinite(value))
					return false;
			for (uint32_t i = 0; i < sample.fireCount; ++i)
				for (float value : sample.fires[i])
					if (!std::isfinite(value))
						return false;
			return true;
		}

		static bool SameFireFootprint(const Sample& a, const Sample& b)
		{
			return a.fireCount == b.fireCount && a.fireStrength == b.fireStrength &&
			       a.fireRadiusScale == b.fireRadiusScale && a.fires == b.fires;
		}

		static bool CanMerge(const Node& a, const Node& b, const Node& c)
		{
			if (c.hours <= a.hours || c.hours - a.hours > 1.0 ||
				!SameFireFootprint(a.sample, b.sample) || !SameFireFootprint(b.sample, c.sample))
				return false;
			const float fraction = static_cast<float>((b.hours - a.hours) / (c.hours - a.hours));
			for (uint32_t i = 0; i < 3; ++i) {
				const float interpolated = std::lerp(a.sample.state[i], c.sample.state[i], fraction);
				const float tolerance = i == 2 ? 0.01f : 0.0001f;
				if (std::abs(interpolated - b.sample.state[i]) > tolerance)
					return false;
			}
			for (uint32_t i = 0; i < 8; ++i)
				if (std::abs(std::lerp(a.dryingClock[i], c.dryingClock[i], static_cast<double>(fraction)) - b.dryingClock[i]) > 1e-6)
					return false;
			return true;
		}

		void RemoveFirst()
		{
			std::move(nodes.begin() + 1, nodes.begin() + count, nodes.begin());
			--count;
		}

		void MakeRoom()
		{
			// Preserve the retention window when rapidly changing fire footprints need more samples.
			uint32_t remove = 1;
			for (uint32_t i = 2; i + 1 < count; ++i)
				if (nodes[i + 1].hours - nodes[i - 1].hours < nodes[remove + 1].hours - nodes[remove - 1].hours)
					remove = i;
			std::move(nodes.begin() + remove + 1, nodes.begin() + count, nodes.begin() + remove);
			--count;
		}

		void Append(const Sample& sample, double hours)
		{
			bool melted = false;
			if (count) {
				const auto& before = nodes[count - 1].sample;
				const auto weatherAmount = [](const Sample& value) {
					const float snow = std::max(0.0f, value.state[0]);
					return snow * snow * snow * std::max(500.0f, value.state[1]);
				};
				melted = sample.state[0] < before.state[0] || weatherAmount(sample) < weatherAmount(before) ||
				         sample.state[2] < before.state[2] || (sample.fireCount > 0 && sample.fireStrength > 0 && !SameFireFootprint(sample, before));
			}
			if (count)
				for (uint32_t i = 0; i < 8; ++i)
					dryingClock[i] += (hours - now) / durations[i];
			if (melted) {
				lastMelt = hours;
				lastMeltClock = dryingClock;
			}
			const Node node{ sample, hours, dryingClock };
			if (count > 1 && CanMerge(nodes[count - 2], nodes[count - 1], node)) {
				nodes[count - 1] = node;
			} else {
				if (count == MaxSamples)
					MakeRoom();
				nodes[count++] = node;
			}
			now = hours;
		}
	};
}
