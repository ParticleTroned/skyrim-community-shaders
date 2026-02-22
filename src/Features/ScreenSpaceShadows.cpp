#include "ScreenSpaceShadows.h"

#include "FeatureConstraints.h"
#include "State.h"
#include "TerrainBlending.h"
#include "Util.h"
#include "VR.h"

#pragma warning(push)
#pragma warning(disable: 4838 4244)
#include "ScreenSpaceShadows/bend_sss_cpu.h"
#pragma warning(pop)

using RE::RENDER_TARGETS;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceShadows::BendSettings,
	Enable,
	SampleCount,
	SurfaceThickness,
	BilinearThreshold,
	ShadowContrast)

namespace
{
	const FeatureConstraints::SettingId kSssEnableSettingId{ "ScreenSpaceShadows", "Enable" };

	bool IsTbVrDepthCullingActive()
	{
		if (!globals::game::isVR) {
			return false;
		}

		auto& tb = globals::features::terrainBlending;
		auto& vr = globals::features::vr;
		return tb.loaded && tb.settings.Enabled && vr.gDepthBufferCulling && *vr.gDepthBufferCulling;
	}

	ID3D11ShaderResourceView* ResolveDepthSrvForSSS()
	{
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return nullptr;
		}

		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		ID3D11ShaderResourceView* resolved = depth.depthSRV;

		if (!IsTbVrDepthCullingActive()) {
			return resolved;
		}

		auto& tb = globals::features::terrainBlending;
		if (tb.blendedDepthTexture && tb.blendedDepthTexture->srv) {
			return tb.blendedDepthTexture->srv.get();
		}
		if (tb.blendedDepthTexture16 && tb.blendedDepthTexture16->srv) {
			return tb.blendedDepthTexture16->srv.get();
		}

		return resolved;
	}

	bool GetDepthSrvDimensions(ID3D11ShaderResourceView* a_depthSrv, uint32_t& o_width, uint32_t& o_height)
	{
		o_width = 0;
		o_height = 0;
		if (!a_depthSrv) {
			return false;
		}

		ID3D11Resource* resource = nullptr;
		a_depthSrv->GetResource(&resource);
		if (!resource) {
			return false;
		}

		ID3D11Texture2D* texture = nullptr;
		const HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
		resource->Release();

		if (FAILED(hr) || !texture) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		texture->Release();

		if (desc.Width == 0 || desc.Height == 0) {
			return false;
		}

		o_width = desc.Width;
		o_height = desc.Height;
		return true;
	}
}

