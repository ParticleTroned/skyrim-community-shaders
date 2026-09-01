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
