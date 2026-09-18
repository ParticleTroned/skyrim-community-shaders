#include "TerrainVariation.h"
#include "../Util.h"
#include "Globals.h"
#include "State.h"
#include "TerrainVariationPolicy.h"

#include <mutex>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix,
	enableMeshSupport)

void TerrainVariation::DrawSettings()
{
	ImGui::TextWrapped(
		"Terrain Variation is always enabled when installed. "
		"To turn it off, use Disable at Boot.");

	ImGui::Spacing();

	bool lodTilingFix = settings.enableLODTerrainTilingFix != 0;
	if (ImGui::Checkbox("Apply to LOD Terrain", &lodTilingFix)) {
		settings.enableLODTerrainTilingFix = lodTilingFix ? 1u : 0u;
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Applies the tiling fix to LOD terrain objects.\n"
			"This helps reduce the visible tiling effect on distant terrain.");
	}
	bool meshSupport = settings.enableMeshSupport != 0;
	if (ImGui::Checkbox("Apply to Landscape-Textured Meshes", &meshSupport))
		SetMeshSupportEnabled(meshSupport);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Reduces texture repetition on eligible cliffs and ground meshes.\n"
			"Keeps foliage, trees, decals and clamped textures unchanged.\n"
			"Additional texture sampling may increase GPU cost.");
	}
}

void TerrainVariation::DrawEssentialSettings()
{
	DrawSettings();
}

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
	SetMeshSupportEnabled(settings.enableMeshSupport != 0);
}

void TerrainVariation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainVariation::RestoreDefaultSettings()
{
	settings = {};
}

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}

void TerrainVariation::SetMeshSupportEnabled(bool a_enabled)
{
	settings.enableMeshSupport = a_enabled ? 1u : 0u;
	if (!a_enabled && globals::state)
		globals::state->permutationData.ExtraFeatureDescriptor &= ~uint32_t(State::ExtraFeatureDescriptors::TVMeshVariation);
}

void TerrainVariation::DataLoaded()
{
	std::unordered_set<std::string> paths;
	if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
		auto addTextureSet = [&paths](RE::BGSTextureSet* textureSet) {
			if (!textureSet)
				return;
			const auto* path = textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
			if (path && *path)
				paths.insert(TerrainVariationPolicy::CanonicaliseTexturePath(path));
		};
		for (auto* landTexture : dataHandler->GetFormArray<RE::TESLandTexture>()) {
			if (!landTexture)
				continue;
			addTextureSet(landTexture->textureSet);
			addTextureSet(Util::GetSeasonalSwap(landTexture->textureSet));
		}
	} else {
		logger::warn("TerrainVariation: landscape records unavailable; mesh support uses texture directory matching");
	}
	const std::unique_lock lock(meshTextureMutex);
	landscapeDiffusePaths.swap(paths);
	meshTextureCache.clear();
	logger::info("TerrainVariation: collected {} landscape diffuse paths", landscapeDiffusePaths.size());
}

bool TerrainVariation::IsLandscapeDiffuseTexture(const RE::BSFixedString& a_name)
{
	const auto* key = a_name.c_str();
	if (!key || !*key)
		return false;
	{
		const std::shared_lock lock(meshTextureMutex);
		if (const auto it = meshTextureCache.find(key); it != meshTextureCache.end())
			return it->second.landscape;
	}
	const std::unique_lock lock(meshTextureMutex);
	if (const auto it = meshTextureCache.find(key); it != meshTextureCache.end())
		return it->second.landscape;
	const auto canonical = TerrainVariationPolicy::CanonicaliseTexturePath(key);
	const bool landscape = TerrainVariationPolicy::IsLandscapeDiffusePath(canonical, landscapeDiffusePaths);
	meshTextureCache.emplace(key, CachedTexture{ a_name, landscape });
	return landscape;
}

void TerrainVariation::UpdateMeshPermutation(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state)
		return;
	auto& descriptor = state->permutationData.ExtraFeatureDescriptor;
	descriptor &= ~uint32_t(State::ExtraFeatureDescriptors::TVMeshVariation);
	if (!IsMeshSupportEnabled() || !a_pass || !a_pass->geometry)
		return;

	const auto& data = a_pass->geometry->GetGeometryRuntimeData();
	if (data.alphaProperty && (data.alphaProperty->GetAlphaTesting() || data.alphaProperty->GetAlphaBlending()))
		return;
	auto* property = a_pass->shaderProperty;
	if (!property || property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get())
		return;

	using enum RE::BSShaderProperty::EShaderPropertyFlag;
	if (property->flags.any(kMultiTextureLandscape, kLODLandscape, kDecal, kDynamicDecal,
			kTreeAnim, kSkinned, kFace, kFaceGenRGBTint, kHairTint, kEyeReflect, kLODObjects,
			kHDLODObjects, kProjectedUV, kMultiLayerParallax))
		return;
	auto* baseMaterial = property->GetBaseMaterial();
	if (!baseMaterial || baseMaterial->GetType() != RE::BSShaderMaterial::Type::kLighting)
		return;
	const auto* material = static_cast<const RE::BSLightingShaderMaterialBase*>(baseMaterial);
	if (material->textureClampMode != static_cast<int32_t>(RE::BSGraphics::TextureAddressMode::kWrapSWrapT))
		return;
	const auto* texture = property->GetBaseTexture();
	if (!texture || !IsLandscapeDiffuseTexture(texture->name))
		return;
	if (auto* reference = a_pass->geometry->GetUserData(); reference && reference->GetBaseObject() && reference->GetBaseObject()->As<RE::TESObjectTREE>())
		return;
	descriptor |= uint32_t(State::ExtraFeatureDescriptors::TVMeshVariation);
}
