#include "CharacterCategoryAuthoring.h"

#include "Deferred.h"
#include "Features/Upscaling.h"
#include "Features/Upscaling/NeuralRendering/CharacterRendering.h"
#include "Globals.h"
#include "State.h"

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

	NeuralRendering::CharacterActorBound ReadBound(const RE::NiAVObject* a_object)
	{
		if (!a_object)
			return {};
		const auto& bound = a_object->worldBound;
		return { bound.center.x, bound.center.y, bound.center.z, bound.radius };
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

void CharacterCategoryAuthoring::Update(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state)
		return;
	constexpr auto categoryFlags =
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterCategoryMask) |
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded) |
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFocusFadeMask);
	static_assert((categoryFlags &
					  static_cast<uint32_t>(State::ExtraShaderDescriptors::AdditiveLighting)) == 0);
	state->permutationData.ExtraShaderDescriptor &= ~categoryFlags;

	if (!globals::features::upscaling.IsNeuralRenderingEnabled() ||
		!globals::deferred || !globals::deferred->deferredPass ||
		!state->inWorld || !a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return;

	const auto& upscaling = globals::features::upscaling;
	static thread_local uint32_t policyFrame = std::numeric_limits<uint32_t>::max();
	static thread_local bool routeRequested = false;
	static thread_local NeuralRendering::CharacterSettings actorPolicy{};
	static thread_local NeuralRendering::CharacterFocusMask focusMask{};
	static thread_local uint32_t projectionWidth = 0;
	static thread_local uint32_t projectionHeight = 0;
	if (policyFrame != state->frameCount) {
		policyFrame = state->frameCount;
		routeRequested = upscaling.IsCharacterNeuralRenderingRouteRequested();
		if (routeRequested) {
			actorPolicy = upscaling.GetCharacterNeuralRenderingSettings();
			focusMask = upscaling.GetCharacterFocusMask();
			projectionWidth = projectionHeight = 0;
			routeRequested = upscaling.GetCharacterNeuralRenderingProjectionExtent(
				projectionWidth, projectionHeight);
		}
	}
	if (!routeRequested)
		return;

	auto* owner = a_pass->geometry->GetUserData();
	auto* actor = owner ? owner->As<RE::Actor>() : nullptr;
	if (!actor)
		return;

	const auto* race = actor->GetRace();
	const auto group = GetActorGroup(state->frameCount, race);
	const bool nonHumanoid = group.group != NeuralRendering::CharacterActorGroup::Humans &&
	                         group.group != NeuralRendering::CharacterActorGroup::OtherHumanoids;
	auto classification = ClassifyCharacterMaterial(
		a_pass->shaderProperty, a_pass->geometry, actor, actorPolicy.armor || actorPolicy.weapons,
		nonHumanoid && actorPolicy.skin);
	auto& characterRendering = NeuralRendering::CharacterRendering::Instance();
	if (classification.rejection == NeuralRendering::CharacterClassificationRejection::BlendedMaterial ||
		classification.rejection == NeuralRendering::CharacterClassificationRejection::AlphaTestAndBlend) {
		characterRendering.ObserveClassificationRejection(state->frameCount, classification.rejection);
		return;
	}

	// Opaque player and rejected NPC surfaces block neighboring
	// character coverage; only uncovered background may receive feathering.
	state->permutationData.ExtraShaderDescriptor |=
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded);
	if (actor->IsPlayerRef()) {
		characterRendering.ObserveClassificationRejection(state->frameCount,
			NeuralRendering::CharacterClassificationRejection::Player);
		return;
	}
	if (classification.category == NeuralRendering::CharacterCategory::None) {
		characterRendering.ObserveClassificationRejection(state->frameCount, classification.rejection);
		return;
	}

	const NeuralRendering::CharacterActorAdmissionArgs admission{
		.actorIdentity = reinterpret_cast<std::uintptr_t>(actor),
		.raceIdentity = reinterpret_cast<std::uintptr_t>(race),
		.actorGroup = group,
		.faceBound = ReadBound(actor->GetFaceNodeSkinned()),
		.actorBound = ReadBound(actor->Get3D()),
		.outputWidthPerEye = projectionWidth,
		.outputHeight = projectionHeight,
		.settings = actorPolicy,
		.focusMask = focusMask,
	};
	std::uint32_t focusFade = 0;
	if (characterRendering.ShouldAuthorActor(state->frameCount, actor->GetFormID(), admission, focusFade) &&
		characterRendering.ObserveGeometry(state->frameCount, actor->GetFormID(),
			a_pass->geometry, classification.category,
			NeuralRendering::IsCharacterCategoryEnabled(classification.category, actorPolicy))) {
		state->permutationData.ExtraShaderDescriptor &= ~categoryFlags;
		state->permutationData.ExtraShaderDescriptor |=
			static_cast<uint32_t>(classification.category) *
			static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFace);
		state->permutationData.ExtraShaderDescriptor |= focusFade << static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFocusFadeShift);
	}
}
