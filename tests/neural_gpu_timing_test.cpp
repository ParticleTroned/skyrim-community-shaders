#include "Features/Upscaling/NeuralRendering/DevelopmentDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <dxgi1_6.h>
#include <format>
#include <iostream>
#include <stdexcept>

namespace logger
{
	int warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
}
namespace globals
{
	struct TimingSource
	{
		std::uint64_t epoch = 0;
		struct Sample
		{
			std::uint64_t epoch;
			std::uint32_t frameId;
			float milliseconds;
		};
		std::vector<Sample> samples;
		std::uint64_t GetGpuCaptureEpoch() const { return epoch; }
		bool PublishExternalGpuTiming(std::uint64_t value, std::string_view name, std::uint32_t frame, float milliseconds)
		{
			if (value != epoch || !epoch || name != "NeuralRendering::Inference")
				throw std::runtime_error("invalid external GPU publication");
			samples.push_back({ value, frame, milliseconds });
			return true;
		}
	} source;
	auto* profiler = &source;
}
// Execute the production timing methods on a real D3D12 WARP queue.
#include "Features/Upscaling/NeuralRendering/ExecutionEvidence.h"
#include "Features/Upscaling/NeuralRendering/SubmissionFenceSnapshot.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/Upscaling/NeuralRendering/LifetimeDiagnostics.h"
#endif
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#define private public
#include "Features/Upscaling/NeuralRendering/D3D12Interop.h"
#undef private
#include "neural_gpu_timing_under_test.h"
namespace NeuralRendering
{
	D3D12Interop::~D3D12Interop() noexcept = default;
}

