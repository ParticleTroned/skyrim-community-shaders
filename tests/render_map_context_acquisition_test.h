#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <thread>

namespace CSX::RenderMap
{
	namespace ContextHookFixture
	{
		enum class AcquisitionOperation
		{
			Draw,
			Dispatch,
			Copy,
			CopyRegion
		};

		enum class AcquisitionTurnoverPoint
		{
			Native,
			FirstGetter,
			OutputGetter
		};

		inline bool concurrentAcquisition = false;
		inline bool acquisitionWaitCompleted = true;
		inline std::thread acquisitionWorker;
		inline std::exception_ptr acquisitionFailure;
		inline bool freshAcquisition = false;
		inline AcquisitionTurnoverPoint acquisitionTurnoverPoint = AcquisitionTurnoverPoint::Native;
		inline constexpr D3D11_BOX acquisitionBox{ 1, 2, 3, 17, 19, 23 };
		inline constexpr D3D11_BOX successorBox{ 4, 6, 8, 20, 24, 28 };

		void AcquisitionTurnover()
		{
			if (!concurrentAcquisition) {
				ChangeCapture();
				return;
			}
			std::promise<void> completed;
			auto ready = completed.get_future();
			acquisitionWorker = std::thread([done = std::move(completed)]() mutable {
				try {
					ChangeCapture();
				} catch (...) {
					acquisitionFailure = std::current_exception();
				}
				done.set_value();
			});
			// Return before joining so an incorrect caller-held lock can unwind.
			acquisitionWaitCompleted = ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		}

		const D3D11_BOX* AcquisitionBox(const State& state)
		{
			if (state.nullOutput)
				return nullptr;
			return freshAcquisition ? &successorBox : &acquisitionBox;
		}

		void AcquisitionNative(ID3D11DeviceContext* context, AcquisitionOperation operation)
		{
			Require(context == &current->context, "acquisition thunk changed native context");
			++current->calls;
			getterEntries = 0;

			// Expose outputs only after native execution so state seeding cannot acquire them.
			if (operation == AcquisitionOperation::Draw)
				current->context.renderTarget = &current->renderTarget;
			else if (operation == AcquisitionOperation::Dispatch)
				current->context.computeOutput = &current->computeOutput;
			if (current->mode == Mode::Stable)
				return;

			switch (acquisitionTurnoverPoint) {
			case AcquisitionTurnoverPoint::Native:
				AcquisitionTurnover();
				break;
			case AcquisitionTurnoverPoint::FirstGetter:
				getterTurnover = AcquisitionTurnover;
				break;
			case AcquisitionTurnoverPoint::OutputGetter:
				current->context.outputTurnover = AcquisitionTurnover;
				break;
			}
		}

		void BindAcquisitionOriginals()
		{
			ID3D11DeviceContext_DrawIndexed::func = +[](ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
				Require(count == (freshAcquisition ? 29u : 13u) && start == 5 && base == -4, "acquisition draw arguments changed");
				AcquisitionNative(context, AcquisitionOperation::Draw);
			};
			ID3D11DeviceContext_Dispatch::func = +[](ID3D11DeviceContext* context, UINT x, UINT y, UINT z) {
				Require(x == (freshAcquisition ? 8u : 2u) && y == 3 && z == 4, "acquisition dispatch arguments changed");
				AcquisitionNative(context, AcquisitionOperation::Dispatch);
			};
			ID3D11DeviceContext_CopyResource::func = +[](ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) {
				Require(destination == (freshAcquisition ? &current->source : &current->destination) &&
							source == (freshAcquisition ? &current->destination : &current->source),
					"acquisition copy arguments changed");
				AcquisitionNative(context, AcquisitionOperation::Copy);
			};
			ID3D11DeviceContext_CopySubresourceRegion::func = +[](ID3D11DeviceContext* context, ID3D11Resource* destination,
																   UINT destinationSubresource, UINT x, UINT y, UINT z, ID3D11Resource* source, UINT sourceSubresource, const D3D11_BOX* box) {
				Require(destination == &current->destination && source == &current->source &&
							destinationSubresource == (freshAcquisition ? 6u : 3u) && x == (freshAcquisition ? 13u : 5u) &&
							y == 7 && z == 11 && sourceSubresource == (freshAcquisition ? 4u : 2u) && box == AcquisitionBox(*current),
					"acquisition copy-region arguments or source box changed");
				AcquisitionNative(context, AcquisitionOperation::CopyRegion);
			};
		}

