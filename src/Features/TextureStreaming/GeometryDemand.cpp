#include "GeometryDemand.h"
#include "Features/ExtendedMaterials.h"
#include "Features/TerrainHelper.h"
#include "Globals.h"
#include "Policy.h"
#include "State.h"
#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "TruePBR/BSLightingShaderMaterialPBRLandscape.h"
#include "Utils/Game.h"
#include <RE/B/BSLightingShaderMaterialMultiLayerParallax.h>
#include <RE/B/BSLightingShaderMaterialParallax.h>
#include <RE/B/BSLightingShaderMaterialParallaxOcc.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <shared_mutex>
#include <span>

namespace StreamingTextures
{
	static bool SharesTextureResource(const RE::NiSourceTexture* first, const RE::NiSourceTexture* second)
	{
		return first && second && (first == second || (first->rendererTexture && second->rendererTexture && first->rendererTexture->texture && first->rendererTexture->texture == second->rendererTexture->texture));
	}

	using TexturePointer = decltype(RE::BSLightingShaderMaterialBase::diffuseTexture);
	struct TerrainLayers
	{
		const RE::NiSourceTexture* diffuse = nullptr;
		const RE::NiSourceTexture* normal = nullptr;
		std::span<const TexturePointer> additionalDiffuse, additionalNormal;
	};

	static std::optional<TerrainLayers> GetTerrainLayers(const RE::BSLightingShaderMaterialBase* material)
	{
		if (const auto* pbr = BSLightingShaderMaterialPBRLandscape::GetRegistered(material)) {
			if (!pbr->numLandscapeTextures || pbr->numLandscapeTextures > BSLightingShaderMaterialPBRLandscape::NumTiles)
				return std::nullopt;
			return TerrainLayers{ pbr->landscapeBaseColorTextures[0].get(), pbr->landscapeNormalTextures[0].get(),
				std::span<const TexturePointer>(pbr->landscapeBaseColorTextures).subspan(1, pbr->numLandscapeTextures - 1),
				std::span<const TexturePointer>(pbr->landscapeNormalTextures).subspan(1, pbr->numLandscapeTextures - 1) };
		}
		const auto* terrain = static_cast<const RE::BSLightingShaderMaterialLandscape*>(material);
		if (!terrain->numLandscapeTextures || terrain->numLandscapeTextures > 6)
			return std::nullopt;
		return TerrainLayers{ material->diffuseTexture.get(), material->normalTexture.get(),
			std::span<const TexturePointer>(terrain->landscapeDiffuseTexture).first(terrain->numLandscapeTextures - 1),
			std::span<const TexturePointer>(terrain->landscapeNormalTexture).first(terrain->numLandscapeTextures - 1) };
	}

	static bool IsDefaultTerrainTexture(const RE::NiSourceTexture* texture)
	{
		if (!globals::game::graphicsState)
			return false;
		const auto& defaults = globals::game::graphicsState->GetRuntimeData();
		return SharesTextureResource(texture, defaults.defaultTextureBlack.get()) || SharesTextureResource(texture, defaults.defaultTextureNormalMap.get());
	}

	static bool IsAuxiliarySlot(const RE::BSLightingShaderMaterialBase* material, const RE::NiSourceTexture* texture)
	{
		const auto matches = [&](const auto& auxiliary) { return SharesTextureResource(texture, auxiliary.get()); };
		if (matches(material->rimSoftLightingTexture) || matches(material->specularBackLightingTexture))
			return true;
		const auto feature = material->GetFeature();
		if (feature == RE::BSShaderMaterial::Feature::kGlowMap)
			return matches(static_cast<const RE::BSLightingShaderMaterialGlowmap*>(material)->glowTexture);
		if (feature == RE::BSShaderMaterial::Feature::kEnvironmentMap) {
			const auto* env = static_cast<const RE::BSLightingShaderMaterialEnvmap*>(material);
			return matches(env->envTexture) || matches(env->envMaskTexture);
		}
		if (feature == RE::BSShaderMaterial::Feature::kParallax)
			return matches(static_cast<const RE::BSLightingShaderMaterialParallax*>(material)->heightTexture);
		if (feature == RE::BSShaderMaterial::Feature::kParallaxOcc)
			return matches(static_cast<const RE::BSLightingShaderMaterialParallaxOcc*>(material)->heightTexture);
		if (feature == RE::BSShaderMaterial::Feature::kMultilayerParallax) {
			const auto* layers = static_cast<const RE::BSLightingShaderMaterialMultiLayerParallax*>(material);
			return matches(layers->layerTexture) || matches(layers->envTexture) || matches(layers->envMaskTexture);
		}
		if (feature == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend) {
			if (const auto* pbr = BSLightingShaderMaterialPBRLandscape::GetRegistered(material))
				return matches(pbr->terrainOverlayTexture) || matches(pbr->terrainNoiseTexture) ||
				       std::ranges::any_of(pbr->landscapeDisplacementTextures, matches) || std::ranges::any_of(pbr->landscapeRMAOSTextures, matches);
			const auto* terrain = static_cast<const RE::BSLightingShaderMaterialLandscape*>(material);
			if (matches(terrain->terrainOverlayTexture) || matches(terrain->terrainNoiseTexture))
				return true;
			auto& helper = globals::features::terrainHelper;
			const std::shared_lock lock(helper.extendedSlotsMutex);
			const auto slots = helper.extendedSlots.find(material->hashKey);
			return slots != helper.extendedSlots.end() && std::ranges::any_of(slots->second.parallax, matches);
		}
		if (feature == BSLightingShaderMaterialPBR::FEATURE) {
			const auto* pbr = static_cast<const BSLightingShaderMaterialPBR*>(material);
			return matches(pbr->rmaosTexture) || matches(pbr->emissiveTexture) || matches(pbr->displacementTexture) ||
			       matches(pbr->featuresTexture0) || matches(pbr->featuresTexture1);
		}
		return false;
	}

