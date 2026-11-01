#include "Features/Upscaling/NeuralRendering/GuidedReconstructionPolicy.h"
#include "Features/Upscaling/NeuralRendering/PeripheralCompressionPolicy.h"
#include "Features/Upscaling/NeuralRendering/ReconstructionTuning.h"

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
		std::array<float, 2> jitterPixels{};
		uint32_t modelResolutionPercent = 100;
		bool guidedReconstructionEnabled = false;
		std::uint32_t guidedConfidenceRejectionPercent = 0;
		std::uint32_t peripheralCompressionPercent = 0;
		PeripheralMask peripheralMask{};
#ifdef DEVBENCH_BRIDGE_ENABLED
		ReconstructionTuning reconstructionTuning{};
		FaceDetailRegions faceDetailRegions{};
		uint32_t sourceWorldFrame = 1;
		uint32_t frameId = 1;
		uint64_t generation = 1;
		GuidedReconstructionOverride reconstructionOverride = GuidedReconstructionOverride::Auto;
		PeripheralCompressionOverride peripheralOverride = PeripheralCompressionOverride::Auto;
#endif
		bool pressureResolutionEnabled = false;
		bool parallelEyes = false;
		uint32_t featureSlot = 0;
		CentralArea centralArea{};
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct CharacterRendering
	{
		static CharacterRendering& Instance()
		{
			static CharacterRendering instance;
			return instance;
		}
		FaceDetailRegions GetFaceDetailRegions(uint32_t, uint32_t, const UpscalingDLSS::ViewportCrop&) const { return {}; }
	};
	struct Renderer
	{
		GuidedReconstructionExperiment experiment;
		PeripheralCompressionExperiment peripheral;
		auto ResolvePeripheralOverride(uint32_t frame, uint64_t generation) { return peripheral.Resolve(frame, generation); }
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
		bool neuralRenderingGuidedReconstruction = true;
		uint32_t neuralRenderingConfidenceRejectionPercent = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		NeuralRendering::ReconstructionTuning neuralReconstructionTuning{};
		bool neuralCharacterRenderingEnabled = false;
#endif
		unsigned neuralRenderingPeripheralCompressionPercent = 0;
		bool neuralRenderingFovOnly = false, neuralRenderingRenderscaleFov = false;
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
	struct FovProfile
	{
		bool available = true;
		float sharedVisibleScale = 0.7f, centerHorizontalScale = 1.0f;
		std::array<float2, 2> centerOffsets{};
	};
	auto GetActiveUpscalingFoveatedProfile() const noexcept { return FovProfile{ true, 0.7f, settings.foveatedCenterHorizontalScale, savedOffsets }; }
	auto GetNeuralRenderingMode() const noexcept { return mode; }
	auto GetJitterForDispatch() const noexcept { return float2{ 0.25f, -0.375f }; }
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
	upscaling.settings.neuralRenderingConfidenceRejectionPercent = 51;
#ifdef DEVBENCH_BRIDGE_ENABLED
	upscaling.settings.neuralReconstructionTuning = { .shadowTransferPercent = 37, .footprintSubdivisions = 4, .perChannelClamping = false, .depthSampleMotion = false };
	auto& experiment = NeuralRendering::Renderer::Instance().experiment;
	if (!experiment.Configure(NeuralRendering::GuidedReconstructionOverride::GuidedExperiment, 2, experiment.Status().revision))
		throw std::runtime_error("Comparison setup failed");
#endif
	for (unsigned repeat = 0; repeat < 4; ++repeat) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		args.faceDetailRegions.current = true;
		args.faceDetailRegions.count = 1;
#endif
		upscaling.SetNeuralExecutionContext(args, {}, {}, {});
		++calls;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (args.faceDetailRegions.current || args.faceDetailRegions.count)
			throw std::runtime_error("Disabled face protection retained stale bounds in a reused request");
#endif
	}
	if (!args.guidedReconstructionEnabled)
		throw std::runtime_error("Guided enable was lost at admission");
	if (args.guidedConfidenceRejectionPercent != 51)
		throw std::runtime_error("Confidence rejection was lost at admission");
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (args.reconstructionTuning != upscaling.settings.neuralReconstructionTuning)
		throw std::runtime_error("Session reconstruction tuning was lost at admission");
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
#ifdef DEVBENCH_BRIDGE_ENABLED
			const auto expectedJitter = mode == RenderingMode::ReducedResolution ? std::array{ -0.25f, 0.375f } : std::array<float, 2>{};
#else
			const std::array<float, 2> expectedJitter{};
#endif
			if (args.jitterPixels != expectedJitter)
				throw std::runtime_error("NR jitter escaped its declared input phase or build gate");
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
	for (bool vr : { false, true })
		for (bool mask : { false, true }) {
			globals::game::isVR = vr;
			upscaling.mode = RenderingMode::ReducedResolution;
			upscaling.settings.neuralRenderingPeripheralCompressionPercent = 20;
			upscaling.settings.neuralRenderingRenderscaleFov = mask;
			upscaling.SetNeuralExecutionContext(args, {}, {}, {});
			++calls;
			if (args.peripheralCompressionPercent != 20 || args.peripheralMask.active != (vr && mask))
				throw std::runtime_error("Peripheral mask lost the FOV/runtime contract");
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
