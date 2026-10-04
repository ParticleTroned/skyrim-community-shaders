#pragma once

#include <atomic>
#include <d3d12.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <wrl/client.h>

namespace NrReplay
{
	/** Caller-owned replay proxy; every GPU command first retires pending native launches. */
	class KernelChainCommandList final : public ID3D12GraphicsCommandList
	{
	public:
		enum class CommandDisposition
		{
			Forward,
			Recorded,
			Reject
		};
		using BoundaryCallback = std::function<bool(std::string_view)>;
		using FailureCallback = std::function<void(std::string_view)>;
		using BarrierCallback = std::function<void(UINT, const D3D12_RESOURCE_BARRIER*)>;
		using HeapCallback = std::function<void(UINT, ID3D12DescriptorHeap* const*)>;
		using BarrierDispositionCallback = std::function<CommandDisposition(UINT, const D3D12_RESOURCE_BARRIER*)>;
		using HeapDispositionCallback = std::function<CommandDisposition(UINT, ID3D12DescriptorHeap* const*)>;
		struct Deleter
		{
			void operator()(KernelChainCommandList* proxy) const noexcept
			{
				if (!proxy)
					return;
				if (proxy->OutstandingReferences() != 1) {
					proxy->AbandonCallbacks();
					++retainedUntilExit_;
				} else {
					delete proxy;
				}
			}
		};
		KernelChainCommandList(ID3D12GraphicsCommandList* real, BoundaryCallback boundary, FailureCallback failure = {},
			BarrierCallback barriers = {}, HeapCallback heaps = {}, BarrierDispositionCallback barrierDisposition = {}, HeapDispositionCallback heapDisposition = {}) :
			real_(real), boundary_(std::move(boundary)), failure_(std::move(failure)), barriers_(std::move(barriers)), heaps_(std::move(heaps)),
			barrierDisposition_(std::move(barrierDisposition)), heapDisposition_(std::move(heapDisposition))
		{
			if (!real || !boundary_)
				throw std::invalid_argument("kernel chain proxy requires a real list and command boundary callback");
		}
		KernelChainCommandList(const KernelChainCommandList&) = delete;
		KernelChainCommandList& operator=(const KernelChainCommandList&) = delete;
		~KernelChainCommandList()
		{
			if (references_.load() != 1)
				Fail("kernel chain proxy destroyed with outstanding COM references");
		}

		ID3D12GraphicsCommandList* Real() const noexcept { return real_.Get(); }
		bool Healthy() const noexcept { return healthy_; }
		std::string_view FailureReason() const noexcept { return reason_; }
		ULONG OutstandingReferences() const noexcept { return references_.load(); }
		static std::size_t RetainedUntilExitCount() noexcept { return retainedUntilExit_.load(); }
		/** Disconnects caller captures before retaining a provider-referenced proxy until exit. */
		void AbandonCallbacks() noexcept
		{
			Fail("kernel chain proxy retained until process exit because COM references remain");
			boundary_ = nullptr;
			failure_ = nullptr;
			barriers_ = nullptr;
			heaps_ = nullptr;
			barrierDisposition_ = nullptr;
			heapDisposition_ = nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
		{
			if (!output)
				return E_POINTER;
			*output = nullptr;
			if (id == __uuidof(IUnknown) || id == __uuidof(ID3D12Object) || id == __uuidof(ID3D12DeviceChild) ||
				id == __uuidof(ID3D12CommandList) || id == __uuidof(ID3D12GraphicsCommandList)) {
				*output = static_cast<ID3D12GraphicsCommandList*>(this);
				AddRef();
				return S_OK;
			}
			Fail("kernel chain proxy rejected an unobserved command-list interface");
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			auto count = references_.load();
			while (count > 1) {
				if (references_.compare_exchange_weak(count, count - 1))
					return count - 1;
			}
			Fail("kernel chain proxy caller-owned reference was released");
			return count;
		}
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* bytes, void* data) override
		{
			return real_->GetPrivateData(guid, bytes, data);
		}
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT bytes, const void* data) override
		{
			return real_->SetPrivateData(guid, bytes, data);
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override
		{
			return real_->SetPrivateDataInterface(guid, data);
		}
		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override { return real_->SetName(name); }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** device) override { return real_->GetDevice(id, device); }
		D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override { return real_->GetType(); }
		HRESULT STDMETHODCALLTYPE Close() override { return Before("Close") ? real_->Close() : E_FAIL; }
		HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* allocator, ID3D12PipelineState* state) override
		{
			return Before("Reset") ? real_->Reset(allocator, state) : E_FAIL;
		}

