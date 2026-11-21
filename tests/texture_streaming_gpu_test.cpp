#include "Features/TextureStreaming/Categories.h"
#include "Features/TextureStreaming/TextureData.h"
#include "Utils/GpuMemoryBudget.h"
#include "asset_load_diagnostics_fixture.h"
#include "commonlib_d3d_bridge_fake.h"
#include "d3d11_shader_test.h"
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using D3D11ShaderTest::Check;
using Microsoft::WRL::ComPtr;
void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void CheckMipChain(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format, UINT drop, UINT width = 1024, UINT height = 1024, UINT mipLevels = 11, bool gpuCopy = false, UINT sourceDrop = 0)
{
	DirectX::ScratchImage source;
	Check(source.Initialize2D(format, width, height, 1, mipLevels));
	for (size_t mip = 0; mip < source.GetImageCount(); ++mip) {
		const auto& image = source.GetImages()[mip];
		for (size_t byte = 0; byte < image.slicePitch; ++byte)
			image.pixels[byte] = static_cast<std::uint8_t>(mip * 17 + byte * 3);
	}
	D3D11_TEXTURE2D_DESC full{ width, height, mipLevels, 1, format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = full.MipLevels;
	Require(StreamingTextures::MatchesDDS(source, full), "DDS metadata rejected an exact original chain");
	StreamingTextures::Upload upload;
	ComPtr<ID3D11Texture2D> resident;
	ComPtr<ID3D11ShaderResourceView> residentView;
	if (gpuCopy) {
		auto residentDesc = full;
		residentDesc.Width >>= sourceDrop;
		residentDesc.Height >>= sourceDrop;
		residentDesc.MipLevels -= sourceDrop;
		residentDesc.Usage = D3D11_USAGE_IMMUTABLE;
		std::vector<D3D11_SUBRESOURCE_DATA> initial;
		for (UINT i = sourceDrop; i < mipLevels; ++i) {
			const auto& image = *source.GetImage(i, 0, 0);
			initial.push_back({ image.pixels, static_cast<UINT>(image.rowPitch), static_cast<UINT>(image.slicePitch) });
		}
		Check(device->CreateTexture2D(&residentDesc, initial.data(), &resident));
		Util::SetResourceName(resident.Get(), "TextureStreamingTest::Resident");
		auto residentSrv = view;
		residentSrv.Texture2D.MipLevels = residentDesc.MipLevels;
		Check(device->CreateShaderResourceView(resident.Get(), &residentSrv, &residentView));
		Util::SetResourceName(residentView.Get(), "TextureStreamingTest::Resident.SRV");
		auto* bound = residentView.Get();
		context->PSSetShaderResources(7, 1, &bound);
		Check(upload.BeginCopy(device, full, view, drop, resident.Get(), sourceDrop));
		Require(upload.AdvanceCopy(context, 1) == S_FALSE && upload.mip == 0 && upload.row == 0 && !upload.submitted,
			"GPU copy exceeded a budget smaller than one compressed row");
	} else {
		Check(upload.Begin(device, full, view, drop));
	}
	const StreamingTextures::Origin origin{ 42, true, StreamingTextures::LandscapeStatics };
	Check(StreamingTextures::WriteOrigin(upload.texture.Get(), origin));
	StreamingTextures::Origin observed;
	Require(StreamingTextures::ReadOrigin(upload.texture.Get(), observed) && observed.serial == 42 && observed.protectedConsumer, "Resource-owned provenance was lost");
	Check(StreamingTextures::WriteOrigin(upload.texture.Get(), { 99, false, StreamingTextures::AlphaTestedStatics }));
	Require(StreamingTextures::ReadOrigin(upload.texture.Get(), observed) && observed.serial == 42 && observed.protectedConsumer, "A cached DDS load reset resource identity or special-consumer protection");
	Require(observed.requiredCategories == (StreamingTextures::LandscapeStatics | StreamingTextures::AlphaTestedStatics), "A shared DDS load erased another consumer category");
	Check(StreamingTextures::WriteOrigin(upload.texture.Get(), { 42, false, 0 }));
	Require(StreamingTextures::ReadOrigin(upload.texture.Get(), observed) && observed.requiredCategories == (StreamingTextures::LandscapeStatics | StreamingTextures::AlphaTestedStatics), "A reload cleared category requirements");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	unsigned steps = 0;
	HRESULT status = S_FALSE;
	while (status == S_FALSE && std::chrono::steady_clock::now() < deadline) {
		status = gpuCopy ? upload.AdvanceCopy(context, 16 * 1024) : upload.Advance(context, source, 16 * 1024);
		++steps;
		context->Flush();
		if (upload.submitted)
			std::this_thread::yield();
	}
	Check(status);
	Require(status == S_OK && steps > 1, "Bounded transfer did not complete asynchronously");
	if (gpuCopy) {
		ComPtr<ID3D11ShaderResourceView> bound;
		context->PSGetShaderResources(7, 1, &bound);
		Require(bound.Get() == residentView.Get(), "GPU copy changed a live shader binding");
		StreamingTextures::Upload invalid;
		Require(invalid.BeginCopy(device, full, view, drop, resident.Get(), drop) == E_INVALIDARG && !invalid.texture,
			"GPU copy attempted to refill discarded source mips");
		auto wrong = full;
		wrong.Format = format == DXGI_FORMAT_BC1_UNORM ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC1_UNORM;
		Require(invalid.BeginCopy(device, wrong, view, drop, resident.Get(), sourceDrop) == E_INVALIDARG && !invalid.texture,
			"GPU copy accepted incompatible resident pixels");
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC actual{};
	upload.view->GetDesc(&actual);
	Require(actual.Format == format && actual.Texture2D.MipLevels == mipLevels - drop && actual.Texture2D.MostDetailedMip == 0, "Typed SRV contract changed");
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
	Require(tooSmall.Begin(device, full, view, std::bit_width(std::min(width, height)) - std::bit_width(TextureStreamingPolicy::MinimumResidentEdge) + 1) == E_INVALIDARG && !tooSmall.texture, "Allocation bypassed the minimum resident edge");
	full.BindFlags |= D3D11_BIND_RENDER_TARGET;
	Require(!StreamingTextures::Suitable(full, view), "Render-target material entered static streaming");
}

#include "texture_streaming_inventory_checks.h"
#include "texture_streaming_publication_checks.h"
#include "texture_streaming_reader_checks.h"

void CheckTextureContracts()
{
	using namespace StreamingTextures;
	D3D11_TEXTURE2D_DESC texture{ 4096, 4096, 13, 1, DXGI_FORMAT_BC7_UNORM_SRGB, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = texture.Format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = UINT_MAX;
	Require(Suitable(texture, view) && ClassifyTexture(texture, view) == TextureRejection::None, "An all-mips typed DDS view was rejected");
	for (unsigned fields = 0; fields < 8; ++fields)
		Require(ClassifyTexture(texture, view, fields & 1 ? 4096 : 0, fields & 2 ? 4096 : 0, fields & 4 ? 13 : 0) == TextureRejection::None,
			"Valid DDS metadata was rejected when engine fields were unset");
	Require(ClassifyTexture(texture, view, 2048, 0, 0) == TextureRejection::EngineDimensions &&
				ClassifyTexture(texture, view, 0, 2048, 0) == TextureRejection::EngineDimensions,
		"Populated inconsistent dimensions escaped validation");
	Require(ClassifyTexture(texture, view, 0, 0, 12) == TextureRejection::EngineMipCount,
		"Populated inconsistent mip metadata escaped validation");
	texture.MipLevels = 12;
	Require(Suitable(texture, view), "A valid shorter DDS mip chain was rejected");
	texture.MipLevels = 14;
	Require(ClassifyTexture(texture, view) == TextureRejection::MipChain, "An impossible mip count escaped validation");
	texture.MipLevels = 1;
	Require(ClassifyTexture(texture, view) == TextureRejection::MipChain, "A texture without reducible mips entered streaming");
	texture.MipLevels = 13;
	texture.Height = 2048;
	Require(Suitable(texture, view), "A rectangular DDS texture was rejected");
	Require(TextureStreamingPolicy::DesiredDrop(4096, 13, 128, 3, 1024) == 2, "A rectangular reduction crossed the minimum short edge");
	texture.Height = 4096;
	view.Texture2D.MostDetailedMip = 1;
	Require(ClassifyTexture(texture, view) == TextureRejection::ViewFirstMip, "A partial SRV entered whole-chain replacement");
	view.Texture2D.MostDetailedMip = 0;
	view.Format = DXGI_FORMAT_BC7_UNORM;
	Require(ClassifyTexture(texture, view) == TextureRejection::ViewFormat, "A different SRGB sampling contract entered replacement");
	view.Format = texture.Format;
	texture.Format = DXGI_FORMAT_BC7_TYPELESS;
	Require(ClassifyTexture(texture, view) == TextureRejection::TypelessFormat, "A typeless resource lost its conservative rejection");
	texture.Format = view.Format;
	texture.MiscFlags = D3D11_RESOURCE_MISC_RESOURCE_CLAMP;
	Require(ClassifyTexture(texture, view) == TextureRejection::MiscFlags, "Resource clamp semantics were silently discarded");
#ifdef DEVBENCH_BRIDGE_ENABLED
	TextureInspection inspection;
	inspection.reason = TextureRejection::MiscFlags;
	inspection.descriptorAvailable = true;
	inspection.texture = texture;
	TextureDiagnostics diagnostics;
	diagnostics.Observe(inspection, std::string(512, 't'));
	diagnostics.Observe(inspection, "later texture");
	const auto reason = static_cast<std::size_t>(inspection.reason);
	Require(diagnostics.reasons[reason] == 2 && diagnostics.texturePaths[reason] == std::string(256, 't') &&
				diagnostics.samples[reason].texture.MiscFlags == D3D11_RESOURCE_MISC_RESOURCE_CLAMP,
		"Texture diagnostics lost their cumulative count, bounded first sample or raw contract");
#endif
}

int main()
{
	try {
		CheckTextureContracts();
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		for (auto format : { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB })
			for (UINT drop : { 0u, 1u, 2u })
				CheckMipChain(device.Get(), context.Get(), format, drop);
		for (UINT drop : { 0u, 1u, 2u })
			CheckMipChain(device.Get(), context.Get(), DXGI_FORMAT_BC3_UNORM_SRGB, drop, 4096, 2048, 12);
		for (auto format : { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB }) {
			for (UINT drop : { 1u, 2u })
				CheckMipChain(device.Get(), context.Get(), format, drop, 1024, 1024, 11, true);
			CheckMipChain(device.Get(), context.Get(), format, 2, 1024, 1024, 11, true, 1);
		}
		for (UINT drop : { 1u, 2u, 3u })
			CheckMipChain(device.Get(), context.Get(), DXGI_FORMAT_BC3_UNORM_SRGB, drop, 4096, 2048, 12, true, drop - 1);
		CheckMipChain(device.Get(), context.Get(), DXGI_FORMAT_BC1_UNORM, 3, 8192, 256, 14, true);
		for (const auto format : { DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC7_UNORM }) {
			D3D11_TEXTURE2D_DESC full{ 8192, 8192, 14, 1, format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
			Require(StreamingTextures::LogicalBytes(full, 0) == 89478512, "A full 8K BC5/BC7 source was rejected or miscounted");
			DirectX::TexMetadata metadata{ 8192, 8192, 1, 1, 14, 0, 0, format, DirectX::TEX_DIMENSION_TEXTURE2D };
			std::array<std::uint8_t, 148> header{};
			size_t written = 0;
			Require(SUCCEEDED(DirectX::EncodeDDSHeader(metadata, DirectX::DDS_FLAGS_FORCE_DX10_EXT, header.data(), header.size(), written)), "Could not encode 8K source header");
			Require(StreamingTextures::MatchesDDSHeader(header, written + 89478512, full), "An ordinary 8K normal/color DDS exceeded admission");
			Require(!StreamingTextures::MatchesDDSHeader(header, TextureStreamingPolicy::MaximumPayloadBytes + 1, full), "Unbounded DDS file was admitted");
		}

		CheckStreamingReader();
		CheckStreamingInventory(device.Get());
		CheckStreamingPublication(device.Get(), context.Get());
		using Memory = Util::GpuMemoryBudget;
		auto& memory = Memory::Get();
		memory.Sample(device.Get(), 0, true);
		memory.SetPriorityWork(Memory::Owner::RenderScale, true);
		memory.SetPriorityWork(Memory::Owner::NeuralRendering, true);
		memory.SetPriorityWork(Memory::Owner::NeuralRendering, false);
		Require(memory.Sample(device.Get()).priorityWork, "NR completion cleared render-scale priority");
		memory.SetPriorityWork(Memory::Owner::RenderScale, false);
		Require(!memory.Sample(device.Get()).priorityWork, "Completed priorities blocked streaming indefinitely");
		memory.SetRecoveryDemand(Memory::Owner::NeuralRendering, 256 * Memory::MiB);
		const auto waiting = memory.Sample(device.Get());
		Require(waiting.recoveryDemandBytes == 256 * Memory::MiB && !waiting.priorityWork && !waiting.pendingBytes, "Future NR demand became an active allocation or priority lock");
		memory.SetRecoveryDemand(Memory::Owner::RenderScale, 64 * Memory::MiB);
		Require(memory.Sample(device.Get()).recoveryDemandBytes == 320 * Memory::MiB, "Future recovery demand was not shared across owners");
		memory.SetRecoveryDemand(Memory::Owner::NeuralRendering, 0);
		Require(memory.Sample(device.Get()).recoveryDemandBytes == 64 * Memory::MiB, "NR admission cleared another owner's recovery demand");
		memory.SetOwnerWork(Memory::Owner::NeuralRendering, true, 0);
		const auto rebuilding = memory.Sample(device.Get());
		Require(rebuilding.priorityWork && rebuilding.recoveryDemandBytes == 64 * Memory::MiB, "Combined admission update lost priority or another owner's demand");
		memory.SetOwnerWork(Memory::Owner::NeuralRendering, false, 256 * Memory::MiB);
		const auto deferred = memory.Sample(device.Get());
		Require(!deferred.priorityWork && deferred.recoveryDemandBytes == 320 * Memory::MiB, "Combined waiting update retained active priority or lost future demand");
		memory.SetPriorityWork(Memory::Owner::NeuralRendering, true);
		Require(memory.Sample(device.Get()).recoveryDemandBytes == 320 * Memory::MiB, "Separate priority update erased recovery demand");
		memory.SetRecoveryDemand(Memory::Owner::NeuralRendering, 0);
		Require(memory.Sample(device.Get()).priorityWork, "Separate demand update erased active priority");
		memory.SetOwnerWork(Memory::Owner::NeuralRendering, false, 0);
		{
			auto first = memory.Reserve(Memory::Owner::Streaming, device.Get(), 64 * Memory::MiB);
			auto second = memory.Reserve(Memory::Owner::NeuralRendering, device.Get(), 128 * Memory::MiB);
			Require(memory.PendingBytes(Memory::Owner::RenderScale) == 192 * Memory::MiB, "Reservations were not shared across consumers");
			memory.SetOwnerWork(Memory::Owner::NeuralRendering, false, 128 * Memory::MiB);
			Require(memory.PendingBytes(Memory::Owner::RenderScale) == 192 * Memory::MiB, "Work scheduling released an in-flight allocation ticket");
			auto moved = std::move(first);
			Require(!first && moved && memory.PendingBytes(Memory::Owner::NeuralRendering) == 64 * Memory::MiB, "Moving an allocation ticket released it early");
			ComPtr<ID3D11Device> replacementDevice;
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &replacementDevice, nullptr, nullptr));
			const auto before = memory.Sample(device.Get());
			const auto after = memory.Sample(replacementDevice.Get());
			Require(after.generation > before.generation && after.pendingBytes == 0 && after.recoveryDemandBytes == 0, "Device replacement reused stale admission reservations");
			auto current = memory.Reserve(Memory::Owner::Streaming, replacementDevice.Get(), 7);
			moved.Reset();
			Require(memory.PendingBytes(Memory::Owner::NeuralRendering) == 7, "Old-generation ticket corrupted a new device reservation");
		}
		Require(memory.PendingBytes(Memory::Owner::RenderScale) == 0, "A ticket leaked after cancellation");
		std::cout << "PASS: WARP exact BC GPU-copy shrink/refill, bounded transfers, immutable restoration cache, source validation, atomic publication and shared ticket lifetime\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
