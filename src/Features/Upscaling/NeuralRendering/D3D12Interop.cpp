#include "D3D12Interop.h"
#include "DevelopmentDiagnostics.h"

#include "PipelinePolicy.h"

#include "Globals.h"
#include "Profiler.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

namespace NeuralRendering
{
	namespace
	{
		class ScopedHandle
		{
		public:
			explicit ScopedHandle(HANDLE a_handle = nullptr) : handle_(a_handle) {}
			~ScopedHandle()
			{
				if (handle_)
					CloseHandle(handle_);
			}

			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;

			[[nodiscard]] HANDLE Get() const { return handle_; }
			[[nodiscard]] HANDLE Release()
			{
				return std::exchange(handle_, nullptr);
			}

		private:
			HANDLE handle_ = nullptr;
		};

		struct ExecutionWaitScope
		{
			std::shared_ptr<ExecutionEvidence>& current;
			ExecutionWaitScope(std::shared_ptr<ExecutionEvidence>& value, const std::shared_ptr<ExecutionEvidence>& evidence) : current(value)
			{
				current = evidence;
				if (current)
					current->Update([](auto& result) { if (!result.cpuWaitMicroseconds) result.cpuWaitMicroseconds = 0; });
			}
			~ExecutionWaitScope() { current.reset(); }
		};

		std::wstring WidenName(std::string_view a_name)
		{
			return { a_name.begin(), a_name.end() };
		}

		void SetD3D12Name(ID3D12Object* a_object, std::string_view a_name)
		{
			if (!a_object || a_name.empty())
				return;
			const auto wideName = WidenName(a_name);
			(void)a_object->SetName(wideName.c_str());
		}

		std::uint64_t CurrentGpuCaptureEpoch() noexcept
		{
			return globals::profiler ? globals::profiler->GetGpuCaptureEpoch() : 0;
		}

		void IncrementSaturating(std::uint64_t& a_value) noexcept
		{
			if constexpr (!kDevelopmentDiagnostics) {
				return;
			} else {
				if (a_value != std::numeric_limits<std::uint64_t>::max())
					++a_value;
			}
		}

		void AddSaturating(std::uint64_t& a_value, std::uint64_t a_increment) noexcept
		{
			if constexpr (!kDevelopmentDiagnostics) {
				return;
			} else {
				if (a_increment > std::numeric_limits<std::uint64_t>::max() - a_value)
					a_value = std::numeric_limits<std::uint64_t>::max();
				else
					a_value += a_increment;
			}
		}
	}

	D3D12Interop::~D3D12Interop() noexcept
	{
		try {
			std::scoped_lock lock(mutex_);
			if (abandonRequested_.load(std::memory_order_acquire) || !ShutdownLocked())
				AbandonObjectsLocked();
		} catch (...) {
			abandonRequested_.store(true, std::memory_order_release);
			AbandonObjectsLocked();
		}
	}

	bool D3D12Interop::RecordFailureLocked(HRESULT a_result, const char* a_operation)
	{
		lastError_ = a_result;
		lastOperation_ = a_operation ? a_operation : "unknown";
		return false;
	}

	bool D3D12Interop::CreateRecordingLocked(CommandRecording& recording, D3D12_COMMAND_LIST_TYPE type, const std::string& name)
	{
		CommandRecording replacement;
		HRESULT result = device12_->CreateCommandAllocator(type, IID_PPV_ARGS(&replacement.allocator));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateCommandAllocator");
		result = device12_->CreateCommandList(0, type, replacement.allocator.Get(), nullptr, IID_PPV_ARGS(&replacement.commandList));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateCommandList");
		result = replacement.commandList->Close();
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12GraphicsCommandList::Close(initial)");
		SetD3D12Name(replacement.allocator.Get(), name + "Allocator");
		SetD3D12Name(replacement.commandList.Get(), name + "Commands");
		recording = std::move(replacement);
		return true;
	}

	bool D3D12Interop::CreateCommandContextLocked(std::size_t a_index)
	{
		if (!device12_ || a_index >= commandContexts_.size())
			return RecordFailureLocked(E_INVALIDARG, "CreateCommandContext arguments");
		CommandRecording commands;
		if (!CreateRecordingLocked(commands, D3D12_COMMAND_LIST_TYPE_DIRECT,
				std::format("NeuralRendering::Primary{}", a_index)))
			return false;
		CommandContext replacement;
		replacement.allocator = std::move(commands.allocator);
		replacement.commandList = std::move(commands.commandList);
		commandContexts_[a_index] = std::move(replacement);
		return true;
	}

	bool D3D12Interop::CreateParallelQueueLocked()
	{
		if (parallelQueue_)
			return true;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		D3D12_COMMAND_QUEUE_DESC description{};
		description.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		HRESULT result = device12_->CreateCommandQueue(&description, IID_PPV_ARGS(&queue));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateCommandQueue(parallel)");
		result = device12_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateFence(parallel)");
		SetD3D12Name(queue.Get(), "NeuralRendering::ParallelQueue");
		SetD3D12Name(fence.Get(), "NeuralRendering::ParallelFence");
		parallelQueue_ = std::move(queue);
		parallelFence_ = std::move(fence);
		return true;
	}

	void D3D12Interop::DisableParallel(const char* reason)
	{
		std::scoped_lock lock(mutex_);
		if (parallelDisabled_)
			return;
		parallelDisabled_ = true;
		logger::warn("[DLSSNR] Parallel eyes disabled for this session: {}; backend recovery retains serial execution", reason);
	}

	ID3D12GraphicsCommandList* D3D12Interop::ParallelList()
	{
		std::scoped_lock lock(mutex_);
		if (parallelDisabled_)
			return nullptr;
		if (!initialized_ || !recording_ || recordingContext_ >= commandContexts_.size() ||
			recordingThread_ != std::this_thread::get_id() || featureTimingOpen_ || featureTimingCompleted_) {
			(void)RecordFailureLocked(E_UNEXPECTED, "ParallelList state");
			return nullptr;
		}
		auto& context = commandContexts_[recordingContext_];
		if (context.parallelRecording)
			return context.parallel.commandList.Get();
		if (!CreateParallelQueueLocked() ||
			(!context.parallel.commandList && !CreateRecordingLocked(context.parallel,
												  D3D12_COMMAND_LIST_TYPE_COMPUTE, std::format("NeuralRendering::Parallel{}", recordingContext_)))) {
			DisableParallel(lastOperation_.c_str());
			return nullptr;
		}
		HRESULT result = context.parallel.allocator->Reset();
		if (SUCCEEDED(result))
			result = context.parallel.commandList->Reset(context.parallel.allocator.Get(), nullptr);
		if (FAILED(result)) {
			(void)RecordFailureLocked(result, "ParallelList reset");
			DisableParallel("compute command recording unavailable");
			return nullptr;
		}
		context.parallel.recording = context.parallelRecording = true;
		return context.parallel.commandList.Get();
	}

	bool D3D12Interop::PrepareParallelTimingLocked(CommandContext& context)
	{
		if (parallelTimingFailed_)
			return false;
		if (!parallelTimestampFrequency_) {
			const HRESULT result = parallelQueue_->GetTimestampFrequency(&parallelTimestampFrequency_);
			if (FAILED(result) || !parallelTimestampFrequency_)
				return RecordFailureLocked(FAILED(result) ? result : E_FAIL, "Parallel timestamp frequency");
		}
		for (auto* recording : { &context.timingBegin, &context.timingEnd }) {
			const auto name = std::format("NeuralRendering::Timing{}{}", recordingContext_, recording == &context.timingBegin ? "Begin" : "End");
			if (!recording->commandList && !CreateRecordingLocked(*recording, D3D12_COMMAND_LIST_TYPE_DIRECT, name))
				return false;
			HRESULT result = recording->allocator->Reset();
			if (SUCCEEDED(result))
				result = recording->commandList->Reset(recording->allocator.Get(), nullptr);
			if (FAILED(result))
				return RecordFailureLocked(result, "Parallel timestamp recording reset");
			recording->recording = true;
		}
		return true;
	}