#define NR_CHAIN_FORWARD(name, signature, arguments) \
	void STDMETHODCALLTYPE name signature override   \
	{                                                \
		if (Before(#name))                           \
			real_->name arguments;                   \
	}

		NR_CHAIN_FORWARD(ClearState, (ID3D12PipelineState * state), (state))
		NR_CHAIN_FORWARD(DrawInstanced, (UINT vertices, UINT instances, UINT firstVertex, UINT firstInstance), (vertices, instances, firstVertex, firstInstance))
		NR_CHAIN_FORWARD(DrawIndexedInstanced, (UINT indices, UINT instances, UINT firstIndex, INT baseVertex, UINT firstInstance), (indices, instances, firstIndex, baseVertex, firstInstance))
		NR_CHAIN_FORWARD(Dispatch, (UINT x, UINT y, UINT z), (x, y, z))
		NR_CHAIN_FORWARD(CopyBufferRegion, (ID3D12Resource * target, UINT64 targetOffset, ID3D12Resource* source, UINT64 sourceOffset, UINT64 bytes), (target, targetOffset, source, sourceOffset, bytes))
		NR_CHAIN_FORWARD(CopyTextureRegion, (const D3D12_TEXTURE_COPY_LOCATION* target, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* source, const D3D12_BOX* box), (target, x, y, z, source, box))
		NR_CHAIN_FORWARD(CopyResource, (ID3D12Resource * target, ID3D12Resource* source), (target, source))
		NR_CHAIN_FORWARD(CopyTiles, (ID3D12Resource * tiled, const D3D12_TILED_RESOURCE_COORDINATE* start, const D3D12_TILE_REGION_SIZE* size, ID3D12Resource* buffer, UINT64 offset, D3D12_TILE_COPY_FLAGS flags), (tiled, start, size, buffer, offset, flags))
		NR_CHAIN_FORWARD(ResolveSubresource, (ID3D12Resource * target, UINT targetSubresource, ID3D12Resource* source, UINT sourceSubresource, DXGI_FORMAT format), (target, targetSubresource, source, sourceSubresource, format))
		NR_CHAIN_FORWARD(IASetPrimitiveTopology, (D3D12_PRIMITIVE_TOPOLOGY topology), (topology))
		NR_CHAIN_FORWARD(RSSetViewports, (UINT count, const D3D12_VIEWPORT* values), (count, values))
		NR_CHAIN_FORWARD(RSSetScissorRects, (UINT count, const D3D12_RECT* values), (count, values))
		NR_CHAIN_FORWARD(OMSetBlendFactor, (const FLOAT factor[4]), (factor))
		NR_CHAIN_FORWARD(OMSetStencilRef, (UINT reference), (reference))
		NR_CHAIN_FORWARD(SetPipelineState, (ID3D12PipelineState * state), (state))
		void STDMETHODCALLTYPE ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers) override
		{
			if (Before("ResourceBarrier") && Observe(barriers_, count, barriers) && Forward(barrierDisposition_, count, barriers))
				real_->ResourceBarrier(count, barriers);
		}
		NR_CHAIN_FORWARD(ExecuteBundle, (ID3D12GraphicsCommandList * bundle), (bundle))
		void STDMETHODCALLTYPE SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap* const* heaps) override
		{
			if (Before("SetDescriptorHeaps") && Observe(heaps_, count, heaps) && Forward(heapDisposition_, count, heaps))
				real_->SetDescriptorHeaps(count, heaps);
		}
		NR_CHAIN_FORWARD(SetComputeRootSignature, (ID3D12RootSignature * signature), (signature))
		NR_CHAIN_FORWARD(SetGraphicsRootSignature, (ID3D12RootSignature * signature), (signature))
		NR_CHAIN_FORWARD(SetComputeRootDescriptorTable, (UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value), (index, value))
		NR_CHAIN_FORWARD(SetGraphicsRootDescriptorTable, (UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value), (index, value))
		NR_CHAIN_FORWARD(SetComputeRoot32BitConstant, (UINT index, UINT value, UINT offset), (index, value, offset))
		NR_CHAIN_FORWARD(SetGraphicsRoot32BitConstant, (UINT index, UINT value, UINT offset), (index, value, offset))
		NR_CHAIN_FORWARD(SetComputeRoot32BitConstants, (UINT index, UINT count, const void* values, UINT offset), (index, count, values, offset))
		NR_CHAIN_FORWARD(SetGraphicsRoot32BitConstants, (UINT index, UINT count, const void* values, UINT offset), (index, count, values, offset))
		NR_CHAIN_FORWARD(SetComputeRootConstantBufferView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(SetGraphicsRootConstantBufferView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(SetComputeRootShaderResourceView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(SetGraphicsRootShaderResourceView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(SetComputeRootUnorderedAccessView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(SetGraphicsRootUnorderedAccessView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		NR_CHAIN_FORWARD(IASetIndexBuffer, (const D3D12_INDEX_BUFFER_VIEW* view), (view))
		NR_CHAIN_FORWARD(IASetVertexBuffers, (UINT first, UINT count, const D3D12_VERTEX_BUFFER_VIEW* views), (first, count, views))
		NR_CHAIN_FORWARD(SOSetTargets, (UINT first, UINT count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* views), (first, count, views))
		NR_CHAIN_FORWARD(OMSetRenderTargets, (UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* targets, BOOL contiguous, const D3D12_CPU_DESCRIPTOR_HANDLE* depth), (count, targets, contiguous, depth))
		NR_CHAIN_FORWARD(ClearDepthStencilView, (D3D12_CPU_DESCRIPTOR_HANDLE view, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT count, const D3D12_RECT* rects), (view, flags, depth, stencil, count, rects))
		NR_CHAIN_FORWARD(ClearRenderTargetView, (D3D12_CPU_DESCRIPTOR_HANDLE view, const FLOAT color[4], UINT count, const D3D12_RECT* rects), (view, color, count, rects))
		NR_CHAIN_FORWARD(ClearUnorderedAccessViewUint, (D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource* resource, const UINT values[4], UINT count, const D3D12_RECT* rects), (gpu, cpu, resource, values, count, rects))
		NR_CHAIN_FORWARD(ClearUnorderedAccessViewFloat, (D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource* resource, const FLOAT values[4], UINT count, const D3D12_RECT* rects), (gpu, cpu, resource, values, count, rects))
		NR_CHAIN_FORWARD(DiscardResource, (ID3D12Resource * resource, const D3D12_DISCARD_REGION* region), (resource, region))
		NR_CHAIN_FORWARD(BeginQuery, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT index), (heap, type, index))
		NR_CHAIN_FORWARD(EndQuery, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT index), (heap, type, index))
		NR_CHAIN_FORWARD(ResolveQueryData, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT first, UINT count, ID3D12Resource* target, UINT64 offset), (heap, type, first, count, target, offset))
		NR_CHAIN_FORWARD(SetPredication, (ID3D12Resource * buffer, UINT64 offset, D3D12_PREDICATION_OP operation), (buffer, offset, operation))
		NR_CHAIN_FORWARD(SetMarker, (UINT metadata, const void* data, UINT bytes), (metadata, data, bytes))
		NR_CHAIN_FORWARD(BeginEvent, (UINT metadata, const void* data, UINT bytes), (metadata, data, bytes))
		NR_CHAIN_FORWARD(EndEvent, (), ())
		NR_CHAIN_FORWARD(ExecuteIndirect, (ID3D12CommandSignature * signature, UINT maximum, ID3D12Resource* arguments, UINT64 argumentOffset, ID3D12Resource* count, UINT64 countOffset), (signature, maximum, arguments, argumentOffset, count, countOffset))

#undef NR_CHAIN_FORWARD

	private:
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> real_;
		BoundaryCallback boundary_;
		FailureCallback failure_;
		BarrierCallback barriers_;
		HeapCallback heaps_;
		BarrierDispositionCallback barrierDisposition_;
		HeapDispositionCallback heapDisposition_;
		std::atomic<ULONG> references_{ 1 };
		inline static std::atomic<std::size_t> retainedUntilExit_{ 0 };
		bool healthy_ = true;
		std::string reason_;
		template <class Callback, class... Args>
		bool Forward(const Callback& callback, Args... args) noexcept
		{
			try {
				if (!callback)
					return true;
				switch (callback(args...)) {
				case CommandDisposition::Forward:
					return true;
				case CommandDisposition::Recorded:
					return false;
				case CommandDisposition::Reject:
					break;
				}
				Fail("kernel chain command disposition rejected");
			} catch (...) {
				Fail("kernel chain command disposition failed");
			}
			return false;
		}
		template <class Callback, class... Args>
		bool Observe(const Callback& callback, Args... args) noexcept
		{
			try {
				if (callback)
					callback(args...);
				return true;
			} catch (...) {
				Fail("kernel chain command observation failed");
				return false;
			}
		}

		void Fail(std::string_view reason) noexcept
		{
			healthy_ = false;
			try {
				if (reason_.empty())
					reason_ = reason;
				if (failure_)
					failure_(reason);
			} catch (...) {
				// Failure remains latched even if the receipt callback cannot allocate.
			}
		}
		bool Before(std::string_view name) noexcept
		{
			if (!healthy_)
				return false;
			try {
				if (boundary_(name))
					return true;
			} catch (...) {
				Fail("kernel chain command boundary threw an exception");
				return false;
			}
			Fail("kernel chain command boundary did not flush successfully");
			return false;
		}
	};
}