	static bool UsesExtendedParallax(const RE::BSLightingShaderMaterialBase* material)
	{
		const auto& extended = globals::features::extendedMaterials;
		if (!extended.loaded || extended.settings.ParallaxStrength <= 0)
			return false;
		const auto feature = material->GetFeature();
		if (feature == RE::BSShaderMaterial::Feature::kEnvironmentMap)
			return extended.settings.EnableComplexMaterial && static_cast<const RE::BSLightingShaderMaterialEnvmap*>(material)->envMaskTexture;
		if (feature == BSLightingShaderMaterialPBR::FEATURE) {
			const auto* pbr = static_cast<const BSLightingShaderMaterialPBR*>(material);
			if (pbr->pbrFlags.any(PBRFlags::InterlayerParallax))
				return true;
			return extended.settings.EnableParallax && pbr->displacementTexture &&
			       (!globals::game::graphicsState || pbr->displacementTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack);
		}
		if (feature != RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)
			return false;
		if (const auto* pbr = BSLightingShaderMaterialPBRLandscape::GetRegistered(material))
			return extended.settings.EnableParallax && std::ranges::any_of(pbr->landscapeDisplacementTextures, [](const auto& texture) {
				return texture && (!globals::game::graphicsState || !SharesTextureResource(texture.get(), globals::game::graphicsState->GetRuntimeData().defaultTextureBlack.get()));
			});
		if (extended.settings.EnableTerrain)
			return true;
		auto& helper = globals::features::terrainHelper;
		if (!extended.settings.EnableParallax || !helper.IsRuntimeEnabled())
			return false;
		const std::shared_lock lock(helper.extendedSlotsMutex);
		const auto slots = helper.extendedSlots.find(material->hashKey);
		return slots != helper.extendedSlots.end() && std::ranges::any_of(slots->second.parallax, [](const auto& texture) {
			return texture && (!globals::game::graphicsState || texture != globals::game::graphicsState->GetRuntimeData().defaultTextureNormalMap);
		});
	}

	bool IsStreamingSlot(const RE::BSLightingShaderMaterialBase* material, const RE::NiSourceTexture* texture)
	{
		if (!material || !texture || IsAuxiliarySlot(material, texture))
			return false;
		if (material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend) {
			if (IsDefaultTerrainTexture(texture))
				return false;
			const auto layers = GetTerrainLayers(material);
			if (!layers)
				return false;
			const auto matches = [&](const auto& layer) { return layer.get() == texture; };
			return texture == layers->diffuse || texture == layers->normal ||
			       std::ranges::any_of(layers->additionalDiffuse, matches) || std::ranges::any_of(layers->additionalNormal, matches);
		}
		return texture == material->diffuseTexture.get() || texture == material->normalTexture.get();
	}

