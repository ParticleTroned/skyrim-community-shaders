#pragma once

#include <vector>

namespace CSX::RenderMap
{
	namespace ContextHookFixture
	{
		std::uintptr_t ObservedShader(const CaptureSnapshot& snapshot, std::uint64_t id)
		{
			const auto found = std::find_if(snapshot.stageShaderObservations.begin(), snapshot.stageShaderObservations.end(),
				[id](const StageShaderObservationRecord& shader) { return shader.observationId == id; });
			return found == snapshot.stageShaderObservations.end() ? 0 : found->pointerEvidence;
		}

		std::uintptr_t ObservedView(const CaptureSnapshot& snapshot, std::uint64_t id)
		{
			const auto found = std::find_if(snapshot.targetViewObservations.begin(), snapshot.targetViewObservations.end(),
				[id](const TargetViewObservationRecord& view) { return view.observationId == id; });
			return found == snapshot.targetViewObservations.end() ? 0 : found->pointerEvidence;
		}

		void ExerciseContextStateReplacement()
		{
			State state;
			current = &state;
			Buffer replacementResource, targetResource, replacementTargetResource;
			View replacementView(replacementResource);
			RenderTargetView target(targetResource), replacementTarget(replacementTargetResource);
			DeviceChild<ID3D11VertexShader> replacementVertex;
			DeviceChild<ID3D11PixelShader> replacementPixel;
			DeviceChild<ID3D11ComputeShader> replacementCompute;
			ContextState original, replacement;
			original.resources[4] = &state.view;
			original.vertex = &state.vertex;
			original.pixel = &state.pixel;
			original.compute = &state.compute;
			original.target = &target;
			replacement.resources[4] = &replacementView;
			replacement.vertex = &replacementVertex;
			replacement.pixel = &replacementPixel;
			replacement.compute = &replacementCompute;
			replacement.target = &replacementTarget;
			state.context.SwapDeviceContextState(&original, nullptr);

			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			const auto context = reinterpret_cast<std::uintptr_t>(&state.context);
			runtime.SetImmediateContext(context);
			runtime.InvalidateContextState(context);
			auto config = Configuration();
			config.maxEvents = 512;
			config.maxBytes = Collector::RequiredStorageBytes(config);
			Require(runtime.StartCapture(config) == StartResult::kStarted, "state replacement capture did not start");
			BindOriginals();
			ID3D11DeviceContext_ClearState::func = +[](ID3D11DeviceContext* nativeContext) {
				Require(nativeContext == &current->context, "clear changed its context argument");
				++current->calls;
				nativeContext->ClearState();
			};
			ID3D11DeviceContext1_SwapDeviceContextState::func = +[](ID3D11DeviceContext1* nativeContext,
																	 ID3DDeviceContextState* nativeState, ID3DDeviceContextState** previous) {
				Require(nativeContext == &current->context, "swap changed its context argument");
				++current->calls;
				nativeContext->SwapDeviceContextState(nativeState, previous);
			};
			const auto execute = [&] {
				ID3D11DeviceContext_Draw::thunk(&state.context, 29, 4);
				ID3D11DeviceContext_Dispatch::thunk(&state.context, 2, 3, 4);
			};
			execute();
			ID3DDeviceContextState* previous = nullptr;
			ID3D11DeviceContext1_SwapDeviceContextState::thunk(&state.context, &replacement, &previous);
			Require(previous == &original && original.references == 2, "swap lost its native previous-state output");
			execute();
			ID3D11DeviceContext1_SwapDeviceContextState::thunk(&state.context, previous, nullptr);
			previous->Release();
			execute();
			ID3D11DeviceContext_ClearState::thunk(&state.context);
			execute();
			const auto snapshot = runtime.StopCapture();
			Require(snapshot.has_value() && state.calls == 7 && state.nestedDraws == 4,
				"state replacement lost or repeated a native call");

			std::vector<const EventRecord*> draws, dispatches, reads;
			for (const auto& event : snapshot->events) {
				if (event.kind == EventKind::kDraw)
					draws.push_back(&event);
				if (event.kind == EventKind::kDispatch)
					dispatches.push_back(&event);
				if (event.kind == EventKind::kResourceViewBind &&
					event.payload.words[1] == static_cast<std::uint64_t>(ResourceBindingKind::kShaderResource) &&
					event.payload.words[2] == static_cast<std::uint64_t>(ResourceStage::kPixel) && event.payload.words[3] == 7)
					reads.push_back(&event);
			}
			Require(draws.size() == 4 && dispatches.size() == 4 && reads.size() == 4,
				"swap, restoration, or clear failed to reseed the execution state");
			const ContextState* expected[] = { &original, &replacement, &original, nullptr };
			for (std::size_t index = 0; index < draws.size(); ++index) {
				const auto* native = expected[index];
				Require(ObservedShader(*snapshot, draws[index]->payload.words[2]) ==
								reinterpret_cast<std::uintptr_t>(native ? native->vertex : nullptr) &&
							ObservedShader(*snapshot, draws[index]->payload.words[3]) ==
								reinterpret_cast<std::uintptr_t>(native ? native->pixel : nullptr) &&
							ObservedShader(*snapshot, dispatches[index]->payload.words[2]) ==
								reinterpret_cast<std::uintptr_t>(native ? native->compute : nullptr),
					"execution retained shaders from another native context state");
				const auto binding = std::find_if(snapshot->targetBindingObservations.begin(), snapshot->targetBindingObservations.end(),
					[&](const TargetBindingObservationRecord& value) { return value.observationId == draws[index]->targetBindingObservationId; });
				Require(binding != snapshot->targetBindingObservations.end() &&
							ObservedView(*snapshot, binding->renderTargetObservationIds[Context::renderTargetSlot]) ==
								reinterpret_cast<std::uintptr_t>(native ? native->target : nullptr),
					"draw retained render targets from another native context state");
				Require(ObservedView(*snapshot, reads[index]->payload.words[0]) ==
							reinterpret_cast<std::uintptr_t>(native ? native->resources[4] : nullptr),
					"draw retained resources from another native context state");
			}
			Require(state.vertex.references == 1 && state.pixel.references == 1 && state.compute.references == 1 &&
						replacementVertex.references == 1 && replacementPixel.references == 1 && replacementCompute.references == 1 &&
						state.view.references == 1 && replacementView.references == 1 && target.references == 1 &&
						replacementTarget.references == 1 && original.references == 1 && replacement.references == 1 &&
						state.source.references == 1 && replacementResource.references == 1 &&
						targetResource.references == 1 && replacementTargetResource.references == 1,
				"context-state recovery leaked a COM acquisition");
		}

