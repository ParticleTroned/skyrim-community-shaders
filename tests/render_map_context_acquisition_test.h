#pragma once

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
		inline bool concurrentAcquisition = false;
		inline bool acquisitionWaitCompleted = true;
		inline std::thread acquisitionWorker;
		inline std::exception_ptr acquisitionFailure;
		inline const D3D11_BOX acquisitionBox{ 1, 2, 3, 17, 19, 23 };

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
			// Returning on timeout lets an incorrect caller-held lock unwind before joining.
			acquisitionWaitCompleted = ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		}

		void AcquisitionNative(ID3D11DeviceContext* context)
		{
			Require(context == &current->context, "acquisition thunk changed native context");
			++current->calls;
			if (current->getterChangesCapture)
				getterTurnover = AcquisitionTurnover;
			else
				AcquisitionTurnover();
			getterEntries = 0;
		}

		void BindAcquisitionOriginals()
		{
			ID3D11DeviceContext_DrawIndexed::func = +[](ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
				Require(count == 13 && start == 5 && base == -4, "acquisition draw arguments changed");
				AcquisitionNative(context);
			};
			ID3D11DeviceContext_Dispatch::func = +[](ID3D11DeviceContext* context, UINT x, UINT y, UINT z) {
				Require(x == 2 && y == 3 && z == 4, "acquisition dispatch arguments changed");
				AcquisitionNative(context);
			};
			ID3D11DeviceContext_CopyResource::func = +[](ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) {
				Require(destination == &current->destination && source == &current->source, "acquisition copy arguments changed");
				AcquisitionNative(context);
			};
			ID3D11DeviceContext_CopySubresourceRegion::func = +[](ID3D11DeviceContext* context, ID3D11Resource* destination,
																   UINT destinationSubresource, UINT x, UINT y, UINT z, ID3D11Resource* source, UINT sourceSubresource, const D3D11_BOX* box) {
				Require(destination == &current->destination && source == &current->source && destinationSubresource == 3 &&
							x == 5 && y == 7 && z == 11 && sourceSubresource == 2 && box == &acquisitionBox,
					"acquisition copy-region arguments or source box changed");
				AcquisitionNative(context);
			};
		}

		void InvokeAcquisition(AcquisitionOperation operation, State& state)
		{
			switch (operation) {
			case AcquisitionOperation::Draw:
				ID3D11DeviceContext_DrawIndexed::thunk(&state.context, 13, 5, -4);
				break;
			case AcquisitionOperation::Dispatch:
				ID3D11DeviceContext_Dispatch::thunk(&state.context, 2, 3, 4);
				break;
			case AcquisitionOperation::Copy:
				ID3D11DeviceContext_CopyResource::thunk(&state.context, &state.destination, &state.source);
				break;
			case AcquisitionOperation::CopyRegion:
				ID3D11DeviceContext_CopySubresourceRegion::thunk(&state.context, &state.destination, 3, 5, 7, 11,
					&state.source, 2, &acquisitionBox);
				break;
			}
		}

		void ExerciseAcquisition(AcquisitionOperation operation, Mode mode, bool getterChange, bool concurrent)
		{
			State state;
			state.mode = mode;
			state.getterChangesCapture = getterChange;
			state.context.resources[4] = &state.view;
			state.context.resources[5] = &state.view;
			state.context.queriedPixel = &state.pixel;
			state.context.queriedCompute = &state.compute;
			current = &state;
			concurrentAcquisition = concurrent;
			acquisitionWaitCompleted = true;
			acquisitionFailure = nullptr;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(&state.context));
			runtime.ForCapture(0).RecordExecuteCommandList(reinterpret_cast<std::uintptr_t>(&state.context), 0, false);
			if (mode != Mode::InactiveStart)
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "acquisition capture did not start");
			BindAcquisitionOriginals();
			try {
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 77);
				InvokeAcquisition(operation, state);
			} catch (...) {
				getterTurnover = nullptr;
				if (acquisitionWorker.joinable())
					acquisitionWorker.join();
				throw;
			}
			getterTurnover = nullptr;
			if (acquisitionWorker.joinable())
				acquisitionWorker.join();
			if (acquisitionFailure)
				std::rethrow_exception(acquisitionFailure);
			Require(acquisitionWaitCompleted, "capture lifecycle blocked inside native/getter callback");
			Require(state.calls == 1, "acquisition thunk did not forward native exactly once");
			Require(state.view.references == 1 && state.source.references == 1 && state.destination.references == 1 &&
						state.pixel.references == 1 && state.compute.references == 1,
				"acquisition COM ownership leaked");
			if (mode == Mode::Stable) {
				Require(getterEntries > 0, "stable post-native getter path was not reached");
				Require(runtime.StopCapture().has_value(), "stable acquisition capture disappeared");
				return;
			}
			Require(getterEntries == (getterChange ? 1u : 0u), "already-stale call entered extra metadata getters");
			if (auto successor = runtime.StopCapture()) {
				CheckStaleSnapshot(*successor, Family::Copy);
				Require(successor->stageShaderObservations.empty(), "stale shader metadata reached successor");
			}
			Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "fresh acquisition successor did not start");
			state.mode = Mode::Stable;
			state.changed = false;
			state.getterChangesCapture = false;
			concurrentAcquisition = false;
			{
				auto scope = runtime.EnterPostProcessing({}, {}, 9, 78);
				InvokeAcquisition(operation, state);
			}
			const auto fresh = runtime.StopCapture();
			const auto expected = operation == AcquisitionOperation::Draw     ? EventKind::kDraw :
			                      operation == AcquisitionOperation::Dispatch ? EventKind::kDispatch :
			                                                                    EventKind::kResourceFlow;
			Require(fresh.has_value() && Has(*fresh, expected) && state.calls == 2 && getterEntries > 0,
				"fresh successor thunk lost native forwarding or observations");
			Require(state.view.references == 1 && state.source.references == 1 && state.pixel.references == 1 && state.compute.references == 1,
				"fresh successor COM references leaked");
		}
	}

	void RunContextAcquisitionTests()
	{
		using namespace ContextHookFixture;
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_CopySubresourceRegion::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::CopySubresourceRegion)>::Type>);
		for (const auto operation : { AcquisitionOperation::Draw, AcquisitionOperation::Dispatch,
				 AcquisitionOperation::Copy, AcquisitionOperation::CopyRegion }) {
			ExerciseAcquisition(operation, Mode::Stable, false, false);
			ExerciseAcquisition(operation, Mode::InactiveStart, false, false);
			for (const auto mode : { Mode::Stop, Mode::Restart }) {
				for (const bool getterChange : { false, true }) {
					ExerciseAcquisition(operation, mode, getterChange, false);
					ExerciseAcquisition(operation, mode, getterChange, true);
				}
			}
		}
	}
}
