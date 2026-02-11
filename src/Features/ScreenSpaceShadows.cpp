#include "ScreenSpaceShadows.h"

#include "State.h"
#include "Features/Upscaling.h"

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

void ScreenSpaceShadows::DrawSettings()
{
	if (ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable", (bool*)&bendSettings.Enable);
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
	if (raymarchPerEyeCS) {
		raymarchPerEyeCS->Release();
		raymarchPerEyeCS = nullptr;
	}
	if (raymarchRightPerEyeCS) {
		raymarchRightPerEyeCS->Release();
		raymarchRightPerEyeCS = nullptr;
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

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarchPerEye()
{
	static uint sampleCount = bendSettings.SampleCount;

	if (sampleCount != bendSettings.SampleCount) {
		sampleCount = bendSettings.SampleCount;
		if (raymarchPerEyeCS) {
			raymarchPerEyeCS->Release();
			raymarchPerEyeCS = nullptr;
		}
	}

	if (!raymarchPerEyeCS) {
		logger::debug("Compiling RaymarchCS (Per-Eye)");
		raymarchPerEyeCS = (ID3D11ComputeShader*)Util::CompileShader(
			L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl",
			{ { "SAMPLE_COUNT", std::format("{}", sampleCount * 64).c_str() }, { "SSS_PER_EYE", "" } },
			"cs_5_0");
	}
	return raymarchPerEyeCS;
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarchRightPerEye()
{
	static uint sampleCount = bendSettings.SampleCount;

	if (sampleCount != bendSettings.SampleCount) {
		sampleCount = bendSettings.SampleCount;
		if (raymarchRightPerEyeCS) {
			raymarchRightPerEyeCS->Release();
			raymarchRightPerEyeCS = nullptr;
		}
	}

	if (!raymarchRightPerEyeCS) {
		logger::debug("Compiling RaymarchCS RIGHT (Per-Eye)");
		raymarchRightPerEyeCS = (ID3D11ComputeShader*)Util::CompileShader(
			L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl",
			{ { "SAMPLE_COUNT", std::format("{}", sampleCount * 64).c_str() }, { "RIGHT", "" }, { "SSS_PER_EYE", "" } },
			"cs_5_0");
	}
	return raymarchRightPerEyeCS;
}

void ScreenSpaceShadows::DrawShadows()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Screen Space Shadows");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	// Ensure output texture matches current shadow mask size (can change across modes).
	auto shadowMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK];
	D3D11_TEXTURE2D_DESC maskDesc{};
	shadowMask.texture->GetDesc(&maskDesc);
	if (!screenSpaceShadowsTexture || screenSpaceShadowsTexture->desc.Width != maskDesc.Width ||
		screenSpaceShadowsTexture->desc.Height != maskDesc.Height) {
		delete screenSpaceShadowsTexture;
		screenSpaceShadowsTexture = nullptr;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};

		shadowMask.SRV->GetDesc(&srvDesc);

		D3D11_TEXTURE2D_DESC texDesc = maskDesc;
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		screenSpaceShadowsTexture = new Texture2D(texDesc);
		screenSpaceShadowsTexture->CreateSRV(srvDesc);
		screenSpaceShadowsTexture->CreateUAV(uavDesc);
	}

	auto accumulator = *globals::game::currentAccumulator.get();
	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

	auto& directionNi = dirLight->GetWorldDirection();
	float3 light = { directionNi.x, directionNi.y, directionNi.z };
	light.Normalize();
	const float4 lightProjectionBase = float4(-light.x, -light.y, -light.z, 0.0f);

	Matrix viewProjMat = Util::GetCameraData(0).viewProjMat;

	float4 lightProjection = DirectX::SimpleMath::Vector4::Transform(lightProjectionBase, viewProjMat);
	float lightProjectionF[4] = { lightProjection.x, lightProjection.y, lightProjection.z, lightProjection.w };

	auto viewport = globals::game::graphicsState;
	float2 dynamicRes = { viewport->GetRuntimeData().dynamicResolutionWidthRatio, viewport->GetRuntimeData().dynamicResolutionHeightRatio };
	auto& upscaler = globals::features::upscaling;
	bool forceDLAAFullRes = upscaler.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS &&
		upscaler.settings.qualityMode == 0;

	auto depthIndex = RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY;
	auto depth = renderer->GetDepthStencilData().depthStencils[depthIndex];
	D3D11_TEXTURE2D_DESC depthDesc{};
	depth.texture->GetDesc(&depthDesc);

	float2 size = Util::ConvertToDynamic(globals::state->screenSize);
	int viewportSize[2] = { (int)size.x, (int)size.y };

	if (REL::Module::IsVR())
		viewportSize[0] /= 2;

	int minRenderBounds[2] = { 0, 0 };
	int maxRenderBounds[2] = { viewportSize[0], viewportSize[1] };

	context->CSSetSamplers(0, 1, &pointBorderSampler);

	auto buffer = raymarchCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	if (forceDLAAFullRes) {
		viewportSize[0] = (int)depthDesc.Width;
		viewportSize[1] = (int)depthDesc.Height;
		if (REL::Module::IsVR())
			viewportSize[0] /= 2;
		minRenderBounds[0] = 0;
		minRenderBounds[1] = 0;
		maxRenderBounds[0] = viewportSize[0];
		maxRenderBounds[1] = viewportSize[1];
	}

	auto dispatchEye = [&](uint32_t eyeIndex,
		ID3D11ShaderResourceView* depthSRV,
		ID3D11UnorderedAccessView* outputUAV,
		bool perEye)
	{
		context->CSSetShaderResources(0, 1, &depthSRV);
		context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
		if (perEye) {
			context->CSSetShader(eyeIndex == 0 ? GetComputeRaymarchPerEye() : GetComputeRaymarchRightPerEye(), nullptr, 0);
		} else {
			context->CSSetShader(eyeIndex == 0 ? GetComputeRaymarch() : GetComputeRaymarchRight(), nullptr, 0);
		}

		auto dispatchList = Bend::BuildDispatchList(lightProjectionF, viewportSize, minRenderBounds, maxRenderBounds);
		if (eyeIndex == 0) {
			static uint32_t lastDepthW = 0;
			static uint32_t lastDepthH = 0;
			static uint32_t lastMaskW = 0;
			static uint32_t lastMaskH = 0;
			static int lastViewportW = 0;
			static int lastViewportH = 0;
			static int lastDispatchCount = -1;
			static bool lastPerEye = false;
			static bool lastDLAA = false;
			static float lastDynX = -1.0f;
			static float lastDynY = -1.0f;
			static int lastDepthIndex = -1;

			const int depthIndexValue = static_cast<int>(depthIndex);
			const bool changed =
				lastDepthW != depthDesc.Width ||
				lastDepthH != depthDesc.Height ||
				lastMaskW != maskDesc.Width ||
				lastMaskH != maskDesc.Height ||
				lastViewportW != viewportSize[0] ||
				lastViewportH != viewportSize[1] ||
				lastDispatchCount != dispatchList.DispatchCount ||
				lastPerEye != perEye ||
				lastDLAA != forceDLAAFullRes ||
				lastDynX != dynamicRes.x ||
				lastDynY != dynamicRes.y ||
				lastDepthIndex != depthIndexValue;

			if (changed) {
				logger::info("[SSS] depth={}x{} mask={}x{} viewport={}x{} dyn={:.3f},{:.3f} dispatch={} perEye={} DLAA={} depthIndex={}",
					depthDesc.Width, depthDesc.Height,
					maskDesc.Width, maskDesc.Height,
					viewportSize[0], viewportSize[1],
					dynamicRes.x, dynamicRes.y,
					dispatchList.DispatchCount,
					perEye ? 1 : 0,
					forceDLAAFullRes ? 1 : 0,
					depthIndexValue);

				lastDepthW = depthDesc.Width;
				lastDepthH = depthDesc.Height;
				lastMaskW = maskDesc.Width;
				lastMaskH = maskDesc.Height;
				lastViewportW = viewportSize[0];
				lastViewportH = viewportSize[1];
				lastDispatchCount = dispatchList.DispatchCount;
				lastPerEye = perEye;
				lastDLAA = forceDLAAFullRes;
				lastDynX = dynamicRes.x;
				lastDynY = dynamicRes.y;
				lastDepthIndex = depthIndexValue;
			}
		}

		for (int i = 0; i < dispatchList.DispatchCount; i++) {
			TracyD3D11Zone(globals::state->tracyCtx, eyeIndex == 0 ? "SSS - Ray March" : "SSS - Ray March (VR Right Eye)");

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
	};

	if (!globals::game::isVR) {
		context->CSSetShaderResources(0, 1, &depth.depthSRV);

		auto uav = screenSpaceShadowsTexture->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		context->CSSetShader(GetComputeRaymarch(), nullptr, 0);

		auto dispatchList = Bend::BuildDispatchList(lightProjectionF, viewportSize, minRenderBounds, maxRenderBounds);

		if (globals::features::upscaling.IsUpscalingActive()) {
			dynamicRes.x = depthDesc.Width > 0 ? (float)viewportSize[0] / (float)depthDesc.Width : 1.0f;
			dynamicRes.y = depthDesc.Height > 0 ? (float)viewportSize[1] / (float)depthDesc.Height : 1.0f;
		}
		if (forceDLAAFullRes)
			dynamicRes = float2(1.0f, 1.0f);

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
	} else {
		// Ensure per-eye output textures exist and match current size.
	const uint32_t eyeOutWidth = maskDesc.Width / 2;
	const uint32_t eyeOutHeight = maskDesc.Height;

		if (!screenSpaceShadowsTextureEye[0] || screenSpaceShadowsTextureEye[0]->desc.Width != eyeOutWidth ||
			screenSpaceShadowsTextureEye[0]->desc.Height != eyeOutHeight) {
			for (int i = 0; i < 2; ++i) {
				delete screenSpaceShadowsTextureEye[i];
				screenSpaceShadowsTextureEye[i] = nullptr;
			}

			D3D11_TEXTURE2D_DESC texDesc = maskDesc;
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};

			shadowMask.SRV->GetDesc(&srvDesc);
			texDesc.Format = DXGI_FORMAT_R8_UNORM;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			texDesc.Width = eyeOutWidth;
			texDesc.Height = eyeOutHeight;

			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			for (int i = 0; i < 2; ++i) {
				screenSpaceShadowsTextureEye[i] = new Texture2D(texDesc);
				screenSpaceShadowsTextureEye[i]->CreateSRV(srvDesc);
				screenSpaceShadowsTextureEye[i]->CreateUAV(uavDesc);
			}
		}

		// Ensure per-eye depth textures match current depth size.
		const uint32_t eyeDepthWidth = depthDesc.Width / 2;
		const uint32_t eyeDepthHeight = depthDesc.Height;

		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
		depth.depthSRV->GetDesc(&depthSrvDesc);

		if (!screenSpaceShadowsDepthEye[0] || screenSpaceShadowsDepthEye[0]->desc.Width != eyeDepthWidth ||
			screenSpaceShadowsDepthEye[0]->desc.Height != eyeDepthHeight) {
			for (int i = 0; i < 2; ++i) {
				delete screenSpaceShadowsDepthEye[i];
				screenSpaceShadowsDepthEye[i] = nullptr;
			}

			D3D11_TEXTURE2D_DESC eyeDepthDesc = depthDesc;
			eyeDepthDesc.Width = eyeDepthWidth;
			eyeDepthDesc.Height = eyeDepthHeight;
			eyeDepthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			eyeDepthDesc.MipLevels = 1;
			eyeDepthDesc.ArraySize = 1;
			eyeDepthDesc.SampleDesc.Count = 1;

			for (int i = 0; i < 2; ++i) {
				screenSpaceShadowsDepthEye[i] = new Texture2D(eyeDepthDesc);
				screenSpaceShadowsDepthEye[i]->CreateSRV(depthSrvDesc);
			}
		}

		const uint32_t expectedCombinedWidth = (uint32_t)viewportSize[0] * 2;
		const uint32_t expectedCombinedHeight = (uint32_t)viewportSize[1];
		const bool depthCombined = depthDesc.Width == expectedCombinedWidth && depthDesc.Height == expectedCombinedHeight;
		const bool maskCombined = maskDesc.Width == expectedCombinedWidth && maskDesc.Height == expectedCombinedHeight;
		bool usePerEye = depthCombined && maskCombined;
		usePerEye = usePerEye && screenSpaceShadowsTextureEye[0] && screenSpaceShadowsTextureEye[1];
		usePerEye = usePerEye && screenSpaceShadowsDepthEye[0] && screenSpaceShadowsDepthEye[1];
		if (forceDLAAFullRes)
			usePerEye = false;

		if (usePerEye) {
			// Copy per-eye depth slices.
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const uint32_t offsetX = eye * eyeDepthWidth;
				D3D11_BOX srcBox = { offsetX, 0, 0, offsetX + eyeDepthWidth, eyeDepthHeight, 1 };
				context->CopySubresourceRegion(screenSpaceShadowsDepthEye[eye]->resource.get(), 0, 0, 0, 0, depth.texture, 0, &srcBox);
			}

			// Clear per-eye outputs.
			float white[4] = { 1, 1, 1, 1 };
			context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTextureEye[0]->uav.get(), white);
			context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTextureEye[1]->uav.get(), white);

			if (globals::features::upscaling.IsUpscalingActive()) {
				dynamicRes.x = eyeDepthWidth > 0 ? (float)viewportSize[0] / (float)eyeDepthWidth : 1.0f;
				dynamicRes.y = eyeDepthHeight > 0 ? (float)viewportSize[1] / (float)eyeDepthHeight : 1.0f;
			}
			if (forceDLAAFullRes)
				dynamicRes = float2(1.0f, 1.0f);

			// Left eye
			{
				Matrix viewProjMatLeft = Util::GetCameraData(0).viewProjMat;
				float4 lightProjLeft = DirectX::SimpleMath::Vector4::Transform(lightProjectionBase, viewProjMatLeft);
				lightProjectionF[0] = lightProjLeft.x;
				lightProjectionF[1] = lightProjLeft.y;
				lightProjectionF[2] = lightProjLeft.z;
				lightProjectionF[3] = lightProjLeft.w;
				dispatchEye(0, screenSpaceShadowsDepthEye[0]->srv.get(), screenSpaceShadowsTextureEye[0]->uav.get(), true);
			}

			// Right eye
			{
				Matrix viewProjMatRight = Util::GetCameraData(1).viewProjMat;
				float4 lightProjRight = DirectX::SimpleMath::Vector4::Transform(lightProjectionBase, viewProjMatRight);
				lightProjectionF[0] = lightProjRight.x;
				lightProjectionF[1] = lightProjRight.y;
				lightProjectionF[2] = lightProjRight.z;
				lightProjectionF[3] = lightProjRight.w;
				dispatchEye(1, screenSpaceShadowsDepthEye[1]->srv.get(), screenSpaceShadowsTextureEye[1]->uav.get(), true);
			}

			// Composite into combined shadow mask
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const uint32_t offsetX = eye * eyeOutWidth;
				D3D11_BOX srcBox = { 0, 0, 0, eyeOutWidth, eyeOutHeight, 1 };
				context->CopySubresourceRegion(screenSpaceShadowsTexture->resource.get(), 0, offsetX, 0, 0, screenSpaceShadowsTextureEye[eye]->resource.get(), 0, &srcBox);
			}
		} else {
			// Fallback: combined stereo path
			context->CSSetShaderResources(0, 1, &depth.depthSRV);
			auto uav = screenSpaceShadowsTexture->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

			if (globals::features::upscaling.IsUpscalingActive()) {
				dynamicRes.x = depthDesc.Width > 0 ? (float)viewportSize[0] * 2.0f / (float)depthDesc.Width : 1.0f;
				dynamicRes.y = depthDesc.Height > 0 ? (float)viewportSize[1] / (float)depthDesc.Height : 1.0f;
			}
			if (forceDLAAFullRes)
				dynamicRes = float2(1.0f, 1.0f);

			// Left eye (combined)
			{
				Matrix viewProjMatLeft = Util::GetCameraData(0).viewProjMat;
				float4 lightProjLeft = DirectX::SimpleMath::Vector4::Transform(lightProjectionBase, viewProjMatLeft);
				lightProjectionF[0] = lightProjLeft.x;
				lightProjectionF[1] = lightProjLeft.y;
				lightProjectionF[2] = lightProjLeft.z;
				lightProjectionF[3] = lightProjLeft.w;
				dispatchEye(0, depth.depthSRV, uav, false);
			}

			// Right eye (combined)
			{
				Matrix viewProjMatRight = Util::GetCameraData(1).viewProjMat;
				float4 lightProjRight = DirectX::SimpleMath::Vector4::Transform(lightProjectionBase, viewProjMatRight);
				lightProjectionF[0] = lightProjRight.x;
				lightProjectionF[1] = lightProjRight.y;
				lightProjectionF[2] = lightProjRight.z;
				lightProjectionF[3] = lightProjRight.w;
				dispatchEye(1, depth.depthSRV, uav, false);
			}
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

	if (auto sky = globals::game::sky)
		if (bendSettings.Enable && sky->mode.get() == RE::Sky::Mode::kFull)
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

		if (globals::game::isVR) {
			const uint32_t eyeWidth = texDesc.Width / 2;
			const uint32_t eyeHeight = texDesc.Height;

			D3D11_TEXTURE2D_DESC eyeDesc = texDesc;
			eyeDesc.Width = eyeWidth;
			eyeDesc.Height = eyeHeight;

			for (int i = 0; i < 2; ++i) {
				screenSpaceShadowsTextureEye[i] = new Texture2D(eyeDesc);
				screenSpaceShadowsTextureEye[i]->CreateSRV(srvDesc);
				screenSpaceShadowsTextureEye[i]->CreateUAV(uavDesc);
			}
		}
	}
}
