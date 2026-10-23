#include "Features/Upscaling/NeuralRendering/GuidedReconstructionPolicy.h"

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
		ReconstructionFilter reconstructionFilter = ReconstructionFilter::Bilinear;
		bool guidedReconstructionEnabled = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		uint32_t frameId = 1;
		uint64_t generation = 1;
		GuidedReconstructionOverride reconstructionOverride = GuidedReconstructionOverride::Auto;
#endif
		bool pressureResolutionEnabled = false;
		bool parallelEyes = false;
		uint32_t featureSlot = 0;
		CentralArea centralArea{};
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct Renderer
	{
		GuidedReconstructionExperiment experiment;
		static Renderer& Instance()
		{
			static Renderer value;
			return value;
		}
		auto ResolveReconstructionOverride(uint32_t frame, uint64_t generation) { return experiment.Resolve(frame, generation); }
	};
#endif
}
struct Upscaling
{
	struct
	{
		uint32_t neuralRenderingModelResolutionPercent = 100;
		uint32_t neuralRenderingReconstructionFilter = 0;
		bool neuralRenderingGuidedReconstruction = false;
		bool neuralRenderingPressureResolutionEnabled = false;
		bool neuralRenderingParallelEyes = false, neuralRenderingBatchedStereo = true;
		uint32_t neuralRenderingCentralAreaPercent = 100, neuralRenderingCentralFeatherPixels = 64;
		float foveatedCenterHorizontalScale = 1.0f;
	} settings;
	NeuralRendering::RenderingMode mode = NeuralRendering::RenderingMode::FullResolution;
	unsigned captures = 0, resets = 0;
	void RequestHistoryReset() noexcept { ++resets; }
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
	upscaling.settings.neuralRenderingGuidedReconstruction = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
	auto& experiment = NeuralRendering::Renderer::Instance().experiment;
	if (!experiment.Configure(NeuralRendering::GuidedReconstructionOverride::GuidedExperiment, 2, experiment.Status().revision))
		throw std::runtime_error("Comparison setup failed");
#endif
	for (const uint32_t value : { 0u, 1u, 2u, 0xffffffffu }) {
		upscaling.settings.neuralRenderingReconstructionFilter = value;
		upscaling.SetNeuralExecutionContext(args, {}, {}, {});
		++calls;
		if (args.reconstructionFilter != (value == 1u ? NeuralRendering::ReconstructionFilter::Lanczos3 : NeuralRendering::ReconstructionFilter::Bilinear))
			throw std::runtime_error("Reconstruction filter was lost or accepted an invalid value");
	}
	if (!args.guidedReconstructionEnabled)
		throw std::runtime_error("Guided enable was lost at admission");
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (upscaling.resets != 1 || args.reconstructionOverride != NeuralRendering::GuidedReconstructionOverride::GuidedExperiment)
		throw std::runtime_error("Comparison history transition repeated or was lost");
	args.frameId += 2;
	upscaling.SetNeuralExecutionContext(args, {}, {}, {});
	++calls;
	if (upscaling.resets != 2 || args.reconstructionOverride != NeuralRendering::GuidedReconstructionOverride::Auto)
		throw std::runtime_error("Expired comparison did not restore history once");
#endif
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
	for (const auto mode : { RenderingMode::ReducedResolution, RenderingMode::FullResolution, RenderingMode::Foveated })
		for (const bool vr : { false, true })
			for (const bool requested : { false, true })
				for (const bool batched : { false, true }) {
					upscaling.mode = mode;
					globals::game::isVR = vr;
					upscaling.settings.neuralRenderingParallelEyes = requested;
					upscaling.settings.neuralRenderingBatchedStereo = batched;
					upscaling.SetNeuralExecutionContext(args, {}, {}, {});
					++calls;
					if (args.parallelEyes != (vr && requested && batched))
						throw std::runtime_error("Parallel NR routing lost runtime, mode or serial-submission policy");
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
