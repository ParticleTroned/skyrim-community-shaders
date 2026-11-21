#include "Features/Skin/WetnessPolicy.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
{
	try {
		using namespace SkinWetness;
		Require(Sweat(1, .75f, .15f) == 0, "Resting actors must stay dry");
		Require(Sweat(0, .75f, .15f) == 1, "Exhausted actors must reach full sweat");
		for (float stamina : { 0.0f, .15f, .45f, .75f, 1.0f })
			Require(Sweat(stamina, .15f, .75f) == Sweat(stamina, .75f, .15f), "Reversed thresholds changed the sweat ramp");
		Require(Sweat(.5f, .5f, .5f) == 0, "Equal thresholds must not divide by zero");
		for (int fps : { 30, 90, 144 }) {
			for (float scale : { 1.0f, 2.0f }) {
				float wetness = scale;
				for (int frame = 0; frame < fps * 5; ++frame)
					wetness = Fade(wetness, 0, 1.0f / fps, 10, scale);
				Require(std::abs(wetness - scale * .5f) < .0002f, "Water and sweat must share the configured drying duration");
				for (int frame = 0; frame < fps * 6; ++frame)
					wetness = Fade(wetness, 0, 1.0f / fps, 10, scale);
				Require(wetness == 0, "Drying must reach zero without becoming negative");
			}
		}
		Require(Fade(.2f, .8f, .01f, 10, 1) == .8f, "Renewed sweat must take effect immediately");
		Require(Fade(1, .4f, 50, 10, 1) == .4f, "Drying cannot undershoot current sweat");
		Require(Fade(2, 0, .01f, 0, 2) == 0, "Zero fade time must dry immediately");
		std::cout << "Skin sweat thresholds and drying duration passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