	bool D3D12Interop::CloseRecordingLocked(CommandContext& context)
	{
		HRESULT result = context.commandList->Close();
		for (auto* recording : { &context.parallel, &context.timingBegin, &context.timingEnd }) {
			if (!recording->recording)
				continue;
			const HRESULT closed = recording->commandList->Close();
			recording->recording = false;
			if (FAILED(closed))
				result = closed;
		}
		return SUCCEEDED(result) || RecordFailureLocked(result, "ID3D12GraphicsCommandList::Close(batch)");
	}

	bool D3D12Interop::CreateTimingResourcesLocked()
	{
		if (!device12_ || !queue12_)
			return RecordFailureLocked(E_UNEXPECTED, "CreateTimingResources state");

		D3D12_QUERY_HEAP_DESC queryDescription{};
		queryDescription.Count = static_cast<UINT>(kCommandContextCount * kQueriesPerContext);
		queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		HRESULT result = device12_->CreateQueryHeap(
			&queryDescription, IID_PPV_ARGS(&timestampQueryHeap_));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateQueryHeap(timestamp)");
		SetD3D12Name(timestampQueryHeap_.Get(), "NeuralRendering::TimestampQueryHeap");

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
		heapProperties.CreationNodeMask = 1;
		heapProperties.VisibleNodeMask = 1;
		D3D12_RESOURCE_DESC bufferDescription{};
		bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDescription.Width = sizeof(std::uint64_t) * queryDescription.Count;
		bufferDescription.Height = 1;
		bufferDescription.DepthOrArraySize = 1;
		bufferDescription.MipLevels = 1;
		bufferDescription.SampleDesc.Count = 1;
		bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		result = device12_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDescription,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&timestampReadback_));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::CreateCommittedResource(timestamp readback)");
		SetD3D12Name(timestampReadback_.Get(), "NeuralRendering::TimestampReadback");

		result = queue12_->GetTimestampFrequency(&timestampFrequency_);
		if (FAILED(result) || !timestampFrequency_) {
			return RecordFailureLocked(
				FAILED(result) ? result : E_FAIL,
				"ID3D12CommandQueue::GetTimestampFrequency");
		}
		return true;
	}

	void D3D12Interop::CollectCompletedTimingLocked(
		std::size_t a_index,
		std::uint64_t a_completedValue) noexcept
	{
		if (a_index >= commandContexts_.size())
			return;
		auto& commandContext = commandContexts_[a_index];
		if (commandContext.timingPending && commandContext.gpuCaptureEpoch != CurrentGpuCaptureEpoch()) {
			if (commandContext.timing.execution)
				commandContext.timing.execution->FailPendingTimings();
			commandContext.timingPending = false;
			commandContext.gpuCaptureEpoch = 0;
		}
		if (!commandContext.timingPending || !commandContext.fenceValue ||
			a_completedValue < commandContext.fenceValue || !timestampReadback_ ||
			!timestampFrequency_) {
			return;
		}
		const std::uint64_t timingFenceValue = commandContext.fenceValue;

		const SIZE_T offset = sizeof(std::uint64_t) * a_index * kQueriesPerContext;
		const D3D12_RANGE readRange{ offset, offset + sizeof(std::uint64_t) * kQueriesPerContext };
		void* mapped = nullptr;
		const HRESULT mapResult = timestampReadback_->Map(0, &readRange, &mapped);
		if (SUCCEEDED(mapResult) && mapped) {
			const auto* timestamps = reinterpret_cast<const std::uint64_t*>(
				static_cast<const std::byte*>(mapped) + offset);
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::array<std::optional<std::uint64_t>, kMaximumExecutionRegions> evaluationMicroseconds{};
#endif
			std::optional<std::uint64_t> inferenceMicroseconds = 0;
			if (static_cast<std::uint32_t>(std::popcount(commandContext.evaluationTimingMask)) != commandContext.timing.evaluationCount)
				inferenceMicroseconds.reset();
			for (std::uint32_t region = 0; region < kMaximumExecutionRegions && inferenceMicroseconds; ++region) {
				if ((commandContext.evaluationTimingMask & (1u << region)) == 0)
					continue;
				const auto query = 2u + region * 2u;
				const auto timing = ResolveExecutionGpuTiming(timestamps[query], timestamps[query + 1u],
					commandContext.parallelRecording && region == 1 ? parallelTimestampFrequency_ : timestampFrequency_);
#ifdef DEVBENCH_BRIDGE_ENABLED
				evaluationMicroseconds[region] = timing.microseconds;
#endif
				if (!timing.microseconds || *timing.microseconds > UINT64_MAX - *inferenceMicroseconds)
					inferenceMicroseconds.reset();
				else
					*inferenceMicroseconds += *timing.microseconds;
			}
			const auto batchTiming = kDevelopmentDiagnostics || commandContext.parallelRecording ?
			                             ResolveExecutionGpuTiming(timestamps[0], timestamps[1], timestampFrequency_) :
			                             ExecutionTiming{};
			if (commandContext.parallelRecording && inferenceMicroseconds)
				inferenceMicroseconds = batchTiming.microseconds;
			if (const auto& execution = commandContext.timing.execution) {
				execution->Update([&](auto& evidence) {
					const auto resolved = [&](std::uint32_t query) -> ExecutionTiming {
						return ResolveExecutionGpuTiming(timestamps[query], timestamps[query + 1u],
							commandContext.parallelRecording && query == ExecutionEvaluationQuery(0, 1) ? parallelTimestampFrequency_ : timestampFrequency_);
					};
					if (evidence.batchGpu.state == ExecutionTimingState::Pending)
						evidence.batchGpu = resolved(0);
					for (std::uint32_t region = 0; region < kMaximumExecutionRegions; ++region)
						if ((commandContext.evaluationTimingMask & (1u << region)) != 0 &&
							evidence.regions[region].evaluationGpu.state == ExecutionTimingState::Pending)
							evidence.regions[region].evaluationGpu = resolved(2u + region * 2u);
				});
			}
			const D3D12_RANGE writtenRange{ 0, 0 };
			timestampReadback_->Unmap(0, &writtenRange);
			if (inferenceMicroseconds && globals::profiler) {
				try {
					globals::profiler->PublishExternalGpuTiming(commandContext.gpuCaptureEpoch,
						"NeuralRendering::Inference", commandContext.timing.frameId,
						static_cast<float>(*inferenceMicroseconds) / 1000.0f);
				} catch (const std::exception& error) {
					logger::warn("[DLSSNR] Cannot publish inference profiling: {}", error.what());
				}
			}

			if (batchTiming.microseconds) {
				const auto elapsedMicroseconds = *batchTiming.microseconds;
				IncrementSaturating(telemetry_.featureGpuSamples);
				AddSaturating(telemetry_.featureGpuMicroseconds, elapsedMicroseconds);
				switch (ClassifyFeatureSlotMask(commandContext.timing.featureSlotMask)) {
				case FeatureSlotRoute::Main:
					IncrementSaturating(telemetry_.mainFeatureGpuSamples);
					AddSaturating(telemetry_.mainFeatureGpuMicroseconds, elapsedMicroseconds);
					break;
				case FeatureSlotRoute::Submit:
					IncrementSaturating(telemetry_.submitFeatureGpuSamples);
					AddSaturating(telemetry_.submitFeatureGpuMicroseconds, elapsedMicroseconds);
					break;
				default:
					break;
				}
				if (IsValidInsertionPoint(commandContext.timing.insertionPoint)) {
					const auto insertionPointIndex = static_cast<std::size_t>(
						commandContext.timing.insertionPoint);
					IncrementSaturating(
						telemetry_.featureGpuSamplesByInsertionPoint[insertionPointIndex]);
					AddSaturating(
						telemetry_.featureGpuMicrosecondsByInsertionPoint[insertionPointIndex],
						elapsedMicroseconds);
				} else {
					IncrementSaturating(telemetry_.invalidInsertionPointSamples);
				}
				telemetry_.maximumFeatureGpuMicroseconds = std::max(
					telemetry_.maximumFeatureGpuMicroseconds, elapsedMicroseconds);
				if (timingFenceValue > lastCompletedTimingFenceValue_) {
					lastCompletedTimingFenceValue_ = timingFenceValue;
					telemetry_.lastFeatureGpuMicroseconds = elapsedMicroseconds;
					telemetry_.lastFeaturePixelCount = commandContext.timing.pixelCount;
					telemetry_.lastFeatureFrameId = commandContext.timing.frameId;
					telemetry_.lastFeatureEvaluationCount =
						commandContext.timing.evaluationCount;
					telemetry_.lastFeatureLogicalEyeCount =
						commandContext.timing.logicalEyeCount;
					telemetry_.lastFeatureSlotMask =
						commandContext.timing.featureSlotMask;
					telemetry_.lastInsertionPoint =
						commandContext.timing.insertionPoint;
#ifdef DEVBENCH_BRIDGE_ENABLED
					telemetry_.lastFeatureParallel = commandContext.parallelRecording;
					telemetry_.lastEvaluationGpuMicroseconds = evaluationMicroseconds;
					const auto& execution = commandContext.timing.execution;
					telemetry_.lastExecutionPlan = SummarizeExecutionPlan(execution ? &execution->Descriptor() : nullptr,
						commandContext.timing.frameId, commandContext.timing.insertionPoint);
#endif
				}
			} else {
				IncrementSaturating(telemetry_.featureGpuReadbackFailures);
			}
		} else {
			if (SUCCEEDED(mapResult)) {
				const D3D12_RANGE writtenRange{ 0, 0 };
				timestampReadback_->Unmap(0, &writtenRange);
			}
			IncrementSaturating(telemetry_.featureGpuReadbackFailures);
			if (!timingReadbackFailureLogged_) {
				logger::warn("[DLSSNR] Inference timestamp readback unavailable (hr=0x{:08X}); rendering will continue", static_cast<std::uint32_t>(mapResult));
				timingReadbackFailureLogged_ = true;
			}
		}
		// A completed command cannot produce a missing end timestamp later.
		if (commandContext.timing.execution)
			commandContext.timing.execution->FailPendingTimings();
		commandContext.timing = {};
		commandContext.timingPending = false;
		commandContext.evaluationTimingOpenMask = commandContext.evaluationTimingMask = 0;
	}

	void D3D12Interop::CollectCompletedTimingsLocked(
		std::uint64_t a_completedValue) noexcept
	{
		if (!timestampReadback_)
			return;
		if (a_completedValue == UINT64_MAX) {
			for (const auto& context : commandContexts_)
				if (context.timing.execution)
					context.timing.execution->FailPendingTimings();
			return;
		}
		std::array<std::size_t, kCommandContextCount> pending{};
		std::size_t count = 0;
		for (std::size_t index = 0; index < commandContexts_.size(); ++index)
			if (commandContexts_[index].timingPending)
				pending[count++] = index;
		// Ring indices wrap independently of submission order and stereo frame identity.
		std::sort(pending.begin(), pending.begin() + count, [&](auto left, auto right) {
			return commandContexts_[left].fenceValue < commandContexts_[right].fenceValue;
		});
		for (std::size_t index = 0; index < count; ++index)
			CollectCompletedTimingLocked(pending[index], a_completedValue);
	}

	bool D3D12Interop::Initialize(IDXGIAdapter* a_adapter, ID3D11Device* a_device, ID3D11DeviceContext* a_context,
		const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		std::scoped_lock lock(mutex_);
		ExecutionWaitScope waitEvidenceGuard{ waitEvidence_, a_evidence };
		if (!ShutdownLocked())
			return false;
		if (!a_adapter || !a_device || !a_context)
			return RecordFailureLocked(E_INVALIDARG, "Initialize arguments");

		HRESULT result = a_device->QueryInterface(IID_PPV_ARGS(&device11_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "QueryInterface(ID3D11Device5)");
		}
		result = a_context->QueryInterface(IID_PPV_ARGS(&context11_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "QueryInterface(ID3D11DeviceContext4)");
		}
		result = D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "D3D12CreateDevice");
		}
		SetD3D12Name(device12_.Get(), "NeuralRendering::Device");

		D3D12_COMMAND_QUEUE_DESC queueDescription{};
		queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		result = device12_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue12_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "ID3D12Device::CreateCommandQueue");
		}
		SetD3D12Name(queue12_.Get(), "NeuralRendering::CommandQueue");

		for (std::size_t index = 0; index < commandContexts_.size(); ++index) {
			if (!CreateCommandContextLocked(index)) {
				ReleaseObjectsLocked();
				return false;
			}
		}

		result = device12_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "ID3D12Device::CreateFence");
		}
		SetD3D12Name(fence12_.Get(), "NeuralRendering::SharedFence12");

		HANDLE sharedFenceHandle = nullptr;
		result = device12_->CreateSharedHandle(fence12_.Get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle);
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "ID3D12Device::CreateSharedHandle(fence)");
		}
		const ScopedHandle sharedFence(sharedFenceHandle);
		result = device11_->OpenSharedFence(sharedFence.Get(), IID_PPV_ARGS(&fence11_));
		if (FAILED(result)) {
			ReleaseObjectsLocked();
			return RecordFailureLocked(result, "ID3D11Device5::OpenSharedFence");
		}
		Util::SetResourceName(fence11_.Get(), "NeuralRendering::SharedFence11");

		initialized_ = true;
		lastError_ = S_OK;
		lastOperation_ = "Initialize";
		return true;
	}

	bool D3D12Interop::CreateSharedTexture(const D3D11_TEXTURE2D_DESC& a_sourceDesc, SharedTexture& a_texture, const char* a_name)
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_ || recording_)
			return RecordFailureLocked(E_UNEXPECTED, "CreateSharedTexture state");
		if (!a_sourceDesc.Width || !a_sourceDesc.Height || a_sourceDesc.Format == DXGI_FORMAT_UNKNOWN ||
			a_sourceDesc.ArraySize != 1 ||
			a_sourceDesc.MipLevels != 1 || a_sourceDesc.SampleDesc.Count != 1 ||
			(a_sourceDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0) {
			return RecordFailureLocked(E_INVALIDARG, "CreateSharedTexture description");
		}

		D3D11_TEXTURE2D_DESC description = a_sourceDesc;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.CPUAccessFlags = 0;
		description.SampleDesc.Quality = 0;
		description.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		SharedTexture replacement;
		HRESULT result = device11_->CreateTexture2D(&description, nullptr, &replacement.resource11);
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D11Device::CreateTexture2D(shared)");

		const std::string resourceName = a_name && *a_name ? a_name : "NeuralRendering::SharedTexture";
		Util::SetResourceName(replacement.resource11.Get(), "%s", resourceName.c_str());

		if ((description.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0) {
			result = device11_->CreateShaderResourceView(replacement.resource11.Get(), nullptr, &replacement.srv11);
			if (FAILED(result))
				return RecordFailureLocked(result, "ID3D11Device::CreateShaderResourceView(shared)");
			Util::SetResourceName(replacement.srv11.Get(), "%s SRV", resourceName.c_str());
		}

		result = device11_->CreateUnorderedAccessView(replacement.resource11.Get(), nullptr, &replacement.uav11);
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D11Device::CreateUnorderedAccessView(shared)");
		Util::SetResourceName(replacement.uav11.Get(), "%s UAV", resourceName.c_str());

		Microsoft::WRL::ComPtr<IDXGIResource1> dxgiResource;
		result = replacement.resource11.As(&dxgiResource);
		if (FAILED(result))
			return RecordFailureLocked(result, "QueryInterface(IDXGIResource1)");

		HANDLE sharedTextureHandle = nullptr;
		result = dxgiResource->CreateSharedHandle(
			nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
			&sharedTextureHandle);
		if (FAILED(result))
			return RecordFailureLocked(result, "IDXGIResource1::CreateSharedHandle(texture)");
		const ScopedHandle sharedTexture(sharedTextureHandle);

		result = device12_->OpenSharedHandle(sharedTexture.Get(), IID_PPV_ARGS(&replacement.resource12));
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12Device::OpenSharedHandle(texture)");
		const D3D12_RESOURCE_DESC openedDescription = replacement.resource12->GetDesc();
		if (openedDescription.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			openedDescription.Width != description.Width ||
			openedDescription.Height != description.Height ||
			openedDescription.DepthOrArraySize != 1 ||
			openedDescription.MipLevels != 1 ||
			openedDescription.Format != description.Format ||
			openedDescription.SampleDesc.Count != 1 ||
			(openedDescription.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
			return RecordFailureLocked(E_FAIL, "ID3D12Device::OpenSharedHandle(texture description)");
		}
		SetD3D12Name(replacement.resource12.Get(), resourceName + " D3D12");
		replacement.desc = description;

		resourceLeases_.push_back({
			.resource11 = replacement.resource11,
			.srv11 = replacement.srv11,
			.uav11 = replacement.uav11,
			.resource12 = replacement.resource12,
		});
		a_texture = std::move(replacement);
		lastError_ = S_OK;
		lastOperation_ = "CreateSharedTexture";
		return true;
	}

	bool D3D12Interop::WaitForFenceLocked(std::uint64_t a_value, DWORD a_timeoutMilliseconds, const char* a_operation)
	{
		if (!a_value)
			return true;
		if (!fence12_)
			return RecordFailureLocked(E_UNEXPECTED, a_operation);

		std::uint64_t completedValue = fence12_->GetCompletedValue();
		if (completedValue == UINT64_MAX) {
			const HRESULT removalReason = device12_ ? device12_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
			return RecordFailureLocked(FAILED(removalReason) ? removalReason : DXGI_ERROR_DEVICE_REMOVED, a_operation);
		}
		RetireCompletedFenceEventsLocked(completedValue);
		if (completedValue >= a_value)
			return true;

		ScopedHandle waitEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		if (!waitEvent.Get())
			return RecordFailureLocked(HRESULT_FROM_WIN32(GetLastError()), a_operation);

		const HRESULT result = fence12_->SetEventOnCompletion(a_value, waitEvent.Get());
		if (FAILED(result))
			return RecordFailureLocked(result, a_operation);

		const auto cpuWaitStarted = waitEvidence_ ? DiagnosticNow() : std::chrono::steady_clock::time_point{};
		const DWORD waitResult = WaitForSingleObject(waitEvent.Get(), a_timeoutMilliseconds);
		const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
		if (waitEvidence_) {
			const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				DiagnosticNow() - cpuWaitStarted)
					.count());
			waitEvidence_->RecordCpuWait({ a_operation, elapsed, waitResult, waitError, a_timeoutMilliseconds });
		}
		completedValue = fence12_->GetCompletedValue();
		if (completedValue == UINT64_MAX) {
			(void)waitEvent.Release();
			const HRESULT removalReason = device12_ ? device12_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
			return RecordFailureLocked(FAILED(removalReason) ? removalReason : DXGI_ERROR_DEVICE_REMOVED, a_operation);
		}
		if (completedValue >= a_value) {
			RetireCompletedFenceEventsLocked(completedValue);
			return true;
		}

		// The registration can outlive a bounded wait. Keep its unique event handle
		// until the fence proves completion so an old signal cannot satisfy a later wait.
		const HANDLE pendingEvent = waitEvent.Release();
		pendingFenceEvents_.push_back({ pendingEvent, a_value });
		if (waitResult == WAIT_TIMEOUT)
			return RecordFailureLocked(HRESULT_FROM_WIN32(ERROR_TIMEOUT), a_operation);
		if (waitResult == WAIT_OBJECT_0)
			return RecordFailureLocked(E_UNEXPECTED, a_operation);
		return RecordFailureLocked(
			HRESULT_FROM_WIN32(waitError ? waitError : ERROR_GEN_FAILURE), a_operation);
	}

	void D3D12Interop::RetireCompletedFenceEventsLocked(std::uint64_t a_completedValue)
	{
		std::erase_if(pendingFenceEvents_, [a_completedValue](const PendingFenceEvent& a_pending) {
			if (a_pending.fenceValue > a_completedValue)
				return false;
			if (a_pending.event)
				CloseHandle(a_pending.event);
			return true;
		});
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	bool D3D12Interop::SimulateBackpressureTimeout()
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_ || recording_ || unfencedSubmission_)
			return false;
		(void)RecordFailureLocked(HRESULT_FROM_WIN32(ERROR_TIMEOUT), "BeginD3D12 backpressure");
		return true;
	}
