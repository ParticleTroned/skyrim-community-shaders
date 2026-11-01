#include "LumaSharpen.h"

#include "../../../Util.h"
#include "../SharpenerDispatch.h"

#include <algorithm>

LumaSharpen::~LumaSharpen()
{
	delete lumaSharpenConfigCB;
	lumaSharpenConfigCB = nullptr;
}

void LumaSharpen::Initialize(bool enableMotionAdaptive)
{
	if (enableMotionAdaptive)
		motionAdaptive.Initialize();
	if (lumaSharpenConfigCB && lumaSharpenComputeShader)
		return;

	if (!lumaSharpenComputeShader)
		CreateComputeShader();
	if (!lumaSharpenConfigCB) {
		logger::info("[LumaSharpen] Creating resources");
		lumaSharpenConfigCB = new ConstantBuffer(ConstantBufferDesc<UpscalingSharpener::FixedConfig>());
	}
}

void LumaSharpen::ClearShaderCache()
{
	lumaSharpenComputeShader = nullptr;
	shaderUnavailableLogged = false;
	motionAdaptive.ClearShaderCache();
}

bool LumaSharpen::ApplyMotionAdaptiveSharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV,
	float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
	ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions)
{
	return motionAdaptive.Apply(inputSRV, outputUAV, sharpness, baseStrength, settings, motionVectors, regions,
		[&]() { return ApplySharpen(inputSRV, outputUAV, sharpness, regions); });
}

#ifdef DEVBENCH_BRIDGE_ENABLED
const char* LumaSharpen::GetMotionAdaptiveStatus() const noexcept
{
	return motionAdaptive.GetStatus();
}
#endif

void LumaSharpen::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	lumaSharpenComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\LumaSharpen\\LumaSharpen.hlsl", defines, "cs_5_0"));
}

bool LumaSharpen::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness,
	std::span<const MotionSharpening::Region> regions)
{
	if (!inputSRV || !outputUAV)
		return false;

	if (!lumaSharpenComputeShader || !lumaSharpenConfigCB)
		Initialize();

	if (!lumaSharpenComputeShader) {
		if (!shaderUnavailableLogged) {
			logger::warn("[LumaSharpen] Compute shader not compiled");
			shaderUnavailableLogged = true;
		}
		return false;
	}
	shaderUnavailableLogged = false;
	if (!lumaSharpenConfigCB) {
		logger::warn("[LumaSharpen] Constant buffer not initialized");
		return false;
	}

	UpscalingSharpener::FixedConfig config{};
	config.sharpness = std::clamp(sharpness, 0.0f, MotionSharpening::kMaximumLumaGain);
	config.limit = MotionSharpening::kLumaDetailLimit;

	return UpscalingSharpener::DispatchFixedComputePass(
		lumaSharpenComputeShader.get(),
		lumaSharpenConfigCB,
		config,
		inputSRV,
		outputUAV,
		UpscalingSharpener::Pass::LumaSharpen, regions);
}
