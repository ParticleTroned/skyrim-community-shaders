#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace globals::game
{
	inline bool isVR = true;
}
struct float2
{
	float x = 0, y = 0;
};
namespace NeuralRendering
{
	struct RendererApplyArgs
	{
		std::optional<RenderingMode> renderingMode;
		uint32_t modelResolutionPercent = 100;
		bool pressureResolutionEnabled = false;
		uint32_t featureSlot = 0;
		CentralArea centralArea{};
	};
}
struct Upscaling
{
	struct
	{
		uint32_t neuralRenderingModelResolutionPercent = 100;
		bool neuralRenderingPressureResolutionEnabled = false;
		uint32_t neuralRenderingCentralAreaPercent = 100, neuralRenderingCentralFeatherPixels = 64;
		float foveatedCenterHorizontalScale = 1.0f;
	} settings;
	NeuralRendering::RenderingMode mode = NeuralRendering::RenderingMode::FullResolution;
	unsigned captures = 0;
	std::array<float2, 2> savedOffsets{};
	auto GetResolvedFoveatedMaskCenterOffsets(bool) const noexcept { return savedOffsets; }
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
		for (const uint32_t percent : { 0u, 29u, 30u, 32u, 33u, 50u, 67u, 100u, 101u }) {
			upscaling.settings.neuralRenderingModelResolutionPercent = percent;
			upscaling.SetNeuralExecutionContext(args, {}, { 2, 3 }, { 4, 5 });
			++calls;
			const uint32_t expected = percent >= 30 && percent <= 100 ? percent : 100;
			if (args.renderingMode != mode || args.modelResolutionPercent != expected)
				throw std::runtime_error("Renderer lost the selected NR mode or model resolution");
		}
	}
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		upscaling.settings.neuralRenderingCentralAreaPercent = 50;
		upscaling.settings.neuralRenderingCentralFeatherPixels = 256;
		upscaling.settings.foveatedCenterHorizontalScale = 1.2f;
		upscaling.savedOffsets = { float2{ -0.1f, 0.02f }, float2{ 0.2f, -0.03f } };
		for (unsigned slot = 0; slot < 4; ++slot) {
			args.featureSlot = slot;
			const auto crop = UpscalingDLSS::ViewportCrop::Identity(900, 700, 1800, 1400);
			upscaling.SetNeuralExecutionContext(args, crop, {}, {});
			++calls;
			const auto expected = vr ? upscaling.savedOffsets[slot % 2] : float2{};
			if (args.centralArea.percent != 50 || args.centralArea.featherPixels != 256 ||
				args.centralArea.finalOutput != crop.fullOutput || args.centralArea.horizontalScale != (vr ? 1.2f : 1.0f) ||
				args.centralArea.offset != std::array{ expected.x, expected.y })
				throw std::runtime_error("Central area lost per-eye centre, flat fallback or final output pixel basis");
		}
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (upscaling.captures != calls)
#else
	if (upscaling.captures != 0)
#endif
		throw std::runtime_error("NR capture did not follow the build boundary");
}
