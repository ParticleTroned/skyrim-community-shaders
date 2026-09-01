#include "Features/Upscaling/FrameGenerationEligibilityPolicy.h"

#include <catch2/catch_test_macros.hpp>

using namespace FrameGenerationEligibilityPolicy;

namespace
{
	Inputs MakeInteractiveGameplay()
	{
		return {
			.runtimeStateAvailable = true,
		};
	}
}

TEST_CASE("frame generation accepts stable interactive gameplay", "[upscaling][frame-generation]")
{
	const auto inputs = MakeInteractiveGameplay();
	CHECK(IsInteractiveGameplay(inputs));
}

TEST_CASE("frame generation rejects non-gameplay state", "[upscaling][frame-generation]")
{
	auto inputs = MakeInteractiveGameplay();

	inputs.communityShadersMenuOpen = true;
	CHECK_FALSE(IsInteractiveGameplay(inputs));
	inputs.communityShadersMenuOpen = false;

	inputs.gamePaused = true;
	CHECK_FALSE(IsInteractiveGameplay(inputs));
	inputs.gamePaused = false;

	inputs.mainOrLoadingMenuOpen = true;
	CHECK_FALSE(IsInteractiveGameplay(inputs));
	inputs.mainOrLoadingMenuOpen = false;

	inputs.runtimeStateAvailable = false;
	CHECK_FALSE(IsInteractiveGameplay(inputs));
}
