#include "Features/Upscaling/DLSSGResourceRetention.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
	using Result = DLSSGResourceRetention::ReleaseResult;
	using Conditions = DLSSGResourceRetention::ReleaseConditions;

	enum class Call
	{
		kDrain,
		kFree,
	};

	Conditions CompletedDisabledPresent()
	{
		return {
			.userEnabled = false,
			.providerAvailable = true,
			.presentCompleted = true,
			.stateAvailable = true,
			.synchronized = true,
			.requiresPresentBoundary = false,
		};
	}

	struct Backend
	{
		DLSSGResourceRetention resources;
		std::vector<Call> calls;
		bool drainSucceeds = true;
		bool freeSucceeds = true;

		Result Release(const Conditions& a_conditions)
		{
			return resources.TryRelease(
				a_conditions,
				[&]() {
					calls.push_back(Call::kDrain);
					return drainSucceeds;
				},
				[&]() {
					calls.push_back(Call::kFree);
					return freeSucceeds;
				});
		}
	};
}

TEST_CASE("DLSS-G keeps allocations through repeated menu pauses", "[upscaling][dlssg]")
{
	Backend backend;
	auto conditions = CompletedDisabledPresent();
	conditions.userEnabled = true;
	for (int pause = 0; pause < 3; ++pause) {
		backend.resources.OnOptionsSucceeded(true);
		backend.resources.OnOptionsSucceeded(false);
		CHECK(backend.Release(conditions) == Result::kDeferred);
		CHECK(backend.resources.HasRetainedResources());
		CHECK(backend.calls.empty());
	}

	conditions.userEnabled = false;
	CHECK(backend.Release(conditions) == Result::kReleased);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree });
	CHECK_FALSE(backend.resources.HasRetainedResources());
	CHECK(backend.Release(conditions) == Result::kDeferred);
	CHECK(backend.calls.size() == 2);
}

TEST_CASE("DLSS-G requires fresh completed Present evidence before cleanup", "[upscaling][dlssg]")
{
	Backend backend;
	backend.resources.OnOptionsSucceeded(true);
	backend.resources.OnOptionsSucceeded(false);
	auto conditions = CompletedDisabledPresent();

	SECTION("unavailable provider") { conditions.providerAvailable = false; }
	SECTION("failed Present") { conditions.presentCompleted = false; }
	SECTION("failed state query") { conditions.stateAvailable = false; }
	SECTION("failed synchronization") { conditions.synchronized = false; }
	SECTION("mode-off or null tags not yet consumed") { conditions.requiresPresentBoundary = true; }
	SECTION("reset frame state is not completion evidence") { conditions = {}; }

	CHECK(backend.Release(conditions) == Result::kDeferred);
	CHECK(backend.resources.HasRetainedResources());
	CHECK(backend.calls.empty());
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree });
}

TEST_CASE("DLSS-G never frees after a failed GPU drain", "[upscaling][dlssg]")
{
	Backend backend;
	backend.resources.OnOptionsSucceeded(true);
	backend.drainSucceeds = false;
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kDrainFailed);
	CHECK(backend.calls == std::vector{ Call::kDrain });
	CHECK(backend.resources.HasRetainedResources());

	backend.drainSucceeds = true;
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kDrain, Call::kFree });
}

TEST_CASE("DLSS-G retries failed release without losing ownership", "[upscaling][dlssg]")
{
	Backend backend;
	backend.resources.OnOptionsSucceeded(true);
	backend.freeSucceeds = false;
	for (int retry = 0; retry < 2; ++retry) {
		CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleaseFailed);
		CHECK(backend.resources.HasRetainedResources());
	}
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree, Call::kDrain, Call::kFree });

	backend.calls.clear();
	backend.freeSucceeds = true;
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	CHECK_FALSE(backend.resources.HasRetainedResources());
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kDeferred);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree });
}

TEST_CASE("DLSS-G re-enable cancels pending cleanup and allows later release", "[upscaling][dlssg]")
{
	Backend backend;
	backend.resources.OnOptionsSucceeded(true);
	backend.freeSucceeds = false;
	REQUIRE(backend.Release(CompletedDisabledPresent()) == Result::kReleaseFailed);
	backend.calls.clear();
	backend.freeSucceeds = true;

	auto conditions = CompletedDisabledPresent();
	conditions.userEnabled = true;
	backend.resources.OnOptionsSucceeded(true);
	CHECK(backend.Release(conditions) == Result::kDeferred);
	CHECK(backend.calls.empty());
	CHECK(backend.resources.HasRetainedResources());

	backend.resources.OnOptionsSucceeded(false);
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	backend.resources.OnOptionsSucceeded(true);
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree, Call::kDrain, Call::kFree });
}

TEST_CASE("DLSS-G has no cleanup work before enable or after successful shutdown", "[upscaling][dlssg]")
{
	Backend backend;
	backend.resources.OnOptionsSucceeded(false);
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kDeferred);
	CHECK(backend.calls.empty());

	backend.resources.OnOptionsSucceeded(true);
	backend.resources.OnShutdownSucceeded();
	CHECK_FALSE(backend.resources.HasRetainedResources());
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kDeferred);
	CHECK(backend.calls.empty());

	backend.resources.OnOptionsSucceeded(true);
	CHECK(backend.Release(CompletedDisabledPresent()) == Result::kReleased);
	CHECK(backend.calls == std::vector{ Call::kDrain, Call::kFree });
}
