#pragma once

#include "Features/SnowCoverMath.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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
	static_assert(offsetof(Sample, state) == 0);
	static_assert(offsetof(Sample, fireCount) == 16);
	static_assert(offsetof(Sample, fireStrength) == 20);
	static_assert(offsetof(Sample, fireRadiusScale) == 24);
	static_assert(offsetof(Sample, fires) == 32);
	static_assert(offsetof(Sample, lightDryingAge) == 160);
	static_assert(offsetof(Sample, heavyDryingAge) == 176);

	struct PerFrame
	{
		uint32_t count = 0;
		uint32_t enabled = 0;
		uint32_t unchangedFireMask = 0;
		uint32_t padding = 0;
		std::array<float, 4> lightDryingHours{};
		std::array<float, 4> heavyDryingHours{};
		std::array<Sample, MaxSamples> samples{};
	};
	static_assert(sizeof(PerFrame) == 48 + MaxSamples * sizeof(Sample));

	/** Field-separated GPU history avoids nested-array materialization during shader indexing. */
	struct ShaderData
	{
		uint32_t count = 0;
		uint32_t enabled = 0;
		uint32_t unchangedFireMask = 0;
		uint32_t padding = 0;
		std::array<float, 4> lightDryingHours{};
		std::array<float, 4> heavyDryingHours{};
		std::array<std::array<float, 4>, MaxSamples> states{};
		std::array<std::array<uint32_t, 4>, MaxSamples> fireParameters{};
		std::array<std::array<float, 4>, MaxSamples * MaxFires> fires{};
		std::array<std::array<float, 4>, MaxSamples> lightDryingAges{};
		std::array<std::array<float, 4>, MaxSamples> heavyDryingAges{};
		std::array<float, 4> maximumState{};

		ShaderData() = default;
		explicit ShaderData(const PerFrame& history) :
			count(std::min(history.count, MaxSamples)), enabled(history.enabled), unchangedFireMask(history.unchangedFireMask),
			lightDryingHours(history.lightDryingHours), heavyDryingHours(history.heavyDryingHours),
			maximumState{ 0.0f, 0.0f, std::numeric_limits<float>::lowest(), 0.0f }
		{
			for (uint32_t i = 0; i < count; ++i) {
				const auto& sample = history.samples[i];
				states[i] = sample.state;
				for (uint32_t component = 0; component < 3; ++component)
					maximumState[component] = std::max(maximumState[component], sample.state[component]);
				fireParameters[i] = { std::min(sample.fireCount, MaxFires), std::bit_cast<uint32_t>(sample.fireStrength),
					std::bit_cast<uint32_t>(sample.fireRadiusScale), 0 };
				std::copy(sample.fires.begin(), sample.fires.end(), fires.begin() + i * MaxFires);
				lightDryingAges[i] = sample.lightDryingAge;
				heavyDryingAges[i] = sample.heavyDryingAge;
			}
		}
	};
	static_assert(sizeof(ShaderData) == sizeof(PerFrame) + 16);
	static_assert(offsetof(ShaderData, states) == 48);
	static_assert(offsetof(ShaderData, fireParameters) == 560);
	static_assert(offsetof(ShaderData, fires) == 1072);
	static_assert(offsetof(ShaderData, lightDryingAges) == 5168);
	static_assert(offsetof(ShaderData, heavyDryingAges) == 5680);
	static_assert(offsetof(ShaderData, maximumState) == 6192);

	/** Bounded world history; the shader evaluates each sample on the original snow footprint. */
	class History
	{
	public:
		void Clear()
		{
			count = 0;
			now = 0.0;
			lastMelt = -MaximumDryingHours;
			dryingClock.fill(0.0);
			lastMeltClock.fill(0.0);
		}
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
		/** Conservative weather/altitude maxima for Snow Cover's existing bounds rejection. */
		std::array<float, 2> SnowBounds() const
		{
			std::array<float, 2> bounds{ 0, -std::numeric_limits<float>::max() };
			for (uint32_t i = 0; i < count; ++i) {
				bounds[0] = std::max(bounds[0], nodes[i].sample.state[0]);
				bounds[1] = std::max(bounds[1], nodes[i].sample.state[2]);
			}
			return bounds;
		}

		void Observe(Sample sample, double hours, SnowCoverMath::AccumulationRates snowRates,
			const std::array<float, 4>& lightHours, const std::array<float, 4>& heavyHours)
		{
			if (!Valid(sample) || !std::isfinite(hours) || hours < 0.0 || !snowRates.IsFinite()) {
				Clear();
				return;
			}
			std::sort(sample.fires.begin(), sample.fires.begin() + sample.fireCount);
			std::fill(sample.fires.begin() + sample.fireCount, sample.fires.end(), std::array<float, 4>{});
			if (count && hours < now)
				Clear();
			if (count && hours == now)
				return;
			for (uint32_t i = 0; i < 4; ++i) {
				durations[i] = std::isfinite(lightHours[i]) ? std::clamp(lightHours[i], 1.0f, 24.0f) : 1.0f;
				durations[i + 4] = std::isfinite(heavyHours[i]) ? std::clamp(heavyHours[i], 1.0f, 24.0f) : 1.0f;
			}
			auto weatherSample = sample;
			const bool arrivalFires = count && hours - now > 1.0 && !SameFireFootprint(nodes[count - 1].sample, sample);
			if (arrivalFires) {
				// Newly observed fires belong to arrival, not the unobserved hours spent travelling.
				const auto& previous = nodes[count - 1].sample;
				weatherSample.fireCount = previous.fireCount;
				weatherSample.fireStrength = previous.fireStrength;
				weatherSample.fireRadiusScale = previous.fireRadiusScale;
				weatherSample.fires = previous.fires;
			}
			if (count) {
				const auto& previous = nodes[count - 1];
				const float before = previous.sample.state[0];
				const float after = sample.state[0];
				const float rate = snowRates.At(before);
				if (snowRates.belowZero != snowRates.aboveZero && rate != 0.0f &&
					((before > 0.0f && after < 0.0f) || (before < 0.0f && after > 0.0f))) {
					const double crossingHours = -static_cast<double>(before) / rate;
					const double elapsed = hours - now;
					if (crossingHours > 0.0 && crossingHours < elapsed) {
						auto crossing = weatherSample;
						crossing.state[0] = 0.0f;
						for (uint32_t i = 1; i < 3; ++i)
							crossing.state[i] = std::lerp(previous.sample.state[i], sample.state[i], static_cast<float>(crossingHours / elapsed));
						// The calm-weather contribution reverses at zero, changing the remaining melt rate.
						Append(crossing, now + crossingHours);
					}
				}
			}
			if (count && snowRates.At(nodes[count - 1].sample.state[0]) != 0.0f) {
				const auto previous = nodes[count - 1];
				const double movingHours = (sample.state[0] - previous.sample.state[0]) / snowRates.At(previous.sample.state[0]);
				const double elapsed = hours - now;
				// Wait/sleep must preserve when snow finished melting, rather than move it to wake-up.
				if (movingHours > 0.0 && movingHours < elapsed) {
					auto stopped = weatherSample;
					for (uint32_t i = 1; i < 3; ++i)
						stopped.state[i] = std::lerp(previous.sample.state[i], sample.state[i], static_cast<float>(movingHours / elapsed));
					Append(stopped, now + movingHours);
				}
			}
			Append(weatherSample, hours);
			if (arrivalFires)
				Append(sample, hours);
			while (count > 2 && nodes[1].hours < now - MaximumDryingHours)
				RemoveFirst();
		}

		PerFrame GetData(bool reuseFireHistory = true) const
		{
			PerFrame data{};
			if (!HasMelt())
				return data;
			data.enabled = 1;
			data.count = count;
			for (uint32_t i = 0; i < 4; ++i) {
				data.lightDryingHours[i] = durations[i];
				data.heavyDryingHours[i] = durations[i + 4];
			}
			for (uint32_t i = 0; i < count; ++i) {
				data.samples[i] = nodes[i].sample;
				if (reuseFireHistory && i && nodes[i].sample.fireCount && SameFireFootprint(nodes[i - 1].sample, nodes[i].sample))
					data.unchangedFireMask |= 1u << i;
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
				sample.fireStrength < 0.0f || sample.fireStrength > 1.0f ||
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

		static bool FireMeltIncreases(const Sample& before, const Sample& after)
		{
			if (after.fireStrength <= 0 || SameFireFootprint(before, after))
				return false;
			for (uint32_t i = 0; i < after.fireCount; ++i) {
				const auto& sphere = after.fires[i];
				bool contained = false;
				for (uint32_t j = 0; j < before.fireCount && !contained; ++j) {
					const auto& oldSphere = before.fires[j];
					contained = before.fireStrength >= after.fireStrength &&
					            oldSphere[3] * before.fireRadiusScale >= sphere[3] * after.fireRadiusScale &&
					            std::equal(sphere.begin(), sphere.begin() + 3, oldSphere.begin());
				}
				if (!contained)
					return true;
			}
			return false;
		}

		static bool LinearTimeline(const Node& a, const Node& b, const Node& c)
		{
			if (c.hours <= a.hours)
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

		static bool CanMerge(const Node& a, const Node& b, const Node& c)
		{
			return c.hours - a.hours <= 1.0 && SameFireFootprint(a.sample, b.sample) &&
			       SameFireFootprint(b.sample, c.sample) && LinearTimeline(a, b, c);
		}

		void RemoveFirst()
		{
			std::move(nodes.begin() + 1, nodes.begin() + count, nodes.begin());
			--count;
		}

		void MakeRoom()
		{
			// Preserve snow/clock turning points before reducing the resolution of fire history.
			uint32_t remove = 1;
			bool linear = LinearTimeline(nodes[0], nodes[1], nodes[2]);
			for (uint32_t i = 2; i + 1 < count; ++i) {
				const bool candidateLinear = LinearTimeline(nodes[i - 1], nodes[i], nodes[i + 1]);
				if ((candidateLinear && !linear) || (candidateLinear == linear &&
														nodes[i + 1].hours - nodes[i - 1].hours < nodes[remove + 1].hours - nodes[remove - 1].hours)) {
					remove = i;
					linear = candidateLinear;
				}
			}
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
				         sample.state[2] < before.state[2] || FireMeltIncreases(before, sample);
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
