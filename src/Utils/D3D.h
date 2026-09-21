#pragma once
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	bool TryGetDepthSrvDimensions(ID3D11ShaderResourceView* a_depthSrv, uint32_t& o_width, uint32_t& o_height);

	ID3D11ShaderResourceView* GetSRVFromRTV(ID3D11RenderTargetView* a_rtv);
	ID3D11RenderTargetView* GetRTVFromSRV(ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromSRV(const ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromRTV(const ID3D11RenderTargetView* a_rtv);
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program = "main");
	void BindFrameBufferConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForPS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindGlobalConstantBuffersForCS(ID3D11DeviceContext* a_context);

	// Texture manipulation utilities
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor = { 1.0f, 0.5f, 0.0f, 0.3f });
	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV);

	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex);
	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV);

	/**
	 * @brief Returns prepass depth until completed opaque depth is copied this frame.
	 * @param prefer16bit Selects Terrain Blending's R16_UNORM prepass texture;
	 * ignored once the final opaque depth copy is available.
	 * @return Borrowed depth SRV, or nullptr when rendering resources are unavailable.
	 */
	ID3D11ShaderResourceView* GetCurrentSceneDepthSRV(bool prefer16bit = false);
}  // namespace Util
