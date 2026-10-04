#include "KernelChainCommandList.h"

#include <array>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class FakeList final : public ID3D12GraphicsCommandList
	{
	public:
		std::vector<std::string> events;
		ULONG references = 1;
		std::array<UINT, 3> dispatch{};
		const D3D12_RESOURCE_BARRIER* barrierPointer = nullptr;
		UINT barrierCount = 0;
		const D3D12_TEXTURE_COPY_LOCATION* copyTarget = nullptr;
		const D3D12_TEXTURE_COPY_LOCATION* copySource = nullptr;
		const D3D12_BOX* copyBox = nullptr;
		std::array<UINT, 3> copyOrigin{};
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override { return --references; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** device) override
		{
			if (device)
				*device = nullptr;
			return E_NOINTERFACE;
		}
		D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override { return D3D12_COMMAND_LIST_TYPE_DIRECT; }
		HRESULT STDMETHODCALLTYPE Close() override
		{
			events.emplace_back("real:Close");
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator*, ID3D12PipelineState*) override
		{
			events.emplace_back("real:Reset");
			return S_OK;
		}
		void STDMETHODCALLTYPE Dispatch(UINT x, UINT y, UINT z) override
		{
			events.emplace_back("real:Dispatch");
			dispatch = { x, y, z };
		}
		void STDMETHODCALLTYPE ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers) override
		{
			events.emplace_back("real:ResourceBarrier");
			barrierCount = count;
			barrierPointer = barriers;
		}
		void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* target, UINT x, UINT y,
			UINT z, const D3D12_TEXTURE_COPY_LOCATION* source, const D3D12_BOX* box) override
		{
			events.emplace_back("real:CopyTextureRegion");
			copyTarget = target;
			copySource = source;
			copyBox = box;
			copyOrigin = { x, y, z };
		}
