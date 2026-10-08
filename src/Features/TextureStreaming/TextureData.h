#pragma once

#include "Policy.h"
#include <DirectXTex.h>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace StreamingTextures
{
	/** DDS provenance lives on the COM resource, so pointer reuse cannot inherit eligibility. */
	struct Origin
	{
		std::uint64_t serial = 0;
		bool protectedConsumer = false;
	};
	bool ReadOrigin(ID3D11Resource* resource, Origin& origin);
	/** Preserve an existing identity and sticky protection when a DDS load reuses its resource. */
	HRESULT WriteOrigin(ID3D11Resource* resource, const Origin& origin);
	bool Suitable(const D3D11_TEXTURE2D_DESC& texture, const D3D11_SHADER_RESOURCE_VIEW_DESC& view);
	std::uint64_t LogicalBytes(const D3D11_TEXTURE2D_DESC& full, std::uint32_t drop);
	bool MatchesDDS(const DirectX::ScratchImage& image, const D3D11_TEXTURE2D_DESC& full);

	/** A private, incrementally uploaded replacement; never visible before completion. */
	struct Upload
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		Microsoft::WRL::ComPtr<ID3D11Query> fence;
		D3D11_TEXTURE2D_DESC desc{};
		std::uint32_t drop = 0, mip = 0, row = 0;
		bool submitted = false;
		HRESULT Begin(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc, std::uint32_t skip);
		/** Upload at most the byte budget, without modifying context bindings. */
		HRESULT Advance(ID3D11DeviceContext* context, const DirectX::ScratchImage& source, std::uint64_t bytes);
	};
}
