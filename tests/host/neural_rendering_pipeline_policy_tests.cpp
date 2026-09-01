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