void ScreenSpaceShadows::DrawSettings()
{
	if (ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool enabled = bendSettings.Enable != 0;
		if (Util::ConstrainedUI::Checkbox("Enable", &enabled, kSssEnableSettingId)) {
			bendSettings.Enable = enabled ? 1u : 0u;
		}
		ImGui::SliderInt("Sample Count Multiplier", (int*)&bendSettings.SampleCount, 1, 4);
		ImGui::SliderFloat("Surface Thickness", &bendSettings.SurfaceThickness, 0.005f, 0.05f);
		ImGui::SliderFloat("Bilinear Threshold", &bendSettings.BilinearThreshold, 0.02f, 1.0f);
		ImGui::SliderFloat("Shadow Contrast", &bendSettings.ShadowContrast, 0.0f, 4.0f);

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void ScreenSpaceShadows::ClearShaderCache()
{
	if (raymarchCS) {
		raymarchCS->Release();
		raymarchCS = nullptr;
	}
	if (raymarchRightCS) {
		raymarchRightCS->Release();
		raymarchRightCS = nullptr;
	}
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarch()
{
	static uint sampleCount = bendSettings.SampleCount;

	if (sampleCount != bendSettings.SampleCount) {
		sampleCount = bendSettings.SampleCount;
		if (raymarchCS) {
			raymarchCS->Release();
			raymarchCS = nullptr;
		}
	}

	if (!raymarchCS) {
		logger::debug("Compiling RaymarchCS");
		raymarchCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl", { { "SAMPLE_COUNT", std::format("{}", sampleCount * 64).c_str() } }, "cs_5_0");
	}
	return raymarchCS;
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarchRight()
{
	static uint sampleCount = bendSettings.SampleCount;

	if (sampleCount != bendSettings.SampleCount) {
		sampleCount = bendSettings.SampleCount;
		if (raymarchRightCS) {
			raymarchRightCS->Release();
			raymarchRightCS = nullptr;
		}
	}

	if (!raymarchRightCS) {
		logger::debug("Compiling RaymarchCS RIGHT");
		raymarchRightCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl", { { "SAMPLE_COUNT", std::format("{}", sampleCount * 64).c_str() }, { "RIGHT", "" } }, "cs_5_0");
	}
	return raymarchRightCS;
}

void ScreenSpaceShadows::DrawShadows()
{
	ZoneScoped;
	auto state = globals::state;
	TracyD3D11Zone(state->tracyCtx, "Screen Space Shadows");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto accumulator = *globals::game::currentAccumulator.get();
	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

	auto& directionNi = dirLight->GetWorldDirection();
	float3 light = { directionNi.x, directionNi.y, directionNi.z };
	light.Normalize();
	float4 lightProjection = float4(-light.x, -light.y, -light.z, 0.0f);

	Matrix viewProjMat = Util::GetCameraData(0).viewProjMat;

	lightProjection = DirectX::SimpleMath::Vector4::Transform(lightProjection, viewProjMat);
	float lightProjectionF[4] = { lightProjection.x, lightProjection.y, lightProjection.z, lightProjection.w };

	float2 size = Util::ConvertToDynamic(state->screenSize);
	int viewportSize[2] = { (int)size.x, (int)size.y };

	if (REL::Module::IsVR())
		viewportSize[0] /= 2;

	int minRenderBounds[2] = { 0, 0 };
	int maxRenderBounds[2] = { viewportSize[0], viewportSize[1] };

	ID3D11ShaderResourceView* depthSrv = ResolveDepthSrvForSSS();
	context->CSSetShaderResources(0, 1, &depthSrv);

	auto uav = screenSpaceShadowsTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetSamplers(0, 1, &pointBorderSampler);

	auto buffer = raymarchCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	context->CSSetShader(GetComputeRaymarch(), nullptr, 0);

	auto dispatchList = Bend::BuildDispatchList(lightProjectionF, viewportSize, minRenderBounds, maxRenderBounds);

	auto viewport = globals::game::graphicsState;

	float2 dynamicRes = { viewport->GetRuntimeData().dynamicResolutionWidthRatio, viewport->GetRuntimeData().dynamicResolutionHeightRatio };
	uint32_t depthWidth = 0;
	uint32_t depthHeight = 0;
	if (GetDepthSrvDimensions(depthSrv, depthWidth, depthHeight)) {
		if (globals::game::isVR) {
			// For VR shadow raymarching, viewport width is per-eye while bound depth can be
			// single-eye or double-wide. Scale X using the actual SRV size.
			dynamicRes.x = (static_cast<float>(viewportSize[0]) * 2.0f) / static_cast<float>(depthWidth);
			dynamicRes.y = static_cast<float>(viewportSize[1]) / static_cast<float>(depthHeight);
		} else {
			dynamicRes.x = static_cast<float>(viewportSize[0]) / static_cast<float>(depthWidth);
			dynamicRes.y = static_cast<float>(viewportSize[1]) / static_cast<float>(depthHeight);
		}
	}

	for (int i = 0; i < dispatchList.DispatchCount; i++) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSS - Ray March");

		auto dispatchData = dispatchList.Dispatch[i];

		RaymarchCB data{};
		data.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
		data.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
		data.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
		data.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];

		data.WaveOffset[0] = dispatchData.WaveOffset_Shader[0];
		data.WaveOffset[1] = dispatchData.WaveOffset_Shader[1];

		data.FarDepthValue = 1.0f;
		data.NearDepthValue = 0.0f;

		data.InvDepthTextureSize[0] = 1.0f / (float)viewportSize[0];
		data.InvDepthTextureSize[1] = 1.0f / (float)viewportSize[1];

		data.DynamicRes = dynamicRes;

		data.settings = bendSettings;

		raymarchCB->Update(data);

		context->Dispatch(dispatchData.WaveCount[0], dispatchData.WaveCount[1], dispatchData.WaveCount[2]);
	}

	if (globals::game::isVR) {
		lightProjection = float4(-light.x, -light.y, -light.z, 0.0f);

		viewProjMat = Util::GetCameraData(1).viewProjMat;

		lightProjection = DirectX::SimpleMath::Vector4::Transform(lightProjection, viewProjMat);

		float lightProjectionRightF[4] = { lightProjection.x, lightProjection.y, lightProjection.z, lightProjection.w };

		context->CSSetShader(GetComputeRaymarchRight(), nullptr, 0);

		dispatchList = Bend::BuildDispatchList(lightProjectionRightF, viewportSize, minRenderBounds, maxRenderBounds);

		for (int i = 0; i < dispatchList.DispatchCount; i++) {
			TracyD3D11Zone(globals::state->tracyCtx, "SSS - Ray March (VR Right Eye)");

			auto dispatchData = dispatchList.Dispatch[i];

			RaymarchCB data{};
			data.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
			data.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
			data.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
			data.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];

			data.WaveOffset[0] = dispatchData.WaveOffset_Shader[0];
			data.WaveOffset[1] = dispatchData.WaveOffset_Shader[1];

			data.FarDepthValue = 1.0f;
			data.NearDepthValue = 0.0f;

			data.InvDepthTextureSize[0] = 1.0f / (float)viewportSize[0];
			data.InvDepthTextureSize[1] = 1.0f / (float)viewportSize[1];

			data.DynamicRes = dynamicRes;

			data.settings = bendSettings;

			raymarchCB->Update(data);

			context->Dispatch(dispatchData.WaveCount[0], dispatchData.WaveCount[1], dispatchData.WaveCount[2]);
		}
	}

	ID3D11ShaderResourceView* views[1]{ nullptr };
	context->CSSetShaderResources(0, 1, views);

	ID3D11UnorderedAccessView* uavs[1]{ nullptr };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);
}