		void ExerciseStoppedAcquisition()
		{
			State state;
			state.stage = 4;
			current = &state;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			const auto context = reinterpret_cast<std::uintptr_t>(&state.context);
			runtime.SetImmediateContext(context);
			runtime.InvalidateContextState(context);
			auto config = Configuration();
			config.maxEvents = 4;
			config.maxBytes = Collector::RequiredStorageBytes(config);
			Require(runtime.StartCapture(config) == StartResult::kStarted, "limited acquisition capture did not start");
			const auto generation = runtime.ActiveCaptureGeneration();
			while (runtime.IsCapturing())
				runtime.RecordDraw(context, DrawOperation::kDraw, 3);
			Require(runtime.ActiveCaptureGeneration() == generation, "capture limit lost retained session identity");
			BindOriginals();
			ID3D11DeviceContext_OMSetRenderTargets::func = +[](ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*) {
				++current->calls;
			};
			ID3D11DeviceContext_CSSetUnorderedAccessViews::func = +[](ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) {
				++current->calls;
			};
			getterEntries = 0;
			ID3D11DeviceContext_OMSetRenderTargets::thunk(&state.context, 0, nullptr, nullptr);
			ID3D11DeviceContext_CSSetUnorderedAccessViews::thunk(&state.context, 0, 0, nullptr, nullptr);
			ResourceCall<ID3D11DeviceContext_PSSetShaderResources>(state);
			ID3D11DeviceContext_Draw::thunk(&state.context, 29, 4);
			ID3D11DeviceContext_Dispatch::thunk(&state.context, 2, 3, 4);
			Require(getterEntries == 0 && state.calls == 4 && state.nestedDraws == 1,
				"capture limits failed to stop getters or changed native forwarding");
			Require(runtime.StopCapture().has_value(), "quiesced capture could not be finalized");
			Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "fresh acquisition capture did not start");
			ID3D11DeviceContext_Draw::thunk(&state.context, 29, 4);
			Require(getterEntries > 0 && runtime.StopCapture().has_value(), "stopping acquisition disabled a successor capture");
		}
	}

	void RunContextStateTests()
	{
		ContextHookFixture::ExerciseContextStateReplacement();
		ContextHookFixture::ExerciseStoppedAcquisition();
	}
}
