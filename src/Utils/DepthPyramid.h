#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"
#include <d3d11_1.h>

/** @brief Conservative max-depth mip chains with independent packed stereo eyes. */
class DepthPyramid
{
public:
	void SetupResources();
	void Reset();
	/** @brief Allocate a power-of-two chain stopping before stereo eyes would mix. */
	void Allocate(uint32_t width, uint32_t height, uint32_t eyes);
	/** @brief Build from live depth; unsupported SPD devices retain per-mip reduction. */
	bool Build(ID3D11DeviceContext1* context, ID3D11ShaderResourceView* source,
		uint32_t sourceWidth, uint32_t sourceHeight, uint32_t activeWidth, uint32_t activeHeight, uint32_t eyes);
	ID3D11ShaderResourceView* SRV() const { return valid ? srv.get() : nullptr; }
	bool Allocated() const { return texture != nullptr; }
	uint32_t Mips() const { return mipCount; }

private:
	Util::LazyShader<ID3D11ComputeShader> baseShader, spdShader;
	std::unique_ptr<Buffer> constants, spdConstants, counter;
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> mipSRVs;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
	uint32_t width = 0, height = 0, mipCount = 0;
	uint32_t eyeCount = 0;
	bool valid = false;
};