void ScreenSpaceShadows::Prepass()
{
	auto context = globals::d3d::context;

	float white[4] = { 1, 1, 1, 1 };
	context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTexture->uav.get(), white);

	bool enabled = bendSettings.Enable != 0;
	auto constraint = FeatureConstraints::GetConstraints(kSssEnableSettingId);
	if (constraint.isConstrained) {
		if (auto* forcedBool = std::get_if<bool>(&constraint.forcedValue)) {
			enabled = *forcedBool;
		}
	}

	if (auto sky = globals::game::sky)
		if (enabled && sky->mode.get() == RE::Sky::Mode::kFull)
			DrawShadows();

	auto view = screenSpaceShadowsTexture->srv.get();
	context->PSSetShaderResources(45, 1, &view);
}

void ScreenSpaceShadows::LoadSettings(json& o_json)
{
	bendSettings = o_json;
}

void ScreenSpaceShadows::SaveSettings(json& o_json)
{
	o_json = bendSettings;
}

void ScreenSpaceShadows::RestoreDefaultSettings()
{
	bendSettings = {};
}

bool ScreenSpaceShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

void ScreenSpaceShadows::SetupResources()
{
	raymarchCB = new ConstantBuffer(ConstantBufferDesc<RaymarchCB>());

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		samplerDesc.BorderColor[0] = 1.0f;
		samplerDesc.BorderColor[1] = 1.0f;
		samplerDesc.BorderColor[2] = 1.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointBorderSampler));
	}

	{
		auto renderer = globals::game::renderer;
		auto shadowMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		shadowMask.texture->GetDesc(&texDesc);
		shadowMask.SRV->GetDesc(&srvDesc);

		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		srvDesc.Format = texDesc.Format;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		screenSpaceShadowsTexture = new Texture2D(texDesc);
		screenSpaceShadowsTexture->CreateSRV(srvDesc);
		screenSpaceShadowsTexture->CreateUAV(uavDesc);
	}
}
