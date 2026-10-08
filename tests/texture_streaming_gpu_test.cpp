#include "Features/TextureStreaming/TextureData.h"
#include "Utils/GpuMemoryBudget.h"
#include "d3d11_shader_test.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using D3D11ShaderTest::Check;
using Microsoft::WRL::ComPtr;
void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void CheckMipChain(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format, UINT drop)
{
	DirectX::ScratchImage source;
	Check(source.Initialize2D(format, 1024, 1024, 1, 11));
	for (size_t mip = 0; mip < source.GetImageCount(); ++mip) {
		const auto& image = source.GetImages()[mip];
		for (size_t byte = 0; byte < image.slicePitch; ++byte)
			image.pixels[byte] = static_cast<std::uint8_t>(mip * 17 + byte * 3);
	}
	D3D11_TEXTURE2D_DESC full{ 1024, 1024, 11, 1, format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = full.MipLevels;
	Require(StreamingTextures::MatchesDDS(source, full), "DDS metadata rejected an exact original chain");
	StreamingTextures::Upload upload;
	Check(upload.Begin(device, full, view, drop));
	const StreamingTextures::Origin origin{ 42, true };
	Check(StreamingTextures::WriteOrigin(upload.texture.Get(), origin));
	StreamingTextures::Origin observed;
	Require(StreamingTextures::ReadOrigin(upload.texture.Get(), observed) && observed.serial == 42 && observed.protectedConsumer, "Resource-owned provenance was lost");
	Check(StreamingTextures::WriteOrigin(upload.texture.Get(), { 99, false }));
	Require(StreamingTextures::ReadOrigin(upload.texture.Get(), observed) && observed.serial == 42 && observed.protectedConsumer, "A cached DDS load reset resource identity or special-consumer protection");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	unsigned steps = 0;
	HRESULT status = S_FALSE;
	while (status == S_FALSE && std::chrono::steady_clock::now() < deadline) {
		status = upload.Advance(context, source, 16 * 1024);
		++steps;
		context->Flush();
		if (upload.submitted)
			std::this_thread::yield();
	}
	Check(status);
	Require(status == S_OK && steps > 1, "Bounded upload did not complete asynchronously");
	D3D11_SHADER_RESOURCE_VIEW_DESC actual{};
	upload.view->GetDesc(&actual);
	Require(actual.Format == format && actual.Texture2D.MipLevels == 11 - drop && actual.Texture2D.MostDetailedMip == 0, "Typed SRV contract changed");
	auto stagingDesc = upload.desc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&stagingDesc, nullptr, &staging));
	Util::SetResourceName(staging.Get(), "TextureStreamingTest::Readback");
	context->CopyResource(staging.Get(), upload.texture.Get());
	for (UINT mip = 0; mip < stagingDesc.MipLevels; ++mip) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), mip, D3D11_MAP_READ, 0, &mapped));
		const auto& expected = *source.GetImage(mip + drop, 0, 0);
		bool equal = true;
		for (size_t row = 0; row < std::max<size_t>(1, (expected.height + 3) / 4); ++row)
			equal &= std::memcmp(static_cast<const std::uint8_t*>(mapped.pData) + row * mapped.RowPitch,
						 expected.pixels + row * expected.rowPitch, expected.rowPitch) == 0;
		context->Unmap(staging.Get(), mip);
		if (!equal)
			throw std::runtime_error("DDS bytes changed: format=" + std::to_string(format) + " drop=" + std::to_string(drop) + " mip=" + std::to_string(mip));
	}
	StreamingTextures::Upload tooSmall;
	Require(tooSmall.Begin(device, full, view, 3) == E_INVALIDARG && !tooSmall.texture, "Allocation bypassed the minimum resident edge");
	full.BindFlags |= D3D11_BIND_RENDER_TARGET;
	Require(!StreamingTextures::Suitable(full, view), "Render-target material entered static streaming");
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (auto format : { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB })
			for (UINT drop : { 0u, 1u, 2u })
				CheckMipChain(device.Get(), context.Get(), format, drop);
		using Memory = Util::GpuMemoryBudget;
		auto& memory = Memory::Get();
		memory.Sample(device.Get(), 0, true);
		memory.SetPriorityWork(Memory::Owner::RenderScale, true);
		memory.SetPriorityWork(Memory::Owner::NeuralRendering, true);
		memory.SetPriorityWork(Memory::Owner::NeuralRendering, false);
		Require(memory.Sample(device.Get()).priorityWork, "NR completion cleared render-scale priority");
		memory.SetPriorityWork(Memory::Owner::RenderScale, false);
		Require(!memory.Sample(device.Get()).priorityWork, "Completed priorities blocked streaming indefinitely");
		{
			auto first = memory.Reserve(Memory::Owner::Streaming, device.Get(), 64 * Memory::MiB);
			auto second = memory.Reserve(Memory::Owner::NeuralRendering, device.Get(), 128 * Memory::MiB);
			Require(memory.PendingBytes(Memory::Owner::RenderScale) == 192 * Memory::MiB, "Reservations were not shared across consumers");
			auto moved = std::move(first);
			Require(!first && moved && memory.PendingBytes(Memory::Owner::NeuralRendering) == 64 * Memory::MiB, "Moving an allocation ticket released it early");
			ComPtr<ID3D11Device> replacementDevice;
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &replacementDevice, nullptr, nullptr));
			const auto before = memory.Sample(device.Get());
			const auto after = memory.Sample(replacementDevice.Get());
			Require(after.generation > before.generation && after.pendingBytes == 0, "Device replacement reused stale admission reservations");
			auto current = memory.Reserve(Memory::Owner::Streaming, replacementDevice.Get(), 7);
			moved.Reset();
			Require(memory.PendingBytes(Memory::Owner::NeuralRendering) == 7, "Old-generation ticket corrupted a new device reservation");
		}
		Require(memory.PendingBytes(Memory::Owner::RenderScale) == 0, "A ticket leaked after cancellation");
		std::cout << "PASS: WARP exact BC mip shrink/refill, bounded uploads, resource provenance and shared ticket lifetime\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
