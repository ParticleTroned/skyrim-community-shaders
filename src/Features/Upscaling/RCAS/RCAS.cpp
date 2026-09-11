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

void RCAS::Initialize(bool enableMotionAdaptive)
{
	if (enableMotionAdaptive)
		motionAdaptive.Initialize();
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
	motionAdaptive.ClearShaderCache();
}

bool RCAS::ApplyMotionAdaptiveSharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV,
	float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
	ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions)
{
	return motionAdaptive.Apply(inputSRV, outputUAV, sharpness, baseStrength, settings, motionVectors, regions,
		[&]() { return ApplySharpen(inputSRV, outputUAV, sharpness); });
}

const char* RCAS::GetMotionAdaptiveStatus() const noexcept
{
	return motionAdaptive.GetStatus();
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
