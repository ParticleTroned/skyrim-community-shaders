#include "TextureData.h"
#include "Utils/ResourceName.h"
#include <algorithm>
#include <bit>
#include <mutex>

namespace StreamingTextures
{
	namespace
	{
		bool MatchesMetadata(const DirectX::TexMetadata& m, const D3D11_TEXTURE2D_DESC& full)
		{
			return m.dimension == DirectX::TEX_DIMENSION_TEXTURE2D && m.width == full.Width && m.height == full.Height &&
			       m.arraySize == 1 && m.depth == 1 && !m.IsCubemap() && m.mipLevels == full.MipLevels && m.format == full.Format;
		}
		std::mutex originMutex;
		constexpr GUID originKey{ 0x4279e6ab, 0xd2ee, 0x4642, { 0x85, 0x2a, 0x2d, 0xa5, 0xea, 0x4f, 0x65, 0x26 } };
	}
	bool ReadOrigin(ID3D11Resource* resource, Origin& origin)
	{
		UINT size = sizeof(origin);
		return resource && SUCCEEDED(resource->GetPrivateData(originKey, &size, &origin)) && size == sizeof(origin) && origin.serial;
	}
	HRESULT WriteOrigin(ID3D11Resource* resource, const Origin& origin)
	{
		if (!resource)
			return E_POINTER;
		if (!origin.serial)
			return E_INVALIDARG;
		std::scoped_lock lock(originMutex);
		auto merged = origin;
		Origin previous;
		if (ReadOrigin(resource, previous)) {
			merged.serial = previous.serial;
			merged.protectedConsumer |= previous.protectedConsumer;
			merged.requiredCategories |= previous.requiredCategories;
		}
		return resource->SetPrivateData(originKey, sizeof(merged), &merged);
	}
	TextureRejection ClassifyTexture(const D3D11_TEXTURE2D_DESC& d, const D3D11_SHADER_RESOURCE_VIEW_DESC& v,
		std::uint32_t engineWidth, std::uint32_t engineHeight, std::uint32_t engineMips)
	{
		if (!std::has_single_bit(d.Width) || !std::has_single_bit(d.Height))
			return TextureRejection::PowerOfTwo;
		if (std::min(d.Width, d.Height) < TextureStreamingPolicy::MinimumEdge || std::max(d.Width, d.Height) < 1024 || std::max(d.Width, d.Height) > 8192)
			return TextureRejection::Dimensions;
		if (d.MipLevels < 2 || d.MipLevels > static_cast<UINT>(std::bit_width(std::max(d.Width, d.Height))))
			return TextureRejection::MipChain;
		if (d.ArraySize != 1)
			return TextureRejection::ArraySize;
		if (d.SampleDesc.Count != 1)
			return TextureRejection::SampleCount;
		if (d.Usage != D3D11_USAGE_DEFAULT && d.Usage != D3D11_USAGE_IMMUTABLE)
			return TextureRejection::Usage;
		if (d.BindFlags != D3D11_BIND_SHADER_RESOURCE)
			return TextureRejection::BindFlags;
		if (d.CPUAccessFlags)
			return TextureRejection::CpuAccess;
		if (d.MiscFlags)
			return TextureRejection::MiscFlags;
		if (!DirectX::IsCompressed(d.Format))
			return TextureRejection::Compression;
		if (DirectX::IsTypeless(d.Format))
			return TextureRejection::TypelessFormat;
		if (v.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
			return TextureRejection::ViewDimension;
		if (v.Texture2D.MostDetailedMip != 0)
			return TextureRejection::ViewFirstMip;
		if (v.Texture2D.MipLevels != d.MipLevels && v.Texture2D.MipLevels != UINT_MAX)
			return TextureRejection::ViewMipCount;
		if (v.Format != d.Format)
			return TextureRejection::ViewFormat;
		if ((engineWidth && engineWidth != d.Width) || (engineHeight && engineHeight != d.Height))
			return TextureRejection::EngineDimensions;
		if (engineMips && engineMips != d.MipLevels)
			return TextureRejection::EngineMipCount;
		return TextureRejection::None;
	}
	bool Suitable(const D3D11_TEXTURE2D_DESC& d, const D3D11_SHADER_RESOURCE_VIEW_DESC& v)
	{
		return ClassifyTexture(d, v) == TextureRejection::None;
	}
	std::uint64_t LogicalBytes(const D3D11_TEXTURE2D_DESC& full, std::uint32_t drop)
	{
		if (drop >= full.MipLevels)
			return 0;
		std::uint64_t bytes = 0;
		for (auto mip = drop; mip < full.MipLevels; ++mip) {
			size_t rowPitch = 0, slicePitch = 0;
			if (FAILED(DirectX::ComputePitch(full.Format, std::max(1u, full.Width >> mip), std::max(1u, full.Height >> mip), rowPitch, slicePitch)) ||
				slicePitch > TextureStreamingPolicy::MaximumPayloadBytes - bytes)
				return 0;
			bytes += slicePitch;
		}
		return bytes;
	}
	bool MatchesDDS(const DirectX::ScratchImage& source, const D3D11_TEXTURE2D_DESC& full)
	{
		return MatchesMetadata(source.GetMetadata(), full) && source.GetPixelsSize() == LogicalBytes(full, 0);
	}
	bool MatchesDDSHeader(std::span<const std::uint8_t> header, std::uint64_t fileBytes, const D3D11_TEXTURE2D_DESC& full)
	{
		DirectX::TexMetadata m{};
		DirectX::DDSMetaData pixelFormat{};
		if (header.size() < 128 || fileBytes > TextureStreamingPolicy::MaximumPayloadBytes ||
			FAILED(DirectX::GetMetadataFromDDSMemoryEx(header.data(), header.size(), DirectX::DDS_FLAGS_NONE, m, &pixelFormat)))
			return false;
		const auto headerBytes = pixelFormat.IsDX10() ? 148u : 128u;
		const auto pixels = LogicalBytes(full, 0);
		return pixels && fileBytes >= headerBytes + pixels && MatchesMetadata(m, full);
	}
	HRESULT Upload::Begin(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc, std::uint32_t skip)
	{
		if (!device || !Suitable(full, viewDesc) || skip >= full.MipLevels || skip > TextureStreamingPolicy::MaximumDrop ||
			(std::min(full.Width, full.Height) >> skip) < TextureStreamingPolicy::MinimumResidentEdge || !LogicalBytes(full, 0))
			return E_INVALIDARG;
		desc = full;
		drop = skip;
		desc.Width >>= drop;
		desc.Height >>= drop;
		desc.MipLevels -= drop;
		desc.Usage = D3D11_USAGE_DEFAULT;
		auto hr = device->CreateTexture2D(&desc, nullptr, &texture);
		if (FAILED(hr))
			return hr;
		Util::SetResourceName(texture.Get(), "TextureStreaming::Replacement");
		viewDesc.Texture2D.MipLevels = desc.MipLevels;
		hr = device->CreateShaderResourceView(texture.Get(), &viewDesc, &view);
		if (FAILED(hr))
			return hr;
		Util::SetResourceName(view.Get(), "TextureStreaming::Replacement.SRV");
		const D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
		hr = device->CreateQuery(&query, &fence);
		if (SUCCEEDED(hr))
			Util::SetResourceName(fence.Get(), "TextureStreaming::UploadFence");
		return hr;
	}
	HRESULT Upload::BeginCopy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc,
		std::uint32_t skip, ID3D11Resource* source, std::uint32_t sourceDrop)
	{
		if (!device || !source || sourceDrop >= skip || sourceDrop >= full.MipLevels || skip > TextureStreamingPolicy::MaximumDrop)
			return E_INVALIDARG;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> resident;
		auto hr = source->QueryInterface(IID_PPV_ARGS(&resident));
		if (FAILED(hr))
			return hr;
		Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice;
		resident->GetDevice(&sourceDevice);
		D3D11_TEXTURE2D_DESC sourceDesc{};
		resident->GetDesc(&sourceDesc);
		if (sourceDevice.Get() != device || sourceDesc.ArraySize != full.ArraySize ||
			sourceDesc.SampleDesc.Count != full.SampleDesc.Count || sourceDesc.SampleDesc.Quality != full.SampleDesc.Quality ||
			sourceDesc.BindFlags != full.BindFlags || sourceDesc.CPUAccessFlags != full.CPUAccessFlags || sourceDesc.MiscFlags != full.MiscFlags ||
			(sourceDesc.Usage != D3D11_USAGE_DEFAULT && sourceDesc.Usage != D3D11_USAGE_IMMUTABLE) || sourceDesc.Width != (full.Width >> sourceDrop) ||
			sourceDesc.Height != (full.Height >> sourceDrop) || sourceDesc.MipLevels != full.MipLevels - sourceDrop || sourceDesc.Format != full.Format)
			return E_INVALIDARG;
		hr = Begin(device, full, viewDesc, skip);
		if (SUCCEEDED(hr)) {
			copySource = std::move(resident);
			copySourceDrop = sourceDrop;
		}
		return hr;
	}
	HRESULT Upload::Advance(ID3D11DeviceContext* context, const DirectX::ScratchImage& source, std::uint64_t bytes)
	{
		if (copySource)
			return E_INVALIDARG;
		return Transfer(context, &source, bytes);
	}
	HRESULT Upload::AdvanceCopy(ID3D11DeviceContext* context, std::uint64_t bytes)
	{
		if (!copySource)
			return E_INVALIDARG;
		return Transfer(context, nullptr, bytes);
	}
	HRESULT Upload::Transfer(ID3D11DeviceContext* context, const DirectX::ScratchImage* source, std::uint64_t bytes)
	{
		if (!context || !texture || !fence || (!source && !copySource))
			return E_UNEXPECTED;
		if (submitted)
			return context->GetData(fence.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
		while (mip < desc.MipLevels) {
			const auto width = std::max(1u, desc.Width >> mip), height = std::max(1u, desc.Height >> mip);
			size_t rowPitch = 0, slicePitch = 0;
			const auto hr = DirectX::ComputePitch(desc.Format, width, height, rowPitch, slicePitch);
			if (FAILED(hr) || !rowPitch || rowPitch > UINT_MAX)
				return E_INVALIDARG;
			const auto totalRows = std::max<std::uint32_t>(1, (height + 3) / 4);
			const auto count = std::min<std::uint64_t>(totalRows - row, bytes / rowPitch);
			if (!count)
				return S_FALSE;
			D3D11_BOX box{ 0, row * 4, 0, width, static_cast<UINT>(std::min<std::uint64_t>(height, (row + count) * 4)), 1 };
			// Sub-block tail mips require a whole-subresource transfer on D3D11 runtimes.
			const auto* region = row == 0 && count == totalRows ? nullptr : &box;
			if (copySource) {
				context->CopySubresourceRegion(texture.Get(), mip, 0, row * 4, 0, copySource.Get(), mip + drop - copySourceDrop, region);
			} else {
				const auto* image = source->GetImage(mip + drop, 0, 0);
				if (!image || image->width != width || image->height != height || image->rowPitch != rowPitch || image->slicePitch != slicePitch)
					return E_INVALIDARG;
				context->UpdateSubresource(texture.Get(), mip, region, image->pixels + row * rowPitch, static_cast<UINT>(rowPitch), 0);
			}
			row += static_cast<std::uint32_t>(count);
			bytes -= count * rowPitch;
			if (row == totalRows) {
				row = 0;
				++mip;
			}
		}
		context->End(fence.Get());
		submitted = true;
		return S_FALSE;
	}
}
