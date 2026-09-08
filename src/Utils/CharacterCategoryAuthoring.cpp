#include "CharacterCategoryAuthoring.h"

#include "Deferred.h"
#include "Features/Upscaling.h"
#include "Features/Upscaling/NeuralRendering/CharacterRendering.h"
#include "Globals.h"
#include "State.h"

#include <cmath>
#include <limits>

namespace
{
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

	CharacterMaterialClassification ClassifyCharacterMaterial(
		RE::BSShaderProperty* a_property,
		RE::BSGeometry* a_geometry,
		RE::Actor* a_actor) noexcept
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

void CharacterCategoryAuthoring::Update(RE::BSRenderPass* a_pass)
{
	auto* state = globals::state;
	if (!state)
		return;
	constexpr auto categoryFlags =
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterCategoryMask) |
		static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterExcluded);
	state->permutationData.ExtraShaderDescriptor &= ~categoryFlags;

	if (!globals::deferred || !globals::deferred->deferredPass ||
		!state->inWorld || !a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return;

	const auto& upscaling = globals::features::upscaling;
	static thread_local uint32_t policyFrame = std::numeric_limits<uint32_t>::max();
	static thread_local bool routeRequested = false;
	static thread_local NeuralRendering::CharacterSettings actorPolicy{};
	static thread_local uint32_t projectionWidth = 0;
	static thread_local uint32_t projectionHeight = 0;
	if (policyFrame != state->frameCount) {
		policyFrame = state->frameCount;
		routeRequested = upscaling.IsCharacterNeuralRenderingRouteRequested();
		if (routeRequested) {
			actorPolicy = upscaling.GetCharacterNeuralRenderingSettings();
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

	auto classification = ClassifyCharacterMaterial(
		a_pass->shaderProperty, a_pass->geometry, actor);
	auto& characterRendering = NeuralRendering::CharacterRendering::Instance();
	if (classification.rejection == NeuralRendering::CharacterClassificationRejection::BlendedMaterial ||
		classification.rejection == NeuralRendering::CharacterClassificationRejection::AlphaTestAndBlend) {
		characterRendering.ObserveClassificationRejection(state->frameCount, classification.rejection);
		return;
	}

	// Opaque player, clothing, and rejected NPC surfaces block neighboring
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
		.faceBound = ReadBound(actor->GetFaceNodeSkinned()),
		.actorBound = ReadBound(actor->Get3D()),
		.outputWidthPerEye = projectionWidth,
		.outputHeight = projectionHeight,
		.settings = actorPolicy,
	};
	const auto& bound = a_pass->geometry->worldBound;
	if (characterRendering.ShouldAuthorActor(state->frameCount, actor->GetFormID(), admission) &&
		characterRendering.ObserveGeometry(state->frameCount, actor->GetFormID(),
			reinterpret_cast<std::uintptr_t>(a_pass->geometry), classification.category,
			bound.center.x, bound.center.y, bound.center.z, bound.radius)) {
		state->permutationData.ExtraShaderDescriptor &= ~categoryFlags;
		state->permutationData.ExtraShaderDescriptor |=
			static_cast<uint32_t>(classification.category) *
			static_cast<uint32_t>(State::ExtraShaderDescriptors::CharacterFace);
	}
}
