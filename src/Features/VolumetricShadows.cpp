#include "VolumetricShadows.h"

#include <algorithm>
#include <array>
#include <limits>

#include "Globals.h"
#include "GpuPass.h"
#include "Profiler.h"
#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricShadows::Settings,
	Enabled)

namespace
{
	bool HasActiveDirectionalShadows()
	{
		return globals::state && globals::state->HasDirectionalShadows();
	}

	void DrawEnabledCheckbox(VolumetricShadows::Settings& a_settings)
	{
		ImGui::Checkbox("Enable", &a_settings.Enabled);
	}

	template <class T>
	void SafeRelease(T*& a_ptr)
	{
		if (a_ptr) {
			a_ptr->Release();
			a_ptr = nullptr;
		}
	}

	constexpr uint32_t kShadowCopySize = 512;
	constexpr uint32_t kBlurGroupSize = 128;

	void ClearComputeSRVs(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_count)
	{
		ID3D11ShaderResourceView* nullSRVs[2]{};
		a_context->CSSetShaderResources(a_startSlot, a_count, nullSRVs);
	}

	void ClearComputeUAV(ID3D11DeviceContext* a_context)
	{
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		a_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}
}

void VolumetricShadows::SetupResources()
{
	auto* device = globals::d3d::device;
	if (!device)
		return;

	if (resourceDevice != device) {
		ReleaseResources();
		resourceDevice = device;
	}

	if (!linearSampler) {
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));
		Util::SetResourceName(linearSampler, "VolumetricShadows::LinearSampler");
	}

	EnsureFallbackShadowTexture();
	CompileComputeShaders();
}

void VolumetricShadows::CompileComputeShaders()
{
	downsampleShadowMip0CS.Get(
		L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl",
		{ { "DOWNSAMPLE_SHADOW_MIP0", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::DownsampleMip0CS");
	downsampleShadowMip1CS.Get(
		L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl",
		{ { "DOWNSAMPLE_SHADOW_MIP1", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::DownsampleMip1CS");
	blurShadowHorizontalCS.Get(
		L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl",
		{ { "BLUR_HORIZONTAL", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::BlurHorizontalCS");
	blurShadowVerticalCS.Get(
		L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl",
		{ { "BLUR_VERTICAL", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::BlurVerticalCS");
}

void VolumetricShadows::EnsureFallbackShadowTexture()
{
	if (fallbackShadowTexture)
		return;

	auto* device = globals::d3d::device;
	if (!device)
		return;

	constexpr uint32_t fallbackSize = 2;
	constexpr uint16_t litMoment = std::numeric_limits<uint16_t>::max();
	std::array<uint16_t, fallbackSize * fallbackSize * 2> mip0{};
	std::array<uint16_t, 2> mip1{};
	mip0.fill(litMoment);
	mip1.fill(litMoment);

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = fallbackSize;
	texDesc.Height = fallbackSize;
	texDesc.MipLevels = 2;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initialData[2]{};
	initialData[0].pSysMem = mip0.data();
	initialData[0].SysMemPitch = fallbackSize * 2 * sizeof(uint16_t);
	initialData[1].pSysMem = mip1.data();
	initialData[1].SysMemPitch = 2 * sizeof(uint16_t);

	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, initialData, &fallbackShadowTexture));
	Util::SetResourceName(fallbackShadowTexture, "VolumetricShadows::FallbackShadow");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fallbackShadowTexture, &srvDesc, &fallbackShadowSRV));
	Util::SetResourceName(fallbackShadowSRV, "VolumetricShadows::FallbackShadow SRV");
}

void VolumetricShadows::EnsureShadowCopyTextures()
{
	if (shadowCopyTexture)
		return;

	auto* device = globals::d3d::device;

	shadowCopyWidth = kShadowCopySize;
	shadowCopyHeight = kShadowCopySize;

	D3D11_TEXTURE2D_DESC copyDesc{};
	copyDesc.Width = shadowCopyWidth;
	copyDesc.Height = shadowCopyHeight;
	copyDesc.MipLevels = 2;
	copyDesc.ArraySize = 1;
	copyDesc.Format = DXGI_FORMAT_R16G16_UNORM;
	copyDesc.SampleDesc.Count = 1;
	copyDesc.SampleDesc.Quality = 0;
	copyDesc.Usage = D3D11_USAGE_DEFAULT;
	copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;

	DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowCopyTexture));
	Util::SetResourceName(shadowCopyTexture, "VolumetricShadows::ShadowCopy");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = copyDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 2;
	DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopySRV));
	Util::SetResourceName(shadowCopySRV, "VolumetricShadows::ShadowCopy SRV");

	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip0SRV));
	Util::SetResourceName(shadowCopyMip0SRV, "VolumetricShadows::ShadowCopy SRV mip0");

	srvDesc.Texture2D.MostDetailedMip = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip1SRV));
	Util::SetResourceName(shadowCopyMip1SRV, "VolumetricShadows::ShadowCopy SRV mip1");

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = copyDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip0UAV));
	Util::SetResourceName(shadowCopyMip0UAV, "VolumetricShadows::ShadowCopy UAV mip0");

	uavDesc.Texture2D.MipSlice = 1;
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowCopyTexture, &uavDesc, &shadowCopyMip1UAV));
	Util::SetResourceName(shadowCopyMip1UAV, "VolumetricShadows::ShadowCopy UAV mip1");

	DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowBlurTempTexture));
	Util::SetResourceName(shadowBlurTempTexture, "VolumetricShadows::ShadowBlurTemp");

	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip0SRV));
	Util::SetResourceName(shadowBlurTempMip0SRV, "VolumetricShadows::ShadowBlurTemp SRV mip0");

	srvDesc.Texture2D.MostDetailedMip = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip1SRV));
	Util::SetResourceName(shadowBlurTempMip1SRV, "VolumetricShadows::ShadowBlurTemp SRV mip1");

	uavDesc.Texture2D.MipSlice = 0;
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip0UAV));
	Util::SetResourceName(shadowBlurTempMip0UAV, "VolumetricShadows::ShadowBlurTemp UAV mip0");

	uavDesc.Texture2D.MipSlice = 1;
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip1UAV));
	Util::SetResourceName(shadowBlurTempMip1UAV, "VolumetricShadows::ShadowBlurTemp UAV mip1");
}

