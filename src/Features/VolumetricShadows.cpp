#include "VolumetricShadows.h"

#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricShadows::Settings,
	Enabled)

namespace
{
	constexpr uint32_t kShadowCopySize = 512;

	void DrawEnabledCheckbox(VolumetricShadows::Settings& a_settings)
	{
		ImGui::Checkbox("Enable", &a_settings.Enabled);
	}

	bool GetTexture2DArrayDescription(
		ID3D11ShaderResourceView* a_srv,
		uint32_t a_requiredSlices,
		D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_srv)
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_srv->GetDesc(&viewDesc);
		if (viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2DARRAY ||
			viewDesc.Texture2DArray.ArraySize < a_requiredSlices) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		a_srv->GetResource(resource.put());
		if (!resource)
			return false;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void())) || !texture)
			return false;

		texture->GetDesc(&a_desc);
		return a_desc.Width > 0 &&
		       a_desc.Height > 0 &&
		       a_desc.ArraySize >= a_requiredSlices &&
		       a_desc.SampleDesc.Count == 1;
	}
}

void VolumetricShadows::SetupResources()
{
	auto device = globals::d3d::device;

	// Create samplers
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
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

	CompileComputeShaders();
}

void VolumetricShadows::CompileComputeShaders()
{
	downsampleShadowMip0CS.Get(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", { { "DOWNSAMPLE_SHADOW_MIP0", nullptr } }, "cs_5_0", "main", "VolumetricShadows::DownsampleShadowMip0CS");
	downsampleShadowMip1CS.Get(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", { { "DOWNSAMPLE_SHADOW_MIP1", nullptr } }, "cs_5_0", "main", "VolumetricShadows::DownsampleShadowMip1CS");
	blurShadowHorizontalCS.Get(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", { { "BLUR_HORIZONTAL", nullptr } }, "cs_5_0", "main", "VolumetricShadows::BlurShadowHorizontalCS");
	blurShadowVerticalCS.Get(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", { { "BLUR_VERTICAL", nullptr } }, "cs_5_0", "main", "VolumetricShadows::BlurShadowVerticalCS");
}

void VolumetricShadows::ClearShaderCache()
{
	downsampleShadowMip0CS.Reset();
	downsampleShadowMip1CS.Reset();
	blurShadowHorizontalCS.Reset();
	blurShadowVerticalCS.Reset();
	CompileComputeShaders();
}

VolumetricShadows::RuntimeReadiness VolumetricShadows::GetRuntimeReadiness(
	ID3D11ShaderResourceView* a_capturedShadowView,
	bool a_requireOutputResources,
	RuntimeContext* a_context) const
{
	if (a_context)
		*a_context = {};

	auto* state = globals::state;
	auto* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;
	// The dispatched shaders do not declare or bind a constant buffer; the
	// linear sampler is their only feature-owned fixed-function input.
	if (!state ||
		!context ||
		!globals::d3d::device ||
		!globals::profiler ||
		!renderer ||
		!linearSampler) {
		return RuntimeReadiness::NoRuntimeResources;
	}

	if (!state->HasDirectionalShadows())
		return RuntimeReadiness::NoDirectionalShadows;

	auto* shaderManager = globals::game::smState;
	if (!shaderManager)
		return RuntimeReadiness::NoDirectionalShadows;

	auto* shadowSceneNode = shaderManager->shadowSceneNode[0];
	if (!shadowSceneNode || !shadowSceneNode->GetRuntimeData().sunShadowDirLight)
		return RuntimeReadiness::NoDirectionalShadows;

	if (!downsampleShadowMip0CS.get() ||
		!downsampleShadowMip1CS.get() ||
		!blurShadowHorizontalCS.get() ||
		!blurShadowVerticalCS.get()) {
		return RuntimeReadiness::ShaderUnavailable;
	}

	// Rendering captures the map before Present; Present advances frameCount before
	// the tuning UI evaluates applicability. Accept that immediately preceding
	// render frame as well as an in-render check, including uint32 wraparound.
	if (!a_capturedShadowView ||
		state->frameCount - shadowViewCaptureFrame > 1u) {
		return RuntimeReadiness::NoCapturedShadowMap;
	}

	D3D11_TEXTURE2D_DESC directionalShadowDesc{};
	if (!GetTexture2DArrayDescription(a_capturedShadowView, 2, directionalShadowDesc) ||
		directionalShadowDesc.Width != directionalShadowDesc.Height ||
		(directionalShadowDesc.Width != 1024u &&
			directionalShadowDesc.Width != 2048u &&
			directionalShadowDesc.Width != 4096u)) {
		return RuntimeReadiness::NoCapturedShadowMap;
	}

	auto& esramDepthStencil = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM];
	auto* esramShadowSRV = esramDepthStencil.depthSRV;
	D3D11_TEXTURE2D_DESC esramShadowDesc{};
	if (!GetTexture2DArrayDescription(esramShadowSRV, 2, esramShadowDesc))
		return RuntimeReadiness::NoRuntimeResources;

	if (a_requireOutputResources) {
		if (!shadowCopyTexture ||
			!shadowCopySRV ||
			!shadowCopyMip0SRV ||
			!shadowCopyMip1SRV ||
			!shadowCopyMip0UAV ||
			!shadowCopyMip1UAV ||
			!shadowBlurTempTexture ||
			!shadowBlurTempMip0SRV ||
			!shadowBlurTempMip1SRV ||
			!shadowBlurTempMip0UAV ||
			!shadowBlurTempMip1UAV ||
			shadowCopyWidth != kShadowCopySize ||
			shadowCopyHeight != kShadowCopySize) {
			return RuntimeReadiness::OutputResourcesUnavailable;
		}
	}

	if (a_context) {
		a_context->context = context;
		a_context->directionalShadowSRV = a_capturedShadowView;
		a_context->esramShadowSRV = esramShadowSRV;
		a_context->directionalShadowDesc = directionalShadowDesc;
	}

	return RuntimeReadiness::Ready;
}

void VolumetricShadows::CopyShadowLightData()
{
	auto* context = globals::d3d::context;
	shadowView = nullptr;
	shadowViewCaptureFrame = UINT32_MAX;
	if (!context)
		return;

	context->PSGetShaderResources(4, 1, shadowView.put());
	if (globals::state)
		shadowViewCaptureFrame = globals::state->frameCount;
	if (!settings.Enabled) {
		// Keep only the current shadow-map identity fresh while Off. Full descriptor,
		// shader, scene, and output validation belongs to the enabled dispatch path
		// (and the tuning controller's readiness check), not the runtime baseline.
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	RuntimeContext runtimeContext;
	const auto readiness = GetRuntimeReadiness(shadowView.get(), false, &runtimeContext);
	if (readiness != RuntimeReadiness::Ready) {
		SetSharedShadowMapSRV(context, nullptr);
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "VolumetricShadows::CopyShadowLightData");
	bool shadowCopyUpdated = false;

	{
		// Downsample shadow texture array to fixed 512x512 (mip1: 256x256)
		{
			// Lazily create fixed-size output textures
			if (!shadowCopyTexture) {
				shadowCopyWidth = kShadowCopySize;
				shadowCopyHeight = kShadowCopySize;

				D3D11_TEXTURE2D_DESC copyDesc{};
				copyDesc.Width = kShadowCopySize;
				copyDesc.Height = kShadowCopySize;
				copyDesc.MipLevels = 2;
				copyDesc.ArraySize = 1;
				copyDesc.Format = DXGI_FORMAT_R16G16_UNORM;
				copyDesc.SampleDesc.Count = 1;
				copyDesc.SampleDesc.Quality = 0;
				copyDesc.Usage = D3D11_USAGE_DEFAULT;
				copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
				copyDesc.MiscFlags = 0;

				auto device = globals::d3d::device;
				DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowCopyTexture));
				Util::SetResourceName(shadowCopyTexture, "VolumetricShadows::ShadowCopy");

				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = copyDesc.Format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 2;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopySRV));
				Util::SetResourceName(shadowCopySRV, "VolumetricShadows::ShadowCopy SRV");

				// Create mip-specific SRVs for blur passes
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowCopyTexture, &srvDesc, &shadowCopyMip0SRV));
				Util::SetResourceName(shadowCopyMip0SRV, "VolumetricShadows::ShadowCopy SRV mip0");

				srvDesc.Texture2D.MostDetailedMip = 1;
				srvDesc.Texture2D.MipLevels = 1;
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

				// Create temporary texture for blur intermediate result
				DX::ThrowIfFailed(device->CreateTexture2D(&copyDesc, nullptr, &shadowBlurTempTexture));
				Util::SetResourceName(shadowBlurTempTexture, "VolumetricShadows::ShadowBlurTemp");

				// Create mip-specific SRVs for blur temp texture
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip0SRV));
				Util::SetResourceName(shadowBlurTempMip0SRV, "VolumetricShadows::ShadowBlurTemp SRV mip0");

				srvDesc.Texture2D.MostDetailedMip = 1;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(shadowBlurTempTexture, &srvDesc, &shadowBlurTempMip1SRV));
				Util::SetResourceName(shadowBlurTempMip1SRV, "VolumetricShadows::ShadowBlurTemp SRV mip1");

				uavDesc.Texture2D.MipSlice = 0;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip0UAV));
				Util::SetResourceName(shadowBlurTempMip0UAV, "VolumetricShadows::ShadowBlurTemp UAV mip0");

				uavDesc.Texture2D.MipSlice = 1;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(shadowBlurTempTexture, &uavDesc, &shadowBlurTempMip1UAV));
				Util::SetResourceName(shadowBlurTempMip1UAV, "VolumetricShadows::ShadowBlurTemp UAV mip1");
			}

			// Resource creation can leave a partial set if the device fails midway.
			// Revalidate the complete dispatch path before binding any of it.
			if (GetRuntimeReadiness(shadowView.get(), true, &runtimeContext) != RuntimeReadiness::Ready) {
				SetSharedShadowMapSRV(context, nullptr);
				return;
			}

			{
				{
					const auto& srcDesc = runtimeContext.directionalShadowDesc;

					ID3D11ShaderResourceView* csSrvs[2]{ runtimeContext.directionalShadowSRV, runtimeContext.esramShadowSRV };
					context->CSSetShaderResources(0, 2, csSrvs);

					context->CSSetSamplers(0, 1, &linearSampler);

					// Supported source sizes are exact multiples of the 16 input texels
					// covered by each 8x8 group before the fixed-output reductions.
					const auto dispatchWidth = srcDesc.Width / 16u;
					const auto dispatchHeight = srcDesc.Height / 16u;

					// Mip 0 (cascade 1)
					ID3D11UnorderedAccessView* csUavs[1]{ shadowCopyMip0UAV };
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip0CS.get(), nullptr, 0);
					globals::profiler->BeginPass("VolumetricShadows::DownsampleMip0");
					context->Dispatch(dispatchWidth, dispatchHeight, 1);
					globals::profiler->EndPass();

					// Mip 1 (cascade 0)
					csUavs[0] = shadowCopyMip1UAV;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					context->CSSetShader(downsampleShadowMip1CS.get(), nullptr, 0);
					globals::profiler->BeginPass("VolumetricShadows::DownsampleMip1");
					context->Dispatch(dispatchWidth, dispatchHeight, 1);
					globals::profiler->EndPass();

					// Unbind SRVs before blur passes
					csSrvs[0] = nullptr;
					csSrvs[1] = nullptr;
					context->CSSetShaderResources(0, 2, csSrvs);
					csUavs[0] = nullptr;
					context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

					constexpr uint32_t mip0Size = kShadowCopySize;
					constexpr uint32_t mip1Size = kShadowCopySize / 2;

					// 11x11 separable blur for Mip 0
					{
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip0 -> shadowBlurTemp mip0
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip0SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip0UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS.get(), nullptr, 0);
						globals::profiler->BeginPass("VolumetricShadows::BlurHMip0");
						context->Dispatch((mip0Size + GROUP_SIZE - 1) / GROUP_SIZE, mip0Size, 1);
						globals::profiler->EndPass();

						// Unbind for next pass
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

						// Vertical pass: shadowBlurTemp mip0 -> shadowCopy mip0
						blurSrvs[0] = shadowBlurTempMip0SRV;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowCopyMip0UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowVerticalCS.get(), nullptr, 0);
						globals::profiler->BeginPass("VolumetricShadows::BlurVMip0");
						context->Dispatch(mip0Size, (mip0Size + GROUP_SIZE - 1) / GROUP_SIZE, 1);
						globals::profiler->EndPass();

						// Unbind
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					}

					// 11x11 separable blur for Mip 1
					{
						const uint32_t GROUP_SIZE = 128;

						// Horizontal pass: shadowCopy mip1 -> shadowBlurTemp mip1
						ID3D11ShaderResourceView* blurSrvs[1]{ shadowCopyMip1SRV };
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowBlurTempMip1UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowHorizontalCS.get(), nullptr, 0);
						globals::profiler->BeginPass("VolumetricShadows::BlurHMip1");
						context->Dispatch((mip1Size + GROUP_SIZE - 1) / GROUP_SIZE, mip1Size, 1);
						globals::profiler->EndPass();

						// Unbind for next pass
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);

						// Vertical pass: shadowBlurTemp mip1 -> shadowCopy mip1
						blurSrvs[0] = shadowBlurTempMip1SRV;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = shadowCopyMip1UAV;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
						context->CSSetShader(blurShadowVerticalCS.get(), nullptr, 0);
						globals::profiler->BeginPass("VolumetricShadows::BlurVMip1");
						context->Dispatch(mip1Size, (mip1Size + GROUP_SIZE - 1) / GROUP_SIZE, 1);
						globals::profiler->EndPass();

						// Unbind
						blurSrvs[0] = nullptr;
						context->CSSetShaderResources(0, 1, blurSrvs);
						csUavs[0] = nullptr;
						context->CSSetUnorderedAccessViews(0, 1, csUavs, nullptr);
					}

					// Cleanup CS state
					ID3D11SamplerState* nullSampler = nullptr;
					context->CSSetSamplers(0, 1, &nullSampler);
					context->CSSetShader(nullptr, nullptr, 0);
					shadowCopyUpdated = true;
				}
			}
		}

		auto* srv = shadowView && shadowCopyUpdated ? shadowCopySRV : nullptr;
		SetSharedShadowMapSRV(context, srv);
	}
}