	RE::BSLightingShaderMaterialBase* StaticMaterial(RE::BSGeometry* geometry, std::uint32_t* categories
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		MaterialInspection* inspection
#endif
	)
	{
		const auto reject = [&]([[maybe_unused]] MaterialRejection reason) -> RE::BSLightingShaderMaterialBase* {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (inspection)
				inspection->reason = reason;
#endif
			return nullptr;
		};
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (inspection) {
			*inspection = {};
			inspection->geometryType = geometry ? geometry->GetType().underlying() : 0;
		}
#endif
		std::uint32_t required = 0;
		if (categories)
			*categories = 0;
		if (!geometry)
			return reject(MaterialRejection::GeometryType);
		const auto type = geometry->GetType();
		// These mesh layouts have stable world bounds; dynamic and LOD layouts stay protected.
		if (type != RE::BSGeometry::Type::kTriShape && type != RE::BSGeometry::Type::kSubIndexTriShape &&
			type != RE::BSGeometry::Type::kMultiIndexTriShape && type != RE::BSGeometry::Type::kMultiStreamInstanceTriShape &&
			type != RE::BSGeometry::Type::kSubIndexLandTriShape)
			return reject(MaterialRejection::GeometryType);
		const auto& data = geometry->GetGeometryRuntimeData();
		const auto* property = data.shaderProperty.get();
		if (!property)
			return reject(MaterialRejection::MissingProperty);
		if (!property->material)
			return reject(MaterialRejection::MissingMaterial);
		if (data.skinInstance)
			return reject(MaterialRejection::Skin);
		if (geometry->GetControllers())
			return reject(MaterialRejection::GeometryController);
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		constexpr auto excluded = static_cast<std::uint64_t>(Flag::kSkinned) | static_cast<std::uint64_t>(Flag::kFace) |
		                          static_cast<std::uint64_t>(Flag::kFaceGenRGBTint) | static_cast<std::uint64_t>(Flag::kHairTint) |
		                          static_cast<std::uint64_t>(Flag::kCharacterLighting) | static_cast<std::uint64_t>(Flag::kEyeReflect) |
		                          static_cast<std::uint64_t>(Flag::kLODLandscape) |
		                          static_cast<std::uint64_t>(Flag::kLODObjects) | static_cast<std::uint64_t>(Flag::kHDLODObjects) |
		                          static_cast<std::uint64_t>(Flag::kCloudLOD) | static_cast<std::uint64_t>(Flag::kBillboard) |
		                          static_cast<std::uint64_t>(Flag::kTreeAnim) | static_cast<std::uint64_t>(Flag::kProjectedUV) |
		                          static_cast<std::uint64_t>(Flag::kParallax) | static_cast<std::uint64_t>(Flag::kParallaxOcclusion) |
		                          static_cast<std::uint64_t>(Flag::kMultiLayerParallax) | static_cast<std::uint64_t>(Flag::kTempRefraction) |
		                          static_cast<std::uint64_t>(Flag::kRefraction) | static_cast<std::uint64_t>(Flag::kRefractionFalloff);
		if (const auto* alpha = data.alphaProperty.get()) {
			if (alpha->GetControllers())
				return reject(MaterialRejection::AlphaController);
			if (alpha->GetAlphaBlending())
				return reject(MaterialRejection::AlphaBlending);
			if (alpha->GetAlphaTesting())
				required |= AlphaTestedStatics;
		}
		constexpr auto emissive = static_cast<std::uint64_t>(Flag::kOwnEmit) |
		                          static_cast<std::uint64_t>(Flag::kExternalEmittance) | static_cast<std::uint64_t>(Flag::kGlowMap);
		const auto feature = property->material->GetFeature();
		const bool terrain = feature == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
		const bool nativeParallax = feature == RE::BSShaderMaterial::Feature::kParallax ||
		                            feature == RE::BSShaderMaterial::Feature::kParallaxOcc || feature == RE::BSShaderMaterial::Feature::kMultilayerParallax;
		constexpr auto parallaxFlags = static_cast<std::uint64_t>(Flag::kParallax) | static_cast<std::uint64_t>(Flag::kParallaxOcclusion) |
		                               static_cast<std::uint64_t>(Flag::kMultiLayerParallax);
		const bool knownParallaxLayout = nativeParallax || terrain || feature == RE::BSShaderMaterial::Feature::kEnvironmentMap || feature == BSLightingShaderMaterialPBR::FEATURE;
		const auto protectedFlags = knownParallaxLayout ? excluded & ~parallaxFlags : excluded;
		if (terrain)
			required |= Terrain;
		if ((type == RE::BSGeometry::Type::kSubIndexLandTriShape ||
				(property->flags.underlying() & static_cast<std::uint64_t>(Flag::kMultiTextureLandscape))) &&
			!terrain)
			return reject(MaterialRejection::MaterialFeature);
		if ((property->flags.underlying() & emissive) || feature == RE::BSShaderMaterial::Feature::kGlowMap)
			required |= EmissiveStatics;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (inspection) {
			inspection->flags = property->flags.underlying();
			inspection->disallowedFlags = property->flags.underlying() & protectedFlags;
			inspection->materialType = static_cast<std::uint32_t>(property->material->GetType());
			inspection->materialFeature = static_cast<std::uint32_t>(feature);
		}
#endif
		if (property->flags.underlying() & protectedFlags)
			return reject(MaterialRejection::ShaderFlags);
		if (property->GetControllers())
			return reject(MaterialRejection::PropertyController);
		if (property->material->GetType() != RE::BSShaderMaterial::Type::kLighting)
			return reject(MaterialRejection::MaterialType);
		using Feature = RE::BSShaderMaterial::Feature;
		if (feature != BSLightingShaderMaterialPBR::FEATURE) {
			switch (feature) {
			case Feature::kDefault:
			case Feature::kEnvironmentMap:
			case Feature::kGlowMap:
			case Feature::kMultiIndexTriShapeSnow:
			case Feature::kMultiTexLandLODBlend:
			case Feature::kParallax:
			case Feature::kParallaxOcc:
			case Feature::kMultilayerParallax:
				break;
			default:
				return reject(MaterialRejection::MaterialFeature);
			}
		}
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(property->material);
		// PBR landscape keeps the native feature ID, but its layer arrays have a different layout.
		if (terrain && (property->flags.underlying() & static_cast<std::uint64_t>(Flag::kVertexLighting)) && !BSLightingShaderMaterialPBRLandscape::GetRegistered(material))
			return reject(MaterialRejection::MaterialFeature);
		if (nativeParallax || (property->flags.underlying() & parallaxFlags) || UsesExtendedParallax(material))
			required |= Parallax;
		if (feature == BSLightingShaderMaterialPBR::FEATURE) {
			const auto* pbr = static_cast<const BSLightingShaderMaterialPBR*>(material);
			if (pbr->emissiveTexture && (!globals::game::graphicsState || pbr->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack))
				required |= EmissiveStatics;
		}
		if (material->diffuseRenderTargetSourceIndex != -1)
			return reject(MaterialRejection::RenderTarget);
		if (material->materialAlpha != 1.0f)
			return reject(MaterialRejection::MaterialAlpha);
		if (material->refractionPower != 0)
			return reject(MaterialRejection::Refraction);
		if (terrain) {
			const auto layers = GetTerrainLayers(material);
			if (!layers)
				return reject(MaterialRejection::MaterialFeature);
			const auto valid = [&](const RE::NiSourceTexture* texture) {
				return !texture || IsDefaultTerrainTexture(texture) || IsStreamingSlot(material, texture);
			};
			const auto validLayer = [&](const auto& texture) { return valid(texture.get()); };
			if (!valid(layers->diffuse) || !valid(layers->normal) ||
				!std::ranges::all_of(layers->additionalDiffuse, validLayer) || !std::ranges::all_of(layers->additionalNormal, validLayer))
				return reject(MaterialRejection::AuxiliaryTextures);
		} else if ((material->diffuseTexture && !IsStreamingSlot(material, material->diffuseTexture.get())) ||
				   (material->normalTexture && !IsStreamingSlot(material, material->normalTexture.get()))) {
			return reject(MaterialRejection::AuxiliaryTextures);
		}
		if (categories)
			*categories = required;
		return material;
	}

