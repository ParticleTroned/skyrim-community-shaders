#pragma once
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	enum class VRDepthLayout
	{
		PerEye,
		CombinedStereo,
		Unknown
	};

	VRDepthLayout DetectVRDepthLayout(uint32_t a_depthWidth, int a_viewportWidthPerEye);

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
	std::string GetNameFromSRV(ID3D11ShaderResourceView* a_srv);
	std::string GetNameFromRTV(ID3D11RenderTargetView* a_rtv);
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);

	/** @brief Optional QPC accumulator separating compiler and device creation cost. */
	struct ShaderCompileTiming
	{
		uint64_t bytecodeCompilationQpcTicks = 0;
		uint64_t d3dObjectCreationQpcTicks = 0;
	};

	ID3D11DeviceChild* CompileShader(
		const wchar_t* FilePath,
		const std::vector<std::pair<const char*, const char*>>& Defines,
		const char* ProgramType,
		const char* Program = "main",
		ShaderCompileTiming* a_timing = nullptr);
	void BindFrameBufferConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForPS(ID3D11DeviceContext* a_context);
	void BindSharedDataConstantBuffersForCS(ID3D11DeviceContext* a_context);
	void BindGlobalConstantBuffersForCS(ID3D11DeviceContext* a_context);

	// Texture manipulation utilities
	void ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted, const std::array<float, 4>& highlightColor = { 1.0f, 0.5f, 0.0f, 0.3f });
	HRESULT CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV);

	// VR-aware counts for render targets
	inline int GetRenderTargetCount()
	{
		return REL::Module::IsVR() ? RE::RENDER_TARGETS::kVRTOTAL : RE::RENDER_TARGETS::kTOTAL;
	}

	inline int GetDepthStencilCount()
	{
		return REL::Module::IsVR() ? RE::RENDER_TARGETS_DEPTHSTENCIL::kVRTOTAL : RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL;
	}

	HRESULT SaveTextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, ID3D11Texture2D* tex);
	HRESULT LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path, ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSRV);

	// Returns the current scene depth SRV, preferring terrain-blended depth when active.
	// The caller does NOT own the returned pointer.
	//
	// prefer16bit = false (default): R32_FLOAT  -- for compute shaders doing arithmetic on depth
	// prefer16bit = true:            R16_UNORM  -- for pixel shaders via slot 17 / SharedData::GetDepth
	ID3D11ShaderResourceView* GetCurrentSceneDepthSRV(bool prefer16bit = false);
}  // namespace Util