void VolumetricShadows::SetSharedShadowMapSRV(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_srv)
{
	if (a_context)
		a_context->PSSetShaderResources(kSharedShadowMapShaderSlot, 1, &a_srv);
}

bool VolumetricShadows::IsPerformanceTuningApplicable() const
{
	return GetRuntimeReadiness(shadowView.get(), true) == RuntimeReadiness::Ready;
}

const char* VolumetricShadows::GetPerformanceTuningApplicabilityReason() const
{
	switch (GetRuntimeReadiness(shadowView.get(), true)) {
	case RuntimeReadiness::Ready:
		return nullptr;
	case RuntimeReadiness::NoDirectionalShadows:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.no_directional_shadows",
			"Volumetric Shadows only perform runtime work while the scene has an active directional shadow light.");
	case RuntimeReadiness::NoCapturedShadowMap:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.no_shadow_map",
			"Volumetric Shadows cannot be measured because no valid directional shadow map was captured this frame.");
	case RuntimeReadiness::ShaderUnavailable:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.shader_unavailable",
			"Volumetric Shadows cannot be measured because one or more downsample or blur shaders are unavailable.");
	case RuntimeReadiness::OutputResourcesUnavailable:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.output_resources_unavailable",
			"Volumetric Shadows cannot be measured because their downsample or blur textures are incomplete.");
	case RuntimeReadiness::NoRuntimeResources:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.no_runtime_resources",
			"Volumetric Shadows cannot be measured because required rendering resources are unavailable.");
	default:
		return T(
			"menu.performance_tuning.feature.volumetric_shadows.not_applicable",
			"Volumetric Shadows cannot perform runtime work in the current scene.");
	}
}