namespace
{
	using namespace NeuralRendering;
	using Microsoft::WRL::ComPtr;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	void Check(HRESULT value) { Require(SUCCEEDED(value), "D3D12 WARP operation failed"); }
	struct Fixture
	{
		D3D12Interop interop;
		std::uint64_t fence = 0;
		bool timingScopeOpened = false;
		Fixture()
		{
			globals::source = {};
			logger::warnings = 0;
			ComPtr<IDXGIFactory4> factory;
			Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
			ComPtr<IDXGIAdapter> adapter;
			Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
			Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&interop.device12_)));
			D3D12_COMMAND_QUEUE_DESC desc{};
			Check(interop.device12_->CreateCommandQueue(&desc, IID_PPV_ARGS(&interop.queue12_)));
			Check(interop.device12_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&interop.fence12_)));
			for (std::size_t index = 0; index < interop.kCommandContextCount; ++index)
				Require(interop.CreateCommandContextLocked(index), "command context creation failed");
			interop.initialized_ = true;
		}
		ID3D12GraphicsCommandList* Begin(std::size_t index = 0)
		{
			auto& context = interop.commandContexts_[index];
			Check(context.allocator->Reset());
			Check(context.commandList->Reset(context.allocator.Get(), nullptr));
			context.timingPending = false;
			context.timing = {};
			context.gpuCaptureEpoch = 0;
			context.evaluationTimingMask = context.evaluationTimingOpenMask = 0;
			interop.recordingContext_ = index;
			interop.recordingThread_ = std::this_thread::get_id();
			interop.recording_ = true;
			interop.featureTimingOpen_ = interop.featureTimingCompleted_ = interop.timingRecording_ = false;
			return context.commandList.Get();
		}
		void Submit(std::size_t index = 0)
		{
			auto& context = interop.commandContexts_[index];
			Check(context.commandList->Close());
			ID3D12CommandList* lists[]{ context.commandList.Get() };
			interop.queue12_->ExecuteCommandLists(1, lists);
			Check(interop.queue12_->Signal(interop.fence12_.Get(), ++fence));
			context.fenceValue = fence;
			HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			Require(event != nullptr, "fence event failed");
			const auto result = interop.fence12_->SetEventOnCompletion(fence, event);
			const auto wait = SUCCEEDED(result) ? WaitForSingleObject(event, 5000) : WAIT_FAILED;
			CloseHandle(event);
			Require(wait == WAIT_OBJECT_0, "test GPU completion timed out");
			interop.recording_ = false;
		}
		static D3D12InteropSubmissionTiming Timing(std::uint32_t evaluations = 2)
		{
			return { .frameId = 42, .pixelCount = 4096, .evaluationCount = evaluations, .featureSlotMask = evaluations == 2 ? 3u : 1u, .logicalEyeCount = evaluations };
		}
		bool BeginFeatureScope(ID3D12GraphicsCommandList* list, const D3D12InteropSubmissionTiming& timing)
		{
			return interop.BeginFeatureTiming(list, timing, timingScopeOpened);
		}
		bool EndFeatureScope(ID3D12GraphicsCommandList* list)
		{
			return interop.EndFeatureTiming(list, timingScopeOpened);
		}
		void Evaluate(ID3D12GraphicsCommandList* list, std::uint32_t region)
		{
			interop.BeginEvaluationTiming(list, region);
			interop.EndEvaluationTiming(list, region);
		}
	};
	void OffAndLive()
	{
		Fixture f;
		auto* list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "off scope failed");
		f.Evaluate(list, 0);
		f.Evaluate(list, 1);
		Require(f.EndFeatureScope(list), "off scope close failed");
		Require(!f.interop.timestampQueryHeap_ && !f.interop.timestampReadback_ && !f.interop.timingResourcesAttempted_, "off or CPU-only profiling allocated timestamp resources");
		f.Submit();
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.empty(), "off mode published inference");

		globals::source.epoch = 1;
		list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "live scope failed");
		Require(f.interop.timestampQueryHeap_ && f.interop.timestampReadback_, "GPU capture did not lazily allocate timestamps");
		f.Evaluate(list, 0);
		f.Evaluate(list, 1);
		Require(f.EndFeatureScope(list), "live scope close failed");
		f.Submit();
		f.interop.CollectCompletedTimingsLocked(f.fence - 1);
		Require(globals::source.samples.empty(), "readback ran before queue completion");
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.size() == 1 && globals::source.samples[0].epoch == 1 && globals::source.samples[0].frameId == 42 && std::isfinite(globals::source.samples[0].milliseconds), "production GPU inference did not publish with frame and capture identity");
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.size() == 1, "completed timing was published twice");
	}
	void CancelAndFailure()
	{
		Fixture f;
		globals::source.epoch = 1;
		auto* list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "cancel scope failed");
		f.interop.BeginEvaluationTiming(list, 0);
		globals::source.epoch = 0;
		f.interop.EndEvaluationTiming(list, 0);
		Require(f.interop.commandContexts_[0].evaluationTimingMask == 0, "disabling profiling completed a new timestamp");
		Require(f.EndFeatureScope(list) && !f.interop.commandContexts_[0].timingPending, "cancelled queries were resolved or failed rendering");
		f.Submit();
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.empty(), "mid-scope cancellation published an incomplete sample");

		globals::source.epoch = 2;
		list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "restart scope failed");
		f.Evaluate(list, 0);
		f.Evaluate(list, 1);
		Require(f.EndFeatureScope(list), "restart close failed");
		f.Submit();
		globals::source.epoch = 3;
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.empty() && !f.interop.commandContexts_[0].timingPending, "previous capture contaminated a new session");

		list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "partial scope failed");
		f.Evaluate(list, 0);
		Require(f.EndFeatureScope(list), "partial close failed");
		f.Submit();
		f.interop.CollectCompletedTimingsLocked(f.fence);
		Require(globals::source.samples.empty(), "missing second eye produced a partial inference sample");
	}
	void EvaluationIntervals()
	{
		for (const bool invalid : { false, true }) {
			Fixture f;
			globals::source.epoch = 1;
			auto* list = f.Begin();
			Require(f.BeginFeatureScope(list, Fixture::Timing()), "interval scope failed");
			f.Evaluate(list, 0);
			f.Evaluate(list, 1);
			Require(f.EndFeatureScope(list), "interval close failed");
			f.Submit();
			// Substitute known completed ticks to separate evaluation cost from batch overhead.
			void* mapped = nullptr;
			const D3D12_RANGE noRead{ 0, 0 };
			Check(f.interop.timestampReadback_->Map(0, &noRead, &mapped));
			auto* ticks = static_cast<std::uint64_t*>(mapped);
			ticks[0] = 100;
			ticks[1] = 9900;
			ticks[2] = 2000;
			ticks[3] = 3000;
			ticks[4] = 4000;
			ticks[5] = invalid ? 3000 : 6000;
			const D3D12_RANGE written{ 0, 6 * sizeof(std::uint64_t) };
			f.interop.timestampReadback_->Unmap(0, &written);
			f.interop.timestampFrequency_ = 1000000;
			f.interop.CollectCompletedTimingsLocked(f.fence);
			Require(globals::source.samples.size() == (invalid ? 0u : 1u), "invalid timestamps entered inference history");
			if (!invalid)
				Require(std::abs(globals::source.samples[0].milliseconds - 3.0f) < .0001f,
					"inference did not sum both evaluation intervals or included batch overhead");
		}
	}
	void SubmissionOrder()
	{
		Fixture f;
		globals::source.epoch = 1;
		for (const auto index : { 1u, 2u, 0u }) {
			auto* list = f.Begin(index);
			auto timing = Fixture::Timing(1);
			timing.frameId = index == 0 ? 43u : 42u;
			Require(f.BeginFeatureScope(list, timing), "ring timing scope failed");
			f.Evaluate(list, 0);
			Require(f.EndFeatureScope(list), "ring timing close failed");
			f.Submit(index);
		}
		f.interop.CollectCompletedTimingsLocked(f.fence);
		const auto& samples = globals::source.samples;
		Require(samples.size() == 3 && samples[0].frameId == 42 && samples[1].frameId == 42 && samples[2].frameId == 43,
			"ring wrap published the later frame before both eyes of the prior frame");
	}
	void OptionalAllocationFailure()
	{
		Fixture f;
		auto queue = f.interop.queue12_;
		f.interop.queue12_.Reset();
		globals::source.epoch = 1;
		auto* list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "optional timestamp failure disabled rendering");
		f.Evaluate(list, 0);
		f.Evaluate(list, 1);
		Require(f.EndFeatureScope(list), "optional timestamp failure blocked submission");
		Require(f.interop.timingResourcesAttempted_ && !f.interop.timestampReadback_, "optional failure did not latch unavailable state");
		f.interop.queue12_ = queue;
		f.Submit();
		list = f.Begin();
		Require(f.BeginFeatureScope(list, Fixture::Timing()), "unavailable scope failed");
		Require(!f.interop.timestampReadback_, "unavailable timestamp resources were retried every frame");
		Require(f.EndFeatureScope(list), "unavailable close failed");
		Require(logger::warnings == 1, "timestamp failure logs every frame instead of once per device");
		f.Submit();
	}
}
int main()
{
	try {
		OffAndLive();
		CancelAndFailure();
		EvaluationIntervals();
		SubmissionOrder();
		OptionalAllocationFailure();
		std::cout << "NR on-demand GPU timing WARP cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
