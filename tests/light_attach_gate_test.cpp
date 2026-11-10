#include "Features/LightLimitFix/LightAttachPolicy.h"

#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
	using namespace LightLimitFixDetail::LightAttach;
	using Owner = std::shared_ptr<int>;

	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Light attachment check failed at line " + std::to_string(a_location.line()));
	}

	void CheckCadenceAndInputs()
	{
		Tracker<Owner, 8> tracker;
		const auto light = std::make_shared<int>(1);
		Signature signature;
		signature.source = light.get();
		Require(tracker.ShouldWalk(light.get(), signature, 1, 600));
		Require(tracker.RecordWalk(light, signature, 1));
		Require(!tracker.ShouldWalk(light.get(), signature, 2, 600));
		for (auto interval : { 0u, 1u, 601u, std::numeric_limits<unsigned>::max() })
			Require(tracker.ShouldWalk(light.get(), signature, 2, interval));
		for (auto member : { &Signature::position, &Signature::radius, &Signature::cachedPosition }) {
			auto changed = signature;
			(changed.*member)[0] = 10;
			Require(tracker.ShouldWalk(light.get(), changed, 3, 600));
			Require(tracker.ShouldWalk(light.get(), signature, 3, 600));
			Require(tracker.RecordWalk(light, signature, 3));
		}
		auto changed = signature;
		changed.fade = std::numeric_limits<float>::quiet_NaN();
		Require(tracker.ShouldWalk(light.get(), changed, 4, 600));
		Require(tracker.RecordWalk(light, changed, 4));
		Require(tracker.ShouldWalk(light.get(), changed, 5, 600));
		Require(tracker.RecordWalk(light, signature, 5));

		std::uint32_t lastWalk = 5;
		unsigned refreshes = 0;
		for (std::uint32_t frame = 6; frame <= 105; ++frame) {
			if (tracker.ShouldWalk(light.get(), signature, frame, 6)) {
				Require(frame - lastWalk <= 6);
				Require(tracker.RecordWalk(light, signature, frame));
				lastWalk = frame;
				++refreshes;
			}
		}
		Require(refreshes >= 16 && refreshes <= 18);
		Require(tracker.ShouldWalk(light.get(), signature, 107, 600));
		Require(tracker.ShouldWalk(light.get(), signature, 108, 600));
		Require(tracker.RecordWalk(light, signature, 108));
		Require(tracker.ShouldWalk(light.get(), signature, 1, 600));
	}

	void CheckOwnershipAndRetirement()
	{
		Tracker<Owner, 8> tracker;
		std::array<Owner, 8> retired;
		auto light = std::make_shared<int>(2);
		std::weak_ptr<int> lifetime = light;
		const auto* key = light.get();
		Signature signature;
		Require(tracker.RecordWalk(light, signature, 10));
		light.reset();
		Require(!lifetime.expired());
		Require(tracker.Retire(11, false, retired) == 0);
		Require(tracker.Retire(12, false, retired) == 1);
		Require(!lifetime.expired() && tracker.Size() == 0);
		retired = {};
		Require(lifetime.expired());
		Require(tracker.ShouldWalk(key, signature, 12, 600));

		light = std::make_shared<int>(3);
		Require(tracker.RecordWalk(light, signature, 20));
		tracker.Forget(light.get());
		Require(tracker.ShouldWalk(light.get(), signature, 21, 600));
		Require(tracker.Retire(21, false, retired) == 1);
		retired = {};
		Require(tracker.RecordWalk(light, signature, 22));
		Require(tracker.Retire(22, true, retired) == 1);
		retired = {};
		Require(tracker.Size() == 0 && tracker.ShouldWalk(light.get(), signature, 22, 600));
	}

	void CheckCollisionsAndReplacement()
	{
		Tracker<Owner, 1> tracker;
		std::array<Owner, 1> retired;
		const auto first = std::make_shared<int>(1);
		const auto second = std::make_shared<int>(2);
		Signature signature;
		Require(tracker.RecordWalk(first, signature, 10));
		Require(tracker.ShouldWalk(second.get(), signature, 11, 600));
		Require(!tracker.RecordWalk(second, signature, 11));
		Require(tracker.Size() == 1);
		Require(!tracker.ShouldWalk(first.get(), signature, 11, 600));
		for (auto member : { &Signature::scene, &Signature::source, &Signature::object }) {
			auto changed = signature;
			changed.*member = second.get();
			Require(tracker.ShouldWalk(first.get(), changed, 12, 600));
			Require(!tracker.RecordWalk(first, changed, 12));
			Require(tracker.Retire(12, false, retired) == 1);
			retired = {};
			Require(tracker.RecordWalk(first, signature, 12));
		}
		Require(tracker.Retire(1, false, retired) == 1);
	}
}

int main()
{
	try {
		CheckCadenceAndInputs();
		CheckOwnershipAndRetirement();
		CheckCollisionsAndReplacement();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
