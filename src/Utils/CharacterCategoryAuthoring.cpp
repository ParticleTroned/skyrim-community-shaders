#include "CharacterCategoryAuthoring.h"

#include "Deferred.h"
#include "Features/Upscaling.h"
#include "Features/Upscaling/NeuralRendering/MonoRenderer.h"
#include "Globals.h"
#include "State.h"
#include "Utils/Game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace
{
	struct ActorRaceKeywords
	{
		RE::BGSKeyword* npc = nullptr;
		RE::BGSKeyword* animal = nullptr;
		RE::BGSKeyword* troll = nullptr;
		std::array<RE::BGSKeyword*, 5> creatures{};
	} actorRaceKeywords;

	bool HasHumanMorphLineage(const RE::TESRace* a_race) noexcept
	{
		// Base, child and elder forms have stable Skyrim.esm identities.
		constexpr std::array<std::uint32_t, 9> humanRaces{
			0x13741, 0x13744, 0x13746, 0x13748, 0x2C659, 0x2C65A, 0x2C65B, 0x2C65C, 0x67CD8
		};
		std::array<const RE::TESRace*, 16> visited{};
		for (std::size_t i = 0; a_race && i < visited.size(); ++i) {
			if (std::find(visited.begin(), visited.begin() + i, a_race) != visited.begin() + i)
				return false;
			if (std::find(humanRaces.begin(), humanRaces.end(), a_race->GetFormID()) != humanRaces.end())
				return true;
			visited[i] = a_race;
			a_race = a_race->morphRace;
		}
		return false;
	}

	NeuralRendering::CharacterActorGroupClassification ClassifyActorRace(const RE::TESRace* a_race) noexcept
	{
		if (!a_race)
			return {};
		const auto has = [&](const RE::BGSKeyword* keyword) { return keyword && a_race->HasKeyword(keyword); };
		return NeuralRendering::ClassifyCharacterActorGroup({
			.present = true,
			.humanLineage = HasHumanMorphLineage(a_race),
			.npc = has(actorRaceKeywords.npc),
			.animal = has(actorRaceKeywords.animal),
			.troll = has(actorRaceKeywords.troll),
			.creature = std::any_of(actorRaceKeywords.creatures.begin(), actorRaceKeywords.creatures.end(), has),
		});
	}

	NeuralRendering::CharacterActorGroupClassification GetActorGroup(std::uint32_t a_frame, const RE::TESRace* a_race)
	{
		struct Entry
		{
			std::uint32_t frame;
			NeuralRendering::CharacterActorGroupClassification group;
		};
		static thread_local std::unordered_map<const RE::TESRace*, Entry> cache;
		const auto found = cache.find(a_race);
		if (found != cache.end() && found->second.frame == a_frame)
			return found->second.group;
		const auto group = ClassifyActorRace(a_race);
		if (found != cache.end())
			found->second = { a_frame, group };
		else if (cache.size() < NeuralRendering::CharacterPolicy::kMaximumObservationsPerFrame)
			cache.emplace(a_race, Entry{ a_frame, group });
		return group;
	}

	enum class AncestryResult
	{
		Descendant,
		NotDescendant,
		Indeterminate,
	};

	AncestryResult ResolveAncestry(
		const RE::NiAVObject* a_object,
		const RE::NiAVObject* a_ancestor) noexcept
	{
		constexpr std::uint32_t kMaximumParentDepth = 64;
		for (std::uint32_t depth = 0;
			a_object && depth < kMaximumParentDepth;
			++depth, a_object = a_object->parent) {
			if (a_object == a_ancestor)
				return AncestryResult::Descendant;
		}
		return a_object ? AncestryResult::Indeterminate :
		                  AncestryResult::NotDescendant;
	}

	struct CharacterMaterialClassification
	{
		NeuralRendering::CharacterCategory category =
			NeuralRendering::CharacterCategory::None;
		NeuralRendering::CharacterClassificationRejection rejection =
			NeuralRendering::CharacterClassificationRejection::UnsupportedMaterial;
	};

	NeuralRendering::CharacterCategory ClassifyEquipment(
		const RE::BSGeometry* a_geometry, RE::Actor* a_actor, bool a_includeEquipment, bool a_includeBody) noexcept
	{
		const auto& biped = a_actor->GetBiped();
		if (!biped || biped->actorRef.get().get() != a_actor)
			return NeuralRendering::CharacterCategory::None;
		const auto* skin = a_actor->GetSkin();
		for (const auto& object : biped->objects) {
			if (!object.item || !object.partClone)
				continue;
			NeuralRendering::CharacterCategory category = NeuralRendering::CharacterCategory::None;
			if (object.item == skin) {
				if (a_includeBody)
					category = NeuralRendering::CharacterCategory::Skin;
			} else if (!a_includeEquipment) {
				continue;
			} else if (object.item->Is(RE::FormType::Armor))
				category = NeuralRendering::CharacterCategory::Armor;
			else if (object.item->Is(RE::FormType::Weapon, RE::FormType::Ammo))
				category = NeuralRendering::CharacterCategory::Weapons;
			if (category != NeuralRendering::CharacterCategory::None &&
				ResolveAncestry(a_geometry, object.partClone.get()) == AncestryResult::Descendant)
				return category;
		}
		return NeuralRendering::CharacterCategory::None;
	}

	CharacterMaterialClassification ClassifyCharacterMaterial(
		RE::BSShaderProperty* a_property,
		RE::BSGeometry* a_geometry,
		RE::Actor* a_actor, bool a_includeEquipment, bool a_includeBody = false) noexcept
	{
		if (!a_property || !a_geometry || !a_actor)
			return {};
		const auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(
			a_geometry->GetGeometryRuntimeData().alphaProperty.get());
		const auto* lightingProperty =
			a_property->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ?
				static_cast<RE::BSLightingShaderProperty*>(a_property) :
				nullptr;
		const bool hasImplicitAlphaBlend =
			lightingProperty && (!std::isfinite(lightingProperty->alpha) ||
									lightingProperty->alpha < 1.0f);
		if (hasImplicitAlphaBlend ||
			(alphaProperty && alphaProperty->GetAlphaBlending())) {
			// Blended surfaces cannot author exact categories. Alpha-tested
			// draws remain eligible because surviving samples are opaque.
			return {
				.rejection = alphaProperty && alphaProperty->GetAlphaTesting() ?
				                 NeuralRendering::CharacterClassificationRejection::AlphaTestAndBlend :
				                 NeuralRendering::CharacterClassificationRejection::BlendedMaterial,
			};
		}

		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		const auto* material = a_property->GetBaseMaterial();
		const auto feature = material ? material->GetFeature() :
		                                RE::BSShaderMaterial::Feature::kNone;
		if (a_property->flags.any(Flag::kFace) ||
			feature == RE::BSShaderMaterial::Feature::kFaceGen) {
			return { .category = NeuralRendering::CharacterCategory::Face };
		}

		const bool isFaceGenRgbTint =
			a_property->flags.any(Flag::kFaceGenRGBTint) ||
			feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
		if (isFaceGenRgbTint) {
			// FaceGen RGB tint is also used by exposed body skin. The actor's
			// face-node ancestry is the reliable distinction; kSkinned is not.
			const auto* faceNode = a_actor->GetFaceNodeSkinned();
			if (!faceNode)
				return {
					.rejection = NeuralRendering::CharacterClassificationRejection::AmbiguousFaceGen,
				};
			switch (ResolveAncestry(a_geometry, faceNode)) {
			case AncestryResult::Descendant:
				return { .category = NeuralRendering::CharacterCategory::Face };
			case AncestryResult::NotDescendant:
				return { .category = NeuralRendering::CharacterCategory::Skin };
			default:
				// Corrupt or unexpectedly deep scene graphs must not broaden the mask.
				return {
					.rejection = NeuralRendering::CharacterClassificationRejection::AmbiguousFaceGen,
				};
			}
		}

		if (a_property->flags.any(Flag::kHairTint) ||
			feature == RE::BSShaderMaterial::Feature::kHairTint) {
			return { .category = NeuralRendering::CharacterCategory::Hair };
		}
		if (lightingProperty && (a_includeEquipment || a_includeBody))
			return { .category = ClassifyEquipment(a_geometry, a_actor, a_includeEquipment, a_includeBody) };
		return {};
	}

	NeuralRendering::CharacterProjectionResult ProjectBound(const RE::NiAVObject* object,
		uint32_t width, uint32_t height, NeuralRendering::CharacterRect& rectangle)
	{
		using namespace NeuralRendering;
		if (!object || !std::isfinite(object->worldBound.radius) || object->worldBound.radius <= 0.0f) {
			rectangle = { 0, 0, width, height };
			return CharacterProjectionResult::Uncertain;
		}
		const auto& bound = object->worldBound;
		const auto& frame = globals::game::frameBufferCached;
		const auto& origin = frame.GetCameraPosAdjust();
		const auto unjittered = frame.GetCameraViewProjUnjittered().Transpose();
		const auto raster = frame.GetCameraViewProj().Transpose();
		std::array<CharacterClipPoint, 8> corners{}, rasterCorners{};
		for (uint32_t index = 0; index < corners.size(); ++index) {
			const float4 point{
				bound.center.x + ((index & 1) ? bound.radius : -bound.radius) - origin.x,
				bound.center.y + ((index & 2) ? bound.radius : -bound.radius) - origin.y,
				bound.center.z + ((index & 4) ? bound.radius : -bound.radius) - origin.z, 1.0f
			};
			const auto clip = DirectX::SimpleMath::Vector4::Transform(point, unjittered);
			const auto rasterClip = DirectX::SimpleMath::Vector4::Transform(point, raster);
			corners[index] = { clip.x, clip.y, clip.w };
			rasterCorners[index] = { rasterClip.x, rasterClip.y, rasterClip.w };
		}
		return ResolveCharacterProjectionPair(corners, rasterCorners, width, height, rectangle);
	}
}

