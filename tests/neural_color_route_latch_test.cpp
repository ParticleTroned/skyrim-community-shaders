#include "Features/Upscaling/NeuralRendering/CaptureEvidence.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace NeuralRendering::Color
{
	Configuration liveConfiguration{};
	std::uint32_t snapshots = 0;
	Registry& Registry::Instance()
	{
		static Registry registry;
		return registry;
	}
	Configuration Registry::Snapshot() const
	{
		++snapshots;
		return liveConfiguration;
	}
}

namespace NeuralRendering
{
	struct Runtime
	{
		static constexpr std::size_t kFeatureSlotCount = 8;
	};
	struct RendererApplyArgs
	{
		std::uint32_t featureSlot = 0, frameId = 40, sourceWorldFrame = 39;
		std::uint64_t generation = 7;
		InsertionPoint insertionPoint = InsertionPoint::UpscaledCenter;
	};
	struct LatchState
	{
#include "neural_color_route_latch_state.h"
	};
}
#include "neural_color_route_latch_pipeline.h"

namespace
{
	using namespace NeuralRendering;
	void Require(bool condition, const char* reason)
	{
		if (!condition)
			throw std::runtime_error(reason);
	}

	struct TransactionCase
	{
		const char* name;
		RenderingMode mode;
		bool fovOnly;
		InsertionPoint insertion;
		ComputeSubrect region;
	};

	// Geometry fixtures enter the shared colour stage; they do not render the game routes.
	constexpr std::array transactionCases{
		TransactionCase{ "A full", RenderingMode::FullResolution, false, InsertionPoint::FinalLdrPreUi, { 0u, 0u, 64u, 40u } },
		TransactionCase{ "A FOV", RenderingMode::FullResolution, true, InsertionPoint::FinalLdrPreUi, { 3u, 5u, 35u, 19u } },
		TransactionCase{ "B centre", RenderingMode::Foveated, false, InsertionPoint::UpscaledCenter, { 3u, 5u, 35u, 19u } },
		TransactionCase{ "B final", RenderingMode::Foveated, false, InsertionPoint::FinalLdrPreUi, { 3u, 5u, 35u, 19u } },
		TransactionCase{ "C full", RenderingMode::ReducedResolution, false, InsertionPoint::UpscaledCenter, { 0u, 0u, 32u, 20u } },
		TransactionCase{ "C FOV", RenderingMode::ReducedResolution, true, InsertionPoint::UpscaledCenter, { 3u, 5u, 17u, 9u } },
	};

	void CheckTransaction(const TransactionCase& fixture, bool stereo, bool submit, std::uint32_t characterRegionsPerEye)
	{
		LatchState state;
		Require(IsRenderingConfigurationSupported(stereo, fixture.mode, characterRegionsPerEye != 0),
			"Fixture must represent a supported runtime configuration");
		Require(stereo || (!submit && !RequiresFoveatedMask(fixture.mode, fixture.fovOnly)),
			"The supported mono fixture must use main full-resolution rendering without FOV");
		const auto insertion = ResolveInsertionPoint(fixture.mode, static_cast<std::uint32_t>(fixture.insertion));
		Require(insertion == fixture.insertion, "Fixture must use its effective production insertion");
		const std::uint32_t routeIndex = submit ? 1u : 0u;
		RendererApplyArgs args;
		args.insertionPoint = insertion;
		args.featureSlot = submit ? 2u : 0u;
		Color::liveConfiguration = {};
		Color::liveConfiguration.settings.mode = Color::Mode::PreserveSource;
		Color::liveConfiguration.settings.lightingPreservation = 0.25f;
		Color::liveConfiguration.experiments.captureFrameEvidence = true;
		Color::liveConfiguration.revision = 14;
		Color::snapshots = 0;
		state.CaptureColorConfiguration(args);
		Require(Color::snapshots == 1, "First region must capture one configuration");
		const auto transaction = state.colorConfiguration_;
		Color::liveConfiguration.settings.lightingPreservation = 0.75f;
		++Color::liveConfiguration.revision;

		const std::array<std::uint32_t, 4> mainSlots{ 0, 1, 4, 5 };
		const std::array<std::uint32_t, 4> submitSlots{ 2, 3, 6, 7 };
		const auto& slots = submit ? submitSlots : mainSlots;
		std::array<Color::Work, 4> work{};
		const std::size_t regionCount = (stereo ? 2u : 1u) * std::max(characterRegionsPerEye, 1u);
		for (std::size_t region = 0; region < regionCount; ++region) {
			args.featureSlot = slots[region];
			state.CaptureColorConfiguration(args);
			Require(Color::snapshots == 1 && state.colorConfiguration_.settings.lightingPreservation == 0.25f,
				"Registry updates cannot split eye or character-region configurations");
			Color::Observation observation;
			observation.insertion = static_cast<std::uint32_t>(insertion);
			observation.slot = args.featureSlot;
			observation.rect = fixture.region;
			if (characterRegionsPerEye != 0) {
				observation.rect.baseX += static_cast<std::uint32_t>(region / 2u);
				observation.rect.width /= 2u;
				observation.rect.height /= 2u;
			}
			work[region].format = insertion == InsertionPoint::FinalLdrPreUi ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
			Color::LatchWork(work[region], state.colorConfiguration_, observation);
			const auto prepared = Color::MakeConstants(work[region]);
			Color::liveConfiguration.settings.lightingPreservation = 1.0f;
			const auto reconstructed = Color::MakeConstants(work[region]);
			Require(prepared.lightingPreservation == 0.25f && reconstructed.lightingPreservation == 0.25f &&
						work[region].observation.lightingPreservation == 0.25f,
				"Repeated constant generation and delayed evidence must use the same frozen control");
			Require(prepared.x == observation.rect.baseX && prepared.y == observation.rect.baseY &&
						prepared.width == observation.rect.width && prepared.height == observation.rect.height,
				"Colour constants must retain full, reduced, or character-region coordinates");
			for (float padding : reconstructed.padding)
				Require(padding == 0.0f, "Extended constant-buffer padding must be initialized");
		}
		Require(state.captureInputs_[routeIndex].configuration.settings.lightingPreservation == 0.25f,
			"Capture companion must preserve the same transaction configuration");
		Require(!Color::ChangesInput(transaction, Color::liveConfiguration, 0) &&
					!Color::ChangesInput(transaction, Color::liveConfiguration, 1),
			"Lighting changes must not reset either input history");
		++args.frameId;
		state.CaptureColorConfiguration(args);
		Require(Color::snapshots == 2 && state.colorConfiguration_.settings.lightingPreservation == 1.0f,
			"The next evaluation transaction must observe the new control");

		for (std::uint32_t changedKey = 0; changedKey < 3; ++changedKey) {
			Color::liveConfiguration.settings.lightingPreservation = 0.5f;
			if (changedKey == 0)
				++args.sourceWorldFrame;
			if (changedKey == 1)
				++args.generation;
			if (changedKey == 2)
				args.insertionPoint = insertion == InsertionPoint::FinalLdrPreUi ? InsertionPoint::UpscaledCenter : InsertionPoint::FinalLdrPreUi;
			const auto before = Color::snapshots;
			state.CaptureColorConfiguration(args);
			Require(Color::snapshots == before + 1, "Source, generation, or insertion changes must start a fresh transaction");
		}
	}