void VolumetricShadows::DrawSettings()
{
	DrawEnabledCheckbox(settings);
	ImGui::BeginDisabled(!settings.Enabled);

	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		auto DisplayRT = [&](const char* label, ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv) {
			if (srv && tex) {
				D3D11_TEXTURE2D_DESC desc;
				tex->GetDesc(&desc);
				char buf[128];
				snprintf(buf, sizeof(buf), "%s (%ux%u)", label, desc.Width, desc.Height);
				if (ImGui::TreeNode(buf)) {
					ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
					ImGui::TreePop();
				}
			}
		};

		DisplayRT("VSM Cascade 0", shadowCopyTexture, shadowCopyMip0SRV);
		DisplayRT("VSM Cascade 1", shadowCopyTexture, shadowCopyMip1SRV);

		ImGui::TreePop();
	}

	ImGui::EndDisabled();
}

void VolumetricShadows::DrawEssentialSettings()
{
	DrawEnabledCheckbox(settings);
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

struct CreateDepthStencil_VolumetricLighting
{
	static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
	{
		RE::BSGraphics::DepthStencilTargetProperties properties = *a_properties;
		properties.height = 1024;
		properties.width = 1024;
		func(This, a_target, &properties);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void VolumetricShadows::PostPostLoad()
{
	stl::write_thunk_call<CreateDepthStencil_VolumetricLighting>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x9DC, 0x9DC));
}

bool VolumetricShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}
