#define NOMINMAX
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <dxgi1_2.h>
#include <winrt/base.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
	WrappedResource(ID3D11Texture2D*, ID3D12Device*, const std::string&, HANDLE);
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

	bool HasInteropCapability(HRESULT a_result)
	{
		if (a_result == E_NOINTERFACE || a_result == DXGI_ERROR_UNSUPPORTED)
			return false;
		DX::ThrowIfFailed(a_result);
		return true;
	}

	void CheckCapabilityFailures()
	{
		Require(HasInteropCapability(S_OK), "Available interop capability was rejected");
		Require(!HasInteropCapability(E_NOINTERFACE), "Missing interface did not skip");
		Require(!HasInteropCapability(DXGI_ERROR_UNSUPPORTED), "Unsupported device did not skip");
		for (const HRESULT failure : { E_OUTOFMEMORY, E_INVALIDARG, E_FAIL, DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET }) {
			bool propagated = false;
			try {
				(void)HasInteropCapability(failure);
			} catch (const winrt::hresult_error& error) {
				propagated = error.code() == failure;
			} catch (const std::bad_alloc&) {
				propagated = failure == E_OUTOFMEMORY;
			}
			Require(propagated, "Interop failure was hidden as unsupported hardware");
		}
		std::cout << "Interop capability failure classification passed\n";
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
			if (!HasInteropCapability(deviceResult))
				return false;
			winrt::com_ptr<ID3D11Device5> device5;
			if (!HasInteropCapability(context11->QueryInterface(IID_PPV_ARGS(context4.put()))) ||
				!HasInteropCapability(device11->QueryInterface(IID_PPV_ARGS(device5.put()))))
				return false;
			winrt::com_ptr<IDXGIAdapter> adapter;
			DX::ThrowIfFailed(device11.as<IDXGIDevice>()->GetAdapter(adapter.put()));
			if (!HasInteropCapability(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device12.put()))))
				return false;
			D3D12_COMMAND_QUEUE_DESC queueDesc{};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			DX::ThrowIfFailed(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.put())));
			DX::ThrowIfFailed(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence12.put())));
			winrt::handle handle;
			DX::ThrowIfFailed(device12->CreateSharedHandle(fence12.get(), nullptr, GENERIC_ALL, nullptr, handle.put()));
			DX::ThrowIfFailed(device5->OpenSharedFence(handle.get(), IID_PPV_ARGS(fence11.put())));
			return true;
		}
	};

	void ReadSharedInput(Devices& a_devices, WrappedResource& imported, const D3D11_TEXTURE2D_DESC& a_desc,
		uint32_t a_bytesPerPixel, std::span<const uint8_t> a_expected, UINT a_width, UINT a_height,
		const std::function<void()>& a_laterWrite = {})
	{
		Require(a_expected.size() == static_cast<size_t>(a_desc.Width) * a_desc.Height * a_bytesPerPixel,
			"Readback pattern has the wrong storage size");
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
		const D3D12_BOX active{ 0, 0, 0, a_width, a_height, 1 };
		commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &active);
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		commands->ResourceBarrier(1, &barrier);
		DX::ThrowIfFailed(commands->Close());
		ID3D12CommandList* lists[]{ commands.get() };
		a_devices.queue->ExecuteCommandLists(1, lists);
		const auto consumerValue = ++a_devices.fenceValue;
		DX::ThrowIfFailed(a_devices.queue->Signal(a_devices.fence12.get(), consumerValue));
		DX::ThrowIfFailed(a_devices.context4->Wait(a_devices.fence11.get(), consumerValue));
		// The next producer write must be queued before any CPU completion wait.
		uint64_t finalValue = consumerValue;
		if (a_laterWrite) {
			a_laterWrite();
			finalValue = ++a_devices.fenceValue;
			DX::ThrowIfFailed(a_devices.context4->Signal(a_devices.fence11.get(), finalValue));
			a_devices.context11->Flush();
		}
		winrt::handle complete(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		winrt::check_bool(static_cast<bool>(complete));
		DX::ThrowIfFailed(a_devices.fence12->SetEventOnCompletion(finalValue, complete.get()));
		Require(WaitForSingleObject(complete.get(), 5000) == WAIT_OBJECT_0, "Shared guide readback timed out");
		void* pixels = nullptr;
		D3D12_RANGE range{ 0, static_cast<SIZE_T>(totalBytes) };
		DX::ThrowIfFailed(readback->Map(0, &range, &pixels));
		bool matches = true;
		for (UINT y = 0; y < a_height; ++y) {
			for (UINT x = 0; x < a_width; ++x) {
				const auto* actual = static_cast<const uint8_t*>(pixels) + y * footprint.Footprint.RowPitch + x * a_bytesPerPixel;
				const auto* expected = a_expected.data() + (y * a_desc.Width + x) * a_bytesPerPixel;
				matches = matches && std::memcmp(actual, expected, a_bytesPerPixel) == 0;
			}
		}
		D3D12_RANGE noWrites{};
		readback->Unmap(0, &noWrites);
		Require(matches, "Imported input did not preserve exact D3D11 contents");
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
		Util::SetResourceName(texture.resource.get(), "InteropTest::Input");
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		DX::ThrowIfFailed(a_devices.device11->CreateUnorderedAccessView(texture.resource.get(), nullptr, uav.put()));
		Util::SetResourceName(uav.get(), "InteropTest::Input UAV");
		const HANDLE original = texture.GetOrCreateSharedHandle();
		for (uint32_t value = 0; value < 2; ++value) {
			WrappedResource imported(texture.resource.get(), a_devices.device12.get(), "InteropTest::GuideImport", texture.GetOrCreateSharedHandle());
			const float clear[4]{ float(value), float(value), float(value), float(value) };
			a_devices.context11->ClearUnorderedAccessViewFloat(uav.get(), clear);
			std::vector<uint8_t> expected(texture.desc.Width * texture.desc.Height * a_bytesPerPixel);
			const uint32_t pixel = value ? a_onePixel : 0;
			for (size_t offset = 0; offset < expected.size(); offset += a_bytesPerPixel)
				std::memcpy(expected.data() + offset, &pixel, a_bytesPerPixel);
			ReadSharedInput(a_devices, imported, texture.desc, a_bytesPerPixel, expected, texture.desc.Width, texture.desc.Height);
			Require(texture.GetOrCreateSharedHandle() == original, "Context recreation replaced the NT handle");
		}
		std::cout << "Shared guide format " << static_cast<uint32_t>(a_format) << ": two fenced imports passed\n";
	}
	std::vector<uint8_t> ColorPattern(DXGI_FORMAT a_format, UINT a_width, UINT a_height,
		UINT a_activeWidth, UINT a_activeHeight, uint32_t a_eye, uint32_t a_phase)
	{
		const uint32_t bytes = a_format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
		std::vector<uint8_t> pixels(a_width * a_height * bytes, 0xD3);
		for (UINT y = 0; y < a_activeHeight; ++y) {
			for (UINT x = 0; x < a_activeWidth; ++x) {
				auto* pixel = pixels.data() + (y * a_width + x) * bytes;
				const auto index = x + y * 3 + a_eye * 5 + a_phase * 7;
				if (bytes == 8) {
					constexpr std::array<uint16_t, 8> half{ 0, 1, 0x1400, 0x3000, 0x3800, 0x3C00, 0x4800, 0x5400 };
					const std::array<uint16_t, 4> rgba{ half[index % 8], half[(index + 2) % 8], half[(index + 5) % 8], half[(index + 1) % 6] };
					std::memcpy(pixel, rgba.data(), bytes);
				} else {
					constexpr std::array<uint32_t, 8> packedFloat{ 0, 1, 0x00400801, 0x701C0380, 0x781E03C0, 0x90240480, 0xB02C0580, 0x40100200 };
					const uint32_t packed = a_format == DXGI_FORMAT_R11G11B10_FLOAT ? packedFloat[index % 8] :
					                                                                  (index * 37u & 255u) | ((index * 53u & 255u) << 8) | ((index * 71u & 255u) << 16) | ((index * 19u & 255u) << 24);
					std::memcpy(pixel, &packed, bytes);
				}
			}
		}
		return pixels;
	}

	void CheckColor(Devices& a_devices, DXGI_FORMAT a_format)
	{
		const uint32_t bytes = a_format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
		for (uint32_t eye = 0; eye < 2; ++eye) {
			TextureHandleOwner texture;
			texture.desc = { 11, 9, 1, 1, a_format, { 1, 0 }, D3D11_USAGE_DEFAULT,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0,
				D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE };
			DX::ThrowIfFailed(a_devices.device11->CreateTexture2D(&texture.desc, nullptr, texture.resource.put()));
			Util::SetResourceName(texture.resource.get(), "InteropTest::Input");
			WrappedResource imported(texture.resource.get(), a_devices.device12.get(), "InteropTest::ColorImport", texture.GetOrCreateSharedHandle());
			TextureHandleOwner staging;
			staging.desc = texture.desc;
			staging.desc.BindFlags = 0;
			DX::ThrowIfFailed(a_devices.device11->CreateTexture2D(&staging.desc, nullptr, staging.resource.put()));
			Util::SetResourceName(staging.resource.get(), "InteropTest::ColorCopy");
			WrappedResource copied(staging.resource.get(), a_devices.device12.get(), "InteropTest::ColorCopy", staging.GetOrCreateSharedHandle());
			for (UINT width : { 3u, 11u, 7u }) {
				const UINT height = width == 11 ? 9 : 5;
				const auto first = ColorPattern(a_format, 11, 9, width, height, eye, 0);
				const auto later = ColorPattern(a_format, 11, 9, width, height, eye, 1);
				a_devices.context11->UpdateSubresource(texture.resource.get(), 0, nullptr, first.data(), 11 * bytes, 0);
				const D3D11_BOX active{ 0, 0, 0, width, height, 1 };
				a_devices.context11->CopySubresourceRegion(staging.resource.get(), 0, 0, 0, 0, texture.resource.get(), 0, &active);
				ReadSharedInput(a_devices, imported, texture.desc, bytes, first, 11, 9, [&] {
					a_devices.context11->UpdateSubresource(texture.resource.get(), 0, nullptr, later.data(), 11 * bytes, 0);
				});
				ReadSharedInput(a_devices, imported, texture.desc, bytes, later, 11, 9);
				ReadSharedInput(a_devices, copied, staging.desc, bytes, first, width, height);
			}
		}
		std::cout << "Colour format " << static_cast<uint32_t>(a_format) << ": both eyes, padding, active copies and queued overwrite passed\n";
	}

}

