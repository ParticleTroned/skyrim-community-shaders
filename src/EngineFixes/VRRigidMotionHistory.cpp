#include "VRRigidMotionHistory.h"

#include "Globals.h"
#include "RigidMotionHistory.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/Finite.h"

#include <RE/B/BSGeometry.h>
#include <RE/B/BSRenderPass.h>
#include <RE/B/BSShaderAccumulator.h>
#include <RE/T/TESObjectREFR.h>
#include <ankerl/unordered_dense.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include <optional>

namespace
{
	constexpr std::size_t kMaximumHistoryEntries = 4096;

	struct Entry
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiNode* parent;
		RE::FormID owner;
		std::uint32_t frame;
		RigidMotion::History<RE::NiTransform> history;
	};

	using Entries = ankerl::unordered_dense::map<RE::BSGeometry*, Entry>;
	thread_local std::optional<Entries> entries;
	thread_local bool allocationFailed = false;
	std::atomic_flag allocationWarning;

	const RE::NiTransform* Observe(RE::BSRenderPass* a_pass, RE::TESObjectREFR* a_owner, std::uint32_t a_renderFlags)
	{
		const auto* state = globals::state;
		constexpr auto cubeMap = static_cast<std::uint32_t>(RE::BSShaderAccumulator::RENDER_FLAG::kCubeMap);
		if (!state || !state->inWorld || (a_renderFlags & cubeMap) || !a_pass || !a_pass->geometry || !a_pass->shaderProperty ||
			a_pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned))
			return nullptr;
		if (!a_owner || !a_owner->Is(RE::FormType::ActorCharacter) ||
			state->IsMainOrLoadingMenuOpen() || state->IsWorldLoadTransitionActive() ||
			!globals::shaderCache || !globals::shaderCache->IsEnabled())
			return nullptr;

		auto* geometry = a_pass->geometry;

		for (const float value : std::bit_cast<std::array<float, sizeof(RE::NiTransform) / sizeof(float)>>(geometry->world)) {
			if (!Util::IsFinite(value)) {
				if (entries)
					entries->erase(geometry);
				return nullptr;
			}
		}
		if (!entries) {
			if (allocationFailed)
				return &geometry->world;
			try {
				// Reserve both storage arrays before admission so draw-time growth cannot fail.
				entries.emplace(kMaximumHistoryEntries);
			} catch (const std::bad_alloc&) {
				allocationFailed = true;
				if (!allocationWarning.test_and_set(std::memory_order_relaxed))
					logger::warn("[VR rigid motion] History allocation failed; using camera-only motion until the next world transition");
				return &geometry->world;
			}
		}
		auto found = entries->find(geometry);
		if (found == entries->end()) {
			if (entries->size() >= kMaximumHistoryEntries)
				return &geometry->world;
			found = entries->emplace(geometry, Entry{ RE::NiPointer<RE::BSGeometry>{ geometry },
												   geometry->parent, a_owner->GetFormID(), state->frameCount, {} })
			            .first;
		} else if (found->second.parent != geometry->parent || found->second.owner != a_owner->GetFormID()) {
			found->second.parent = geometry->parent;
			found->second.owner = a_owner->GetFormID();
			found->second.history = {};
		}
		auto& entry = found->second;
		entry.frame = state->frameCount;
		return &entry.history.Observe(state->frameCount, geometry->world);
	}
}

void VRRigidMotionHistory::Reset(std::uint32_t a_frame, bool a_clear)
{
	if (a_clear) {
		if (entries)
			entries->clear();
		allocationFailed = false;
		return;
	}
	if (!entries)
		return;
	// Retained geometry cannot be recycled into a different object between frames.
	for (auto it = entries->begin(); it != entries->end();) {
		if (a_frame - it->second.frame > 1)
			it = entries->erase(it);
		else
			++it;
	}
}

void VRRigidMotionHistory::SetupGeometry(RE::BSShader* a_shader, RE::BSRenderPass* a_pass,
	std::uint32_t a_renderFlags, RE::TESObjectREFR* a_owner, GeometrySetup a_setup)
{
	if (const auto* previous = Observe(a_pass, a_owner, a_renderFlags)) {
		try {
			RigidMotion::PreviousWorldScope scope(*a_pass->geometry, *previous);
			a_setup(a_shader, a_pass, a_renderFlags);
		} catch (...) {
			if (entries)
				entries->erase(a_pass->geometry);
			throw;
		}
	} else {
		a_setup(a_shader, a_pass, a_renderFlags);
	}
}
