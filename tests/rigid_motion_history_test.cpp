#include "EngineFixes/RigidMotionHistory.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct Geometry
	{
		std::array<float, 3> previousWorld;
	};
}

int main()
{
	try {
		RigidMotion::History<std::array<float, 3>> history;
		Geometry geometry{ { 195.0f, -2612.0f, -100.0f } };
		const auto stale = geometry.previousWorld;
		const std::array first{ 42.0f, 75.0f, -42.0f };
		const std::array second{ 43.0f, 76.0f, -41.0f };
		const std::array lateUpdate{ 44.0f, 77.0f, -40.0f };

		{
			RigidMotion::PreviousWorldScope scope(geometry, history.Observe(100, first));
			Require(geometry.previousWorld == first, "First observation must reject unobserved native history");
		}
		Require(geometry.previousWorld == stale, "Native history must be restored after setup");
		{
			RigidMotion::PreviousWorldScope outer(geometry, first);
			{
				RigidMotion::PreviousWorldScope inner(geometry, second);
				Require(geometry.previousWorld == second, "A nested setup must see its own history");
			}
			Require(geometry.previousWorld == first, "Nested setup must restore the surrounding override");
		}
		Require(geometry.previousWorld == stale, "Nested scopes must restore native history");
		Require(history.Observe(100, lateUpdate) == first, "Another eye must not advance previous-frame history");
		Require(history.Observe(101, second) == lateUpdate, "The next frame must retain the last rendered transform");
		Require(history.Observe(101, first) == lateUpdate, "Repeated material passes must share previous history");
		Require(history.Observe(102, second) == first, "History must advance exactly once per frame");
		Require(history.Observe(104, lateUpdate) == lateUpdate, "Invisible-frame gaps must invalidate old history");
		Require(history.Observe(0, first) == first, "A reset frame counter must invalidate history");

		RigidMotion::History<std::array<float, 3>> wrapping;
		wrapping.Observe(std::numeric_limits<std::uint32_t>::max(), first);
		Require(wrapping.Observe(0, second) == first, "Unsigned frame rollover must preserve adjacent history");
		wrapping = {};
		Require(wrapping.Observe(1, second) == second, "A replaced attachment must begin with fresh history");

		try {
			RigidMotion::PreviousWorldScope scope(geometry, first);
			Require(geometry.previousWorld == first, "Native setup must see the corrected transform");
			throw 1;
		} catch (int) {
			Require(geometry.previousWorld == stale, "Exceptional setup must restore the native transform");
		}

		RigidMotion::History<std::array<float, 3>> independent;
		independent.Observe(100, first);
		Require(independent.Observe(101, second) == first, "Geometry histories must remain independent");
		RigidMotion::History<std::array<float, 13>> complete;
		const std::array<float, 13> initialMatrix{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 42, 75, -42, 1 };
		const std::array<float, 13> animatedMatrix{ 0, 1, 0, -1, 0, 0, 0, 0, 1, 43, 76, -41, 2 };
		complete.Observe(1, initialMatrix);
		Require(complete.Observe(2, animatedMatrix) == initialMatrix, "History must preserve rotation, translation and scale");
		std::cout << "Rigid motion history checks passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
