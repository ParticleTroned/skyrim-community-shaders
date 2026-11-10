#include "../package/Shaders/Common/WetternessDrying.hlsli"
#include "Features/Wetterness/SnowMeltHistory.h"

#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	WetternessSnowMelt::Sample Snow(float amount, float altitude = 0.0f)
	{
		WetternessSnowMelt::Sample sample;
		sample.state = { amount, 500.0f, altitude, 0.0f };
		return sample;
	}

	void Observe(WetternessSnowMelt::History& history, WetternessSnowMelt::Sample sample, double hours, float snowRate,
		const std::array<float, 4>& light = { 3, 3, 3, 6 }, const std::array<float, 4>& heavy = { 12, 12, 12, 15 })
	{
		history.Observe(sample, hours, snowRate, light, heavy);
	}
}

int main()
{
	using WetternessSnowMelt::History;
	History history;
	Observe(history, Snow(0), 0, 1);
	Observe(history, Snow(1), 1, 1);
	Require(!history.HasMelt(), "Accumulation created meltwater");
	Observe(history, Snow(0), 2, -1);
	Require(history.HasMelt(), "Melting did not publish history");
	Observe(history, Snow(0), 27, -1);
	Require(!history.HasMelt() && history.GetData().count == 0, "Finished melt kept shader processing active");

	history.Clear();
	Observe(history, Snow(1), 0, 0);
	Observe(history, Snow(0), 1, -1, { 1, 1, 1, 1 }, { 1, 1, 1, 1 });
	Observe(history, Snow(0), 2, 0, { 1, 1, 1, 1 }, { 1, 1, 1, 1 });
	Require(!history.HasMelt(), "Short configured drying kept the maximum history window active");

	history.Clear();
	Observe(history, Snow(2), 100, 1);
	Observe(history, Snow(0), 110, -1);
	auto data = history.GetData();
	Require(data.count == 3 && data.samples[1].state[3] == 8, "Wait moved the end of melting to wake-up");
	Require(std::abs(data.samples[1].heavyDryingAge[3] - 8.0f / 15.0f) < 0.0001f,
		"Wait did not advance drying after the actual melting endpoint");
	Require(WetternessSnowMelt::PackedSize(data) == offsetof(WetternessSnowMelt::PerFrame, samples) + 3 * sizeof(WetternessSnowMelt::Sample),
		"Short history uploaded unused samples");
	Require(history.SnowBounds()[0] == 2, "Bounds culling discarded the earlier snowy footprint");
	Require(data.unchangedFireMask == 0, "Empty fire lists retained reuse metadata work");
	Observe(history, Snow(0), 126, -1);
	Require(!history.HasMelt(), "Wait retained expired meltwater");

	history.Clear();
	Observe(history, Snow(2), 100, 0);
	Observe(history, Snow(0), 268, -1);
	Require(!history.HasMelt(), "Multi-day travel revived snow that melted near departure");

	history.Clear();
	Observe(history, Snow(0, 1), 100, 0);
	Observe(history, Snow(0, 0), 101, 0);
	Observe(history, Snow(0, 0), 110, 0);
	const auto beforeCold = history.GetData();
	Observe(history, Snow(0, 0), 111, 0, { 24, 24, 24, 24 }, { 24, 24, 24, 24 });
	data = history.GetData();
	Require(data.samples[0].lightDryingAge[0] > beforeCold.samples[0].lightDryingAge[0],
		"Colder arrival weather reversed drying completed before travel");
	const auto paused = data;
	Observe(history, Snow(0, 0), 111, 0);
	Require(history.GetData().samples[0].lightDryingAge == paused.samples[0].lightDryingAge,
		"Paused calendar advanced snow drying");

	history.Clear();
	Observe(history, Snow(0, 1), 0, 0);
	Observe(history, Snow(0, 0), 1, 0);
	Require(history.HasMelt(), "Seasonal retreat was not recorded");
	Observe(history, Snow(0, 1), 0, 0);
	Require(!history.HasMelt(), "Time rewind reused another timeline's wetness");
	Observe(history, Snow(0), std::numeric_limits<double>::quiet_NaN(), 0);
	Require(!history.HasMelt(), "Invalid clock retained history");
	Require(WetternessSnowMelt::PackedSize(history.GetData()) == offsetof(WetternessSnowMelt::PerFrame, samples),
		"Inactive history still uploaded its sample array");

	history.Clear();
	Observe(history, Snow(2, 1), 100, 0);
	auto arrival = Snow(0, 1);
	arrival.fireCount = 1;
	arrival.fireStrength = 1;
	arrival.fires[0] = { 0, 0, 0, 10 };
	Observe(history, arrival, 110, -1);
	data = history.GetData();
	Require(data.count >= 3 && data.samples[data.count - 2].fireCount == 0 &&
				data.samples[data.count - 1].fireCount == 1 && data.samples[data.count - 2].state[3] == 0 &&
				data.samples[data.count - 1].heavyDryingAge[3] == 0,
		"Travel backdated newly observed fire melt");
	Require((data.unchangedFireMask & (1u << (data.count - 1))) == 0,
		"Arrival reused a different fire footprint");

	history.Clear();
	auto stableFires = Snow(1, 1);
	stableFires.fireCount = 2;
	stableFires.fireStrength = 1;
	stableFires.fires[0] = { 0, 0, 0, 10 };
	stableFires.fires[1] = { 50, 0, 0, 10 };
	Observe(history, stableFires, 0, 0);
	stableFires.state[2] = .9f;
	std::swap(stableFires.fires[0], stableFires.fires[1]);
	Observe(history, stableFires, .25, 0);
	Require(history.GetData().unchangedFireMask == 2u, "Reordered identical fire sources did not reuse their footprint");
	const auto referenceFires = history.GetData(false);
	Require(referenceFires.unchangedFireMask == 0 && referenceFires.count == history.GetData().count && referenceFires.enabled,
		"Uncached fire history reference changed its active timeline");
	stableFires.state[2] = .8f;
	stableFires.fireStrength = .5f;
	Observe(history, stableFires, .5, 0);
	stableFires.state[2] = .7f;
	stableFires.fireRadiusScale = 1.5f;
	Observe(history, stableFires, .75, 0);
	Require(history.GetData().unchangedFireMask == 2u, "Changed fire strength or radius reused a different footprint");
	stableFires.state[2] = .6f;
	stableFires.fireCount = 0;
	Observe(history, stableFires, 1, 0);
	Require(history.GetData().unchangedFireMask == 2u, "Empty fire sources reused an earlier nonempty footprint");

	history.Clear();
	Observe(history, Snow(1), 0, 0);
	auto fire = Snow(1);
	fire.fireCount = 1;
	fire.fireStrength = 1;
	fire.fires[0] = { 0, 0, 0, 10 };
	for (int i = 1; i <= 200; ++i) {
		fire.fires[0][3] = 10.0f + static_cast<float>(i) * 0.001f;
		Observe(history, fire, static_cast<double>(i) / 200.0, 0);
	}
	data = history.GetData();
	Require(data.count <= WetternessSnowMelt::MaxSamples && data.samples[0].state[3] == 1,
		"Frequent fire updates evicted the start of local snow history");
	history.Clear();
	Observe(history, Snow(1), 0, 0);
	Observe(history, Snow(0), 0.5, -2);
	fire.state[0] = 0;
	for (int i = 2; i <= 40; ++i) {
		fire.fires[0][3] = static_cast<float>(i);
		Observe(history, fire, i * 0.5, 0);
	}
	data = history.GetData();
	Require(std::any_of(data.samples.begin(), data.samples.begin() + data.count, [](const auto& sample) {
		return sample.state[0] == 0 && sample.state[3] == 19.5f;
	}),
		"History compression moved the snow-clearing endpoint");
	history.Clear();
	Require(!history.HasMelt(), "Reset retained another world's meltwater");

	Observe(history, Snow(1), 0, 0);
	fire = Snow(1);
	fire.fireCount = 2;
	fire.fireStrength = 1;
	fire.fires[0] = { 0, 0, 0, 100 };
	fire.fires[1] = { 50, 0, 0, 100 };
	Observe(history, fire, 1, 0);
	for (int i = 2; i <= 20; ++i) {
		std::swap(fire.fires[0], fire.fires[1]);
		fire.fires[0][3] -= 1;
		fire.fires[1][3] -= 1;
		Observe(history, fire, i, 0);
	}
	Require(!history.HasMelt(), "Reordered or shrinking fires kept renewing dry history");

	Require(WetternessDrying::Remaining(0, 12) == 1 && WetternessDrying::Remaining(6, 12) == 0.5f &&
				WetternessDrying::Remaining(12, 12) == 0 && WetternessDrying::Remaining(100, 12) == 0,
		"Shared rain/snow drying envelope does not reach a finite dry endpoint");
}