#endif

	bool D3D12Interop::BeginD3D12(ID3D12GraphicsCommandList** a_commandList, const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		std::scoped_lock lock(mutex_);
		ExecutionWaitScope waitEvidenceGuard{ waitEvidence_, a_evidence };
		if (a_commandList)
			*a_commandList = nullptr;
		if (!initialized_ || recording_ || !a_commandList || unfencedSubmission_)
			return RecordFailureLocked(E_UNEXPECTED, "BeginD3D12 state");

		const std::uint64_t completedValue = fence12_->GetCompletedValue();
		if (completedValue == UINT64_MAX) {
			const HRESULT removalReason = device12_->GetDeviceRemovedReason();
			return RecordFailureLocked(FAILED(removalReason) ? removalReason : DXGI_ERROR_DEVICE_REMOVED, "BeginD3D12 device removed");
		}
		CollectCompletedTimingsLocked(completedValue);

		std::size_t contextIndex = kCommandContextCount;
		for (std::size_t offset = 0; offset < kCommandContextCount; ++offset) {
			const std::size_t candidate = (commandContextCursor_ + offset) % kCommandContextCount;
			const auto& commandContext = commandContexts_[candidate];
			if (commandContext.usable && (!commandContext.fenceValue || completedValue >= commandContext.fenceValue)) {
				contextIndex = candidate;
				break;
			}
		}

		if (contextIndex == kCommandContextCount) {
			contextIndex = commandContextCursor_;
			auto& commandContext = commandContexts_[contextIndex];
			if (!commandContext.usable)
				return RecordFailureLocked(E_FAIL, "BeginD3D12 unusable context");
			if (!backpressureLogged_) {
				logger::warn("[DLSSNR] D3D12 command contexts saturated; applying bounded CPU backpressure");
				backpressureLogged_ = true;
			}
			// Submit D3D11 readiness signals before the CPU waits on dependent D3D12 work.
			context11_->Flush();
			IncrementSaturating(telemetry_.backpressureWaits);
			const auto waitStarted = DiagnosticNow();
			const bool waitSucceeded = WaitForFenceLocked(
				commandContext.fenceValue,
				kBackpressureTimeoutMilliseconds,
				"BeginD3D12 backpressure");
			const auto waitMicroseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					DiagnosticNow() - waitStarted)
					.count());
			AddSaturating(telemetry_.backpressureWaitMicroseconds, waitMicroseconds);
			telemetry_.maximumBackpressureWaitMicroseconds = std::max(
				telemetry_.maximumBackpressureWaitMicroseconds, waitMicroseconds);
			if (!waitSucceeded) {
				if (commandContext.parallelRecording)
					DisableParallel("parallel submission backpressure");
				return false;
			}
			CollectCompletedTimingLocked(contextIndex, fence12_->GetCompletedValue());
		}

		auto& commandContext = commandContexts_[contextIndex];
		commandContext.fenceValue = 0;
		commandContext.timing = {};
		commandContext.timingPending = false;
		commandContext.parallelRecording = false;
		commandContext.evaluationTimingOpenMask = commandContext.evaluationTimingMask = 0;
		const std::uint64_t readyValue = ++fenceValue_;
		commandContext.inputReady = readyValue;
		HRESULT result = context11_->Signal(fence11_.Get(), readyValue);
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D11DeviceContext4::Signal");
		result = queue12_->Wait(fence12_.Get(), readyValue);
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12CommandQueue::Wait");
		result = commandContext.allocator->Reset();
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12CommandAllocator::Reset");
		result = commandContext.commandList->Reset(commandContext.allocator.Get(), nullptr);
		if (FAILED(result))
			return RecordFailureLocked(result, "ID3D12GraphicsCommandList::Reset");

		recording_ = true;
		featureTimingOpen_ = false;
		featureTimingCompleted_ = false;
		timingRecording_ = false;
		recordingContext_ = contextIndex;
		recordingThread_ = std::this_thread::get_id();
		commandContextCursor_ = (contextIndex + 1) % kCommandContextCount;
		*a_commandList = commandContext.commandList.Get();
		lastError_ = S_OK;
		lastOperation_ = "BeginD3D12";
		return true;
	}

	bool D3D12Interop::RecordTransportSubmission(const D3D12InteropSubmissionTiming& timing)
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_ || !recording_ || recordingContext_ >= commandContexts_.size() ||
			recordingThread_ != std::this_thread::get_id() || featureTimingOpen_ ||
			timingRecording_ || featureTimingCompleted_) {
			return RecordFailureLocked(E_UNEXPECTED, "RecordTransportSubmission state");
		}
		if (timing.evaluationCount != 0u || timing.pixelCount == 0u ||
			timing.logicalEyeCount == 0u || timing.logicalEyeCount > 2u ||
			!IsValidInsertionPoint(timing.insertionPoint) ||
			ClassifyFeatureSlotMask(timing.featureSlotMask) == FeatureSlotRoute::Unexpected) {
			return RecordFailureLocked(E_INVALIDARG, "RecordTransportSubmission metadata");
		}
		auto& context = commandContexts_[recordingContext_];
		context.timing = timing;
		context.timingPending = false;
		// EndD3D12 still enforces one complete metadata scope and its normal
		// queue/fence/D3D11 wait path. No timestamp is mistaken for NR inference.
		featureTimingCompleted_ = true;
		lastError_ = S_OK;
		lastOperation_ = "RecordTransportSubmission";
		return true;
	}

	bool D3D12Interop::BeginFeatureTiming(
		ID3D12GraphicsCommandList* a_commandList,
		const D3D12InteropSubmissionTiming& a_timing, bool& a_scopeOpened)
	{
		a_scopeOpened = false;
		const auto captureEpoch = CurrentGpuCaptureEpoch();
		if (!kDevelopmentDiagnostics && !captureEpoch)
			return true;
		std::scoped_lock lock(mutex_);
		if (!initialized_ || !recording_ ||
			recordingContext_ >= commandContexts_.size() ||
			recordingThread_ != std::this_thread::get_id() ||
			a_commandList != commandContexts_[recordingContext_].commandList.Get()) {
			return RecordFailureLocked(E_UNEXPECTED, "BeginFeatureTiming state");
		}
		if (featureTimingOpen_ || featureTimingCompleted_)
			return RecordFailureLocked(E_UNEXPECTED, "BeginFeatureTiming scope already used");
		const auto route = ClassifyFeatureSlotMask(a_timing.featureSlotMask);
		const bool insertionPointValid = IsValidInsertionPoint(a_timing.insertionPoint);
		if (a_timing.frameId == std::numeric_limits<std::uint32_t>::max() ||
			!a_timing.pixelCount || !a_timing.evaluationCount ||
			a_timing.evaluationCount > kMaximumRegionEvaluations ||
			a_timing.logicalEyeCount == 0u || a_timing.logicalEyeCount > 2u ||
			static_cast<std::uint32_t>(std::popcount(
				LogicalRegionMask(a_timing.featureSlotMask))) !=
				a_timing.logicalEyeCount ||
			static_cast<std::uint32_t>(std::popcount(a_timing.featureSlotMask)) !=
				a_timing.evaluationCount ||
			route == FeatureSlotRoute::Unexpected ||
			!insertionPointValid) {
			if (route == FeatureSlotRoute::Unexpected)
				IncrementSaturating(telemetry_.unexpectedFeatureSlotMaskSamples);
			if (!insertionPointValid)
				IncrementSaturating(telemetry_.invalidInsertionPointSamples);
			return RecordFailureLocked(E_INVALIDARG, "BeginFeatureTiming metadata");
		}

		auto& commandContext = commandContexts_[recordingContext_];
		commandContext.timing = a_timing;
		commandContext.gpuCaptureEpoch = captureEpoch;
		commandContext.timingPending = false;
		if (captureEpoch && !timingResourcesAttempted_) {
			timingResourcesAttempted_ = true;
			if (!CreateTimingResourcesLocked()) {
				logger::warn("[DLSSNR] Inference GPU profiling unavailable at {} (hr=0x{:08X}); rendering will continue", lastOperation_, static_cast<std::uint32_t>(lastError_));
				timestampReadback_.Reset();
				timestampQueryHeap_.Reset();
				timestampFrequency_ = 0;
			}
		}
		commandContext.evaluationTimingOpenMask = commandContext.evaluationTimingMask = 0;
		featureTimingOpen_ = true;
		a_scopeOpened = true;
		if (a_timing.execution)
			a_timing.execution->Update([&](auto& evidence) {
				evidence.parallelEyes = commandContext.parallelRecording;
				evidence.batchGpu = { captureEpoch && timestampQueryHeap_ && timestampReadback_ && timestampFrequency_ ?
										  ExecutionTimingState::Pending :
										  ExecutionTimingState::Unavailable,
					std::nullopt };
			});
		if (!captureEpoch || !timestampQueryHeap_ || !timestampReadback_ || !timestampFrequency_)
			return true;
		if (commandContext.parallelRecording && !PrepareParallelTimingLocked(commandContext)) {
			if (!parallelTimingFailed_)
				logger::warn("[DLSSNR] Parallel inference profiling unavailable at {}; rendering will continue", lastOperation_);
			parallelTimingFailed_ = true;
			if (a_timing.execution)
				a_timing.execution->FailPendingTimings();
			return true;
		}

		if (kDevelopmentDiagnostics || commandContext.parallelRecording) {
			auto* beginList = commandContext.parallelRecording ? commandContext.timingBegin.commandList.Get() : a_commandList;
			beginList->EndQuery(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
				static_cast<UINT>(recordingContext_ * kQueriesPerContext));
		}
		timingRecording_ = true;
		return true;
	}

	void D3D12Interop::BeginEvaluationTiming(ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_region) noexcept
	{
		if (!kDevelopmentDiagnostics && !CurrentGpuCaptureEpoch())
			return;
		try {
			std::scoped_lock lock(mutex_);
			if (!recording_ || recordingContext_ >= commandContexts_.size() || a_region >= kMaximumExecutionRegions)
				return;
			auto& context = commandContexts_[recordingContext_];
			const auto* expected = context.parallelRecording && a_region == 1 ?
			                           context.parallel.commandList.Get() :
			                           context.commandList.Get();
			if (a_commandList != expected)
				return;
			const bool available = context.gpuCaptureEpoch != 0 && context.gpuCaptureEpoch == CurrentGpuCaptureEpoch() && featureTimingOpen_ && timingRecording_ && timestampQueryHeap_ && timestampReadback_ &&
			                       timestampFrequency_ && recordingThread_ == std::this_thread::get_id() &&
			                       ((context.evaluationTimingOpenMask | context.evaluationTimingMask) & (1u << a_region)) == 0;
			if (context.timing.execution)
				context.timing.execution->Update([&](auto& evidence) {
					evidence.regions[a_region].evaluationGpu = { available ? ExecutionTimingState::Pending :
																			 ExecutionTimingState::Unavailable,
						std::nullopt };
				});
			if (!available)
				return;
			a_commandList->EndQuery(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
				ExecutionEvaluationQuery(static_cast<std::uint32_t>(recordingContext_), a_region));
			context.evaluationTimingOpenMask |= 1u << a_region;
		} catch (...) {
			executionTimingFailed_.store(true, std::memory_order_relaxed);
		}
	}

	void D3D12Interop::EndEvaluationTiming(ID3D12GraphicsCommandList* a_commandList, std::uint32_t a_region) noexcept
	{
		if (!CurrentGpuCaptureEpoch())
			return;
		try {
			std::scoped_lock lock(mutex_);
			if (!recording_ || recordingContext_ >= commandContexts_.size() || a_region >= kMaximumExecutionRegions)
				return;
			auto& context = commandContexts_[recordingContext_];
			const auto* expected = context.parallelRecording && a_region == 1 ?
			                           context.parallel.commandList.Get() :
			                           context.commandList.Get();
			if (a_commandList != expected || !context.gpuCaptureEpoch || context.gpuCaptureEpoch != CurrentGpuCaptureEpoch() || (context.evaluationTimingOpenMask & (1u << a_region)) == 0)
				return;
			const auto query = ExecutionEvaluationQuery(static_cast<std::uint32_t>(recordingContext_), a_region);
			a_commandList->EndQuery(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1u);
			context.evaluationTimingOpenMask &= ~(1u << a_region);
			context.evaluationTimingMask |= 1u << a_region;
		} catch (...) {
			executionTimingFailed_.store(true, std::memory_order_relaxed);
		}
	}

	bool D3D12Interop::EndFeatureTiming(ID3D12GraphicsCommandList* a_commandList, bool a_scopeOpened)
	{
		if (!kDevelopmentDiagnostics && !a_scopeOpened)
			return true;
		std::scoped_lock lock(mutex_);
		if (!initialized_ || !recording_ ||
			recordingContext_ >= commandContexts_.size() ||
			recordingThread_ != std::this_thread::get_id() ||
			a_commandList != commandContexts_[recordingContext_].commandList.Get()) {
			return RecordFailureLocked(E_UNEXPECTED, "EndFeatureTiming state");
		}
		if (!featureTimingOpen_)
			return RecordFailureLocked(E_UNEXPECTED, "EndFeatureTiming not open");
		featureTimingOpen_ = false;
		if (!timingRecording_ || commandContexts_[recordingContext_].gpuCaptureEpoch != CurrentGpuCaptureEpoch()) {
			timingRecording_ = false;
			if (commandContexts_[recordingContext_].timing.execution)
				commandContexts_[recordingContext_].timing.execution->FailPendingTimings();
			featureTimingCompleted_ = true;
			return true;
		}

		auto& context = commandContexts_[recordingContext_];
		auto* resolveList = context.parallelRecording ? context.timingEnd.commandList.Get() : a_commandList;
		const UINT queryIndex = static_cast<UINT>(recordingContext_ * kQueriesPerContext);
		if (kDevelopmentDiagnostics || context.parallelRecording) {
			resolveList->EndQuery(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex + 1u);
			resolveList->ResolveQueryData(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
				queryIndex, 2u, timestampReadback_.Get(), sizeof(std::uint64_t) * queryIndex);
		}
		for (std::uint32_t region = 0; region < kMaximumExecutionRegions; ++region) {
			if ((commandContexts_[recordingContext_].evaluationTimingMask & (1u << region)) == 0)
				continue;
			const auto query = ExecutionEvaluationQuery(static_cast<std::uint32_t>(recordingContext_), region);
			resolveList->ResolveQueryData(timestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2u,
				timestampReadback_.Get(), sizeof(std::uint64_t) * query);
		}
		commandContexts_[recordingContext_].timingPending = true;
		timingRecording_ = false;
		featureTimingCompleted_ = true;
		return true;
	}

	bool D3D12Interop::EndD3D12(bool joinD3D11)
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_ || !recording_ || recordingContext_ >= kCommandContextCount ||
			recordingThread_ != std::this_thread::get_id()) {
			return RecordFailureLocked(E_UNEXPECTED, "EndD3D12 state");
		}
		if (kDevelopmentDiagnostics && (featureTimingOpen_ || timingRecording_ || !featureTimingCompleted_)) {
			const char* operation = featureTimingOpen_ || timingRecording_ ?
			                            "EndD3D12 feature timing still open" :
			                            "EndD3D12 feature timing scope missing";
			if (!AbortD3D12Locked())
				return false;
			return RecordFailureLocked(E_UNEXPECTED, operation);
		}

		auto& commandContext = commandContexts_[recordingContext_];
		const std::size_t submittedContext = recordingContext_;
		const bool closed = CloseRecordingLocked(commandContext);
		const HRESULT closeResult = closed ? S_OK : lastError_;
		recording_ = false;
		recordingContext_ = kCommandContextCount;
		recordingThread_ = {};
		if (FAILED(closeResult)) {
			if (commandContext.parallelRecording)
				DisableParallel("parallel command close failed");
			if (commandContext.timing.execution)
				commandContext.timing.execution->FailPendingTimings();
			commandContext.usable = false;
			commandContext.allocator.Reset();
			commandContext.commandList.Reset();
			(void)CreateCommandContextLocked(submittedContext);
			return RecordFailureLocked(closeResult, "ID3D12GraphicsCommandList::Close");
		}

		const auto submit = [](ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
			ID3D12CommandList* commands[]{ list };
			queue->ExecuteCommandLists(1, commands);
		};
		const auto failSubmission = [&](HRESULT result, const char* operation) {
			commandContext.usable = false;
			if (commandContext.timing.execution)
				commandContext.timing.execution->FailPendingTimings();
			if (commandContext.parallelRecording)
				DisableParallel(operation);
			return RecordFailureLocked(result, operation);
		};
		unfencedSubmission_ = true;
		if (commandContext.parallelRecording) {
			std::uint64_t ready = commandContext.inputReady;
			if (commandContext.timingPending) {
				// The common timestamp precedes both queues; the end timestamp follows their join.
				submit(queue12_.Get(), commandContext.timingBegin.commandList.Get());
				ready = ++fenceValue_;
				const HRESULT result = queue12_->Signal(fence12_.Get(), ready);
				if (FAILED(result))
					return failSubmission(result, "ID3D12CommandQueue::Signal(parallel timing start)");
			}
			const HRESULT readyResult = parallelQueue_->Wait(fence12_.Get(), ready);
			if (FAILED(readyResult))
				return failSubmission(readyResult, "ID3D12CommandQueue::Wait(parallel input)");
		}
		submit(queue12_.Get(), commandContext.commandList.Get());
		if (commandContext.parallelRecording) {
			submit(parallelQueue_.Get(), commandContext.parallel.commandList.Get());
			const auto parallelComplete = ++parallelFenceValue_;
			const HRESULT signal = parallelQueue_->Signal(parallelFence_.Get(), parallelComplete);
			if (FAILED(signal))
				return failSubmission(signal, "ID3D12CommandQueue::Signal(parallel completion)");
			const HRESULT join = queue12_->Wait(parallelFence_.Get(), parallelComplete);
			if (FAILED(join))
				return failSubmission(join, "ID3D12CommandQueue::Wait(parallel completion)");
			if (commandContext.timingPending)
				submit(queue12_.Get(), commandContext.timingEnd.commandList.Get());
		}
		const std::uint64_t completeValue = ++fenceValue_;
		const HRESULT signalResult = queue12_->Signal(fence12_.Get(), completeValue);
		if (FAILED(signalResult))
			return failSubmission(signalResult, "ID3D12CommandQueue::Signal(submission)");
		commandContext.fenceValue = completeValue;
		IncrementSaturating(telemetry_.commandSubmissions);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (commandContext.parallelRecording)
			IncrementSaturating(telemetry_.parallelCommandSubmissions);
