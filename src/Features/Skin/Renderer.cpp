#include "Features/Skin.h"

#include "Globals.h"
#include "State.h"
#include "Utils/DdsResource.h"
#include "Utils/StringUtils.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cstring>

namespace
{
	constexpr UINT PerGeometrySlot = 7;
	constexpr UINT DetailSlot = 72;
	constexpr UINT ThicknessSlot = 99;
	constexpr UINT ExtraSlot = 74;
	constexpr UINT SamplerSlot = 7;
	constexpr size_t MaxTextureEntries = 256;
	constexpr size_t TextureLoadsPerFrame = 4;

	winrt::com_ptr<ID3D11ShaderResourceView> LoadTexture(const char* a_path, const char* a_name, bool* a_reconstructZ = nullptr, bool* a_grayscale = nullptr)
	{
		RE::BSResourceNiBinaryStream stream(a_path);
		if (!stream.good())
			return {};
		try {
			DirectX::ScratchImage image;
			DX::ThrowIfFailed(Util::LoadDDSImageFromResource(a_path, image));
			const auto format = DirectX::MakeLinear(image.GetMetadata().format);
			switch (format) {
			case DXGI_FORMAT_R8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC4_UNORM:
			case DXGI_FORMAT_BC5_UNORM:
			case DXGI_FORMAT_BC7_UNORM:
				break;
			default:
				throw std::runtime_error("Skin maps require an unsigned normalized DDS format");
			}
			if (!a_reconstructZ && format == DXGI_FORMAT_BC5_UNORM)
				throw std::runtime_error("Skin parameter and wetness maps require color or grayscale channels");
			const bool grayscale = format == DXGI_FORMAT_R8_UNORM || format == DXGI_FORMAT_BC4_UNORM;
			if (grayscale && !a_grayscale)
				throw std::runtime_error("Skin detail and parameter maps require multiple channels");
			image.OverrideFormat(format);
			auto view = Util::CreateDDSTextureFromImage(globals::d3d::device, image, a_name);
			if (a_reconstructZ)
				*a_reconstructZ = format == DXGI_FORMAT_BC5_UNORM;
			if (a_grayscale)
				*a_grayscale = grayscale;
			return view;
		} catch (const std::exception& error) {
			logger::warn("Advanced Skin texture {} unavailable: {}", a_path, error.what());
			return {};
		}
	}

	std::string_view TextureBase(const RE::BSLightingShaderMaterialBase& a_material)
	{
		auto path = [](const RE::NiSourceTexture* texture, RE::BSTextureSet* set, RE::BSTextureSet::Texture slot) -> std::string_view {
			if (texture && !texture->name.empty())
				return texture->name.c_str();
			const auto* value = set ? set->GetTexturePath(slot) : nullptr;
			return value ? value : "";
		};
		const auto specular = path(a_material.specularBackLightingTexture.get(), a_material.textureSet.get(), RE::BSTextureSet::Texture::kSpecular);
		if (Util::IEndsWithAsciiInsensitive(specular, "_s.dds"))
			return specular.substr(0, specular.size() - 6);
		const auto normal = path(a_material.normalTexture.get(), a_material.textureSet.get(), RE::BSTextureSet::Texture::kNormal);
		for (std::string_view suffix : { "_msn.dds", "_n.dds", ".dds" })
			if (Util::IEndsWithAsciiInsensitive(normal, suffix))
				return normal.substr(0, normal.size() - suffix.size());
		return {};
	}
}

Skin::SamplerState::SamplerState(ID3D11DeviceContext* a_context) : context(a_context)
{
	context->PSGetSamplers(SamplerSlot, 1, previous.put());
}

Skin::SamplerState::~SamplerState()
{
	auto* sampler = previous.get();
	context->PSSetSamplers(SamplerSlot, 1, &sampler);
}

void Skin::EndPass()
{
	samplerState.reset();
	resourcesDirty = false;
}

