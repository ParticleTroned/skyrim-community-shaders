#pragma once
#include "ResourceName.h"
#include "ShaderCompiler.h"
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	/** @brief Returns whether two COM interfaces expose the same IUnknown identity. */
	[[nodiscard]] bool HaveSameCOMIdentity(
		IUnknown* a_first,
		IUnknown* a_second);
	/** @brief Returns whether two valid COM interfaces expose distinct identities. */
	[[nodiscard]] bool HaveDistinctCOMIdentity(
		IUnknown* a_first,
		IUnknown* a_second);

	/** @brief Restores compute shader, the first one or two SRVs, UAV0, and CB0. */
	class ScopedComputeBindings
	{
	public:
		explicit ScopedComputeBindings(
			ID3D11DeviceContext* a_context,
			uint32_t a_shaderResourceCount = 1);
		~ScopedComputeBindings();

		ScopedComputeBindings(const ScopedComputeBindings&) = delete;
		ScopedComputeBindings& operator=(const ScopedComputeBindings&) = delete;

	private:
		ID3D11DeviceContext* context = nullptr;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::array<winrt::com_ptr<ID3D11ClassInstance>,
			D3D11_SHADER_MAX_INTERFACES>
			classInstances;
		UINT classInstanceCount = 0;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> shaderResources;
		uint32_t shaderResourceCount = 0;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
	};

	/**
	 * @brief Reads the Texture2D description backing a D3D11 view.
	 * @param a_view SRV, UAV, RTV, or DSV whose resource should be inspected.
	 * @param o_desc Receives the underlying Texture2D description on success.
	 * @return true when the view references a Texture2D resource.
	 */
	bool GetTexture2DDesc(ID3D11View* a_view, D3D11_TEXTURE2D_DESC& o_desc);
	bool TryGetDepthSrvDimensions(ID3D11ShaderResourceView* a_depthSrv, uint32_t& o_width, uint32_t& o_height);

	ID3D11ShaderResourceView* GetSRVFromRTV(ID3D11RenderTargetView* a_rtv);
	ID3D11RenderTargetView* GetRTVFromSRV(ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromSRV(const ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromRTV(const ID3D11RenderTargetView* a_rtv);

	void BindFrameBufferConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForPS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindGlobalConstantBuffersForCS(ID3D11DeviceContext* a_context);

	// Texture manipulation utilities
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor = { 1.0f, 0.5f, 0.0f, 0.3f });
	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV);

	// Native single-view render-target bounds
	inline int GetRenderTargetCount()
	{
		return RE::RENDER_TARGETS::kTOTAL;
	}

	inline int GetDepthStencilCount()
	{
		return RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL;
	}

	/** @brief Checks a texture-backed render-target index; the material sentinel -1 is not a target. */
	[[nodiscard]] inline bool IsValidRenderTargetIndex(std::int32_t index)
	{
		return index >= 0 && index < GetRenderTargetCount();
	}

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
