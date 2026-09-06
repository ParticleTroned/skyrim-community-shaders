#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("Vincent SE keeps NR before DLSS", "[upscaling][neural-rendering]")
{
	CHECK(NeuralRendering::RunsBeforeDlss());
	CHECK_FALSE(NeuralRendering::RunsAfterDlss());
	CHECK_FALSE(NeuralRendering::UsesFeatureUpscaling());
	CHECK(std::strcmp(
			  NeuralRendering::GetPipelineArrangementName(),
			  "neural_then_dlss") == 0);
}

TEST_CASE("Vincent SE preserves NR through safe paused gameplay menus",
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
