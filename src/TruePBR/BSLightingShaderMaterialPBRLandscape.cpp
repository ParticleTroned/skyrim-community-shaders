#include "BSLightingShaderMaterialPBRLandscape.h"

#include <algorithm>

BSLightingShaderMaterialPBRLandscape::BSLightingShaderMaterialPBRLandscape()
{
	std::fill(isPbr.begin(), isPbr.end(), false);
	std::fill(roughnessScales.begin(), roughnessScales.end(), 1.f);
	std::fill(displacementScales.begin(), displacementScales.end(), 1.f);
	std::fill(specularLevels.begin(), specularLevels.end(), 0.04f);
}

BSLightingShaderMaterialPBRLandscape::~BSLightingShaderMaterialPBRLandscape()
{
	All.Unregister(this);
}

BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::Make()
{
	auto* scrapHeap = globals::game::memoryManager->GetThreadScrapHeap();
	auto* material = static_cast<BSLightingShaderMaterialPBRLandscape*>(scrapHeap->Allocate(sizeof(BSLightingShaderMaterialPBRLandscape), 8));
	if (material) {
		std::memset(material, 0, sizeof(BSLightingShaderMaterialPBRLandscape));
		std::construct_at(material);
	}
	return material;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::Create()
{
	// Must use regular heap (not scrap heap like Make()). BSLightingShaderProperty::LinkObject
	// calls ScrapHeap::Free() after LinkMaterial — if Create() used scrap heap, it would pop
	// the canonical off the stack, causing immediate use-after-free in property->material.
	auto* material = new BSLightingShaderMaterialPBRLandscape();
	All.Register(material);
	return material;
}

void BSLightingShaderMaterialPBRLandscape::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBRLandscape*>(that);

	numLandscapeTextures = pbrThat->numLandscapeTextures;

	for (uint32_t textureIndex = 0; textureIndex < NumTiles; ++textureIndex) {
		landscapeBaseColorTextures[textureIndex] = pbrThat->landscapeBaseColorTextures[textureIndex];
		landscapeNormalTextures[textureIndex] = pbrThat->landscapeNormalTextures[textureIndex];
		landscapeDisplacementTextures[textureIndex] = pbrThat->landscapeDisplacementTextures[textureIndex];
		landscapeRMAOSTextures[textureIndex] = pbrThat->landscapeRMAOSTextures[textureIndex];
	}
	terrainOverlayTexture = pbrThat->terrainOverlayTexture;
	terrainNoiseTexture = pbrThat->terrainNoiseTexture;
	landBlendParams = pbrThat->landBlendParams;
	isPbr = pbrThat->isPbr;
	roughnessScales = pbrThat->roughnessScales;
	displacementScales = pbrThat->displacementScales;
	specularLevels = pbrThat->specularLevels;
	terrainTexOffsetX = pbrThat->terrainTexOffsetX;
	terrainTexOffsetY = pbrThat->terrainTexOffsetY;
	terrainTexFade = pbrThat->terrainTexFade;
	glintParameters = pbrThat->glintParameters;

	All.Copy(this, pbrThat);
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBRLandscape::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
	//return FEATURE;
}

void BSLightingShaderMaterialPBRLandscape::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	for (auto& texture : landscapeBaseColorTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeNormalTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeDisplacementTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeRMAOSTextures) {
		texture.reset();
	}
	terrainOverlayTexture.reset();
	terrainNoiseTexture.reset();
}

void BSLightingShaderMaterialPBRLandscape::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();
	if (terrainOverlayTexture == nullptr) {
		terrainOverlayTexture = stateData.defaultTextureNormalMap;
	}
	if (terrainNoiseTexture == nullptr) {
		terrainNoiseTexture = stateData.defaultTextureNormalMap;
	}
	for (uint32_t textureIndex = 0; textureIndex < std::min(numLandscapeTextures, NumTiles); ++textureIndex) {
		if (landscapeBaseColorTextures[textureIndex] == nullptr) {
			landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeNormalTextures[textureIndex] == nullptr) {
			landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
		}
		if (landscapeDisplacementTextures[textureIndex] == nullptr) {
			landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeRMAOSTextures[textureIndex] == nullptr) {
			landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
		}
	}
}

uint32_t BSLightingShaderMaterialPBRLandscape::GetTextures(RE::NiSourceTexture** textures)
{
	// The engine's ForEachTexture owns a fixed twenty-pointer scratch buffer.
	return textures ? CollectTextures({ textures, NativeTextureCapacity }) : 0;
}

uint32_t BSLightingShaderMaterialPBRLandscape::CollectTextures(std::span<RE::NiSourceTexture*> textures) const
{
	uint32_t textureIndex = 0;
	const auto append = [&](const auto& texture) {
		if (texture && textureIndex < textures.size())
			textures[textureIndex++] = texture.get();
	};
	const auto tileCount = std::min(numLandscapeTextures, NumTiles);
	append(rimSoftLightingTexture);
	append(specularBackLightingTexture);
	// Native visitors must retain every diffuse/normal layer and shared terrain texture.
	for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
		append(landscapeBaseColorTextures[tileIndex]);
		append(landscapeNormalTextures[tileIndex]);
	}
	append(terrainOverlayTexture);
	append(terrainNoiseTexture);
	for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
		append(landscapeDisplacementTextures[tileIndex]);
		append(landscapeRMAOSTextures[tileIndex]);
	}

	return textureIndex;
}

const BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::GetRegistered(const RE::BSShaderMaterial* material)
{
	// Membership is tested as an address before dereferencing an unknown derived layout.
	auto* candidate = reinterpret_cast<const BSLightingShaderMaterialPBRLandscape*>(material);
	return material && All.Contains(const_cast<BSLightingShaderMaterialPBRLandscape*>(candidate)) ? static_cast<const BSLightingShaderMaterialPBRLandscape*>(material) : nullptr;
}

bool BSLightingShaderMaterialPBRLandscape::HasGlint() const
{
	for (uint32_t textureIndex = 0; textureIndex < std::min(numLandscapeTextures, NumTiles); ++textureIndex) {
		if (glintParameters[textureIndex].enabled) {
			return true;
		}
	}
	return false;
}
