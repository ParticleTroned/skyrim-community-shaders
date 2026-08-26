#include "RCAS.h"

#include "../../../Util.h"
#include "../SharpenerDispatch.h"

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

bool RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
{
	if (!inputSRV || !outputUAV)
		return false;

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

	RCASConfig config{};
	config.sharpness = sharpness;

	return UpscalingSharpener::DispatchComputePass(
		rcasComputeShader.get(),
		rcasConfigCB,
		config,
		inputSRV,
		outputUAV,
		UpscalingSharpener::Pass::RCAS);
}
