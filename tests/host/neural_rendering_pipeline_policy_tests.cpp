#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("Gogh SE keeps DLSS before NR", "[upscaling][neural-rendering]")
{
	CHECK_FALSE(NeuralRendering::RunsBeforeDlss());
	CHECK(NeuralRendering::RunsAfterDlss());
	CHECK(NeuralRendering::UsesFeatureUpscaling());
	CHECK(std::strcmp(
			  NeuralRendering::GetPipelineArrangementName(),
			  "dlss_then_neural") == 0);
}

TEST_CASE("Gogh SE preserves NR through safe paused gameplay menus",
	"[upscaling][neural-rendering][menus]")
{
	CHECK(NeuralRendering::IsHardMenuContext({ .mainMenu = true }));
	CHECK(NeuralRendering::IsHardMenuContext({ .loading = true }));
	CHECK(NeuralRendering::IsHardMenuContext({
		.communityShadersMenu = true,
	}));
	CHECK_FALSE(NeuralRendering::IsHardMenuContext({}));

	const auto activeWorldMenu = NeuralRendering::EvaluateMenuContinuity({
		.gamePaused = true,
		.worldFrameStateAvailable = true,
		.currentFrame = 42u,
		.lastWorldRenderFrame = 42u,
		.lastCompletedWorldRenderFrame = 41u,
	});
	CHECK(activeWorldMenu.admitted);
	CHECK(activeWorldMenu.currentWorldFrame);
	CHECK_FALSE(activeWorldMenu.retainedWorldFrame);

	const auto retainedWorldMenu = NeuralRendering::EvaluateMenuContinuity({
		.gamePaused = true,
		.worldFrameStateAvailable = true,
		.currentFrame = 42u,
		.lastWorldRenderFrame = 41u,
		.lastCompletedWorldRenderFrame = 41u,
	});
	CHECK(retainedWorldMenu.admitted);
	CHECK(retainedWorldMenu.retainedWorldFrame);

	const auto staleWorldMenu = NeuralRendering::EvaluateMenuContinuity({
		.gamePaused = true,
		.worldFrameStateAvailable = true,
		.currentFrame = 42u,
		.lastWorldRenderFrame = 40u,
		.lastCompletedWorldRenderFrame = 41u,
	});
	CHECK_FALSE(staleWorldMenu.admitted);

	const auto hardMenu = NeuralRendering::EvaluateMenuContinuity({
		.hardMenuContext = true,
		.gamePaused = true,
		.worldFrameStateAvailable = true,
		.currentFrame = 42u,
		.lastWorldRenderFrame = 41u,
		.lastCompletedWorldRenderFrame = 41u,
	});
	CHECK_FALSE(hardMenu.admitted);
}

TEST_CASE("Character submissions reject unsafe regions before GPU work",
	"[upscaling][neural-rendering][characters]")
{
	using namespace NeuralRendering;
	CharacterComputeRegionPlan plan{};
	const ComputeSubrect support{ 0, 0, 100, 60 };
	const auto valid = [&] {
		return GetCharacterRegionSubmissionViolation(0, plan, support, 100, 60, true).empty();
	};
	CHECK(valid());
	plan.count = 2;
	plan.regions = { ComputeSubrect{ 0, 0, 30, 60 }, ComputeSubrect{ 70, 0, 30, 60 } };
	plan.historyKeys = { 10, 20 };
	plan.clusterIdentities = { 1, 2 };
	CHECK(valid());
	CHECK_FALSE(GetCharacterRegionSubmissionViolation(1, plan, support, 100, 60, true).empty());
	CHECK_FALSE(GetCharacterRegionSubmissionViolation(0, plan, support, 100, 60, false).empty());
	plan.regions[1].baseX = 29;
	CHECK_FALSE(valid());
	plan.regions[1].baseX = 71;
	CHECK_FALSE(valid());
	plan.regions[1].baseX = 70;
	plan.historyKeys[1] = plan.historyKeys[0];
	CHECK_FALSE(valid());
	plan.historyKeys[1] = 20;
	plan.clusterIdentities[1] = 0;
	CHECK_FALSE(valid());
	plan.count = 3;
	CHECK_FALSE(valid());
}

TEST_CASE("Full scene submissions preserve the default mono contract",
	"[upscaling][neural-rendering]")
{
	using namespace NeuralRendering;
	CHECK(GetCharacterRegionSubmissionViolation(
		0, {}, {}, 1920, 1080, false)
			.empty());
	CHECK_FALSE(GetCharacterRegionSubmissionViolation(
		1, {}, {}, 1920, 1080, false)
			.empty());
}

TEST_CASE("Explicit character regions require containment and persistent identities",
	"[upscaling][neural-rendering][characters]")
{
	using namespace NeuralRendering;
	const ComputeSubrect support{ 20, 10, 60, 40 };
	CharacterComputeRegionPlan plan{};
	plan.count = 1;
	plan.regions[0] = support;
	plan.historyKeys[0] = 10;
	plan.clusterIdentities[0] = 1;
	const auto valid = [&] {
		return GetCharacterRegionSubmissionViolation(
			0, plan, support, 100, 60, true)
		    .empty();
	};
	CHECK(valid());
	CHECK_FALSE(GetCharacterRegionSubmissionViolation(
		0, plan, {}, 100, 60, true)
			.empty());
	CHECK_FALSE(GetCharacterRegionSubmissionViolation(
		0, plan, support, 100, 60, false)
			.empty());

	SECTION("Output bounds alone cannot admit a region outside its support")
	{
		plan.regions[0].baseX = 19;
		CHECK(plan.regions[0].Fits(100, 60));
		CHECK_FALSE(valid());
	}
	SECTION("Every explicit region requires both identities")
	{
		plan.historyKeys[0] = 0;
		CHECK_FALSE(valid());
		plan.historyKeys[0] = 10;
		plan.clusterIdentities[0] = 0;
		CHECK_FALSE(valid());
	}
	SECTION("Two distinct histories cannot claim the same cluster")
	{
		plan.count = 2;
		plan.regions = { ComputeSubrect{ 20, 10, 20, 40 }, ComputeSubrect{ 60, 10, 20, 40 } };
		plan.historyKeys = { 10, 20 };
		plan.clusterIdentities = { 1, 1 };
		CHECK_FALSE(valid());
		plan.clusterIdentities[1] = 2;
		CHECK(valid());
	}
}
