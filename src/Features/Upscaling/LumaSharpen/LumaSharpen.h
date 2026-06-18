#pragma once

#include "../../../Buffer.h"
#include "../../../State.h"

#include <cstdint>
#include <d3d11_4.h>
#include <winrt/base.h>

/**
 * @brief Luma-only adaptive unsharp mask for DLSS output.
 *
 * Uses a 5-tap cross blur and applies the sharpened detail only to luminance,
 * preserving chroma ratio and alpha.
 */
class LumaSharpen
{
public:
	LumaSharpen() = default;
	~LumaSharpen();

	void Initialize();
	void ClearShaderCache();

	bool ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness, uint32_t width = 0, uint32_t height = 0);

private:
	void CreateComputeShader();

	winrt::com_ptr<ID3D11ComputeShader> lumaSharpenComputeShader;
	ConstantBuffer* lumaSharpenConfigCB = nullptr;
};