	void CheckReconstructionPreflight()
	{
		Color::ReconstructionPreflight gate;
		gate.work.configuration.revision = 14;
		const auto prepared = gate.work.configuration;
		Require(gate.Admit(prepared), "Matching prepared work must pass reconstruction preflight");
		auto later = prepared;
		++later.revision;
		later.settings.lightingPreservation = 0.25f;
		Require(!gate.Admit(later), "New registry revision must not reconstruct an earlier prepared transaction");
		Require(gate.Admit(prepared), "A registry change must not reject the original latched transaction");
		const std::array required{ &gate.context, &gate.neural, &gate.neuralSRV, &gate.preparedSRV,
			&gate.work.result.uav, &gate.reconstruct_, &gate.constants_, &gate.work.prepared };
		for (auto* present : required) {
			*present = false;
			Require(!gate.Admit(prepared), "Each missing prerequisite must reject before reconstruction side effects");
			*present = true;
		}
	}
}

int main()
{
	try {
		std::uint32_t transactions = 0;
		for (const auto& fixture : transactionCases) {
			for (const bool submit : { false, true })
				for (const std::uint32_t characterRegionsPerEye : { 0u, 1u, 2u }) {
					try {
						CheckTransaction(fixture, true, submit, characterRegionsPerEye);
					} catch (const std::exception& error) {
						throw std::runtime_error(std::string(fixture.name) + ": " + error.what());
					}
					++transactions;
				}
			Require(!IsRenderingConfigurationSupported(false, fixture.mode, true),
				"Character rendering must remain unavailable on flat runtimes");
			if (fixture.mode != RenderingMode::FullResolution)
				Require(!IsRenderingConfigurationSupported(false, fixture.mode, false),
					"Flat runtimes must reject the VR-only foveated and reduced routes");
		}
		CheckTransaction(transactionCases.front(), false, false, 0u);
		++transactions;
		CheckReconstructionPreflight();
		NeuralRendering::LatchState interleaved;
		NeuralRendering::RendererApplyArgs args;
		NeuralRendering::Color::liveConfiguration.settings.lightingPreservation = 0.0f;
		interleaved.CaptureColorConfiguration(args);
		NeuralRendering::Color::liveConfiguration.settings.lightingPreservation = 1.0f;
		args.featureSlot = 2;
		interleaved.CaptureColorConfiguration(args);
		Require(interleaved.colorConfiguration_.settings.lightingPreservation == 1.0f, "Submit has an independent configuration latch");
		args.featureSlot = 1;
		interleaved.CaptureColorConfiguration(args);
		Require(interleaved.colorConfiguration_.settings.lightingPreservation == 0.0f, "Interleaved submit cannot replace the main transaction");
		std::cout << "Passed " << transactions << " CPU colour transaction/constant cases, reconstruction preflight, and route isolation (no game rendering)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
