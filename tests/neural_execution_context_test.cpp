#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace NeuralRendering
{
	struct RendererApplyArgs
	{
		std::optional<RenderingMode> renderingMode;
		uint32_t modelResolutionPercent = 100;
	};
}
struct Upscaling
{
	struct
	{
		uint32_t neuralRenderingModelResolutionPercent = 100;
	} settings;
	NeuralRendering::RenderingMode mode = NeuralRendering::RenderingMode::FullResolution;
	unsigned captures = 0;
	auto GetNeuralRenderingMode() const noexcept { return mode; }
	void SetNeuralExecutionContext(NeuralRendering::RendererApplyArgs&,
		const UpscalingDLSS::ViewportCrop&, const std::array<uint32_t, 2>&,
		const std::array<uint32_t, 2>&) noexcept;
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetNeuralCaptureExecutionContext(NeuralRendering::RendererApplyArgs&,
		const UpscalingDLSS::ViewportCrop&, const std::array<uint32_t, 2>&,
		const std::array<uint32_t, 2>&) noexcept { ++captures; }
#endif
};
#include "neural_execution_context_under_test.h"

int main()
{
	using NeuralRendering::RenderingMode;
	Upscaling upscaling;
	NeuralRendering::RendererApplyArgs args;
	unsigned calls = 0;
	for (const auto mode : { RenderingMode::ReducedResolution, RenderingMode::FullResolution, RenderingMode::Foveated }) {
		upscaling.mode = mode;
		for (const uint32_t percent : { 0u, 32u, 33u, 50u, 67u, 100u, 101u }) {
			upscaling.settings.neuralRenderingModelResolutionPercent = percent;
			upscaling.SetNeuralExecutionContext(args, {}, { 2, 3 }, { 4, 5 });
			++calls;
			const uint32_t expected = mode == RenderingMode::ReducedResolution && percent >= 33 && percent <= 100 ? percent : 100;
			if (args.renderingMode != mode || args.modelResolutionPercent != expected)
				throw std::runtime_error("Renderer lost the selected NR mode or model resolution");
		}
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (upscaling.captures != calls)
#else
	if (upscaling.captures != 0)
#endif
		throw std::runtime_error("NR capture did not follow the build boundary");
}
