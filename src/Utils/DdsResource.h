#pragma once
#include <d3d11.h>
#include <filesystem>
#include <winrt/base.h>
namespace DirectX
{
	class ScratchImage;
}
namespace Util
{
	class HeightMap;
	/** Decodes a complete DDS resource with a bounded input allocation. */
	HRESULT LoadDDSImageFromResource(const char* filename, DirectX::ScratchImage& image);
	/** Creates a named texture and view from an already decoded DDS image. */
	winrt::com_ptr<ID3D11ShaderResourceView> CreateDDSTextureFromImage(ID3D11Device* device, const DirectX::ScratchImage& image, const char* name);
	/** Copies an R8 or BC4 map from the GPU source image; failure leaves it empty. */
	bool LoadR8HeightMapFromDDS(const DirectX::ScratchImage& image, HeightMap& map);
	/** Creates a named immutable single-pixel float texture and its view. */
	winrt::com_ptr<ID3D11ShaderResourceView> CreateConstantFloatTexture(ID3D11Device* device, float value, const char* name);
}
