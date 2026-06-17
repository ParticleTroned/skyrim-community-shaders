#include "RCAS.h"

#include "../SharpenerDispatch.h"
#include "../../../Util.h"

struct RCASConfig
{
	float sharpness;
	float3 pad;
};

RCAS::~RCAS()
{
	delete rcasConfigCB;
	rcasConfigCB = nullptr;
}

void RCAS::Initialize()
{
	if (rcasConfigCB && rcasComputeShader)
		return;

	logger::info("[RCAS] Creating resources");
	if (!rcasComputeShader)
		CreateComputeShader();
	if (!rcasConfigCB)
		rcasConfigCB = new ConstantBuffer(ConstantBufferDesc<RCASConfig>());
}

void RCAS::ClearShaderCache()
{
	rcasComputeShader = nullptr;
}

void RCAS::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	rcasComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
}

bool RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness, uint32_t width, uint32_t height)
{
	auto state = globals::state;
	if (!state || !inputSRV || !outputUAV)
		return false;

	ZoneScoped;
	TracyD3D11Zone(state->tracyCtx, "RCAS Sharpening");

	if (!rcasComputeShader || !rcasConfigCB)
		Initialize();

	if (!rcasComputeShader) {
		logger::warn("[RCAS] Compute shader not compiled");
		return false;
	}
	if (!rcasConfigCB) {
		logger::warn("[RCAS] Constant buffer not initialized");
		return false;
	}

	uint32_t screenWidth = width ? width : static_cast<uint32_t>(state->screenSize.x);
	uint32_t screenHeight = height ? height : static_cast<uint32_t>(state->screenSize.y);
	if (!screenWidth || !screenHeight)
		return false;

	RCASConfig config{};
	config.sharpness = sharpness;

	return UpscalingSharpener::DispatchComputePass(
		rcasComputeShader.get(),
		rcasConfigCB,
		config,
		inputSRV,
		outputUAV,
		screenWidth,
		screenHeight,
		"RCAS Sharpening",
		"Upscaling::RCAS");
}