		void InvokeAcquisition(AcquisitionOperation operation, State& state)
		{
			state.context.renderTarget = nullptr;
			state.context.computeOutput = nullptr;
			state.context.outputReferencesReturned = 0;
			state.context.lastOutputGetterEntry = 0;
			switch (operation) {
			case AcquisitionOperation::Draw:
				ID3D11DeviceContext_DrawIndexed::thunk(&state.context, freshAcquisition ? 29u : 13u, 5, -4);
				break;
			case AcquisitionOperation::Dispatch:
				ID3D11DeviceContext_Dispatch::thunk(&state.context, freshAcquisition ? 8u : 2u, 3, 4);
				break;
			case AcquisitionOperation::Copy:
				ID3D11DeviceContext_CopyResource::thunk(&state.context,
					freshAcquisition ? &state.source : &state.destination, freshAcquisition ? &state.destination : &state.source);
				break;
			case AcquisitionOperation::CopyRegion:
				ID3D11DeviceContext_CopySubresourceRegion::thunk(&state.context, &state.destination,
					freshAcquisition ? 6u : 3u, freshAcquisition ? 13u : 5u, 7, 11,
					&state.source, freshAcquisition ? 4u : 2u, AcquisitionBox(state));
				break;
			}
		}

		const EventRecord& OneAcquisitionEvent(const CaptureSnapshot& snapshot, PayloadSchema schema, EventKind kind)
		{
			const auto matches = [schema](const EventRecord& event) {
				return event.payload.schema == static_cast<std::uint16_t>(schema);
			};
			Require(std::count_if(snapshot.events.begin(), snapshot.events.end(), matches) == 1,
				"acquisition snapshot lost or duplicated a concrete payload");
			const auto& event = *std::find_if(snapshot.events.begin(), snapshot.events.end(), matches);
			Require(event.kind == kind && event.sessionGeneration == snapshot.sessionGeneration,
				"acquisition payload kind or generation changed");
			return event;
		}

		std::uint64_t AcquisitionResource(const CaptureSnapshot& snapshot, const Buffer& buffer)
		{
			const auto resource = std::find_if(snapshot.resourceObservations.begin(), snapshot.resourceObservations.end(),
				[&buffer](const auto& row) { return row.d3dObject == reinterpret_cast<std::uintptr_t>(&buffer); });
			Require(resource != snapshot.resourceObservations.end() && resource->observationId != 0 && resource->widthOrBytes == 64,
				"acquisition payload lost its resource description");
			return resource->observationId;
		}

