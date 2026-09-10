#include "RCAS.h"

#include "../../../Util.h"
#include "../SharpenerDispatch.h"

#include <array>

struct RCASConfig
{
	float sharpness;
	float3 pad;
};

struct MotionAdaptiveRCASConfig
{
	RCASConfig fixed;
	MotionSharpening::Rect output;
	MotionSharpening::Rect source;
	float motionToOutputPixels[2];
	float baseStrength;
	float adjustment;
	float thresholdPixels;
	float strengthCap;
	float maximumGain;
	float pad;
};
STATIC_ASSERT_ALIGNAS_16(MotionAdaptiveRCASConfig);

RCAS::~RCAS()
{
	delete rcasConfigCB;
	rcasConfigCB = nullptr;
}

void RCAS::Initialize(bool motionAdaptive)
{
	if (motionAdaptive)
		EnsureMotionAdaptiveResources();
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
	motionAdaptiveComputeShader = nullptr;
	motionAdaptiveShaderFailed = false;
}

bool RCAS::EnsureMotionAdaptiveResources()
{
	if (motionAdaptiveShaderFailed)
		return false;
	try {
		if (!motionAdaptiveComputeShader) {
			std::vector<std::pair<const char*, const char*>> defines{ { "MOTION_ADAPTIVE", "1" } };
			motionAdaptiveComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(
				L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
		}
		if (!motionAdaptiveComputeShader) {
			motionAdaptiveShaderFailed = true;
			logger::warn("[RCAS] Motion-adaptive shader unavailable; retaining fixed sharpening.");
			return false;
		}
		if (!motionAdaptiveConfigCB)
			motionAdaptiveConfigCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<MotionAdaptiveRCASConfig>(), "Upscaling::MotionAdaptiveRCASConfig");
		return true;
	} catch (const std::exception& error) {
		motionAdaptiveShaderFailed = true;
		logger::warn("[RCAS] Motion-adaptive setup failed; retaining fixed sharpening: {}", error.what());
		return false;
	}
}

bool RCAS::ApplyMotionAdaptiveSharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV,
	float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
	ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions)
{
	const auto fixedFallback = [&](MotionStatus reason = MotionStatus::InvalidGeometry) {
		motionStatus.store(reason, std::memory_order_relaxed);
		return ApplySharpen(inputSRV, outputUAV, sharpness);
	};
	const auto sanitized = MotionSharpening::Sanitize(settings);
	if (!sanitized.enabled)
		return fixedFallback(MotionStatus::Disabled);
	if (!motionVectors || regions.empty())
		return fixedFallback(MotionStatus::MotionUnavailable);
	if (motionAdaptiveShaderFailed)
		return fixedFallback(MotionStatus::ShaderUnavailable);
	if (regions.size() > 2)
		return fixedFallback();

	D3D11_TEXTURE2D_DESC colorDesc{}, motionDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC colorView{}, motionView{};
	uint32_t outputWidth = 0, outputHeight = 0;
	if (!inputSRV || !Util::GetTexture2DDesc(inputSRV, colorDesc) ||
		!Util::GetTexture2DDesc(motionVectors, motionDesc) ||
		!UpscalingSharpener::TryGetOutputDimensions(outputUAV, outputWidth, outputHeight))
		return fixedFallback();
	inputSRV->GetDesc(&colorView);
	motionVectors->GetDesc(&motionView);
	if (colorView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || colorView.Texture2D.MostDetailedMip != 0 ||
		motionView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || motionView.Texture2D.MostDetailedMip != 0 ||
		colorDesc.Width != outputWidth || colorDesc.Height != outputHeight ||
		!std::isfinite(baseStrength))
		return fixedFallback();

	uint64_t coveredPixels = 0;
	for (const auto& region : regions) {
		if (!MotionSharpening::IsValid(region, outputWidth, outputHeight, motionDesc.Width, motionDesc.Height))
			return fixedFallback();
		coveredPixels += static_cast<uint64_t>(region.output.width) * region.output.height;
	}
	if (coveredPixels != static_cast<uint64_t>(outputWidth) * outputHeight ||
		(regions.size() == 2 &&
			(regions[0].output.y != 0 || regions[1].output.y != 0 || regions[0].output.x != 0 ||
				regions[0].output.height != outputHeight || regions[1].output.height != outputHeight ||
				regions[1].output.x != regions[0].output.width)))
		return fixedFallback();

	if (!EnsureMotionAdaptiveResources())
		return fixedFallback(MotionStatus::ShaderUnavailable);

	auto* context = globals::d3d::context;
	if (!context)
		return fixedFallback(MotionStatus::DispatchFailed);
	ID3D11ComputeShader* previousShader = nullptr;
	std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> previousInstances{};
	UINT previousInstanceCount = static_cast<UINT>(previousInstances.size());
	std::array<ID3D11ShaderResourceView*, 2> previousSRVs{};
	ID3D11UnorderedAccessView* previousUAV = nullptr;
	ID3D11Buffer* previousCB = nullptr;
	context->CSGetShader(&previousShader, previousInstances.data(), &previousInstanceCount);
	context->CSGetShaderResources(0, 2, previousSRVs.data());
	context->CSGetUnorderedAccessViews(0, 1, &previousUAV);
	context->CSGetConstantBuffers(0, 1, &previousCB);
	const SKSE::stl::scope_exit restoreState([&]() noexcept {
		ID3D11ShaderResourceView* nullSRVs[2] = {};
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 2, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetShader(previousShader, previousInstances.data(), previousInstanceCount);
		context->CSSetConstantBuffers(0, 1, &previousCB);
		context->CSSetShaderResources(0, 2, previousSRVs.data());
		context->CSSetUnorderedAccessViews(0, 1, &previousUAV, nullptr);
		if (previousShader)
			previousShader->Release();
		for (UINT i = 0; i < previousInstanceCount; ++i)
			if (previousInstances[i])
				previousInstances[i]->Release();
		for (auto* srv : previousSRVs)
			if (srv)
				srv->Release();
		if (previousUAV)
			previousUAV->Release();
		if (previousCB)
			previousCB->Release();
	});

	try {
		for (const auto& region : regions) {
			const MotionAdaptiveRCASConfig config{
				.fixed = { sharpness, {} },
				.output = region.output,
				.source = region.source,
				.motionToOutputPixels = {
					MotionSharpening::MotionToOutputPixels(region.sourceEye.width, region.source.width, region.output.width),
					MotionSharpening::MotionToOutputPixels(region.sourceEye.height, region.source.height, region.output.height) },
				.baseStrength = std::clamp(baseStrength, 0.0f, 1.0f),
				.adjustment = sanitized.adjustment,
				.thresholdPixels = sanitized.thresholdPixels,
				.strengthCap = sanitized.strengthCap,
				.maximumGain = MotionSharpening::kMaximumRCASGain,
			};
			if (!UpscalingSharpener::DispatchComputePass(motionAdaptiveComputeShader.get(), motionAdaptiveConfigCB.get(), config,
					inputSRV, outputUAV, UpscalingSharpener::Pass::RCAS, motionVectors, region.output.width, region.output.height))
				return fixedFallback(MotionStatus::DispatchFailed);
		}
		motionStatus.store(MotionStatus::Applied, std::memory_order_relaxed);
		return true;
	} catch (const std::exception& error) {
		logger::warn("[RCAS] Motion-adaptive dispatch failed; retaining fixed sharpening: {}", error.what());
		return fixedFallback(MotionStatus::DispatchFailed);
	}
}

const char* RCAS::GetMotionAdaptiveStatus() const noexcept
{
	switch (motionStatus.load(std::memory_order_relaxed)) {
	case MotionStatus::NotDispatched:
		return "not_dispatched";
	case MotionStatus::Disabled:
		return "disabled";
	case MotionStatus::MotionUnavailable:
		return "fixed_motion_unavailable";
	case MotionStatus::InvalidGeometry:
		return "fixed_invalid_geometry";
	case MotionStatus::ShaderUnavailable:
		return "fixed_shader_unavailable";
	case MotionStatus::Applied:
		return "applied";
	case MotionStatus::DispatchFailed:
		return "fixed_dispatch_failed";
	default:
		return "unknown";
	}
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
