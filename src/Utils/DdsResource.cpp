#include "DdsResource.h"
#include "D3D.h"
#include "HeightMap.h"
#include <DirectXTex.h>
#include <vector>

namespace Util
{
	HRESULT LoadDDSImageFromResource(const char* filename, DirectX::ScratchImage& image)
	{
		if (!filename || !*filename)
			return E_INVALIDARG;
		RE::BSResourceNiBinaryStream bsaStream(filename);
		if (!bsaStream.good())
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

		constexpr size_t MaxResourceBytes = 256u * 1024u * 1024u;
		const auto size = bsaStream.stream->totalSize;
		if (!size || size > MaxResourceBytes)
			return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
		std::vector<uint8_t> ddsData(size);
		if (!bsaStream.read(reinterpret_cast<char*>(ddsData.data()), size))
			return HRESULT_FROM_WIN32(ERROR_READ_FAULT);

		DirectX::TexMetadata metadata{};
		const auto result = DirectX::GetMetadataFromDDSMemory(ddsData.data(), ddsData.size(), DirectX::DDS_FLAGS_NONE, metadata);
		if (FAILED(result))
			return result;
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.IsCubemap() ||
			!metadata.width || !metadata.height || metadata.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
			metadata.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || !metadata.mipLevels || metadata.mipLevels > D3D11_REQ_MIP_LEVELS)
			return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
		size_t bytes = 0;
		for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
			size_t rowPitch = 0, slicePitch = 0;
			if (FAILED(DirectX::ComputePitch(metadata.format, std::max<size_t>(1, metadata.width >> mip),
					std::max<size_t>(1, metadata.height >> mip), rowPitch, slicePitch)) ||
				slicePitch > MaxResourceBytes - bytes)
				return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
			bytes += slicePitch;
		}
		return DirectX::LoadFromDDSMemory(ddsData.data(), ddsData.size(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	}

	winrt::com_ptr<ID3D11ShaderResourceView> CreateDDSTextureFromImage(ID3D11Device* device, const DirectX::ScratchImage& image, const char* name)
	{
		if (!device || !name)
			throw std::invalid_argument("A device and resource name are required");
		winrt::com_ptr<ID3D11Resource> resource;
		DX::ThrowIfFailed(DirectX::CreateTexture(device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), resource.put()));
		SetResourceName(resource.get(), "%s", name);
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), nullptr, view.put()));
		SetResourceName(view.get(), "%s SRV", name);
		return view;
	}

	bool LoadR8HeightMapFromDDS(const DirectX::ScratchImage& image, HeightMap& map)
	{
		map = {};
		const auto* source = image.GetImage(0, 0, 0);
		DirectX::ScratchImage decoded;
		if (source && source->format == DXGI_FORMAT_BC4_UNORM) {
			if (SUCCEEDED(DirectX::Decompress(*source, DXGI_FORMAT_R8_UNORM, decoded)))
				source = decoded.GetImage(0, 0, 0);
		}
		if (source && source->format == DXGI_FORMAT_R8_UNORM)
			return map.Load(static_cast<uint32_t>(source->width), static_cast<uint32_t>(source->height), { source->pixels, source->slicePitch }, source->rowPitch);
		return false;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> CreateConstantFloatTexture(ID3D11Device* device, float value, const char* name)
	{
		if (!device || !name)
			throw std::invalid_argument("A device and resource name are required");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data{ &value, sizeof(value), 0 };
		winrt::com_ptr<ID3D11Texture2D> texture;
		DX::ThrowIfFailed(device->CreateTexture2D(&desc, &data, texture.put()));
		SetResourceName(texture.get(), "%s", name);
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture.get(), nullptr, view.put()));
		SetResourceName(view.get(), "%s SRV", name);
		return view;
	}

}
