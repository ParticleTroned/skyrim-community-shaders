#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	/** Owned immutable resource metadata, local to one validation/preparation operation. */
	struct TextureInfo
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		D3D11_TEXTURE2D_DESC desc{};
	};

	/** Capture the description from the same retained interface. */
	inline bool GetTextureInfo(ID3D11Resource* resource, TextureInfo& info) noexcept
	{
		info = {};
		if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&info.texture))))
			return false;
		info.texture->GetDesc(&info.desc);
		return true;
	}
}
