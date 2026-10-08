#include "TextureData.h"
#include "Utils/ResourceName.h"
#include <algorithm>
#include <bit>
#include <mutex>

namespace StreamingTextures
{
	namespace
	{
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
		}
		return resource->SetPrivateData(originKey, sizeof(merged), &merged);
	}
	bool Suitable(const D3D11_TEXTURE2D_DESC& d, const D3D11_SHADER_RESOURCE_VIEW_DESC& v)
	{
		return d.Width == d.Height && std::has_single_bit(d.Width) && d.Width >= 1024 && d.Width <= 8192 &&
		       d.MipLevels == static_cast<UINT>(std::bit_width(d.Width)) && d.ArraySize == 1 && d.SampleDesc.Count == 1 &&
		       (d.Usage == D3D11_USAGE_DEFAULT || d.Usage == D3D11_USAGE_IMMUTABLE) &&
		       d.BindFlags == D3D11_BIND_SHADER_RESOURCE && !d.CPUAccessFlags && !d.MiscFlags &&
		       DirectX::IsCompressed(d.Format) && !DirectX::IsTypeless(d.Format) &&
		       v.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && v.Texture2D.MostDetailedMip == 0 &&
		       (v.Texture2D.MipLevels == d.MipLevels || v.Texture2D.MipLevels == UINT_MAX) && v.Format == d.Format;
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
		const auto& m = source.GetMetadata();
		return m.dimension == DirectX::TEX_DIMENSION_TEXTURE2D && m.width == full.Width && m.height == full.Height &&
		       m.arraySize == 1 && m.depth == 1 && !m.IsCubemap() && m.mipLevels == full.MipLevels && m.format == full.Format &&
		       source.GetPixelsSize() == LogicalBytes(full, 0);
	}
	HRESULT Upload::Begin(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc, std::uint32_t skip)
	{
		if (!device || !Suitable(full, viewDesc) || skip >= full.MipLevels || skip > TextureStreamingPolicy::MaximumDrop ||
			(full.Width >> skip) < TextureStreamingPolicy::MinimumEdge || !LogicalBytes(full, 0))
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
	HRESULT Upload::Advance(ID3D11DeviceContext* context, const DirectX::ScratchImage& source, std::uint64_t bytes)
	{
		if (!context || !texture || !fence)
			return E_UNEXPECTED;
		if (submitted)
			return context->GetData(fence.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
		while (mip < desc.MipLevels) {
			const auto* image = source.GetImage(mip + drop, 0, 0);
			if (!image || !image->rowPitch || image->rowPitch > UINT_MAX)
				return E_INVALIDARG;
			const auto totalRows = std::max<size_t>(1, (image->height + 3) / 4);
			const auto count = std::min<std::uint64_t>(totalRows - row, bytes / image->rowPitch);
			if (!count)
				return S_FALSE;
			D3D11_BOX box{ 0, row * 4, 0, static_cast<UINT>(image->width), static_cast<UINT>(std::min(image->height, (row + count) * 4)), 1 };
			// Sub-block tail mips require a whole-subresource update on D3D11 runtimes.
			const auto* region = row == 0 && count == totalRows ? nullptr : &box;
			context->UpdateSubresource(texture.Get(), mip, region, image->pixels + row * image->rowPitch, static_cast<UINT>(image->rowPitch), 0);
			row += static_cast<std::uint32_t>(count);
			bytes -= count * image->rowPitch;
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