void CharacterCategoryAuthoring::DataLoaded()
{
	actorRaceKeywords.npc = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
	actorRaceKeywords.animal = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeAnimal");
	actorRaceKeywords.troll = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeTroll");
	constexpr std::array names{ "ActorTypeCreature", "ActorTypeUndead", "ActorTypeDaedra", "ActorTypeDragon", "ActorTypeDwarven" };
	for (std::size_t i = 0; i < names.size(); ++i)
		actorRaceKeywords.creatures[i] = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(names[i]);
}

void CharacterCategoryAuthoring::Update(RE::BSRenderPass* pass)
{
	using namespace NeuralRendering;
	auto* state = globals::state;
	if (!state)
		return;
	constexpr auto flags = static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterCategoryMask) |
	                       static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded) |
	                       static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFocusFadeMask);
	state->permutationData.ExtraShaderDescriptor &= ~flags;
	auto& upscaling = globals::features::upscaling;
#ifdef DEVBENCH_BRIDGE_ENABLED
	const auto& faceSettings = upscaling.GetNeuralRenderingSettings();
	if (MonoRenderer::Instance().WantsFaceDetail() && faceSettings.peripheralCompressionPercent &&
		!faceSettings.charactersOnly && !faceSettings.sceneCategoryStrengths && upscaling.IsNeuralRenderingRequested() &&
		globals::deferred && globals::deferred->deferredPass && state->inWorld && pass && pass->geometry && pass->shaderProperty) {
		auto* faceOwner = pass->geometry->GetUserData();
		auto* faceActor = faceOwner ? faceOwner->As<RE::Actor>() : nullptr;
		if (faceActor && !faceActor->IsPlayerRef() && faceActor->GetFaceNodeSkinned() &&
			ClassifyCharacterMaterial(pass->shaderProperty, pass->geometry, faceActor, false).category == CharacterCategory::Face) {
			const auto facePlan = upscaling.GetRuntimeResolutionPlan();
			CharacterRect head{};
			if (!facePlan.menuContextActive && !facePlan.loadingMenuActive &&
				ProjectBound(faceActor->GetFaceNodeSkinned(), facePlan.engineRenderSize.x, facePlan.engineRenderSize.y, head) == CharacterProjectionResult::Visible)
				MonoRenderer::Instance().ObserveFaceDetail(state->frameCount, faceActor->GetFormID(), head,
					facePlan.engineRenderSize.x, facePlan.engineRenderSize.y);
		}
	}
