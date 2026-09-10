#define NOMINMAX
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <dxgi1_2.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace DX
{
	void ThrowIfFailed(HRESULT a_result) { winrt::check_hresult(a_result); }
}

struct TextureHandleOwner
{
	D3D11_TEXTURE2D_DESC desc{};
	winrt::com_ptr<ID3D11Texture2D> resource;
	winrt::handle sharedHandle_;
#include "shared_guide_handle_under_test.h"
};

struct WrappedResource
{
	WrappedResource(ID3D11Texture2D*, ID3D12Device*, HANDLE);
	winrt::com_ptr<ID3D11Texture2D> resource11;
	winrt::com_ptr<ID3D12Resource> resource;
};
#include "shared_guide_import_under_test.h"

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct Devices
	{
		winrt::com_ptr<ID3D11Device> device11;
		winrt::com_ptr<ID3D11DeviceContext> context11;
		winrt::com_ptr<ID3D11DeviceContext4> context4;
		winrt::com_ptr<ID3D12Device> device12;
		winrt::com_ptr<ID3D12CommandQueue> queue;
		winrt::com_ptr<ID3D12Fence> fence12;
		winrt::com_ptr<ID3D11Fence> fence11;
		uint64_t fenceValue = 0;

		bool Initialize()
		{
			const HRESULT deviceResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				0, nullptr, 0, D3D11_SDK_VERSION, device11.put(), nullptr, context11.put());
			if (FAILED(deviceResult))
				return false;
			winrt::com_ptr<IDXGIAdapter> adapter;
			DX::ThrowIfFailed(device11.as<IDXGIDevice>()->GetAdapter(adapter.put()));
			if (FAILED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device12.put()))))
				return false;
			context4 = context11.as<ID3D11DeviceContext4>();
			D3D12_COMMAND_QUEUE_DESC queueDesc{};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			DX::ThrowIfFailed(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.put())));
			DX::ThrowIfFailed(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence12.put())));
			winrt::handle handle;
			DX::ThrowIfFailed(device12->CreateSharedHandle(fence12.get(), nullptr, GENERIC_ALL, nullptr, handle.put()));
			DX::ThrowIfFailed(device11.as<ID3D11Device5>()->OpenSharedFence(handle.get(), IID_PPV_ARGS(fence11.put())));
			return true;
		}
	};

	void ReadSharedGuide(Devices& a_devices, TextureHandleOwner& a_texture, ID3D11UnorderedAccessView* a_uav,
		uint32_t a_bytesPerPixel, uint32_t a_expectedPixel, float a_value)
	{
		WrappedResource imported(a_texture.resource.get(), a_devices.device12.get(), a_texture.GetOrCreateSharedHandle());
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64 totalBytes = 0;
		const auto resourceDesc = imported.resource->GetDesc();
		a_devices.device12->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC readbackDesc{};
		readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readbackDesc.Width = totalBytes;
		readbackDesc.Height = 1;
		readbackDesc.DepthOrArraySize = 1;
		readbackDesc.MipLevels = 1;
		readbackDesc.SampleDesc.Count = 1;
		readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		winrt::com_ptr<ID3D12Resource> readback;
		DX::ThrowIfFailed(a_devices.device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put())));
		winrt::com_ptr<ID3D12CommandAllocator> allocator;
		winrt::com_ptr<ID3D12GraphicsCommandList> commands;
		DX::ThrowIfFailed(a_devices.device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())));
		DX::ThrowIfFailed(a_devices.device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			allocator.get(), nullptr, IID_PPV_ARGS(commands.put())));
		const float clear[4]{ a_value, a_value, a_value, a_value };
		a_devices.context11->ClearUnorderedAccessViewFloat(a_uav, clear);
		const auto producerValue = ++a_devices.fenceValue;
		DX::ThrowIfFailed(a_devices.context4->Signal(a_devices.fence11.get(), producerValue));
		a_devices.context11->Flush();
		DX::ThrowIfFailed(a_devices.queue->Wait(a_devices.fence12.get(), producerValue));
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = imported.resource.get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		commands->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = imported.resource.get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readback.get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = footprint;
		commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		commands->ResourceBarrier(1, &barrier);
		DX::ThrowIfFailed(commands->Close());
		ID3D12CommandList* lists[]{ commands.get() };
		a_devices.queue->ExecuteCommandLists(1, lists);
		const auto consumerValue = ++a_devices.fenceValue;
		DX::ThrowIfFailed(a_devices.queue->Signal(a_devices.fence12.get(), consumerValue));
		DX::ThrowIfFailed(a_devices.context4->Wait(a_devices.fence11.get(), consumerValue));
		winrt::handle complete(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		winrt::check_bool(static_cast<bool>(complete));
		DX::ThrowIfFailed(a_devices.fence12->SetEventOnCompletion(consumerValue, complete.get()));
		Require(WaitForSingleObject(complete.get(), 5000) == WAIT_OBJECT_0, "Shared guide readback timed out");
		void* pixels = nullptr;
		D3D12_RANGE range{ 0, static_cast<SIZE_T>(totalBytes) };
		DX::ThrowIfFailed(readback->Map(0, &range, &pixels));
		bool matches = true;
		for (UINT y = 0; y < a_texture.desc.Height; ++y) {
			for (UINT x = 0; x < a_texture.desc.Width; ++x) {
				uint32_t pixel = 0;
				std::memcpy(&pixel, static_cast<const uint8_t*>(pixels) + y * footprint.Footprint.RowPitch + x * a_bytesPerPixel, a_bytesPerPixel);
				matches = matches && pixel == a_expectedPixel;
			}
		}
		D3D12_RANGE noWrites{};
		readback->Unmap(0, &noWrites);
		Require(matches, "Imported guide did not preserve D3D11 UAV contents");
	}

	void CheckFormat(Devices& a_devices, DXGI_FORMAT a_format, uint32_t a_bytesPerPixel, uint32_t a_onePixel)
	{
		TextureHandleOwner texture;
		texture.desc.Width = 8;
		texture.desc.Height = 8;
		texture.desc.MipLevels = 1;
		texture.desc.ArraySize = 1;
		texture.desc.Format = a_format;
		texture.desc.SampleDesc.Count = 1;
		texture.desc.Usage = D3D11_USAGE_DEFAULT;
		texture.desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texture.desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		DX::ThrowIfFailed(a_devices.device11->CreateTexture2D(&texture.desc, nullptr, texture.resource.put()));
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		DX::ThrowIfFailed(a_devices.device11->CreateUnorderedAccessView(texture.resource.get(), nullptr, uav.put()));
		const HANDLE original = texture.GetOrCreateSharedHandle();
		ReadSharedGuide(a_devices, texture, uav.get(), a_bytesPerPixel, 0, 0.0f);
		Require(texture.GetOrCreateSharedHandle() == original, "Context recreation replaced the NT handle");
		ReadSharedGuide(a_devices, texture, uav.get(), a_bytesPerPixel, a_onePixel, 1.0f);
		std::cout << "Shared guide format " << static_cast<uint32_t>(a_format) << ": two fenced imports passed\n";
	}
}

int main()
{
	try {
		Devices devices;
		if (!devices.Initialize()) {
			std::cout << "SKIP: compatible D3D11/D3D12 hardware unavailable\n";
			return 77;
		}
		CheckFormat(devices, DXGI_FORMAT_R32_FLOAT, 4, 0x3F800000u);
		CheckFormat(devices, DXGI_FORMAT_R16G16_FLOAT, 4, 0x3C003C00u);
		CheckFormat(devices, DXGI_FORMAT_R8_UNORM, 1, 0xFFu);
		CheckFormat(devices, DXGI_FORMAT_R8_UNORM, 1, 0xFFu);
		return 0;
	} catch (const winrt::hresult_error& error) {
		std::cerr << "D3D interop failed: 0x" << std::hex << static_cast<uint32_t>(error.code()) << "\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
	}
	return 1;
}