void Skin::ResetResources()
{
	EndPass();
	pendingViews = {};
	pendingThickness = nullptr;
	extraTextures.clear();
	pendingTextureCount = 0;
	hasTextureLoadFrame = false;
	actorWetness.clear();
	hasPreviousData = false;
	lastPruneFrame = 0;
}

void Skin::ReloadTextures()
{
	EndPass();
	pendingViews = {};
	pendingThickness = nullptr;
	extraTextures.clear();
	pendingTextureCount = 0;
	hasTextureLoadFrame = false;
	LoadDetailTexture();
}

void Skin::LoadDetailTexture()
{
	bool reconstruct = false;
	if (auto replacement = LoadTexture("Shaders\\Skin\\skin_detail_n.dds", "Skin::Detail", &reconstruct)) {
		detailTexture = std::move(replacement);
		detailReconstructZ = reconstruct;
	} else
		logger::warn("Advanced Skin detail reload failed; retaining the previous valid detail texture");
}

void Skin::SetupResources()
{
	ResetResources();
	perGeometryCB.reset();
	detailTexture = nullptr;
	detailSampler = nullptr;
	resourceError.clear();
	try {
		auto description = ConstantBufferDesc<PerGeometryData>(false);
		description.ByteWidth = sizeof(PerGeometryData);
		perGeometryCB = std::make_unique<ConstantBuffer>(description, "Skin::PerGeometry");
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_ANISOTROPIC;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.MaxAnisotropy = 16;
		sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&sampler, detailSampler.put()));
		Util::SetResourceName(detailSampler.get(), "Skin::DetailSampler");
		LoadDetailTexture();
	} catch (const std::exception& error) {
		resourceError = error.what();
		logger::error("Advanced Skin resources unavailable; retaining normal lighting: {}", resourceError);
	}
}

void Skin::Prepass()
{
	if (!settings.EnableSkin || !resourceError.empty() || pendingTextureCount == 0 ||
		(hasTextureLoadFrame && lastTextureLoadFrame == globals::state->frameCount))
		return;
	lastTextureLoadFrame = globals::state->frameCount;
	hasTextureLoadFrame = true;
	size_t loadedCount = 0;
	for (auto& [path, entry] : extraTextures) {
		if (!entry.pending)
			continue;
		const auto resourcePath = Util::ToLowerAscii(path);
		entry.views[0] = LoadTexture((resourcePath + "_wet.dds").c_str(), "Skin::Wetness", nullptr, &entry.wetnessIsGrayscale);
		entry.views[1] = LoadTexture((resourcePath + "_rfaos.dds").c_str(), "Skin::Parameters");
		entry.pending = false;
		--pendingTextureCount;
		if (++loadedCount == TextureLoadsPerFrame)
			break;
	}
}

Skin::ExtraTextures* Skin::FindExtraTextures(const RE::BSLightingShaderMaterialBase& a_material)
{
	const auto key = TextureBase(a_material);
	if (key.empty())
		return nullptr;
	const auto frame = globals::state->frameCount;
	if (auto it = extraTextures.find(key); it != extraTextures.end()) {
		it->second.lastFrame = frame;
		return &it->second;
	}
	if (extraTextures.size() >= MaxTextureEntries) {
		const auto oldest = std::ranges::max_element(extraTextures, [frame](const auto& a, const auto& b) {
			return frame - a.second.lastFrame < frame - b.second.lastFrame;
		});
		if (oldest->second.lastFrame == frame)
			return nullptr;
		if (oldest->second.pending)
			--pendingTextureCount;
		extraTextures.erase(oldest);
	}
	auto& entry = extraTextures.try_emplace(std::string(key), ExtraTextures{ {}, frame, true }).first->second;
	++pendingTextureCount;
	return &entry;
}