#define MOCK_COMMAND(name, signature, arguments)           \
	void STDMETHODCALLTYPE name signature override         \
	{                                                      \
		RecordArguments(#name, [&] { Ignore arguments; }); \
	}
		MOCK_COMMAND(ClearState, (ID3D12PipelineState * state), (state))
		MOCK_COMMAND(DrawInstanced, (UINT vertices, UINT instances, UINT firstVertex, UINT firstInstance), (vertices, instances, firstVertex, firstInstance))
		MOCK_COMMAND(DrawIndexedInstanced, (UINT indices, UINT instances, UINT firstIndex, INT baseVertex, UINT firstInstance), (indices, instances, firstIndex, baseVertex, firstInstance))
		MOCK_COMMAND(CopyBufferRegion, (ID3D12Resource * target, UINT64 targetOffset, ID3D12Resource* source, UINT64 sourceOffset, UINT64 bytes), (target, targetOffset, source, sourceOffset, bytes))
		MOCK_COMMAND(CopyResource, (ID3D12Resource * target, ID3D12Resource* source), (target, source))
		MOCK_COMMAND(CopyTiles, (ID3D12Resource * tiled, const D3D12_TILED_RESOURCE_COORDINATE* start, const D3D12_TILE_REGION_SIZE* size, ID3D12Resource* buffer, UINT64 offset, D3D12_TILE_COPY_FLAGS flags), (tiled, start, size, buffer, offset, flags))
		MOCK_COMMAND(ResolveSubresource, (ID3D12Resource * target, UINT targetSubresource, ID3D12Resource* source, UINT sourceSubresource, DXGI_FORMAT format), (target, targetSubresource, source, sourceSubresource, format))
		MOCK_COMMAND(IASetPrimitiveTopology, (D3D12_PRIMITIVE_TOPOLOGY topology), (topology))
		MOCK_COMMAND(RSSetViewports, (UINT count, const D3D12_VIEWPORT* values), (count, values))
		MOCK_COMMAND(RSSetScissorRects, (UINT count, const D3D12_RECT* values), (count, values))
		MOCK_COMMAND(OMSetBlendFactor, (const FLOAT factor[4]), (factor))
		MOCK_COMMAND(OMSetStencilRef, (UINT reference), (reference))
		MOCK_COMMAND(SetPipelineState, (ID3D12PipelineState * state), (state))
		MOCK_COMMAND(ExecuteBundle, (ID3D12GraphicsCommandList * bundle), (bundle))
		MOCK_COMMAND(SetDescriptorHeaps, (UINT count, ID3D12DescriptorHeap* const* heaps), (count, heaps))
		MOCK_COMMAND(SetComputeRootSignature, (ID3D12RootSignature * signature), (signature))
		MOCK_COMMAND(SetGraphicsRootSignature, (ID3D12RootSignature * signature), (signature))
		MOCK_COMMAND(SetComputeRootDescriptorTable, (UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value), (index, value))
		MOCK_COMMAND(SetGraphicsRootDescriptorTable, (UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value), (index, value))
		MOCK_COMMAND(SetComputeRoot32BitConstant, (UINT index, UINT value, UINT offset), (index, value, offset))
		MOCK_COMMAND(SetGraphicsRoot32BitConstant, (UINT index, UINT value, UINT offset), (index, value, offset))
		MOCK_COMMAND(SetComputeRoot32BitConstants, (UINT index, UINT count, const void* values, UINT offset), (index, count, values, offset))
		MOCK_COMMAND(SetGraphicsRoot32BitConstants, (UINT index, UINT count, const void* values, UINT offset), (index, count, values, offset))
		MOCK_COMMAND(SetComputeRootConstantBufferView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(SetGraphicsRootConstantBufferView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(SetComputeRootShaderResourceView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(SetGraphicsRootShaderResourceView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(SetComputeRootUnorderedAccessView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(SetGraphicsRootUnorderedAccessView, (UINT index, D3D12_GPU_VIRTUAL_ADDRESS address), (index, address))
		MOCK_COMMAND(IASetIndexBuffer, (const D3D12_INDEX_BUFFER_VIEW* view), (view))
		MOCK_COMMAND(IASetVertexBuffers, (UINT first, UINT count, const D3D12_VERTEX_BUFFER_VIEW* views), (first, count, views))
		MOCK_COMMAND(SOSetTargets, (UINT first, UINT count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* views), (first, count, views))
		MOCK_COMMAND(OMSetRenderTargets, (UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* targets, BOOL contiguous, const D3D12_CPU_DESCRIPTOR_HANDLE* depth), (count, targets, contiguous, depth))
		MOCK_COMMAND(ClearDepthStencilView, (D3D12_CPU_DESCRIPTOR_HANDLE view, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT count, const D3D12_RECT* rects), (view, flags, depth, stencil, count, rects))
		MOCK_COMMAND(ClearRenderTargetView, (D3D12_CPU_DESCRIPTOR_HANDLE view, const FLOAT color[4], UINT count, const D3D12_RECT* rects), (view, color, count, rects))
		MOCK_COMMAND(ClearUnorderedAccessViewUint, (D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource* resource, const UINT values[4], UINT count, const D3D12_RECT* rects), (gpu, cpu, resource, values, count, rects))
		MOCK_COMMAND(ClearUnorderedAccessViewFloat, (D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu, ID3D12Resource* resource, const FLOAT values[4], UINT count, const D3D12_RECT* rects), (gpu, cpu, resource, values, count, rects))
		MOCK_COMMAND(DiscardResource, (ID3D12Resource * resource, const D3D12_DISCARD_REGION* region), (resource, region))
		MOCK_COMMAND(BeginQuery, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT index), (heap, type, index))
		MOCK_COMMAND(EndQuery, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT index), (heap, type, index))
		MOCK_COMMAND(ResolveQueryData, (ID3D12QueryHeap * heap, D3D12_QUERY_TYPE type, UINT first, UINT count, ID3D12Resource* target, UINT64 offset), (heap, type, first, count, target, offset))
		MOCK_COMMAND(SetPredication, (ID3D12Resource * buffer, UINT64 offset, D3D12_PREDICATION_OP operation), (buffer, offset, operation))
		MOCK_COMMAND(SetMarker, (UINT metadata, const void* data, UINT bytes), (metadata, data, bytes))
		MOCK_COMMAND(BeginEvent, (UINT metadata, const void* data, UINT bytes), (metadata, data, bytes))
		MOCK_COMMAND(EndEvent, (), ())
		MOCK_COMMAND(ExecuteIndirect, (ID3D12CommandSignature * signature, UINT maximum, ID3D12Resource* arguments, UINT64 argumentOffset, ID3D12Resource* count, UINT64 countOffset), (signature, maximum, arguments, argumentOffset, count, countOffset))
#undef MOCK_COMMAND
	private:
		template <class... Args>
		static void Ignore(Args&&...)
		{}
		template <class Action>
		void RecordArguments(const char* name, Action&& action)
		{
			action();
			events.emplace_back(std::string("real:") + name);
		}
	};
}

int main()
{
	try {
		FakeList real;
		{
			NrReplay::KernelChainCommandList proxy(&real, [&](std::string_view name) {
				real.events.emplace_back(std::string("flush:") + std::string(name));
				return true;
			});
			Require(real.references == 2 && proxy.Real() == &real, "proxy must retain the exact underlying list");
			void* identity = nullptr;
			Require(proxy.QueryInterface(__uuidof(IUnknown), &identity) == S_OK &&
						identity == static_cast<ID3D12GraphicsCommandList*>(&proxy),
				"IUnknown must preserve proxy identity");
			Require(proxy.OutstandingReferences() == 2, "QI reference was not retained");
			static_cast<IUnknown*>(identity)->Release();
			Require(proxy.OutstandingReferences() == 1 && proxy.Healthy(), "QI reference did not retire");
			Require(proxy.GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT && real.events.empty(), "read-only getters must not flush");
			proxy.Dispatch(7, 11, 13);
			Require(real.dispatch == std::array<UINT, 3>{ 7, 11, 13 }, "dispatch arguments changed");
			D3D12_RESOURCE_BARRIER barriers[2]{};
			proxy.ResourceBarrier(2, barriers);
			Require(real.barrierCount == 2 && real.barrierPointer == barriers, "barrier batch changed");
			D3D12_TEXTURE_COPY_LOCATION source{}, target{};
			D3D12_BOX box{};
			proxy.CopyTextureRegion(&target, 17, 19, 23, &source, &box);
			Require(real.copyTarget == &target && real.copySource == &source && real.copyBox == &box &&
						real.copyOrigin == std::array<UINT, 3>{ 17, 19, 23 },
				"copy command arguments changed");
			proxy.EndQuery(nullptr, D3D12_QUERY_TYPE_TIMESTAMP, 5);
			Require(proxy.Close() == S_OK, "close did not forward");
			Require(real.events == std::vector<std::string>{ "flush:Dispatch", "real:Dispatch",
									   "flush:ResourceBarrier", "real:ResourceBarrier", "flush:CopyTextureRegion", "real:CopyTextureRegion",
									   "flush:EndQuery", "real:EndQuery", "flush:Close", "real:Close" },
				"pending launch flush must precede every command");
		}
		Require(real.references == 1, "underlying reference was not released");
		real.events.clear();
		{
			D3D12_RESOURCE_BARRIER barrier{};
			ID3D12DescriptorHeap* heaps[1]{};
			NrReplay::KernelChainCommandList proxy(&real, [&](std::string_view name) {
				real.events.emplace_back("flush:" + std::string(name));
				return true; }, {}, [&](UINT count, const D3D12_RESOURCE_BARRIER* values) {
				Require(count == 1 && values == &barrier, "observer changed the barrier arguments");
				real.events.emplace_back("observe:ResourceBarrier"); }, [&](UINT count, ID3D12DescriptorHeap* const* values) {
				Require(count == 1 && values == heaps, "observer changed the heap arguments");
				real.events.emplace_back("observe:SetDescriptorHeaps"); });
			proxy.ResourceBarrier(1, &barrier);
			proxy.SetDescriptorHeaps(1, heaps);
			Require(real.events == std::vector<std::string>{ "flush:ResourceBarrier", "observe:ResourceBarrier", "real:ResourceBarrier",
									   "flush:SetDescriptorHeaps", "observe:SetDescriptorHeaps", "real:SetDescriptorHeaps" },
				"observers must run after flush and before unchanged commands");
		}
		real.events.clear();
		{
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) { return true; }, {}, [](UINT, const D3D12_RESOURCE_BARRIER*) { throw std::runtime_error("capture failed"); });
			proxy.ResourceBarrier(0, nullptr);
			proxy.SetDescriptorHeaps(0, nullptr);
			Require(!proxy.Healthy() && real.events.empty(), "failed observer must latch failure and suppress commands");
		}
		{
			using Disposition = NrReplay::KernelChainCommandList::CommandDisposition;
			D3D12_RESOURCE_BARRIER barrier{};
			ID3D12DescriptorHeap* heaps[1]{};
			NrReplay::KernelChainCommandList proxy(&real, [&](std::string_view name) {
				real.events.emplace_back("boundary:" + std::string(name));
				return true; }, {}, [&](UINT, const D3D12_RESOURCE_BARRIER*) { real.events.emplace_back("observe:barriers"); }, [&](UINT, ID3D12DescriptorHeap* const*) { real.events.emplace_back("observe:heaps"); }, [&](UINT count, const D3D12_RESOURCE_BARRIER* values) {
				Require(count == 1 && values == &barrier, "recording changed barrier arguments");
				real.events.emplace_back("record:barriers");
				return Disposition::Recorded; }, [&](UINT count, ID3D12DescriptorHeap* const* values) {
				Require(count == 1 && values == heaps, "recording changed heap arguments");
				real.events.emplace_back("record:heaps");
				return Disposition::Recorded; });
			proxy.ResourceBarrier(1, &barrier);
			proxy.SetDescriptorHeaps(1, heaps);
			Require(proxy.Healthy() && real.events == std::vector<std::string>{
														  "boundary:ResourceBarrier", "observe:barriers", "record:barriers",
														  "boundary:SetDescriptorHeaps", "observe:heaps", "record:heaps" },
				"recorded commands must stay healthy without immediate forwarding");
		}
		real.events.clear();
		for (const auto disposition : { NrReplay::KernelChainCommandList::CommandDisposition::Reject,
				 static_cast<NrReplay::KernelChainCommandList::CommandDisposition>(99) }) {
			std::size_t calls = 0;
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) { return true; }, {}, {}, {}, [&](UINT, const D3D12_RESOURCE_BARRIER*) { ++calls; return disposition; });
			proxy.ResourceBarrier(0, nullptr);
			proxy.Dispatch(1, 1, 1);
			Require(!proxy.Healthy() && calls == 1 && real.events.empty(), "rejected or invalid disposition must fail closed");
		}
		{
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) { return true; }, {}, {}, {}, {}, [](UINT, ID3D12DescriptorHeap* const*) -> NrReplay::KernelChainCommandList::CommandDisposition { throw std::runtime_error("recording failed"); });
			proxy.SetDescriptorHeaps(0, nullptr);
			proxy.ResourceBarrier(0, nullptr);
			Require(!proxy.Healthy() && real.events.empty(), "recording exceptions must suppress subsequent commands");
		}
		{
			std::size_t callbacks = 0;
			NrReplay::KernelChainCommandList proxy(&real, [&](std::string_view) { ++callbacks; return false; });
			proxy.Dispatch(1, 1, 1);
			proxy.ResourceBarrier(0, nullptr);
			Require(proxy.Close() == E_FAIL && !proxy.Healthy() && callbacks == 1 && real.events.empty(),
				"a failed flush must latch failure and suppress subsequent commands");
		}
		{
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) -> bool { throw std::runtime_error("test"); });
			proxy.Dispatch(1, 1, 1);
			Require(!proxy.Healthy() && real.events.empty(), "exceptions must not cross the COM boundary or forward commands");
		}
		{
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) { return true; });
			void* unsupported = reinterpret_cast<void*>(1);
			Require(proxy.QueryInterface(__uuidof(ID3D12GraphicsCommandList1), &unsupported) == E_NOINTERFACE &&
						unsupported == nullptr && !proxy.Healthy(),
				"unknown interfaces must not expose an unobserved real list");
			proxy.Dispatch(1, 1, 1);
			Require(real.events.empty(), "unknown interface failure must suppress submission");
		}
		{
			NrReplay::KernelChainCommandList proxy(&real, [](std::string_view) { return true; });
			Require(proxy.Release() == 1 && !proxy.Healthy(), "borrower must not release caller-owned reference");
		}
		{
			std::size_t callbackCalls = 0;
			const auto retainedBefore = NrReplay::KernelChainCommandList::RetainedUntilExitCount();
			using Owner = std::unique_ptr<NrReplay::KernelChainCommandList, NrReplay::KernelChainCommandList::Deleter>;
			Owner owner(new NrReplay::KernelChainCommandList(&real, [&](std::string_view) {
                ++callbackCalls;
                return true; }, [&](std::string_view) { ++callbackCalls; }));
			void* retained = nullptr;
			Require(owner->QueryInterface(__uuidof(ID3D12GraphicsCommandList), &retained) == S_OK,
				"retained test interface was not acquired");
			owner.reset();
			Require(NrReplay::KernelChainCommandList::RetainedUntilExitCount() == retainedBefore + 1,
				"provider-retained proxy must be preserved until process exit");
			const auto callbacksAtRetirement = callbackCalls;
			auto* retainedList = static_cast<ID3D12GraphicsCommandList*>(retained);
			retainedList->Dispatch(1, 1, 1);
			Require(callbackCalls == callbacksAtRetirement && real.events.empty(),
				"abandoned proxy must not call expired callbacks or forward commands");
			Require(retainedList->Release() == 1, "late provider release must reach a valid retained proxy");
			Require(real.references == 2, "retained proxy must keep the real list alive");
		}
		std::cout << "Kernel chain command-list CPU checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
