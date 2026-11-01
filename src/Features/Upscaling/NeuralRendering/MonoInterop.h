#pragma once

#include "SubmissionFenceSnapshot.h"

#include <cstdint>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	struct MonoSharedTexture
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
		Microsoft::WRL::ComPtr<ID3D12Resource> shared;
		void Abandon() noexcept;
	};

	/** One mono command stream; GPU waits order APIs and retirement polls never block a game frame. */
	class MonoInterop
	{
	public:
		bool Initialize(ID3D11Device*, ID3D11DeviceContext*);
		HRESULT CreateTexture(std::uint32_t width, std::uint32_t height, DXGI_FORMAT, MonoSharedTexture&, const char* name);
		ID3D12GraphicsCommandList* Begin();
		bool Submit();
		void BeginGpuTiming(std::uint64_t captureEpoch, std::uint32_t frame);
		void EndGpuTiming();
		void PollGpuTiming();
		/** Enqueue the D3D11 tail once, then return true only when both APIs have completed it. */
		bool PollRetired();
		void ReleaseRetired();
		void Abandon() noexcept;
		[[nodiscard]] ID3D12Device* Device() const noexcept { return device12_.Get(); }
		[[nodiscard]] bool Initialized() const noexcept { return device12_ != nullptr; }
		[[nodiscard]] bool OwnsQueuedWork() const noexcept { return value_ != 0 || recording_; }
		[[nodiscard]] bool Failed() const noexcept { return failed_; }
		[[nodiscard]] HRESULT Error() const noexcept { return error_; }
		[[nodiscard]] bool Matches(ID3D11Device*) const noexcept;
		[[nodiscard]] LifetimeFenceSnapshot Snapshot() const noexcept;
		[[nodiscard]] bool Stalled(std::uint64_t now) const noexcept;

	private:
		bool Check(HRESULT) noexcept;
		Microsoft::WRL::ComPtr<ID3D11Device5> device11_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context11_;
		Microsoft::WRL::ComPtr<ID3D12Device> device12_;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands_;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence12_;
		Microsoft::WRL::ComPtr<ID3D11Fence> fence11_;
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
		Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadback_;
		std::uint64_t timestampFrequency_ = 0, timestampFence_ = 0, timestampEpoch_ = 0;
		std::uint32_t timestampFrame_ = UINT32_MAX;
		bool timestampActive_ = false, timestampRecorded_ = false, timestampUnavailable_ = false;
		std::uint64_t value_ = 0, lastSubmission_ = 0, retirement_ = 0, pendingSinceMs_ = 0;
		bool recording_ = false, failed_ = false;
		HRESULT error_ = S_OK;
	};
}
