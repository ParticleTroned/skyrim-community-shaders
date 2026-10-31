#include "Features/SnowCoverMath.h"
#include "Utils/HeightMap.h"
#include "Utils/StringUtils.h"

#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Snow coverage invariant failed");
	}
}

int main()
{
	using namespace SnowCoverMath;
	Require(SeasonalAltitude(6, 6, 0, 20000, -20000) == -20000);
	Require(SeasonalAltitude(0, 6, 0, 20000, -20000) == 20000);
	Require(SeasonalAltitude(12, 6, 0, 20000, -20000) == 20000);
	Require(SeasonalAltitude(3, 6, 0, 20000, -20000) == 0);
	Require(SeasonalAltitude(9, 6, 0, 20000, -20000) == 0);
	Require(std::isfinite(SeasonalAltitude(6, 6, 6, 20000, -20000)));
	Require(Accumulate(0, 1000, true, false, 1, 1) == MaxWeatherAccumulation);
	Require(Accumulate(0, 1000, false, true, 1, 1) == -MaxWeatherAccumulation);
	Require(Accumulate(1, 0.25f, false, true, 1, 1) == 0.5f);
	Require(Accumulate(-1, 0.25f, false, false, 1, 1) == -0.75f);
	Require(Accumulate(1, 10, false, false, 1, 1) == 0);
	Require(Accumulate(1, -1, true, false, 1, 1) == 1);
	Require(Accumulate(1, std::numeric_limits<float>::infinity(), true, false, 1, 1) == 1);
	Require(Accumulate(std::numeric_limits<float>::quiet_NaN(), 0, false, false, 1, 1) == 0);
	Require(SeasonalAltitude(-12, 6, 0, 20000, -20000) == 20000);
	Require(std::isfinite(SeasonalAltitude(std::numeric_limits<float>::quiet_NaN(), 6, 0, 20000, -20000)));

	Util::HeightMap map;
	Require(!map.Maximum(0, 0, 1, 1));
	Require(!map.ContainsSample(0.5f, 0.5f));
	map.SetConstant(0.5f);
	Require(map.IsLoaded() && map.ContainsSample(-100, 100));
	Require(map.Maximum(-100, -100, 100, 100) == 0.5f);
	Require(map.Sample(-100, 100) == 0.5f);
	Require(!map.ContainsSample(std::numeric_limits<float>::quiet_NaN(), 0.5f));
	const std::array<uint8_t, 8> padded{ 0, 64, 128, 255, 255, 128, 64, 0 };
	Require(map.Load(3, 2, padded, 4));
	Require(map.ContainsSample(0.5f, 0.5f));
	Require(!map.ContainsSample(0, 0));
	Require(map.Sample(0, 0) == 0);
	Require(map.Sample(0, 1) == 1);
	Require(map.Sample(1, 0) == 128.0f / 255.0f);
	Require(map.Sample(std::numeric_limits<float>::quiet_NaN(), 0) == 0.5f);
	Require(!map.Maximum(-0.01f, 0, 0.5f, 0.5f));
	Require(!map.Maximum(0, 0, std::numeric_limits<float>::infinity(), 1));
	Require(!map.Load(3, 2, padded, 2));
	Require(!map.IsLoaded());
	Require(!map.Load(3, 3, padded, 3));

	// Odd dimensions exercise the maximum hierarchy's incomplete edge blocks.
	std::array<uint8_t, 35> pixels{};
	pixels[17] = 255;
	Require(map.Load(7, 5, pixels, 7));
	for (float u0 : { 0.1f, 0.25f, 0.5f }) {
		for (float v0 : { 0.15f, 0.25f, 0.5f }) {
			const auto maximum = map.Maximum(u0, v0, 0.85f, 0.85f);
			Require(maximum.has_value());
			Require(maximum == map.Maximum(0.85f, 0.85f, u0, v0));
			for (int y = 0; y <= 16; ++y)
				for (int x = 0; x <= 16; ++x)
					Require(map.Sample(std::lerp(u0, 0.85f, x / 16.0f), std::lerp(v0, 0.85f, y / 16.0f)) <= *maximum);
		}
	}

	std::vector<std::string> lines;
	std::istringstream valid("  roof  # comment\n\n tree\r\n");
	Util::ReadConfigLines(valid, lines);
	Require(lines == std::vector<std::string>{ "roof", "tree" });
	std::istringstream failed;
	failed.setstate(std::ios::failbit);
	bool rejected = false;
	try {
		Util::ReadConfigLines(failed, lines);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected);
}
