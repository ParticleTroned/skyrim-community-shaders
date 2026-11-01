#include "EngineFixes/SunReceiverPolicy.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace SunShadowReceiver::Policy;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	Input Eligible()
	{
		Input input;
		input.primaryPass = input.available = input.identityStable = input.shadowsEnabled = true;
		input.flags = ReceiveShadows;
		input.lightData = 0x1000;
		input.activeMask = 0xA;
		input.sunMask = 0x40;
		input.shadowDistance = 100;
		input.radius = 10;
		input.center = { 109, 0, 0 };
		return input;
	}
	void Eligibility()
	{
		auto input = Eligible();
		auto mask = input.activeMask;
		const auto decision = Evaluate(input);
		Require(decision.reason == Reason::candidate && decision.rangeValid, "sphere overlaps shadow range");
		Require(!Apply(mask, input.lightData, input, decision, false) && mask == 0xA, "observe must preserve every native bit");
		Require(Apply(mask, input.lightData, input, decision, true) && mask == 0x4A, "repair must add only the missing sun bit");
		Require(!Apply(mask, input.lightData, input, decision, true) && mask == 0x4A, "stale eligibility must not write twice");
		input.activeMask = mask;
		Require(Evaluate(input).reason == Reason::alreadyPresent, "already-shadowed receivers must be skipped");
		for (std::size_t bit : { 33, 34, 52, 63 }) {
			input = Eligible();
			input.flags |= std::uint64_t{ 1 } << bit;
			Require(Evaluate(input).reason == Reason::excludedLod, "each LOD exclusion is independent");
		}
		input = Eligible();
		input.flags = 0;
		Require(Evaluate(input).reason == Reason::notReceiver, "respect receive-shadows opt out");
		for (auto member : { &Input::primaryPass, &Input::available, &Input::identityStable, &Input::shadowsEnabled }) {
			input = Eligible();
			input.*member = false;
			mask = input.activeMask;
			Require(!Apply(mask, input.lightData, input, Evaluate(input), true) && mask == input.activeMask, "invalid context cannot repair");
		}
		input = Eligible();
		input.sunMask = 0;
		Require(Evaluate(input).reason == Reason::noSun, "missing sun mapping cannot manufacture a receiver");
		input = Eligible();
		input.sunMask = 0x42;
		Require(Evaluate(input).reason == Reason::alreadyPresent, "one matching sun bit already supplies reception");
		input = Eligible();
		mask = input.activeMask;
		Require(!Apply(mask, 0, input, Evaluate(input), true) && mask == input.activeMask, "missing light data cannot repair");
		Require(!Apply(mask, input.lightData + 8, input, Evaluate(input), true) && mask == input.activeMask,
			"replacement light data with an identical mask must not receive a stale repair");
		input = Eligible();
		mask = 0x80;
		Require(!Apply(mask, input.lightData, input, Evaluate(input), true) && mask == 0x80, "changed native mask cannot be overwritten");
	}
	void Bounds()
	{
		auto input = Eligible();
		input.center[0] = 110;
		Require(Evaluate(input).reason == Reason::outsideRange, "strict range boundary is excluded");
		input.center[0] = 111;
		Require(Evaluate(input).reason == Reason::outsideRange, "outside-range receiver is excluded");
		input = Eligible();
		input.radius = 0;
		input.center = { 0, 0, 0 };
		Require(Evaluate(input).reason == Reason::candidate, "finite point bounds are valid");
		for (const auto invalid : { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			input = Eligible();
			input.radius = invalid;
			Require(Evaluate(input).reason == Reason::invalidBounds, "invalid radius fails closed");
			input = Eligible();
			input.shadowDistance = invalid;
			Require(Evaluate(input).reason == Reason::invalidBounds, "invalid shadow range fails closed");
		}
		input = Eligible();
		input.shadowDistance = 0;
		Require(Evaluate(input).reason == Reason::invalidBounds, "disabled range fails closed");
		for (std::size_t axis = 0; axis < 3; ++axis) {
			input = Eligible();
			input.center[axis] = std::numeric_limits<float>::quiet_NaN();
			Require(Evaluate(input).reason == Reason::invalidBounds, "nonfinite center fails closed");
			input = Eligible();
			input.camera[axis] = std::numeric_limits<float>::infinity();
			Require(Evaluate(input).reason == Reason::invalidBounds, "nonfinite camera fails closed");
		}
		input = Eligible();
		input.center = { std::numeric_limits<float>::max(), 0, 0 };
		input.camera = { -std::numeric_limits<float>::max(), 0, 0 };
		Require(Evaluate(input).reason == Reason::outsideRange, "large finite coordinates cannot overflow into eligibility");
		input = Eligible();
		input.center = { 500, 600, 700 };
		input.camera = input.center;
		Require(Evaluate(input).reason == Reason::candidate, "distance is relative to the camera");
	}


	void Mapping()
	{
		std::array<std::uintptr_t, 40> lights{};
		const std::uintptr_t sun = 99;
		lights[3] = lights[31] = lights[32] = sun;

		Require(SunMask(lights.data(), 40, sun) == 0x80000008, "flat collects duplicate representable entries");
		Require(SunMask(lights.data(), 3, sun) == 0, "respect array length");
		Require(SunMask(lights.data(), 40, std::uintptr_t{}) == 0, "null sun cannot match null entries");
		Require(SunMask(static_cast<const std::uintptr_t*>(nullptr), 40, sun) == 0, "missing array fails closed");
	}
}
int main()
{
	try {
		Eligibility();
		Bounds();
		Mapping();
		std::cout << "PASS: observe/repair, eligibility, finite bounds and SE/AE sun mapping\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