void VolumetricShadows::CopyShadowLightData()
{
	auto* context = globals::d3d::context;
	if (!context)
		return;

	if (!settings.Enabled) {
		shadowCopyValid = false;
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	ZoneScoped;
	CS_GPU_PASS("VolumetricShadows::CopyShadowLightData");

	if (!globals::state->HasDirectionalShadows()) {
		shadowCopyValid = false;
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	SetupResources();
	if (!linearSampler) {
		shadowCopyValid = false;
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	EnsureShadowCopyTextures();
	shadowCopyValid = false;

	context->PSGetShaderResources(4, 1, &shadowView);
	if (!shadowView) {
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	auto* downsampleMip0 = downsampleShadowMip0CS.Get(
		L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl",
		{ { "DOWNSAMPLE_SHADOW_MIP0", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::DownsampleMip0CS");
	auto* downsampleMip1 = downsampleShadowMip1CS.Get(
		L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl",
		{ { "DOWNSAMPLE_SHADOW_MIP1", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::DownsampleMip1CS");
	auto* blurHorizontal = blurShadowHorizontalCS.Get(
		L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl",
		{ { "BLUR_HORIZONTAL", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::BlurHorizontalCS");
	auto* blurVertical = blurShadowVerticalCS.Get(
		L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl",
		{ { "BLUR_VERTICAL", nullptr } }, "cs_5_0", "main",
		"VolumetricShadows::BlurVerticalCS");
	if (!downsampleMip0 || !downsampleMip1 || !blurHorizontal || !blurVertical) {
		SetSharedShadowMapSRV(context, shadowView);
		SafeRelease(shadowView);
		return;
	}

	ID3D11Resource* shadowResource = nullptr;
	shadowView->GetResource(&shadowResource);

	ID3D11Texture2D* shadowTexture = nullptr;
	if (shadowResource) {
		shadowResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&shadowTexture));
		shadowResource->Release();
	}

	bool copiedShadow = false;
	if (shadowTexture) {
		D3D11_TEXTURE2D_DESC srcDesc{};
		shadowTexture->GetDesc(&srcDesc);

		auto* renderer = globals::game::renderer;
		auto& esramDepthStencil = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM];

		ID3D11ShaderResourceView* csSRVs[2]{ shadowView, esramDepthStencil.depthSRV };
		context->CSSetShaderResources(0, 2, csSRVs);
		context->CSSetSamplers(0, 1, &linearSampler);

		const uint32_t dispatchX = std::max(1u, (srcDesc.Width + 15) / 16);
		const uint32_t dispatchY = std::max(1u, (srcDesc.Height + 15) / 16);

		ID3D11UnorderedAccessView* csUAV = shadowCopyMip0UAV;
		context->CSSetUnorderedAccessViews(0, 1, &csUAV, nullptr);
		context->CSSetShader(downsampleMip0, nullptr, 0);
		{
			CS_GPU_PASS("VolumetricShadows::DownsampleMip0");
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		csUAV = shadowCopyMip1UAV;
		context->CSSetUnorderedAccessViews(0, 1, &csUAV, nullptr);
		context->CSSetShader(downsampleMip1, nullptr, 0);
		{
			CS_GPU_PASS("VolumetricShadows::DownsampleMip1");
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ClearComputeSRVs(context, 0, 2);
		ClearComputeUAV(context);

		auto blurMip = [&](ID3D11ShaderResourceView* a_input, ID3D11UnorderedAccessView* a_tempUAV, ID3D11ShaderResourceView* a_tempSRV, ID3D11UnorderedAccessView* a_outputUAV, uint32_t a_size, bool a_mip1) {
			ID3D11ShaderResourceView* blurSRV = a_input;
			context->CSSetShaderResources(0, 1, &blurSRV);
			context->CSSetUnorderedAccessViews(0, 1, &a_tempUAV, nullptr);
			context->CSSetShader(blurHorizontal, nullptr, 0);
			{
				CS_GPU_PASS_SELECT(a_mip1, "VolumetricShadows::BlurHMip1", "VolumetricShadows::BlurHMip0");
				context->Dispatch((a_size + kBlurGroupSize - 1) / kBlurGroupSize, a_size, 1);
			}

			ClearComputeSRVs(context, 0, 1);
			ClearComputeUAV(context);

			blurSRV = a_tempSRV;
			context->CSSetShaderResources(0, 1, &blurSRV);
			context->CSSetUnorderedAccessViews(0, 1, &a_outputUAV, nullptr);
			context->CSSetShader(blurVertical, nullptr, 0);
			{
				CS_GPU_PASS_SELECT(a_mip1, "VolumetricShadows::BlurVMip1", "VolumetricShadows::BlurVMip0");
				context->Dispatch(a_size, (a_size + kBlurGroupSize - 1) / kBlurGroupSize, 1);
			}

			ClearComputeSRVs(context, 0, 1);
			ClearComputeUAV(context);
		};

		blurMip(shadowCopyMip0SRV, shadowBlurTempMip0UAV, shadowBlurTempMip0SRV, shadowCopyMip0UAV, kShadowCopySize, false);
		blurMip(shadowCopyMip1SRV, shadowBlurTempMip1UAV, shadowBlurTempMip1SRV, shadowCopyMip1UAV, kShadowCopySize / 2, true);

		ID3D11SamplerState* nullSampler = nullptr;
		context->CSSetSamplers(0, 1, &nullSampler);
		context->CSSetShader(nullptr, nullptr, 0);

		shadowTexture->Release();
		shadowCopyValid = true;
		copiedShadow = true;
	}

	SetSharedShadowMapSRV(context, copiedShadow ? shadowCopySRV : shadowView);
	SafeRelease(shadowView);
}

void VolumetricShadows::SetSharedShadowMapSRV(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_srv)
{
	a_context->PSSetShaderResources(kSharedShadowMapShaderSlot, 1, &a_srv);
}

void VolumetricShadows::SetShaderResources(ID3D11DeviceContext* a_context)
{
	if (!a_context)
		return;

	const bool hasDirectionalShadows = settings.Enabled && globals::state && globals::state->HasDirectionalShadows();
	if (hasDirectionalShadows)
		SetupResources();

	ID3D11ShaderResourceView* srv = nullptr;
	if (hasDirectionalShadows)
		srv = shadowCopyValid ? shadowCopySRV : fallbackShadowSRV;
	SetSharedShadowMapSRV(a_context, srv);
}

void VolumetricShadows::DrawSettings()
{
	DrawEnabledCheckbox(settings);
	ImGui::BeginDisabled(!settings.Enabled);

	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		auto displayRT = [&](const char* a_label, ID3D11Texture2D* a_texture, ID3D11ShaderResourceView* a_srv) {
			if (!a_srv || !a_texture)
				return;

			D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(&desc);

			char label[128];
			snprintf(label, sizeof(label), "%s (%ux%u)", a_label, desc.Width, desc.Height);
			if (ImGui::TreeNode(label)) {
				ImGui::Image(a_srv, { desc.Width * debugRescale, desc.Height * debugRescale });
				ImGui::TreePop();
			}
		};

		displayRT("VSM Cascade 0", shadowCopyTexture, shadowCopyMip0SRV);
		displayRT("VSM Cascade 1", shadowCopyTexture, shadowCopyMip1SRV);

		ImGui::TreePop();
	}

	ImGui::EndDisabled();
}

void VolumetricShadows::DrawEssentialSettings()
{
	DrawEnabledCheckbox(settings);
}

void VolumetricShadows::DrawPerformanceSettings(bool)
{
	const bool hasActiveDirectionalShadows = HasActiveDirectionalShadows();
	ImGui::BeginDisabled(!hasActiveDirectionalShadows);
	DrawEnabledCheckbox(settings);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Disabling skips the directional shadow-map copy, downsample, and blur passes.");
	}
	if (!hasActiveDirectionalShadows)
		ImGui::TextDisabled("Volumetric Shadows has no active directional shadows in the current scene.");
}

json VolumetricShadows::CapturePerformanceSettingsState() const
{
	return {
		{ "Enabled", settings.Enabled }
	};
}

bool VolumetricShadows::IsPerformanceCostMeasurementEnabled() const
{
	return settings.Enabled && HasActiveDirectionalShadows();
}

bool VolumetricShadows::IsPerformanceCostMeasurementReady() const
{
	return HasActiveDirectionalShadows();
}

void VolumetricShadows::LoadSettings(json& o_json)
{
	settings = o_json;
}

void VolumetricShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VolumetricShadows::RestoreDefaultSettings()
{
	settings = {};
}

void VolumetricShadows::ClearShaderCache()
{
	ReleaseComputeShaders();
	CompileComputeShaders();
}

void VolumetricShadows::ReleaseComputeShaders()
{
	downsampleShadowMip0CS.Reset();
	downsampleShadowMip1CS.Reset();
	blurShadowHorizontalCS.Reset();
	blurShadowVerticalCS.Reset();
}

void VolumetricShadows::ReleaseTextures()
{
	SafeRelease(shadowView);
	SafeRelease(shadowCopySRV);
	SafeRelease(shadowCopyMip0SRV);
	SafeRelease(shadowCopyMip1SRV);
	SafeRelease(shadowCopyMip0UAV);
	SafeRelease(shadowCopyMip1UAV);
	SafeRelease(shadowCopyTexture);
	SafeRelease(shadowBlurTempMip0SRV);
	SafeRelease(shadowBlurTempMip1SRV);
	SafeRelease(shadowBlurTempMip0UAV);
	SafeRelease(shadowBlurTempMip1UAV);
	SafeRelease(shadowBlurTempTexture);
	SafeRelease(fallbackShadowSRV);
	SafeRelease(fallbackShadowTexture);
	shadowCopyWidth = 0;
	shadowCopyHeight = 0;
	shadowCopyValid = false;
}

void VolumetricShadows::ReleaseSampler()
{
	SafeRelease(linearSampler);
}

void VolumetricShadows::ReleaseResources()
{
	ReleaseComputeShaders();
	ReleaseTextures();
	ReleaseSampler();
}

struct CreateDepthStencil_VolumetricLighting
{
	static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		RE::BSGraphics::DepthStencilTargetProperties overriddenProperties = *a_properties;
		overriddenProperties.height = 1024;
		overriddenProperties.width = 1024;
		func(This, a_target, &overriddenProperties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void VolumetricShadows::PostPostLoad()
{
	stl::write_thunk_call<CreateDepthStencil_VolumetricLighting>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x9DC, 0x9DC, 0xC60));
}
