#pragma once
#include <d3d11_1.h>

#include "Buffer.h"
#include "Utils/LazyShader.h"

/** @brief Current main-depth max pyramid; incompatible targets fail open. */
class GrassHiZ
{
public:
	void SetupResources();
	void Reset();
	bool Build(ID3D11DeviceContext1* context, uint32_t frame);
	ID3D11ShaderResourceView* SRV() const { return srv.get(); }
	uint32_t Width() const { return width; }
	uint32_t Height() const { return height; }
	uint32_t Mips() const { return mipCount; }
	std::array<float, 4> Scale() const { return scale; }

private:
	void Allocate(uint32_t width, uint32_t height, uint32_t eyes);
	Util::LazyShader<ID3D11ComputeShader> shader;
	std::unique_ptr<Buffer> constants;
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> mipSRVs;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
	winrt::com_ptr<ID3D11Resource> sourceIdentity;
	winrt::com_ptr<ID3D11Resource> depthViewIdentity;
	winrt::com_ptr<ID3D11ShaderResourceView> depthView;
	uint32_t width = 0, height = 0, mipCount = 0, builtFrame = UINT32_MAX;
	uint32_t activeWidth = 0, activeHeight = 0;
	std::array<float, 4> scale{};
};
