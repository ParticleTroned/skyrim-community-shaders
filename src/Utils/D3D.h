#pragma once
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

	/** @brief Restores compute shader, the first requested SRVs (up to three), UAV0, and CB0. */
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
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> shaderResources;
		uint32_t shaderResourceCount = 0;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
	};

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

	// Returns the current scene depth SRV, preferring terrain-blended depth when active.
	// The caller does NOT own the returned pointer.
	//
	// prefer16bit = false (default): R32_FLOAT  -- for compute shaders doing arithmetic on depth
	// prefer16bit = true:            R16_UNORM  -- for pixel shaders via slot 17 / SharedData::GetDepth
	ID3D11ShaderResourceView* GetCurrentSceneDepthSRV(bool prefer16bit = false);
}  // namespace Util