	double RequiredEdge(RE::BSGeometry* geometry, const DemandContext& demand)
	{
		const auto invalid = std::numeric_limits<double>::infinity();
		const auto* material = StaticMaterial(geometry);
		if (!material || !globals::state || !demand.Fresh(globals::state->frameCount, GetTickCount64()))
			return invalid;
		double uvSpan = 1.0;
		for (const auto& scale : material->texCoordScale) {
			if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !scale.x || !scale.y)
				return invalid;
			uvSpan = std::min({ uvSpan, std::abs(double(scale.x)), std::abs(double(scale.y)) });
		}
		auto eyes = demand.eyes;
		for (unsigned i = 0; i < demand.eyeCount; ++i) {
			const auto& p = demand.positions[i];
			const auto offset = geometry->worldBound.center - RE::NiPoint3{ p[0], p[1], p[2] };
			// Include a movement horizon so walking does not immediately outrun an in-flight refill.
			const double distance = std::hypot(double(offset.x), double(offset.y), double(offset.z));
			if (!std::isfinite(geometry->worldBound.radius) || geometry->worldBound.radius <= 0)
				return invalid;
			eyes[i].distanceToBound = distance - geometry->worldBound.radius - TextureStreamingPolicy::MovementHorizon;
		}
		return TextureStreamingPolicy::RequiredEdge(eyes, demand.eyeCount, 2.0 * geometry->worldBound.radius, demand.mipBias) / uvSpan;
	}
}
