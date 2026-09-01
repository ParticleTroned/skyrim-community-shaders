#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	struct D3D12InteropSubmissionTiming
	{
		std::uint64_t pixelCount = 0;
		std::uint32_t evaluationCount = 0;
		std::uint32_t featureSlotMask = 0;
	};

	struct D3D12InteropTelemetry
	{
		std::uint64_t commandSubmissions = 0;
		std::uint64_t mainCommandSubmissions = 0;
		std::uint64_t submitCommandSubmissions = 0;
		std::uint64_t stereoCommandSubmissions = 0;
		std::uint64_t mainStereoCommandSubmissions = 0;
		std::uint64_t submitStereoCommandSubmissions = 0;
		std::uint64_t backpressureWaits = 0;
		std::uint64_t backpressureWaitMicroseconds = 0;
		std::uint64_t maximumBackpressureWaitMicroseconds = 0;
		std::uint64_t featureGpuSamples = 0;
		std::uint64_t featureGpuReadbackFailures = 0;
		std::uint64_t featureGpuMicroseconds = 0;
		std::uint64_t mainFeatureGpuSamples = 0;
		std::uint64_t mainFeatureGpuMicroseconds = 0;
		std::uint64_t submitFeatureGpuSamples = 0;
		std::uint64_t submitFeatureGpuMicroseconds = 0;
		std::uint64_t unexpectedFeatureSlotMaskSamples = 0;
		std::uint64_t lastFeatureGpuMicroseconds = 0;
		std::uint64_t maximumFeatureGpuMicroseconds = 0;
		std::uint64_t lastFeaturePixelCount = 0;
		std::uint32_t lastFeatureEvaluationCount = 0;
		std::uint32_t lastFeatureSlotMask = 0;
	};

	struct SharedTexture
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> resource11;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv11;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav11;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource12;
		D3D11_TEXTURE2D_DESC desc{};

		[[nodiscard]] explicit operator bool() const { return resource11 && resource12; }
	};

	class D3D12Interop
	{
	public:
		static constexpr std::size_t kCommandContextCount = 3;
		static constexpr DWORD kBackpressureTimeoutMilliseconds = 250;
		static constexpr DWORD kTeardownTimeoutMilliseconds = 1000;

		D3D12Interop() = default;
		~D3D12Interop() noexcept;
		D3D12Interop(const D3D12Interop&) = delete;
		D3D12Interop& operator=(const D3D12Interop&) = delete;

		bool Initialize(IDXGIAdapter* a_adapter, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		bool CreateSharedTexture(const D3D11_TEXTURE2D_DESC& a_sourceDesc, SharedTexture& a_texture, const char* a_name);

		bool BeginD3D12(ID3D12GraphicsCommandList** a_commandList);
		/** Opens the required Feature 18 submission-metadata scope and optional GPU timestamp. */
		bool BeginFeatureTiming(
			ID3D12GraphicsCommandList* a_commandList,
			const D3D12InteropSubmissionTiming& a_timing);
		/** Closes the Feature 18 scope; EndD3D12 requires exactly one completed scope. */
		bool EndFeatureTiming(ID3D12GraphicsCommandList* a_commandList);
		bool EndD3D12();
		bool AbortD3D12();
		bool WaitForIdle();
		bool Shutdown();
		/** Permanently detaches unsafe interop ownership without releasing it. */
		void AbandonUnsafe() noexcept;

		[[nodiscard]] bool IsInitialized() const;
		[[nodiscard]] bool IsRecording() const;
		[[nodiscard]] ID3D12Device* Device() const;
		[[nodiscard]] HRESULT LastError() const;
		[[nodiscard]] std::string LastOperation() const;
		[[nodiscard]] D3D12InteropTelemetry GetTelemetry();

	private:
		struct CommandContext
		{
			Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
			std::uint64_t fenceValue = 0;
			D3D12InteropSubmissionTiming timing{};
			bool timingPending = false;
			bool usable = true;
		};

		struct ResourceLease
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> resource11;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv11;
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav11;
			Microsoft::WRL::ComPtr<ID3D12Resource> resource12;
		};

		struct PendingFenceEvent
		{
			HANDLE event = nullptr;
			std::uint64_t fenceValue = 0;
		};

		bool CreateCommandContextLocked(std::size_t a_index);
		bool CreateTimingResourcesLocked();
		void CollectCompletedTimingLocked(
			std::size_t a_index,
			std::uint64_t a_completedValue) noexcept;
		void CollectCompletedTimingsLocked(std::uint64_t a_completedValue) noexcept;
		bool WaitForFenceLocked(std::uint64_t a_value, DWORD a_timeoutMilliseconds, const char* a_operation);
		void RetireCompletedFenceEventsLocked(std::uint64_t a_completedValue);
		bool AbortD3D12Locked();
		bool WaitForIdleLocked();
		bool ShutdownLocked();
		bool RecordFailureLocked(HRESULT a_result, const char* a_operation);
		void ReleaseObjectsLocked();
		void AbandonObjectsLocked() noexcept;

		mutable std::recursive_mutex mutex_;
		Microsoft::WRL::ComPtr<ID3D11Device5> device11_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context11_;
		Microsoft::WRL::ComPtr<ID3D12Device> device12_;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue12_;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence12_;
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampQueryHeap_;
		Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadback_;
		Microsoft::WRL::ComPtr<ID3D11Fence> fence11_;
		std::array<CommandContext, kCommandContextCount> commandContexts_{};
		std::vector<ResourceLease> resourceLeases_;
		std::vector<PendingFenceEvent> pendingFenceEvents_;
		std::uint64_t fenceValue_ = 0;
		std::size_t commandContextCursor_ = 0;
		std::size_t recordingContext_ = kCommandContextCount;
		std::thread::id recordingThread_{};
		D3D12InteropTelemetry telemetry_{};
		std::uint64_t timestampFrequency_ = 0;
		HRESULT lastError_ = S_OK;
		std::string lastOperation_;
		bool initialized_ = false;
		bool recording_ = false;
		bool featureTimingOpen_ = false;
		bool featureTimingCompleted_ = false;
		bool timingRecording_ = false;
		bool unfencedSubmission_ = false;
		bool backpressureLogged_ = false;
		std::atomic_bool abandonRequested_{ false };
	};
}
