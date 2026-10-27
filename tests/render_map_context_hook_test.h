#pragma once

#include "render_map_context_com_fixture.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <type_traits>

namespace CSX::RenderMap
{
	namespace ContextHookFixture
	{
		template <class>
		struct StaticSignature;
		template <class Result, class Interface, class... Args>
		struct StaticSignature<Result (Interface::*)(Args...)>
		{
			using Type = Result (*)(Interface*, Args...);
		};
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_DrawIndexed::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::DrawIndexed)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_Dispatch::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::Dispatch)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_Map::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::Map)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_FinishCommandList::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::FinishCommandList)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_PSSetShader::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::PSSetShader)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_VSSetShader::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::VSSetShader)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_CSSetShader::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::CSSetShader)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_ExecuteCommandList::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::ExecuteCommandList)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_CopyResource::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::CopyResource)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_VSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::VSSetShaderResources)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_HSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::HSSetShaderResources)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_DSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::DSSetShaderResources)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_GSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::GSSetShaderResources)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_PSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::PSSetShaderResources)>::Type>);
		static_assert(std::is_same_v<decltype(&ID3D11DeviceContext_CSSetShaderResources::thunk),
			typename StaticSignature<decltype(&ID3D11DeviceContext::CSSetShaderResources)>::Type>);
		enum class Mode
		{
			Stable,
			Stop,
			Restart,
			InactiveStart
		};
		enum class Family
		{
			Draw,
			Dispatch,
			Resources,
			Map,
			Finish,
			PixelShader,
			VertexShader,
			ComputeShader,
			Execute,
			Copy
		};
		CollectorConfig Configuration()
		{
			CollectorConfig config{ .captureNumericId = 91, .maxFrames = 2, .maxEvents = 256, .maxBytes = 1, .maxDuration = std::chrono::minutes(1), .maxScopeDepth = 8 };
			config.maxBytes = Collector::RequiredStorageBytes(config);
			return config;
		}
		struct State
		{
			Context context;
			Context immediate;
			Buffer source;
			Buffer destination;
			View view{ source };
			CommandList commandList;
			DeviceChild<ID3D11PixelShader> pixel;
			DeviceChild<ID3D11VertexShader> vertex;
			DeviceChild<ID3D11ComputeShader> compute;
			ID3D11ShaderResourceView* views[1]{ &view };
			ID3D11ClassInstance* instances[2]{};
			std::array<unsigned char, 64> data{};
			D3D11_MAPPED_SUBRESOURCE mapped{};
			ID3D11CommandList* output = nullptr;
			Mode mode = Mode::Stable;
			HRESULT result = S_OK;
			unsigned calls = 0;
			unsigned nestedDraws = 0;
			unsigned stage = 0;
			bool getterChangesCapture = false;
			bool changed = false;
			bool nullOutput = false;
			std::optional<CaptureSnapshot> old;
		};
		inline State* current = nullptr;
		void ChangeCapture()
		{
			auto& state = *current;
			if (state.changed)
				return;
			state.changed = true;
			auto& runtime = GetRuntime();
			if (state.mode == Mode::Stop || state.mode == Mode::Restart) {
				state.old = runtime.StopCapture();
				Require(state.old.has_value(), "original/getter could not stop capture outside the lifecycle lock");
			}
			if (state.mode == Mode::Restart || state.mode == Mode::InactiveStart)
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted,
					"original/getter could not start successor outside the lifecycle lock");
		}
		void Native(ID3D11DeviceContext* context)
		{
			Require(context == &current->context, "concrete thunk changed the context argument");
			++current->calls;
			if (!current->getterChangesCapture)
				ChangeCapture();
		}
		void BindOriginals()
		{
			ID3D11DeviceContext_DrawIndexed::func = +[](ID3D11DeviceContext* context, UINT count, UINT start, INT base) {
				Require(count == 13 && start == 5 && base == -4, "draw arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_Dispatch::func = +[](ID3D11DeviceContext* context, UINT x, UINT y, UINT z) {
				Require(x == 2 && y == 3 && z == 4, "dispatch arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_Map::func = +[](ID3D11DeviceContext* context, ID3D11Resource* resource,
												 UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* output) {
				Require(resource == &current->source && subresource == 3 && type == D3D11_MAP_READ &&
							flags == D3D11_MAP_FLAG_DO_NOT_WAIT && output == (current->nullOutput ? nullptr : &current->mapped),
					"Map arguments/output pointer changed");
				Native(context);
				if (output)
					*output = { current->data.data(), 17, 31 };
				return current->result;
			};
			ID3D11DeviceContext_Draw::func = +[](ID3D11DeviceContext* context, UINT count, UINT start) {
				Require(context == &current->context && count == 29 && start == 4, "nested/fresh draw arguments changed");
				++current->nestedDraws;
			};
			ID3D11DeviceContext_FinishCommandList::func = +[](ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList** output) {
				Require(restore == FALSE && output == (current->nullOutput ? nullptr : &current->output), "FinishCommandList arguments/output pointer changed");
				Native(context);
				if (current->mode == Mode::Restart || current->mode == Mode::InactiveStart)
					ID3D11DeviceContext_Draw::thunk(context, 29, 4);
				if (output)
					*output = &current->commandList;
				return current->result;
			};
			ID3D11DeviceContext_PSSetShader::func = +[](ID3D11DeviceContext* context, ID3D11PixelShader* shader,
														 ID3D11ClassInstance* const* instances, UINT count) {
				Require(shader == &current->pixel && instances == current->instances && count == 2, "PS setter arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_VSSetShader::func = +[](ID3D11DeviceContext* context, ID3D11VertexShader* shader,
														 ID3D11ClassInstance* const* instances, UINT count) {
				Require(shader == &current->vertex && instances == current->instances && count == 2, "VS setter arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_CSSetShader::func = +[](ID3D11DeviceContext* context, ID3D11ComputeShader* shader,
														 ID3D11ClassInstance* const* instances, UINT count) {
				Require(shader == &current->compute && instances == current->instances && count == 2, "CS setter arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_ExecuteCommandList::func = +[](ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore) {
				Require(list == &current->commandList && restore == FALSE, "ExecuteCommandList arguments changed");
				Native(context);
			};
			ID3D11DeviceContext_CopyResource::func = +[](ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) {
				Require(destination == &current->destination && source == &current->source, "CopyResource arguments changed");
				Native(context);
			};
		}
		template <class Hook>
		void ResourceCall(State& state)
		{
			Hook::func = +[](ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView* const* views) {
				Require(start == 7 && count == 1 && views == current->views, "SRV setter arguments changed");
				Native(context);
				current->context.resources[current->stage] = views[0];
			};
			Hook::thunk(&state.context, 7, 1, state.views);
		}
		void Invoke(Family family, State& state)
		{
			switch (family) {
			case Family::Draw:
				ID3D11DeviceContext_DrawIndexed::thunk(&state.context, 13, 5, -4);
				break;
			case Family::Dispatch:
				ID3D11DeviceContext_Dispatch::thunk(&state.context, 2, 3, 4);
				break;
			case Family::Resources:
				switch (state.stage) {
				case 0:
					ResourceCall<ID3D11DeviceContext_VSSetShaderResources>(state);
					break;
				case 1:
					ResourceCall<ID3D11DeviceContext_HSSetShaderResources>(state);
					break;
				case 2:
					ResourceCall<ID3D11DeviceContext_DSSetShaderResources>(state);
					break;
				case 3:
					ResourceCall<ID3D11DeviceContext_GSSetShaderResources>(state);
					break;
				case 4:
					ResourceCall<ID3D11DeviceContext_PSSetShaderResources>(state);
					break;
				case 5:
					ResourceCall<ID3D11DeviceContext_CSSetShaderResources>(state);
					break;
				default:
					throw std::logic_error("invalid fixture SRV stage");
				}
				break;
			case Family::Map:
				Require(ID3D11DeviceContext_Map::thunk(&state.context, &state.source, 3, D3D11_MAP_READ,
							D3D11_MAP_FLAG_DO_NOT_WAIT, state.nullOutput ? nullptr : &state.mapped) == state.result,
					"Map HRESULT changed");
				Require(state.nullOutput ? state.mapped.pData == nullptr :
										   (state.mapped.pData == state.data.data() && state.mapped.RowPitch == 17 && state.mapped.DepthPitch == 31),
					"Map native output fields changed");
				break;
			case Family::Finish:
				Require(ID3D11DeviceContext_FinishCommandList::thunk(&state.context, FALSE, state.nullOutput ? nullptr : &state.output) == state.result &&
							state.output == (state.nullOutput ? nullptr : &state.commandList),
					"FinishCommandList HRESULT/output changed");
				break;
			case Family::PixelShader:
				ID3D11DeviceContext_PSSetShader::thunk(&state.context, &state.pixel, state.instances, 2);
				break;
			case Family::VertexShader:
				ID3D11DeviceContext_VSSetShader::thunk(&state.context, &state.vertex, state.instances, 2);
				break;
			case Family::ComputeShader:
				ID3D11DeviceContext_CSSetShader::thunk(&state.context, &state.compute, state.instances, 2);
				break;
			case Family::Execute:
				ID3D11DeviceContext_ExecuteCommandList::thunk(&state.context, &state.commandList, FALSE);
				break;
			case Family::Copy:
				ID3D11DeviceContext_CopyResource::thunk(&state.context, &state.destination, &state.source);
				break;
			}
		}
		bool Has(const CaptureSnapshot& snapshot, EventKind kind)
		{
			return std::any_of(snapshot.events.begin(), snapshot.events.end(), [&](const EventRecord& event) { return event.kind == kind; });
		}
		void CheckStaleSnapshot(const CaptureSnapshot& snapshot, Family family)
		{
			Require(snapshot.resourceObservations.empty() && snapshot.targetViewObservations.empty() &&
						snapshot.targetBindingObservations.empty(),
				"concrete stale call populated successor resource tables");
			Require(!Has(snapshot, EventKind::kResourceFlow) && !Has(snapshot, EventKind::kResourceCpuAccess) &&
						!Has(snapshot, EventKind::kFinishCommandList) && !Has(snapshot, EventKind::kExecuteCommandList) &&
						!Has(snapshot, EventKind::kResourceViewBind),
				"concrete stale call emitted a successor event");
			if (family != Family::Finish)
				Require(!Has(snapshot, EventKind::kDraw) && !Has(snapshot, EventKind::kDispatch), "old call advanced successor draw/dispatch stream");
		}
		void Exercise(Family family, Mode mode, unsigned stage = 0, HRESULT result = S_OK, bool getterChange = false, bool nullOutput = false)
		{
			State state;
			state.mode = mode;
			state.stage = stage;
			state.result = result;
			state.nullOutput = nullOutput;
			state.getterChangesCapture = getterChange;
			if (family == Family::Finish)
				state.context.type = D3D11_DEVICE_CONTEXT_DEFERRED;
			current = &state;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(family == Family::Finish ? &state.immediate : &state.context));
			runtime.ResetImmediatePipelineState();
			if (family == Family::Execute)
				runtime.SetImmediateBoundStage(ShaderStage::kPixel, reinterpret_cast<std::uintptr_t>(&state.pixel));
			if (mode != Mode::InactiveStart)
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "initial concrete capture did not start");
			BindOriginals();
			if (getterChange)
				getterTurnover = ChangeCapture;
			Invoke(family, state);
			getterTurnover = nullptr;
			Require(state.calls == 1, "concrete thunk did not call original exactly once");
			Require(state.view.references == 1 && state.source.references == 1 && state.destination.references == 1,
				"post-call COM description leaked or over-released references");
			if (family == Family::Resources && mode == Mode::Stable)
				Require(state.context.queries > 0 && state.source.descriptions > 0, "SRV concrete getter path was not exercised");
			if (family == Family::Copy && mode != Mode::Stop)
				Require(state.source.descriptions > 0 && state.destination.descriptions > 0, "copy descriptions were not exercised");
			if (mode == Mode::Stop) {
				Require(!runtime.IsCapturing(), "stopped call reactivated capture");
				if (family != Family::PixelShader && family != Family::VertexShader &&
					family != Family::ComputeShader && family != Family::Execute)
					return;
				Require(runtime.StartCapture(Configuration()) == StartResult::kStarted,
					"later capture could not inspect physical shadow");
			}
			if (mode == Mode::Stable) {
				auto snapshot = runtime.StopCapture();
				Require(snapshot.has_value(), "stable concrete capture disappeared");
				const auto kind = family == Family::Draw ? EventKind::kDraw : family == Family::Dispatch ? EventKind::kDispatch :
				                                                          family == Family::Map          ? EventKind::kResourceCpuAccess :
				                                                          family == Family::Finish       ? EventKind::kFinishCommandList :
				                                                          family == Family::Execute      ? EventKind::kExecuteCommandList :
				                                                          family == Family::Copy         ? EventKind::kResourceFlow :
				                                                          family == Family::Resources    ? EventKind::kResourceViewBind :
				                                                                                           EventKind::kStageShaderObserved;
				Require(Has(*snapshot, kind), "stable concrete call lost its observation");
				return;
			}
			Require(runtime.CommandListCatalogueSizeForTesting() == 0, "old finish created a successor command-list identity");
			if (family == Family::Finish) {
				ID3D11DeviceContext_Draw::thunk(&state.context, 29, 4);
				auto snapshot = runtime.StopCapture();
				Require(snapshot.has_value() && state.nestedDraws == 2, "successor recording fixture was not exercised");
				CheckStaleSnapshot(*snapshot, family);
				const auto last = std::find_if(snapshot->events.rbegin(), snapshot->events.rend(), [](const EventRecord& event) { return event.kind == EventKind::kDraw; });
				Require(last != snapshot->events.rend() && last->commandStreamSequence == 2, "old FinishCommandList reset successor recording");
				return;
			}
			if (family == Family::PixelShader || family == Family::VertexShader || family == Family::ComputeShader || family == Family::Execute) {
				ID3D11DeviceContext_Draw::thunk(&state.context, 29, 4);
				auto snapshot = runtime.StopCapture();
				Require(snapshot.has_value(), "physical shadow successor disappeared");
				const auto object = reinterpret_cast<std::uintptr_t>(family == Family::VertexShader  ? static_cast<void*>(&state.vertex) :
																	 family == Family::ComputeShader ? static_cast<void*>(&state.compute) :
																									   static_cast<void*>(&state.pixel));
				const bool observed = std::any_of(snapshot->stageShaderObservations.begin(), snapshot->stageShaderObservations.end(),
					[&](const StageShaderObservationRecord& item) { return item.d3dObject == object; });
				Require(observed == (family != Family::Execute), "stale diagnostic rejection changed the actual physical shader shadow");
				return;
			}
			auto snapshot = runtime.StopCapture();
			Require(snapshot.has_value(), "successor concrete capture disappeared");
			CheckStaleSnapshot(*snapshot, family);
		}
		void OriginalException()
		{
			State state;
			state.mode = Mode::Restart;
			current = &state;
			auto& runtime = GetRuntime();
			(void)runtime.StopCapture();
			runtime.SetImmediateContext(reinterpret_cast<std::uintptr_t>(&state.context));
			runtime.ResetImmediatePipelineState();
			Require(runtime.StartCapture(Configuration()) == StartResult::kStarted, "exception fixture capture failed");
			ID3D11DeviceContext_Map::func = +[](ID3D11DeviceContext* context, ID3D11Resource* resource,
												 UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* output) -> HRESULT {
				Require(resource == &current->source && subresource == 3 && type == D3D11_MAP_READ &&
							flags == 0 && output == &current->mapped,
					"exception path arguments changed");
				Native(context);
				throw std::runtime_error("controlled native exception");
			};
			bool propagated = false;
			try {
				(void)ID3D11DeviceContext_Map::thunk(&state.context, &state.source, 3, D3D11_MAP_READ, 0, &state.mapped);
			} catch (const std::runtime_error& error) {
				propagated = std::string_view(error.what()) == "controlled native exception";
			}
			Require(propagated && state.calls == 1, "concrete thunk swallowed or replayed native exception");
			auto snapshot = runtime.StopCapture();
			Require(snapshot.has_value() && !Has(*snapshot, EventKind::kResourceCpuAccess),
				"exception path invented successor CPU access");
		}

	}

	void RunContextHookTests()
	{
		using namespace ContextHookFixture;
		for (const auto mode : { Mode::Stable, Mode::Stop, Mode::Restart, Mode::InactiveStart }) {
			for (const auto family : { Family::Draw, Family::Dispatch, Family::Map, Family::Finish,
					 Family::PixelShader, Family::VertexShader, Family::ComputeShader, Family::Execute, Family::Copy })
				Exercise(family, mode);
			for (unsigned stage = 0; stage < 6; ++stage)
				Exercise(Family::Resources, mode, stage);
			Exercise(Family::Map, mode, 0, E_FAIL);
			Exercise(Family::Finish, mode, 0, E_FAIL);
			Exercise(Family::Map, mode, 0, E_INVALIDARG, false, true);
			Exercise(Family::Finish, mode, 0, E_INVALIDARG, false, true);
		}
		Exercise(Family::Draw, Mode::Restart, 0, S_OK, true);
		Exercise(Family::Resources, Mode::Restart, 4, S_OK, true);
		Exercise(Family::Copy, Mode::Restart, 0, S_OK, true);
		Exercise(Family::Draw, Mode::InactiveStart, 0, S_OK, true);
		OriginalException();
		GetRuntime().SetImmediateContext(0);
		current = nullptr;
	}
}
