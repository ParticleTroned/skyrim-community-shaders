#pragma once

#include "Policy.h"
#include "TextureDiagnostics.h"
#include <DirectXTex.h>
#include <cstdint>
#include <d3d11.h>
#include <span>
#include <wrl/client.h>

namespace StreamingTextures
{
	/** DDS provenance lives on the COM resource, so pointer reuse cannot inherit eligibility. */
	struct Origin
	{
		std::uint64_t serial = 0;
		bool protectedConsumer = false;
		std::uint32_t requiredCategories = 0;
		bool preloaded = false;
	};
	bool ReadOrigin(ID3D11Resource* resource, Origin& origin);
	/** Preserve identity and shared-consumer requirements when a DDS load reuses its resource. */
	HRESULT WriteOrigin(ID3D11Resource* resource, const Origin& origin);
	/** Validate the GPU contract and optional engine metadata; zero engine fields are unavailable. */
	TextureRejection ClassifyTexture(const D3D11_TEXTURE2D_DESC& texture, const D3D11_SHADER_RESOURCE_VIEW_DESC& view,
		std::uint32_t engineWidth = 0, std::uint32_t engineHeight = 0, std::uint32_t engineMips = 0);
	bool Suitable(const D3D11_TEXTURE2D_DESC& texture, const D3D11_SHADER_RESOURCE_VIEW_DESC& view);
	std::uint64_t LogicalBytes(const D3D11_TEXTURE2D_DESC& full, std::uint32_t drop);
	bool MatchesDDS(const DirectX::ScratchImage& image, const D3D11_TEXTURE2D_DESC& full);
	/** Validate a bounded DDS header and source length before discarding resident top mips. */
	bool MatchesDDSHeader(std::span<const std::uint8_t> header, std::uint64_t fileBytes, const D3D11_TEXTURE2D_DESC& full);

	/** Keep available engine dimensions consistent with the current GPU resource. */
	template <class Texture>
	void UpdateEngineMetadata(Texture& engine, const D3D11_TEXTURE2D_DESC& desc)
	{
		if (engine.width)
			engine.width = static_cast<decltype(engine.width)>(desc.Width);
		if (engine.height)
			engine.height = static_cast<decltype(engine.height)>(desc.Height);
		if (engine.mips)
			engine.mips = static_cast<decltype(engine.mips)>(desc.MipLevels);
	}

	/** A private replacement, incrementally uploaded or GPU-copied before publication. */
	struct Upload
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		Microsoft::WRL::ComPtr<ID3D11Query> fence;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> copySource;
		D3D11_TEXTURE2D_DESC desc{};
		std::uint32_t drop = 0, mip = 0, row = 0, copySourceDrop = 0;
		bool submitted = false;
		HRESULT Begin(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc, std::uint32_t skip);
		/** Upload at most the byte budget, without modifying context bindings. */
		HRESULT Advance(ID3D11DeviceContext* context, const DirectX::ScratchImage& source, std::uint64_t bytes);
		/** Allocate a smaller replacement for compatible lower mips already resident on this device. */
		HRESULT BeginCopy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& full, D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc,
			std::uint32_t skip, ID3D11Resource* source, std::uint32_t sourceDrop);
		/** Copy at most the byte budget, preserving bindings and retaining the source until retirement. */
		HRESULT AdvanceCopy(ID3D11DeviceContext* context, std::uint64_t bytes);

	private:
		HRESULT Transfer(ID3D11DeviceContext* context, const DirectX::ScratchImage* source, std::uint64_t bytes);
	};
}