#endif
		const auto route = ClassifyFeatureSlotMask(commandContext.timing.featureSlotMask);
		if (route == FeatureSlotRoute::Main)
			IncrementSaturating(telemetry_.mainCommandSubmissions);
		else if (route == FeatureSlotRoute::Submit)
			IncrementSaturating(telemetry_.submitCommandSubmissions);
		if (commandContext.timing.logicalEyeCount == 2u) {
			IncrementSaturating(telemetry_.stereoCommandSubmissions);
			if (route == FeatureSlotRoute::Main)
				IncrementSaturating(telemetry_.mainStereoCommandSubmissions);
			else if (route == FeatureSlotRoute::Submit)
				IncrementSaturating(telemetry_.submitStereoCommandSubmissions);
		}

		if (joinD3D11) {
			const HRESULT waitResult = context11_->Wait(fence11_.Get(), completeValue);
			if (FAILED(waitResult))
				return failSubmission(waitResult, "ID3D11DeviceContext4::Wait");
		}
		unfencedSubmission_ = false;

		lastError_ = S_OK;
		lastSubmissionParallel_ = commandContext.parallelRecording;
		lastOperation_ = "EndD3D12";
		return true;
	}

	bool D3D12Interop::AbortD3D12Locked()
	{
		if (!initialized_ || !recording_ || recordingContext_ >= kCommandContextCount ||
			recordingThread_ != std::this_thread::get_id()) {
			return RecordFailureLocked(E_UNEXPECTED, "AbortD3D12 state");
		}

		const std::size_t abortedContext = recordingContext_;
		auto& commandContext = commandContexts_[abortedContext];
		const bool closed = CloseRecordingLocked(commandContext);
		const HRESULT closeResult = closed ? S_OK : lastError_;
		recording_ = false;
		recordingContext_ = kCommandContextCount;
		recordingThread_ = {};
		commandContext.fenceValue = 0;
		if (commandContext.timing.execution)
			commandContext.timing.execution->FailPendingTimings();
		commandContext.timing = {};
		commandContext.timingPending = false;
		featureTimingOpen_ = false;
		featureTimingCompleted_ = false;
		timingRecording_ = false;
		if (FAILED(closeResult)) {
			commandContext.usable = false;
			commandContext.allocator.Reset();
			commandContext.commandList.Reset();
			(void)CreateCommandContextLocked(abortedContext);
			return RecordFailureLocked(closeResult, "ID3D12GraphicsCommandList::Close(abort)");
		}

		lastError_ = S_OK;
		lastOperation_ = "AbortD3D12";
		return true;
	}

	bool D3D12Interop::AbortD3D12()
	{
		std::scoped_lock lock(mutex_);
		return AbortD3D12Locked();
	}

	bool D3D12Interop::WaitForIdleLocked()
	{
		if (!initialized_)
			return true;
		if (recording_)
			return RecordFailureLocked(E_PENDING, "WaitForIdle while recording");

		context11_->Flush();
		// A partial submission may have skipped the normal join; retire compute independently.
		if (parallelQueue_) {
			const auto complete = ++parallelFenceValue_;
			HRESULT result = parallelQueue_->Signal(parallelFence_.Get(), complete);
			if (SUCCEEDED(result))
				result = queue12_->Wait(parallelFence_.Get(), complete);
			if (FAILED(result)) {
				unfencedSubmission_ = true;
				DisableParallel("parallel drain failed");
				return RecordFailureLocked(result, "WaitForIdle parallel fence");
			}
		}
		const std::uint64_t idleValue = ++fenceValue_;
		const HRESULT signalResult = queue12_->Signal(fence12_.Get(), idleValue);
		if (FAILED(signalResult))
			return RecordFailureLocked(signalResult, "ID3D12CommandQueue::Signal(idle)");
		if (!WaitForFenceLocked(idleValue, kTeardownTimeoutMilliseconds, "WaitForIdle D3D12 fence")) {
			if (parallelQueue_)
				DisableParallel("parallel retirement did not complete");
			return false;
		}

		// A D3D12 completion alone does not prove that D3D11 consumed its queued
		// waits and copies. Signal after the D3D11 tail and wait for that value too.
		const std::uint64_t acknowledgementValue = ++fenceValue_;
		const HRESULT acknowledgementResult = context11_->Signal(
			fence11_.Get(), acknowledgementValue);
		if (FAILED(acknowledgementResult))
			return RecordFailureLocked(acknowledgementResult, "ID3D11DeviceContext4::Signal(idle acknowledgement)");
		context11_->Flush();
		if (!WaitForFenceLocked(
				acknowledgementValue, kTeardownTimeoutMilliseconds,
				"WaitForIdle D3D11 acknowledgement")) {
			return false;
		}
		CollectCompletedTimingsLocked(fence12_->GetCompletedValue());

		if (!pendingFenceEvents_.empty())
			return RecordFailureLocked(E_PENDING, "WaitForIdle pending fence registrations");

		unfencedSubmission_ = false;
		for (auto& commandContext : commandContexts_)
			commandContext.fenceValue = 0;
		// External SharedTexture owners keep active resources alive after this synchronization point.
		resourceLeases_.clear();
		lastError_ = S_OK;
		lastOperation_ = "WaitForIdle";
		return true;
	}

	bool D3D12Interop::WaitForIdle(const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		std::scoped_lock lock(mutex_);
		ExecutionWaitScope waitEvidenceGuard{ waitEvidence_, a_evidence };
		return WaitForIdleLocked();
	}

	void D3D12Interop::ReleaseObjectsLocked()
	{
		for (const auto& context : commandContexts_)
			if (context.timing.execution)
				context.timing.execution->FailPendingTimings();
		for (const auto& pending : pendingFenceEvents_) {
			if (pending.event)
				CloseHandle(pending.event);
		}
		pendingFenceEvents_.clear();
		resourceLeases_.clear();
		commandContexts_ = {};
		fence11_.Reset();
		fence12_.Reset();
		timestampReadback_.Reset();
		timestampQueryHeap_.Reset();
		parallelQueue_.Reset();
		parallelFence_.Reset();
		parallelFenceValue_ = 0;
		parallelTimestampFrequency_ = 0;
		parallelTimingFailed_ = false;
		lastSubmissionParallel_ = false;
		queue12_.Reset();
		device12_.Reset();
		context11_.Reset();
		device11_.Reset();
		fenceValue_ = 0;
		lastCompletedTimingFenceValue_ = 0;
		commandContextCursor_ = 0;
		recordingContext_ = kCommandContextCount;
		recordingThread_ = {};
		timestampFrequency_ = 0;
		timingResourcesAttempted_ = false;
		timingReadbackFailureLogged_ = false;
		initialized_ = false;
		recording_ = false;
		featureTimingOpen_ = false;
		featureTimingCompleted_ = false;
		timingRecording_ = false;
		unfencedSubmission_ = false;
		backpressureLogged_ = false;
	}

	void D3D12Interop::AbandonObjectsLocked() noexcept
	{
		for (const auto& context : commandContexts_)
			if (context.timing.execution)
				context.timing.execution->FailPendingTimings();
		// A bounded wait failure leaves ownership intentionally leaked so in-flight GPU work cannot dereference freed objects.
		// Pending event registrations are leaked with the fence to prevent handle reuse.
		pendingFenceEvents_.clear();
		for (auto& lease : resourceLeases_) {
			(void)lease.resource11.Detach();
			(void)lease.srv11.Detach();
			(void)lease.uav11.Detach();
			(void)lease.resource12.Detach();
		}
		resourceLeases_.clear();
		for (auto& commandContext : commandContexts_) {
			(void)commandContext.allocator.Detach();
			(void)commandContext.commandList.Detach();
			for (auto* recording : { &commandContext.parallel, &commandContext.timingBegin, &commandContext.timingEnd }) {
				(void)recording->allocator.Detach();
				(void)recording->commandList.Detach();
			}
		}
		(void)fence11_.Detach();
		(void)fence12_.Detach();
		(void)timestampReadback_.Detach();
		(void)timestampQueryHeap_.Detach();
		(void)parallelQueue_.Detach();
		(void)parallelFence_.Detach();
		(void)queue12_.Detach();
		(void)device12_.Detach();
		(void)context11_.Detach();
		(void)device11_.Detach();
		initialized_ = false;
		recording_ = false;
		featureTimingOpen_ = false;
		featureTimingCompleted_ = false;
		timingRecording_ = false;
		try {
			lastOperation_ = "AbandonObjectsAfterUnsafeTeardown";
		} catch (...) {
		}
	}

	bool D3D12Interop::ShutdownLocked()
	{
		if (!initialized_) {
			ReleaseObjectsLocked();
			return true;
		}
		if (recording_) {
			if (recordingThread_ != std::this_thread::get_id())
				return RecordFailureLocked(E_PENDING, "Shutdown recording on another thread");
			if (!AbortD3D12Locked())
				return false;
		}
		if (!WaitForIdleLocked())
			return false;

		ReleaseObjectsLocked();
		lastError_ = S_OK;
		lastOperation_ = "Shutdown";
		return true;
	}

	bool D3D12Interop::Shutdown(const std::shared_ptr<ExecutionEvidence>& a_evidence)
	{
		std::scoped_lock lock(mutex_);
		ExecutionWaitScope waitEvidenceGuard{ waitEvidence_, a_evidence };
		return ShutdownLocked();
	}

	void D3D12Interop::AbandonUnsafe() noexcept
	{
		abandonRequested_.store(true, std::memory_order_release);
		try {
			std::scoped_lock lock(mutex_);
			AbandonObjectsLocked();
		} catch (...) {
		}
	}

	bool D3D12Interop::IsInitialized() const
	{
		std::scoped_lock lock(mutex_);
		return initialized_;
	}

	bool D3D12Interop::IsRecording() const
	{
		std::scoped_lock lock(mutex_);
		return recording_;
	}

	ID3D12Device* D3D12Interop::Device() const
	{
		std::scoped_lock lock(mutex_);
		return device12_.Get();
	}

	HRESULT D3D12Interop::LastError() const
	{
		std::scoped_lock lock(mutex_);
		return lastError_;
	}

	std::string D3D12Interop::LastOperation() const
	{
		std::scoped_lock lock(mutex_);
		return lastOperation_;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	std::vector<SharedTexture> D3D12Interop::GetResourceLeases() const
	{
		std::scoped_lock lock(mutex_);
		std::vector<SharedTexture> result;
		result.reserve(resourceLeases_.size());
		for (const auto& lease : resourceLeases_) {
			SharedTexture texture;
			texture.resource12 = lease.resource12;
			texture.resource11 = lease.resource11;
			if (texture.resource11)
				texture.resource11->GetDesc(&texture.desc);
			result.push_back(std::move(texture));
		}
		return result;
	}

#endif
	LifetimeFenceSnapshot D3D12Interop::GetLifetimeSnapshot() const
	{
		std::scoped_lock lock(mutex_);
		LifetimeFenceSnapshot result;
		result.device = reinterpret_cast<std::uintptr_t>(device12_.Get());
		result.device11 = reinterpret_cast<std::uintptr_t>(device11_.Get());
		result.context11 = reinterpret_cast<std::uintptr_t>(context11_.Get());
		result.queue = reinterpret_cast<std::uintptr_t>(queue12_.Get());
		result.fence = reinterpret_cast<std::uintptr_t>(fence12_.Get());
		result.initialized = initialized_;
		result.recording = recording_;
		result.issued = fenceValue_;
		if (fence12_) {
			result.completed = fence12_->GetCompletedValue();
			result.deviceRemoved = result.completed == UINT64_MAX;
			result.completedKnown = !result.deviceRemoved;
		}
		if (parallelFence_) {
			result.parallelQueue = reinterpret_cast<std::uintptr_t>(parallelQueue_.Get());
			result.parallelFence = reinterpret_cast<std::uintptr_t>(parallelFence_.Get());
			result.parallelIssued = parallelFenceValue_;
			result.parallelCompleted = parallelFence_->GetCompletedValue();
			result.deviceRemoved |= result.parallelCompleted == UINT64_MAX;
			result.completedKnown &= !result.deviceRemoved && result.parallelCompleted >= result.parallelIssued;
		}
		result.completedKnown &= !unfencedSubmission_;
		for (std::size_t index = 0; index < commandContexts_.size(); ++index)
			result.contexts[index] = commandContexts_[index].fenceValue;
		return result;
	}
	ParallelEyeStatus D3D12Interop::GetParallelStatus() const
	{
		std::scoped_lock lock(mutex_);
		return { parallelQueue_ != nullptr, IsParallelDisabled(), lastSubmissionParallel_ };
	}

	D3D12InteropTelemetry D3D12Interop::GetTelemetry()
	{
		std::scoped_lock lock(mutex_);
		if (executionTimingFailed_.exchange(false, std::memory_order_relaxed))
			for (const auto& context : commandContexts_)
				if (context.timing.execution)
					context.timing.execution->FailPendingTimings();
		if (fence12_)
			CollectCompletedTimingsLocked(fence12_->GetCompletedValue());
		return telemetry_;
	}
}