		void CheckAcquisitionPayloads(const CaptureSnapshot& snapshot, AcquisitionOperation operation, const State& state)
		{
			const bool draw = operation == AcquisitionOperation::Draw;
			const bool dispatch = operation == AcquisitionOperation::Dispatch;
			const bool copyRegion = operation == AcquisitionOperation::CopyRegion;
			const auto& operationEvent = OneAcquisitionEvent(snapshot, PayloadSchema::kTransferOperation, EventKind::kRasterStateObserved);
			const auto operationId = operationEvent.payload.words[0];
			const auto flow = copyRegion ? ResourceFlowOperation::kCopySubresourceRegion : ResourceFlowOperation::kCopyResource;
			const auto transferCode = draw     ? static_cast<std::uint64_t>(DrawOperation::kDrawIndexed) :
			                          dispatch ? (1ull << 32u) | static_cast<std::uint64_t>(DispatchOperation::kDispatch) :
			                                     0x100u + static_cast<std::uint64_t>(flow);
			Require(operationId != 0 && operationEvent.payload.words[1] == transferCode,
				"transfer operation identity or opcode changed");

			const auto countKind = [&snapshot](EventKind kind) {
				return std::count_if(snapshot.events.begin(), snapshot.events.end(),
					[kind](const EventRecord& event) { return event.kind == kind; });
			};
			Require(countKind(EventKind::kDraw) == (draw ? 1 : 0) && countKind(EventKind::kDispatch) == (dispatch ? 1 : 0) &&
						countKind(EventKind::kResourceFlow) == (copyRegion ? 2 : draw || dispatch ? 0 :
																									1),
				"stale or duplicate draw, dispatch or copy reached the snapshot");

			const auto countSchema = [&snapshot](PayloadSchema schema) {
				return std::count_if(snapshot.events.begin(), snapshot.events.end(),
					[schema](const EventRecord& event) { return event.payload.schema == static_cast<std::uint16_t>(schema); });
			};
			Require(countSchema(PayloadSchema::kRasterState) == (draw || dispatch ? 1 : 0) &&
						countSchema(PayloadSchema::kPostProcessingBoundary) == 2,
				"stale raster or scope metadata reached the snapshot");
			for (const auto& event : snapshot.events) {
				if (event.payload.schema == static_cast<std::uint16_t>(PayloadSchema::kPostProcessingBoundary))
					Require(event.payload.words[4] == (freshAcquisition ? 78u : 77u), "stale post-processing scope reached the snapshot");
			}
			if (draw || dispatch)
				Require(state.context.outputReferencesReturned == 1, "fresh thunk did not acquire its output view");

			const bool swappedCopy = operation == AcquisitionOperation::Copy && freshAcquisition;
			const auto readResource = AcquisitionResource(snapshot, swappedCopy ? state.destination : state.source);
			const auto writeResource = AcquisitionResource(snapshot, swappedCopy ? state.source : state.destination);
			const unsigned sourceSubresource = copyRegion ? (freshAcquisition ? 4u : 2u) : 0u;
			const unsigned destinationSubresource = copyRegion ? (freshAcquisition ? 6u : 3u) : 0u;
			if (draw) {
				const auto& words = OneAcquisitionEvent(snapshot, PayloadSchema::kDrawCall, EventKind::kDraw).payload.words;
				Require(words[1] == static_cast<std::uint64_t>(DrawOperation::kDrawIndexed) &&
							words[4] == (freshAcquisition ? 29u : 13u) && words[5] == 5 && words[6] == static_cast<std::uint32_t>(-4),
					"draw payload lost the expected arguments");
			} else if (dispatch) {
				const auto& words = OneAcquisitionEvent(snapshot, PayloadSchema::kDispatchCall, EventKind::kDispatch).payload.words;
				Require(words[1] == static_cast<std::uint64_t>(DispatchOperation::kDispatch) &&
							words[3] == (freshAcquisition ? 8u : 2u) && words[4] == 3 && words[5] == 4,
					"dispatch payload lost the expected arguments");
			} else {
				const auto& words = OneAcquisitionEvent(snapshot, PayloadSchema::kResourceFlow, EventKind::kResourceFlow).payload.words;
				Require(words[0] == static_cast<std::uint64_t>(flow) && words[1] == readResource && words[2] == writeResource &&
							words[3] == sourceSubresource && words[4] == destinationSubresource,
					"copy payload lost its resource roles or subresources");
			}
			if (copyRegion) {
				const auto pack = [](UINT low, UINT high) { return low | (static_cast<std::uint64_t>(high) << 32u); };
				const auto* sourceBox = AcquisitionBox(state);
				const auto box = sourceBox ? *sourceBox : D3D11_BOX{};
				const std::array<std::uint64_t, 8> expected{
					operationId, pack(sourceSubresource, destinationSubresource), pack(freshAcquisition ? 13u : 5u, 7), 11,
					pack(box.left, box.top), pack(box.front, box.right), pack(box.bottom, box.back), sourceBox != nullptr
				};
				Require(OneAcquisitionEvent(snapshot, PayloadSchema::kTransferCopyRegion, EventKind::kResourceFlow).payload.words == expected,
					"copy-region payload lost coordinates, source box, or null-box semantics");
			}

			unsigned reads = 0;
			unsigned writes = 0;
			for (const auto& event : snapshot.events) {
				if (event.payload.schema != static_cast<std::uint16_t>(PayloadSchema::kTransferResourceAccess))
					continue;
				const auto& words = event.payload.words;
				const bool write = (words[6] & 1u) != 0;
				Require(event.kind == EventKind::kTransferResourceAccess && words[7] == operationId &&
							words[0] == (write ? writeResource : readResource),
					"transfer access lost its operation or resource identity");
				const auto stage = dispatch ? ResourceStage::kCompute : write ? ResourceStage::kOutputMerger :
				                                                                ResourceStage::kPixel;
				const auto slot = draw     ? (write ? Context::renderTargetSlot : 7u) :
				                  dispatch ? (write ? Context::computeOutputSlot : 7u) :
				                             (write ? destinationSubresource : sourceSubresource);
				Require(words[4] == static_cast<std::uint64_t>(stage) && words[5] == slot,
					"transfer access lost its stage or slot");
				if (write) {
					++writes;
					Require(words[2] != 0 && words[3] != 0, "transfer write lost its version or command sequence");
				} else {
					++reads;
				}
				if (draw || dispatch) {
					const auto view = std::find_if(snapshot.targetViewObservations.begin(), snapshot.targetViewObservations.end(),
						[&words](const auto& row) { return row.observationId == words[1]; });
					const auto kind = write ? (draw ? TargetViewKind::kRenderTarget : TargetViewKind::kUnorderedAccess) :
					                          TargetViewKind::kShaderResource;
					const auto pointer = !write ? reinterpret_cast<std::uintptr_t>(&state.view) :
					                     draw   ? reinterpret_cast<std::uintptr_t>(&state.renderTarget) :
					                              reinterpret_cast<std::uintptr_t>(&state.computeOutput);
					Require(view != snapshot.targetViewObservations.end() && view->kind == kind &&
								view->pointerEvidence == pointer && view->resourceObservationId == words[0] && view->format == DXGI_FORMAT_R32_FLOAT,
						"transfer access lost the acquired view or its descriptor");
				}
			}
			Require(reads == 1 && writes == 1, "transfer payload lost or duplicated an access");
		}