bool Skin::IsOverlay(const RE::BSRenderPass& a_pass)
{
	const auto& geometry = *a_pass.geometry;
	const auto* alpha = geometry.GetGeometryRuntimeData().alphaProperty.get();
	if (!alpha || !alpha->GetAlphaBlending())
		return false;
	if (geometry.name.empty())
		return false;
	std::string_view name = geometry.name.c_str();
	const auto separator = name.find(" [");
	if (separator == std::string_view::npos)
		return false;
	const auto part = name.substr(0, separator);
	if (part != "Body" && part != "Hands" && part != "Feet" && part != "Face" && part != "Hair")
		return false;
	name.remove_prefix(separator + 2);
	if (name.starts_with("SOvl"))
		name.remove_prefix(4);
	else if (name.starts_with("Ovl"))
		name.remove_prefix(3);
	else
		return false;
	if (!name.ends_with(']'))
		return false;
	name.remove_suffix(1);
	return !name.empty() && name.find_first_not_of("0123456789") == std::string_view::npos;
}

void Skin::BeginPass(RE::BSRenderPass* a_pass)
{
	EndPass();
	if (!loaded || !settings.EnableSkin || !perGeometryCB || !resourceError.empty())
		return;
	pendingViews = {};
	pendingThickness = nullptr;
	useDetailSampler = false;
	PerGeometryData data{};
	data.flags.w = -1;
	if (a_pass && a_pass->geometry && a_pass->shaderProperty && !IsOverlay(*a_pass)) {
		if (IsWetnessEnabled())
			data.wetness = GetWetness(a_pass->geometry);
		const auto* material = a_pass->shaderProperty->material;
		using MaterialFeature = RE::BSShaderMaterial::Feature;
		if (material && material->GetType() == RE::BSShaderMaterial::Type::kLighting &&
			(material->GetFeature() == MaterialFeature::kFaceGen || material->GetFeature() == MaterialFeature::kFaceGenRGBTint)) {
			const auto& lighting = *static_cast<const RE::BSLightingShaderMaterialBase*>(material);
			bool grayscale = false;
			if (const auto* textures = FindExtraTextures(lighting)) {
				pendingViews = textures->views;
				grayscale = textures->wetnessIsGrayscale;
			}
			const auto* thickness = lighting.rimSoftLightingTexture.get();
			if (thickness && thickness->rendererTexture && !thickness->name.empty())
				pendingThickness.copy_from(REX::W32::AsReal(thickness->rendererTexture->resourceView));
			data.flags = float4(float(bool(pendingViews[1])), pendingViews[0] ? (grayscale ? 2.0f : 1.0f) : 0.0f, float(detailReconstructZ), float(bool(pendingThickness)));
			useDetailSampler = settings.EnableSkinDetail && settings.SkinDetailStrength != 0 && detailTexture && detailSampler;
		}
	}
	if (!hasPreviousData || std::memcmp(&previousData, &data, sizeof(data)) != 0) {
		perGeometryCB->Update(data);
		previousData = data;
		hasPreviousData = true;
	}
	resourcesDirty = true;
}

void Skin::SetShaderResources(ID3D11DeviceContext* a_context)
{
	if (!resourcesDirty)
		return;
	ID3D11Buffer* buffer = perGeometryCB->CB();
	a_context->PSSetConstantBuffers(PerGeometrySlot, 1, &buffer);
	resourcesDirty = false;
	if (previousData.flags.w < 0)
		return;
	const std::array views{ pendingViews[0].get(), pendingViews[1].get() };
	a_context->PSSetShaderResources(ExtraSlot, static_cast<UINT>(views.size()), views.data());
	ID3D11ShaderResourceView* thickness = pendingThickness.get();
	a_context->PSSetShaderResources(ThicknessSlot, 1, &thickness);
	ID3D11ShaderResourceView* detail = detailTexture.get();
	a_context->PSSetShaderResources(DetailSlot, 1, &detail);
	if (useDetailSampler) {
		samplerState.emplace(a_context);
		auto* sampler = detailSampler.get();
		a_context->PSSetSamplers(SamplerSlot, 1, &sampler);
	}
}
