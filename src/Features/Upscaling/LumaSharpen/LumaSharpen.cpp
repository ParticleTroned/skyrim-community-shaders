#include "LumaSharpen.h"

#include "../../../Util.h"
#include "../SharpenerDispatch.h"

#include <algorithm>

struct LumaSharpenConfig
{
	float sharpness;
	float limit;
	float pad[2];
};

LumaSharpen::~LumaSharpen()
{
	delete lumaSharpenConfigCB;
	lumaSharpenConfigCB = nullptr;
}

void LumaSharpen::Initialize()
{
	if (lumaSharpenConfigCB && lumaSharpenComputeShader)
		return;

	logger::info("[LumaSharpen] Creating resources");
	if (!lumaSharpenComputeShader)
		CreateComputeShader();
	if (!lumaSharpenConfigCB)
		lumaSharpenConfigCB = new ConstantBuffer(ConstantBufferDesc<LumaSharpenConfig>());
}

void LumaSharpen::ClearShaderCache()
{
	lumaSharpenComputeShader = nullptr;
}

void LumaSharpen::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	lumaSharpenComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\LumaSharpen\\LumaSharpen.hlsl", defines, "cs_5_0"));
}

bool LumaSharpen::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness, uint32_t width, uint32_t height)
{
	auto state = globals::state;
	if (!state || !inputSRV || !outputUAV)
		return false;

	ZoneScoped;
	TracyD3D11Zone(state->tracyCtx, "Luma Sharpening");

	if (!lumaSharpenComputeShader || !lumaSharpenConfigCB)
		Initialize();

	if (!lumaSharpenComputeShader) {
		logger::warn("[LumaSharpen] Compute shader not compiled");
		return false;
	}
	if (!lumaSharpenConfigCB) {
		logger::warn("[LumaSharpen] Constant buffer not initialized");
		return false;
	}

	uint32_t screenWidth = width ? width : static_cast<uint32_t>(state->screenSize.x);
	uint32_t screenHeight = height ? height : static_cast<uint32_t>(state->screenSize.y);
	if (!screenWidth || !screenHeight)
		return false;

	LumaSharpenConfig config{};
	config.sharpness = std::clamp(sharpness, 0.0f, 2.5f);
	config.limit = 0.75f;

	return UpscalingSharpener::DispatchComputePass(
		lumaSharpenComputeShader.get(),
		lumaSharpenConfigCB,
		config,
		inputSRV,
		outputUAV,
		screenWidth,
		screenHeight,
		"Luma Sharpening");
}