		void CheckAcquisitionReferences(const State& state)
		{
			Require(state.view.references == 1 && state.source.references == 1 && state.destination.references == 1 &&
						state.pixel.references == 1 && state.compute.references == 1 &&
						state.renderTarget.references == 1 && state.computeOutput.references == 1,
				"acquisition COM ownership leaked");
		}

		void ExerciseAcquisition(AcquisitionOperation operation, Mode mode, AcquisitionTurnoverPoint point,
			bool concurrent, bool nullBox = false)
		{
			State state;
			state.mode = mode;
			state.nullOutput = nullBox;
			state.context.resources[4] = &state.view;
			state.context.resources[5] = &state.view;
			state.context.queriedPixel = &state.pixel;
			state.context.queriedCompute = &state.compute;
			state.renderTarget.description.Format = DXGI_FORMAT_R32_FLOAT;
			state.renderTarget.description.ViewDimension = D3D11_RTV_DIMENSION_BUFFER;
			state.renderTarget.description.Buffer.NumElements = 16;
			state.computeOutput.description.Format = DXGI_FORMAT_R32_FLOAT;
			state.computeOutput.description.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			state.computeOutput.description.Buffer.NumElements = 16;
			current = &state;
			freshAcquisition = false;
			acquisitionTurnoverPoint = point;
			concurrentAcquisition = concurrent;
			acquisitionWaitCompleted = true;
			acquisitionFailure = nullptr;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(&state.context));
			runtime.ForCapture(0).RecordExecuteCommandList(reinterpret_cast<std::uintptr_t>(&state.context), 0, false);
			if (mode != Mode::InactiveStart)
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "acquisition capture did not start");
			runtime.SetCpuFrame(1);
			const auto entryGeneration = runtime.ActiveCaptureGeneration();
			BindAcquisitionOriginals();
			try {
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 77);
				InvokeAcquisition(operation, state);
			} catch (...) {
				getterTurnover = nullptr;
				state.context.outputTurnover = nullptr;
				if (acquisitionWorker.joinable())
					acquisitionWorker.join();
				throw;
			}
			getterTurnover = nullptr;
			state.context.outputTurnover = nullptr;
			if (acquisitionWorker.joinable())
				acquisitionWorker.join();
			if (acquisitionFailure)
				std::rethrow_exception(acquisitionFailure);
			Require(acquisitionWaitCompleted, "capture lifecycle blocked inside native/getter callback");
			Require(state.calls == 1, "acquisition thunk did not forward native exactly once");
			CheckAcquisitionReferences(state);
			if (mode == Mode::Stable) {
				const auto stable = runtime.StopCapture();
				Require(stable.has_value() && stable->sessionGeneration == entryGeneration && getterEntries > 0,
					"stable acquisition capture or getter path disappeared");
				CheckAcquisitionPayloads(*stable, operation, state);
				return;
			}
			if (point == AcquisitionTurnoverPoint::OutputGetter) {
				Require(state.context.outputReferencesReturned == 1 && state.context.lastOutputGetterEntry > 0 &&
							getterEntries == state.context.lastOutputGetterEntry && state.renderTarget.resourceQueries == 0 &&
							state.renderTarget.descriptionQueries == 0 && state.computeOutput.resourceQueries == 0 &&
							state.computeOutput.descriptionQueries == 0,
					"stale output acquisition leaked metadata queries or failed to return a view");
			} else {
				Require(getterEntries == (point == AcquisitionTurnoverPoint::FirstGetter ? 1u : 0u),
					"already-stale call entered extra metadata getters");
			}

			// A restart or inactive-entry callback has already started the successor.
			if (mode == Mode::Stop) {
				Require(runtime.ActiveCaptureGeneration() == 0, "stop-only callback left a capture active");
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "successor after stop did not start");
			}
			const auto successorGeneration = runtime.ActiveCaptureGeneration();
			Require(successorGeneration != 0 && successorGeneration != entryGeneration, "callback did not retain a fresh successor");
			runtime.SetCpuFrame(1);
			state.mode = Mode::Stable;
			freshAcquisition = true;
			{
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 78);
				InvokeAcquisition(operation, state);
			}
			const auto successor = runtime.StopCapture();
			Require(successor.has_value() && successor->sessionGeneration == successorGeneration &&
						state.calls == 2 && getterEntries > 0,
				"fresh work did not complete in the callback's successor");
			CheckAcquisitionPayloads(*successor, operation, state);
			CheckAcquisitionReferences(state);
		}

		enum class BootstrapDeadline
		{
			Stable,
			BeforeEntry,
			DuringVertexShader
		};

		struct BootstrapClock
		{
			static inline std::uint64_t ticks = 0;
			static inline unsigned expiryGetter = 0;

			BootstrapClock()
			{
				ticks = 1;
				expiryGetter = 0;
				Collector::SetActivationTestHooks(Read, nullptr);
			}

			~BootstrapClock() { Collector::SetActivationTestHooks(nullptr, nullptr); }

			static std::uint64_t Read() noexcept { return ticks; }

			static void Expire() noexcept
			{
				expiryGetter = getterEntries;
				ticks += 2 * Collector::ClockFrequencyHz();
			}
		};

		void ExerciseBootstrapDeadline(BootstrapDeadline a_case)
		{
			BootstrapClock clock;
			State state;
			current = &state;
			auto& runtime = GetRuntime();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(&state.context));
			auto config = Configuration();
			config.latePostProcessingWindow = true;
			config.activationTarget = 9;
			config.maxDuration = std::chrono::seconds(1);
			Require(Collector::ClockFrequencyHz() > 0, "bootstrap clock frequency unavailable");
			Require(runtime.StartCapture(config) == StartResult::kStarted, "deadline capture did not arm");
			runtime.SetCpuFrame(12);
			Require(runtime.ActivatePostProcessingWindow(9, 12, 77), "deadline capture did not activate");
			const auto generation = runtime.ActiveCaptureGeneration();
			getterEntries = 0;
			{
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 77);
				Require(runtime.IsInsidePostProcessing(), "deadline bootstrap scope did not enter");
				if (a_case == BootstrapDeadline::BeforeEntry)
					BootstrapClock::Expire();
				if (a_case == BootstrapDeadline::DuringVertexShader)
					state.context.onVertexShaderQuery = BootstrapClock::Expire;
				const auto accepted = CapturePostProcessingBootstrap(&state.context);
				if (a_case == BootstrapDeadline::Stable) {
					Require(accepted && getterEntries > 1, "stable late-window bootstrap did not observe metadata");
					runtime.CompleteWindowBootstrap(accepted);
					Require(runtime.GetCaptureWindow().bootstrapComplete, "stable late-window bootstrap did not complete");
				} else {
					Require(!accepted && getterEntries == BootstrapClock::expiryGetter,
						"expired bootstrap accepted or entered another getter");
					Require(a_case != BootstrapDeadline::DuringVertexShader || BootstrapClock::expiryGetter > 1,
						"deadline fixture did not reach the vertex shader getter");
					Require(runtime.ActiveCaptureGeneration() == generation && runtime.IsInsidePostProcessing() &&
								runtime.GetCaptureWindow().failure == CaptureWindowFailure::kActiveTimeout,
						"expiry did not retain the original generation and scope");
				}
			}
			Require(runtime.StopCapture().has_value(), "deadline capture did not stop");
		}

		void ExerciseBootstrapAdmission()
		{
			State state;
			current = &state;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(&state.context));
			getterEntries = 0;
			Require(!CapturePostProcessingBootstrap(&state.context) && getterEntries == 0,
				"inactive bootstrap entered an optional getter");

			Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "bootstrap capture did not start");
			Require(!CapturePostProcessingBootstrap(&state.context) && getterEntries == 0,
				"bootstrap without a scope entered an optional getter");
			{
				auto stale = runtime.EnterPostProcessing({}, {}, 9, 77);
				state.mode = Mode::Restart;
				ChangeCapture();
				Require(!CapturePostProcessingBootstrap(&state.context) && getterEntries == 0,
					"stale bootstrap scope entered an optional getter");
			}
			{
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 78);
				state.changed = false;
				getterTurnover = ChangeCapture;
				Require(!CapturePostProcessingBootstrap(&state.context) && getterEntries == 1,
					"bootstrap continued metadata acquisition after GetType changed capture");
			}
			{
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 79);
				Require(CapturePostProcessingBootstrap(&state.context) && getterEntries > 1,
					"matching bootstrap scope lost its observation path");
			}
			const auto capture = runtime.StopCapture();
			Require(capture.has_value() && std::count_if(capture->events.begin(), capture->events.end(), [](const EventRecord& event) {
				return event.payload.schema == static_cast<std::uint16_t>(PayloadSchema::kRasterState);
			}) == 2,
				"matching bootstrap failed to record its raster snapshots");
		}
	}

	void RunContextAcquisitionTests()
	{
		using namespace ContextHookFixture;
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_CopySubresourceRegion::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::CopySubresourceRegion)>::Type>);
		for (const auto operation : { AcquisitionOperation::Draw, AcquisitionOperation::Dispatch,
				 AcquisitionOperation::Copy, AcquisitionOperation::CopyRegion }) {
			ExerciseAcquisition(operation, Mode::Stable, AcquisitionTurnoverPoint::Native, false);
			ExerciseAcquisition(operation, Mode::InactiveStart, AcquisitionTurnoverPoint::Native, false);
			for (const auto mode : { Mode::Stop, Mode::Restart }) {
				for (const auto point : { AcquisitionTurnoverPoint::Native, AcquisitionTurnoverPoint::FirstGetter }) {
					ExerciseAcquisition(operation, mode, point, false);
					ExerciseAcquisition(operation, mode, point, true);
				}
			}
		}
		ExerciseAcquisition(AcquisitionOperation::CopyRegion, Mode::Stable, AcquisitionTurnoverPoint::Native, false, true);
		for (const auto operation : { AcquisitionOperation::Draw, AcquisitionOperation::Dispatch }) {
			ExerciseAcquisition(operation, Mode::Restart, AcquisitionTurnoverPoint::OutputGetter, false);
			ExerciseAcquisition(operation, Mode::Restart, AcquisitionTurnoverPoint::OutputGetter, true);
		}
		ExerciseBootstrapAdmission();
		for (const auto deadline : { BootstrapDeadline::Stable, BootstrapDeadline::BeforeEntry,
				 BootstrapDeadline::DuringVertexShader })
			ExerciseBootstrapDeadline(deadline);
	}
}
