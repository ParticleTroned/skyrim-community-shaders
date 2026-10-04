#include "KernelCommandCapture.h"

#include <array>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	using Capture = NrReplay::KernelCommandCapture;
	using Json = nlohmann::json;
	unsigned checks = 0;
	void Check(bool condition)
	{
		++checks;
		if (!condition)
			throw std::runtime_error("command capture assertion failed at " + std::to_string(checks));
	}
	template <class Function>
	void Reject(Function&& function)
	{
		try {
			function();
		} catch (const std::exception&) {
			++checks;
			return;
		}
		throw std::runtime_error("invalid command capture was accepted");
	}
	class FakeHeap final : public ID3D12DescriptorHeap
	{
	public:
		D3D12_DESCRIPTOR_HEAP_DESC descriptor{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65537, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 3 };
		unsigned reads = 0;
		bool fail = false;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc() override
		{
			++reads;
			if (fail)
				throw std::runtime_error("fake descriptor query rejected");
			return descriptor;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart() override { return {}; }
		D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart() override { return {}; }
	};
	class FakeResource final : public ID3D12Resource
	{
	public:
		D3D12_RESOURCE_DESC descriptor{ D3D12_RESOURCE_DIMENSION_BUFFER, 65536, 4096, 1, 1, 1, DXGI_FORMAT_UNKNOWN,
			{ 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
		D3D12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			D3D12_MEMORY_POOL_UNKNOWN, 1, 3 };
		D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_SHARED;
		D3D12_GPU_VIRTUAL_ADDRESS address = 0x100000;
		HRESULT heapResult = S_OK;
		unsigned descReads = 0, addressReads = 0, heapReads = 0;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** output) override
		{
			if (output)
				*output = nullptr;
			return E_NOINTERFACE;
		}
		HRESULT STDMETHODCALLTYPE Map(UINT, const D3D12_RANGE*, void**) override { return E_NOTIMPL; }
		void STDMETHODCALLTYPE Unmap(UINT, const D3D12_RANGE*) override {}
		D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override
		{
			++descReads;
			return descriptor;
		}
		D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override
		{
			++addressReads;
			return address;
		}
		HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT, const D3D12_BOX*, const void*, UINT, UINT) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*, UINT, UINT, UINT, const D3D12_BOX*) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* output, D3D12_HEAP_FLAGS* flags) override
		{
			++heapReads;
			*output = properties;
			*flags = heapFlags;
			return heapResult;
		}
	};

	D3D12_RESOURCE_BARRIER Uav()
	{
		D3D12_RESOURCE_BARRIER value{};
		value.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		return value;
	}
	void TestFieldsAndOwnership()
	{
		Json sample;
		Capture capture(sample);
		capture.SetEvaluation(1, 3);
		std::array<D3D12_RESOURCE_BARRIER, 3> barriers{};
		barriers[0] = Uav();
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
		barriers[1].Transition = { reinterpret_cast<ID3D12Resource*>(0x1234), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
		barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
		barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
		barriers[2].Aliasing = { reinterpret_cast<ID3D12Resource*>(0x5678), nullptr };
		capture.BeforeCommand();
		capture.Barriers(static_cast<UINT>(barriers.size()), barriers.data());
		FakeHeap heap;
		ID3D12DescriptorHeap* heaps[]{ &heap };
		capture.BeforeCommand();
		capture.Heaps(1, heaps);
		capture.BeforeCommand();
		capture.Barriers(0, nullptr);
		capture.Complete();
		barriers[1].Transition.Subresource = 42;
		heap.descriptor.NumDescriptors = 0;
		const auto& details = sample["kernelCommandDetails"];
		Check(details.size() == 3 && details[0]["commandOrdinal"] == 1 && details[1]["commandOrdinal"] == 2);
		Check(details[0]["eye"] == 1 && details[0]["region"] == 3 && details[0]["command"] == "ResourceBarrier");
		const auto& saved = details[0]["barriers"];
		Check(saved[0] == Json({ { "type", D3D12_RESOURCE_BARRIER_TYPE_UAV }, { "flags", 0 }, { "resource", 0 } }));
		Check(saved[1] == Json({ { "type", D3D12_RESOURCE_BARRIER_TYPE_TRANSITION }, { "flags", D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY },
							  { "resource", 0x1234 }, { "subresource", D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES }, { "before", D3D12_RESOURCE_STATE_COPY_DEST },
							  { "after", D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE } }));
		Check(saved[2] == Json({ { "type", D3D12_RESOURCE_BARRIER_TYPE_ALIASING }, { "flags", D3D12_RESOURCE_BARRIER_FLAG_END_ONLY },
							  { "beforeResource", 0x5678 }, { "afterResource", 0 } }));
		Check(details[1]["heaps"][0] == Json({ { "heap", reinterpret_cast<std::uintptr_t>(&heap) }, { "type", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
											{ "count", 65537 }, { "flags", D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE }, { "nodeMask", 3 } }));
		Check(details[2]["barriers"].empty() && heap.reads == 1);
		const auto& receipt = sample["kernelCommandCapture"];
		Check(receipt["commands"] == 3 && receipt["details"] == 3 && receipt["barriers"] == 3 && receipt["descriptorHeaps"] == 1);
		Check(receipt["failed"] == false && receipt["status"] == "command_recording_complete" && receipt["timingQualified"] == false);
	}
	void TestMalformedCommands()
	{
		for (unsigned scenario = 0; scenario < 8; ++scenario) {
			Json sample;
			Capture capture(sample);
			if (scenario != 0)
				capture.BeforeCommand();
			FakeHeap heap;
			ID3D12DescriptorHeap* heaps[]{ scenario == 5 ? nullptr : &heap };
			std::array barriers{ Uav(), Uav() };
			barriers[1].Type = static_cast<D3D12_RESOURCE_BARRIER_TYPE>(99);
			Reject([&] {
				switch (scenario) {
				case 0:
					capture.Barriers(0, nullptr);
					break;
				case 1:
					capture.Barriers(1, nullptr);
					break;
				case 2:
					capture.Barriers(Capture::kMaximumBarriersPerCommand + 1, barriers.data());
					break;
				case 3:
					capture.Barriers(2, barriers.data());
					break;
				case 4:
					capture.Heaps(1, nullptr);
					break;
				case 5:
					capture.Heaps(1, heaps);
					break;
				case 6:
					capture.Heaps(3, heaps);
					break;
				case 7:
					heap.fail = true;
					capture.Heaps(1, heaps);
					break;
				}
			});
			Check(sample["kernelCommandDetails"].empty());
			Check(sample["kernelCommandCapture"]["failed"] == true && sample["kernelCommandCapture"].contains("failure"));
			const auto original = sample["kernelCommandCapture"]["failure"];
			capture.RecordFailure("later generic proxy failure");
			Reject([&] { capture.BeforeCommand(); });
			Check(sample["kernelCommandCapture"]["failure"] == original);
		}
	}
	void TestResourceExtents()
	{
		Json sample;
		Capture capture(sample, true);
		FakeResource buffer, texture, unavailable;
		texture.descriptor = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 65536, 1008, 1120, 2, 3, DXGI_FORMAT_R16G16B16A16_FLOAT,
			{ 4, 7 }, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
		unavailable.heapResult = E_INVALIDARG;
		unavailable.address = std::numeric_limits<UINT64>::max() - 4;
		std::array<D3D12_RESOURCE_BARRIER, 4> barriers{ Uav(), Uav(), {}, {} };
		barriers[1].UAV.pResource = &buffer;
		barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[2].Transition = { &texture, 0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET };
		barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
		barriers[3].Aliasing = { &unavailable, nullptr };
		capture.BeforeCommand();
		capture.Barriers(static_cast<UINT>(barriers.size()), barriers.data());
		capture.Complete();
		const auto& saved = sample["kernelCommandDetails"][0]["barriers"];
		Check(saved[0]["resourceInfo"].is_null());
		const auto& info = saved[1]["resourceInfo"];
		Check(info["gpuVirtualAddress"] == 0x100000 && info["bufferRangeEndExclusive"] == 0x101000 && info["bufferRangeAvailable"] == true);
		Check(info["allocationAliasingProven"] == false && info["scope"] == "resource_identity_and_buffer_virtual_range_only");
		Check(info["heapProperties"]["hresult"] == S_OK && info["heapProperties"]["available"] == true);
		Check(info["heapProperties"]["properties"] == Json({ { "type", D3D12_HEAP_TYPE_DEFAULT }, { "cpuPageProperty", D3D12_CPU_PAGE_PROPERTY_UNKNOWN },
														  { "memoryPoolPreference", D3D12_MEMORY_POOL_UNKNOWN }, { "creationNodeMask", 1 }, { "visibleNodeMask", 3 } }));
		Check(info["heapProperties"]["flags"] == D3D12_HEAP_FLAG_SHARED);
		const auto& textureInfo = saved[2]["resourceInfo"];
		Check(textureInfo["gpuVirtualAddress"].is_null() && textureInfo["bufferRangeEndExclusive"].is_null() && textureInfo["bufferRangeAvailable"] == false);
		Check(textureInfo["bufferRangeUnavailableReason"] == "resource_is_not_a_buffer");
		Check(textureInfo["description"] == Json({ { "dimension", D3D12_RESOURCE_DIMENSION_TEXTURE2D }, { "alignment", 65536 }, { "width", 1008 },
												{ "height", 1120 }, { "depthOrArraySize", 2 }, { "mipLevels", 3 }, { "format", DXGI_FORMAT_R16G16B16A16_FLOAT },
												{ "sampleCount", 4 }, { "sampleQuality", 7 }, { "layout", D3D12_TEXTURE_LAYOUT_UNKNOWN }, { "flags", D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET } }));
		Check(texture.descReads == 1 && texture.addressReads == 0 && texture.heapReads == 1);
		const auto& failure = saved[3]["beforeResourceInfo"];
		Check(saved[3]["afterResourceInfo"].is_null() && failure["bufferRangeAvailable"] == false && failure["bufferRangeEndExclusive"].is_null());
		Check(failure["heapProperties"]["hresult"] == static_cast<std::int32_t>(E_INVALIDARG) && failure["heapProperties"]["available"] == false);
		Check(failure["heapProperties"]["properties"].is_null() && failure["heapProperties"]["flags"].is_null());
		Check(failure["heapProperties"]["unavailableReason"] == "GetHeapProperties_failed");
		Check(buffer.descReads == 1 && buffer.addressReads == 1 && buffer.heapReads == 1);
		Check(sample["kernelCommandCapture"]["resourceExtentsRequested"] == true && sample["kernelCommandCapture"]["failed"] == false);
		buffer.address = 0;
		Json next;
		Capture second(next, true);
		second.BeforeCommand();
		second.Barriers(1, &barriers[1]);
		second.Complete();
		Check(next["kernelCommandDetails"][0]["barriers"][0]["resourceInfo"]["bufferRangeAvailable"] == false);
		Check(info["gpuVirtualAddress"] == 0x100000);
	}
	void TestAggregateBudgets()
	{
		for (unsigned scenario = 0; scenario < 5; ++scenario) {
			Json sample;
			Capture capture(sample);
			std::vector barriers(Capture::kMaximumBarriersPerCommand, Uav());
			FakeHeap heap;
			ID3D12DescriptorHeap* heaps[]{ &heap, &heap };
			if (scenario == 0) {
				for (std::size_t i = 0; i < Capture::kMaximumCommands; ++i)
					capture.BeforeCommand();
				Reject([&] { capture.BeforeCommand(); });
			} else if (scenario == 1) {
				for (std::size_t i = 0; i < Capture::kMaximumDetails; ++i) {
					capture.BeforeCommand();
					capture.Barriers(0, nullptr);
				}
				capture.BeforeCommand();
				Reject([&] { capture.Barriers(0, nullptr); });
			} else if (scenario == 2) {
				for (std::size_t i = 0; i < Capture::kMaximumHeaps / 2; ++i) {
					capture.BeforeCommand();
					capture.Heaps(2, heaps);
				}
				capture.BeforeCommand();
				Reject([&] { capture.Heaps(1, heaps); });
				Check(heap.reads == Capture::kMaximumHeaps);
			} else {
				for (std::size_t i = 0; i < Capture::kMaximumBarriers / barriers.size(); ++i) {
					capture.BeforeCommand();
					capture.Barriers(static_cast<UINT>(barriers.size()), barriers.data());
				}
				capture.BeforeCommand();
				if (scenario == 3)
					Reject([&] { capture.Barriers(1, barriers.data()); });
				else
					Reject([&] { capture.Heaps(1, heaps); });
				Check(heap.reads == 0 && sample["kernelCommandCapture"]["barriers"] == Capture::kMaximumBarriers);
			}
			Check(sample["kernelCommandCapture"]["status"] == "failed");
			Check(sample["kernelCommandCapture"]["details"] == sample["kernelCommandDetails"].size());
		}
	}
	void TestLifetime()
	{
		Json interrupted;
		{
			Capture capture(interrupted);
			capture.SetEvaluation(1, 2);
		}
		Check(interrupted["kernelCommandCapture"]["status"] == "failed");
		Check(interrupted["kernelCommandCapture"]["failure"]["eye"] == 1 && interrupted["kernelCommandCapture"]["failure"]["region"] == 2);
		Json sample;
		{
			Capture capture(sample);
			capture.BeforeCommand();
			capture.Complete();
		}
		Check(sample["kernelCommandCapture"]["failed"] == false);
		Json next;
		{
			Capture capture(next);
			capture.Complete();
		}
		Check(next["kernelCommandCapture"]["commands"] == 0 && next["kernelCommandDetails"].empty());
	}
}

int main()
{
	try {
		TestFieldsAndOwnership();
		TestMalformedCommands();
		TestResourceExtents();
		TestAggregateBudgets();
		TestLifetime();
		std::cout << checks << " command capture CPU checks passed; no GPU used\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
