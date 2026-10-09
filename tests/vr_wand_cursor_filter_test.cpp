#include "Features/VR/WandCursorFilter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::fprintf(stderr, "%s\n", a_message);
			std::exit(1);
		}
	}

	void TestStationaryNoiseAndPrecision()
	{
		WandCursorFilter::Filter filter;
		filter.Update({ 0.5f, 0.5f }, 0.0);
		double rawEnergy = 0.0;
		double filteredEnergy = 0.0;
		for (int frame = 1; frame <= 900; ++frame) {
			const double time = frame / 90.0;
			const float noise = 0.003f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 20.0 * time));
			const auto position = filter.Update({ 0.5f + noise, 0.5f - noise }, time);
			if (frame > 90) {
				rawEnergy += noise * noise;
				filteredEnergy += (position.x - 0.5f) * (position.x - 0.5f);
			}
		}
		Require(filteredEnergy < rawEnergy * 0.15, "Stationary high-frequency tremor must be attenuated");
		for (int frame = 901; frame <= 990; ++frame)
			filter.Update({ 0.5005f, 0.5005f }, frame / 90.0);
		const auto precise = filter.Update({ 0.5005f, 0.5005f }, 991 / 90.0);
		Require(std::abs(precise.x - 0.5005f) < 0.00001f, "Subpixel adjustments must converge without a dead zone");
	}

	float TestMotionAtRate(int a_rate)
	{
		WandCursorFilter::Filter filter;
		filter.Update({ 0.0f, 0.0f }, 0.0);
		float result = 0.0f;
		for (int frame = 1; frame <= a_rate; ++frame) {
			const float raw = static_cast<float>(frame) / a_rate;
			const auto position = filter.Update({ raw, raw }, static_cast<double>(frame) / a_rate);
			Require(position.x >= result && position.x <= raw, "Deliberate motion must remain monotonic without overshoot");
			result = position.x;
		}
		Require(1.0f - result < 0.012f, "Fast movement must follow with less than 1.2 percent of canvas-height lag");
		return result;
	}

	void TestClickAndReacquisition()
	{
		WandCursorFilter::Filter filter;
		const auto initial = filter.Update({ 0.5f, 0.5f }, 1.0);
		const auto pressed = filter.Update({ 0.505f, 0.502f }, 1.01, true);
		Require(initial.x == pressed.x && initial.y == pressed.y, "A press must retain the displayed target despite trigger movement");
		const auto repeated = filter.Update({ 0.8f, 0.8f }, 1.01);
		Require(repeated.x == pressed.x, "Duplicate sample times must not advance smoothing");
		const auto drag = filter.Update({ 0.51f, 0.5f }, 1.02);
		Require(drag.x > pressed.x && drag.x < 0.51f, "Dragging must resume without bypassing smoothing");
		const auto resumed = filter.Update({ 0.8f, 0.2f }, 2.0, true);
		Require(resumed.x == 0.8f && resumed.y == 0.2f, "A stale press must reacquire rather than reuse an obsolete target");
		filter.Reset();
		Require(filter.Update({ 0.1f, 0.2f }, 2.01).x == 0.1f, "Explicit target reset must initialize at the new hand or surface");
		Require(filter.Update({ 0.7f, 0.2f }, 1.0).x == 0.7f, "Clock reversal must discard history");
		filter.Update({ std::numeric_limits<float>::quiet_NaN(), 0.0f }, 1.01);
		Require(filter.Update({ 0.3f, 0.4f }, 1.02).x == 0.3f, "Invalid samples must not poison subsequent valid tracking");
	}
}

int main()
{
	TestStationaryNoiseAndPrecision();
	const float at60 = TestMotionAtRate(60);
	const float at90 = TestMotionAtRate(90);
	const float at144 = TestMotionAtRate(144);
	Require(std::abs(at60 - at144) < 0.005f && std::abs(at90 - at144) < 0.005f, "Frame rate must not materially change motion latency");
	TestClickAndReacquisition();
	return 0;
}