#endif
	if (!upscaling.IsNeuralCharacterSelectionRequested() || !globals::deferred ||
		!globals::deferred->deferredPass || !globals::deferred->IsCharacterCategoryEnabled() ||
		!state->inWorld || !pass || !pass->geometry || !pass->shaderProperty)
		return;
	auto* owner = pass->geometry->GetUserData();
	auto* actor = owner ? owner->As<RE::Actor>() : nullptr;
	if (!actor)
		return;
	const auto& settings = upscaling.GetNeuralRenderingSettings();
	const auto group = GetActorGroup(state->frameCount, actor->GetRace());
	const bool nonHumanoid = group.group != CharacterActorGroup::Humans && group.group != CharacterActorGroup::OtherHumanoids;
	const auto classification = ClassifyCharacterMaterial(pass->shaderProperty, pass->geometry, actor,
		(settings.categoryMask & ((1u << 4) | (1u << 5))) != 0,
		nonHumanoid && (settings.categoryMask & (1u << 2)) != 0);
	if (classification.rejection == CharacterClassificationRejection::BlendedMaterial ||
		classification.rejection == CharacterClassificationRejection::AlphaTestAndBlend)
		return;
	// Opaque excluded actors prevent semantic feathering onto player or unrelated geometry.
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded);
	if (actor->IsPlayerRef() || classification.category == CharacterCategory::None ||
		(settings.categoryMask & CharacterPolicy::CategoryBit(classification.category)) == 0)
		return;
	const auto plan = upscaling.GetRuntimeResolutionPlan();
	const auto width = plan.engineRenderSize.x, height = plan.engineRenderSize.y;
	if (!width || !height || plan.menuContextActive || plan.loadingMenuActive)
		return;
	ActorObservation observation{
		.identity = reinterpret_cast<std::uintptr_t>(actor),
		.frame = state->frameCount,
		.group = group.group,
		.viewportWidth = width,
		.viewportHeight = height
	};
	const auto* actorRoot = actor->Get3D();
	const auto projection = ProjectBound(actorRoot, width, height, observation.rectangle);
	if (projection == CharacterProjectionResult::Offscreen)
		return;
	observation.projectionUncertain = projection == CharacterProjectionResult::Uncertain;
	CharacterRect faceRect{};
	const RE::NiAVObject* detail = actor->GetFaceNodeSkinned();
	if (!detail)
		detail = actorRoot;
	const auto faceProjection = ProjectBound(detail, width, height, faceRect);
	observation.projectionUncertain |= faceProjection == CharacterProjectionResult::Uncertain;
	if (faceRect.IsValid())
		observation.facePixelSize = std::max(faceRect.maxX - faceRect.minX, faceRect.maxY - faceRect.minY);
	if (detail) {
		const auto& origin = globals::game::frameBufferCached.GetCameraPosAdjust();
		const auto& bound = detail->worldBound;
		const float dx = bound.center.x - origin.x, dy = bound.center.y - origin.y, dz = bound.center.z - origin.z;
		observation.distanceMeters = Util::Units::GameUnitsToMeters(std::max(0.0f,
			std::sqrt(dx * dx + dy * dy + dz * dz) - bound.radius));
		if (!std::isfinite(observation.distanceMeters)) {
			observation.distanceMeters = 0.0f;
			observation.projectionUncertain = true;
		}
	}
	const auto admission = upscaling.ObserveNeuralActor(observation);
	if (!admission.selected || admission.frame != state->frameCount)
		return;
	state->permutationData.ExtraShaderDescriptor &= ~flags;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(classification.category) << static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterCategoryShift);
	state->permutationData.ExtraShaderDescriptor |= std::min(admission.focusFade, 255u) << static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFocusFadeShift);
}