int main(int a_argc, char** a_argv)
{
	try {
		Require(a_argc == 1 || (a_argc == 2 && std::string_view(a_argv[1]) == "--capability-policy-only"),
			"Unknown test argument");
		CheckCapabilityFailures();
		if (a_argc == 2)
			return 0;
		Devices devices;
		if (!devices.Initialize()) {
			std::cout << "SKIP: compatible D3D11/D3D12 hardware or fence interfaces unavailable\n";
			return 77;
		}
		CheckFormat(devices, DXGI_FORMAT_R32_FLOAT, 4, 0x3F800000u);
		CheckFormat(devices, DXGI_FORMAT_R16G16_FLOAT, 4, 0x3C003C00u);
		CheckFormat(devices, DXGI_FORMAT_R8_UNORM, 1, 0xFFu);
		CheckFormat(devices, DXGI_FORMAT_R8_UNORM, 1, 0xFFu);
		CheckColor(devices, DXGI_FORMAT_R16G16B16A16_FLOAT);
		CheckColor(devices, DXGI_FORMAT_R11G11B10_FLOAT);
		CheckColor(devices, DXGI_FORMAT_R8G8B8A8_UNORM);
		return 0;
	} catch (const winrt::hresult_error& error) {
		std::cerr << "D3D interop failed: 0x" << std::hex << static_cast<uint32_t>(error.code()) << "\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
	}
	return 1;
}
