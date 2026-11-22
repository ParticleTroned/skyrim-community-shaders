#include "Features/Skin.h"

#include "Features/Skin/WetnessPolicy.h"
#include "Globals.h"
#include "State.h"
#include "Utils/Finite.h"

#include <algorithm>

namespace
{
	RE::Actor* GeometryOwner(RE::BSGeometry* a_geometry, bool a_skinned)
	{
		auto* reference = a_geometry->GetUserData();
		auto* actor = reference && reference->IsActor() ? reference->As<RE::Actor>() : nullptr;
		if (!actor && !a_skinned)
			return nullptr;
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* actorRoot = actor ? actor->Get3D(false) : nullptr;
		const auto* playerRoot = actor == player ? actorRoot : (player ? player->Get3D(false) : nullptr);
		const auto* firstPerson = player ? player->Get3D(true) : nullptr;
		// First-person arms can lack user data; detached preview meshes are not live actors.
		for (const auto* node = static_cast<RE::NiAVObject*>(a_geometry); node; node = node->parent) {
			if (node == actorRoot)
				return actor;
			if (node == playerRoot || node == firstPerson)
				return player;
		}
		return nullptr;
	}
}

// Water-bound fallback follows powerof3's Splashes of Skyrim water-height lookup.
float Skin::GetWaterHeight(const RE::TESObjectREFR* a_actor, const RE::NiPoint3& a_position)
{
	const auto height = a_actor->GetWaterHeight();
	if (Util::IsFinite(height) && height != -RE::NI_INFINITY)
		return height;
	if (const auto* water = globals::game::waterSystem)
		for (const auto& object : water->waterObjects)
			if (object)
				for (const auto& bound : object->multiBounds)
					if (bound && bound->size.z <= 10.0f &&
						std::abs(a_position.x - bound->center.x) <= bound->size.x &&
						std::abs(a_position.y - bound->center.y) <= bound->size.y)
						return bound->center.z;
	return -RE::NI_INFINITY;
}

void Skin::ResetRuntimeStateAfterGameLoad()
{
	actorWetness.clear();
	lastPruneFrame = 0;
}

float4 Skin::GetWetness(RE::BSGeometry* a_geometry, bool a_skinned)
{
	// Geometry setup precedes State::Draw, which consumes the pending load reset.
	if (globals::state->pendingPostLoadRuntimeReset)
		ResetRuntimeStateAfterGameLoad();
	auto* actor = GeometryOwner(a_geometry, a_skinned);
	if (!actor)
		return {};
	const auto frame = globals::state->frameCount;
	if (actorWetness.size() > 1024 && lastPruneFrame != frame) {
		std::erase_if(actorWetness, [frame](const auto& entry) { return frame - entry.second.frame > 1; });
		lastPruneFrame = frame;
	}
	auto [it, inserted] = actorWetness.try_emplace(actor->GetFormID());
	auto& cached = it->second;
	if (!inserted && cached.frame == frame)
		return cached.value;
	cached.frame = frame;
	const float time = globals::state->timer;
	const float elapsed = SkinWetness::Elapsed(cached.time, time);
	cached.time = Util::IsFinite(time) ? time : 0.0f;
	const auto position = actor->GetPosition();
	float4 wetness{ 0.0f, 0.0f, position.z, 0.0f };
	auto* values = actor->AsActorValueOwner();
	if (!Util::IsFinite(position.x) || !Util::IsFinite(position.y) || !Util::IsFinite(position.z) || !values)
		return cached.value = float4{};
	const float maximum = values->GetPermanentActorValue(RE::ActorValue::kStamina) +
	                      actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina);
	const float current = values->GetActorValue(RE::ActorValue::kStamina);
	const float stamina = actor->IsDead() || !Util::IsFinite(maximum) || maximum <= 0 || !Util::IsFinite(current) ? 1.0f :
	                                                                                                                std::clamp(current / maximum, 0.0f, 1.0f);
	wetness.x = SkinWetness::Sweat(stamina, settings.StartSweat, settings.FullSweat);
	if (actor->IsInWater()) {
		const auto height = GetWaterHeight(actor, position);
		if (Util::IsFinite(height) && height != -RE::NI_INFINITY) {
			wetness.y = 2.0f;
			wetness.w = std::max(cached.value.w, std::max(0.0f, height - position.z));
		}
	}
	if (!inserted) {
		wetness.x = SkinWetness::Fade(cached.value.x, wetness.x, elapsed, settings.WetFadeTime, 1.0f);
		wetness.y = SkinWetness::Fade(cached.value.y, wetness.y, elapsed, settings.WetFadeTime, 2.0f);
		if (wetness.y > 0.0f)
			wetness.w = std::max(wetness.w, cached.value.w);
	}
	cached.value = wetness;
	return wetness;
}
