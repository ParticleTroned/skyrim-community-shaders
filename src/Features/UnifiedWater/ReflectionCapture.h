#pragma once

#include "RE/B/BSMultiBoundNode.h"
#include "RE/B/BSWaterShaderMaterial.h"
#include "RE/B/BSWaterShaderProperty.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiRefObject.h"
#include "RE/N/NiSmartPointer.h"
#include "RE/T/TESWaterReflections.h"
#include "RE/T/TESWaterSystem.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

/** @brief Pins native dynamic captures and their raw material dependencies under the water-system lock. */
class UnifiedWaterReflectionCapture
{
public:
	using DeferredRelease = void (*)(RE::NiPointer<RE::NiRefObject>&);
	struct Source
	{
		RE::NiPointer<RE::BSMultiBoundNode> parent;
		const RE::TESWorldSpace* worldSpace;
		RE::TESWaterForm* waterType;
		float waterHeight;
	};

	/** @brief Publishes a fully attached tile as a possible native initialization source. */
	void RegisterSource(RE::BSMultiBoundNode* parent, const RE::TESWorldSpace* worldSpace, RE::TESWaterForm* waterType, float waterHeight)
	{
		sourceResolved = false;
		sources.insert_or_assign(parent, Source{ RE::NiPointer<RE::BSMultiBoundNode>{ parent }, worldSpace, waterType, waterHeight });
	}

	/** @brief Invalidates the cached source before its node can be detached or rebuilt. */
	void RemoveSource(RE::BSMultiBoundNode* parent)
	{
		if (sources.erase(parent) != 0)
			sourceResolved = false;
	}

	/** @brief Discards source pointers before world teardown. */
	void ClearSources()
	{
		sourceResolved = false;
		sources.clear();
	}

	bool HasSources() const { return !sources.empty(); }

	/** @brief Caches both hits and misses so steady frames do not scan the tile registry. */
	const Source* FindSource(const RE::TESWorldSpace* worldSpace)
	{
		if (!sourceResolved || sourceWorldSpace != worldSpace) {
			const auto it = std::ranges::find_if(sources, [&](const auto& entry) { return entry.second.worldSpace == worldSpace; });
			selectedSource = it == sources.end() ? nullptr : &it->second;
			sourceWorldSpace = worldSpace;
			sourceResolved = true;
		}
		return selectedSource;
	}

	/** @brief Selects the exterior dynamic path without changing native scheduling flags. */
	static bool IsEligible(const RE::TESWaterReflections* reflection)
	{
		return reflection && reflection->flags.any(RE::TESWaterReflections::Flags::kDynamicCubemap) &&
		       !reflection->flags.any(RE::TESWaterReflections::Flags::kStaticCubemap, RE::TESWaterReflections::Flags::kInterior);
	}

	/** @brief Accepts only a material verified against a live water shader property. */
	bool Retain(RE::TESWaterReflections* reflection, RE::BSWaterShaderProperty* property)
	{
		const auto material = property ? property->material : nullptr;
		if (!IsEligible(reflection) || !material || reflection->waterMaterial != material)
			return false;

		if (!Contains(reflection)) {
			RE::NiCloningProcess cloningProcess;
			RE::NiPointer<RE::NiObject> clone{ property->CreateClone(cloningProcess) };
			if (!clone || clone.get() == property || clone->GetRTTI() != property->GetRTTI())
				return false;
			const auto materialOwner = static_cast<RE::BSWaterShaderProperty*>(clone.get());
			if (materialOwner->material != material) {
				materialOwner->SetMaterial(nullptr, false);
				// Native property destruction removes a final material from the engine cache.
				material->IncRef();
				materialOwner->material = material;
			}
			RE::NiPointer<Owner> owner{ new Owner(reflection, materialOwner) };
			owners.push_back(std::move(owner));
		}
		return true;
	}

	/** @brief Reports whether both dependencies already have a lifetime guard. */
	bool Contains(const RE::TESWaterReflections* reflection) const
	{
		return std::ranges::any_of(owners, [&](const auto& owner) { return owner->reflection.get() == reflection; });
	}

	/** @brief Retires unused captures once native borrowers have released them, then defers destruction. */
	void RetireUnused(RE::TESWaterSystem& waterSystem, const RE::TESWaterReflections* keep, DeferredRelease defer)
	{
		std::erase_if(owners, [&](const auto& owner) {
			const auto reflection = owner->reflection.get();
			if (reflection == keep)
				return false;

			auto& entries = waterSystem.waterReflections;
			const auto entry = std::ranges::find_if(entries, [&](const auto& candidate) { return candidate.get() == reflection; });
			const auto ownedReferences = entry == entries.end() ? 1u : 2u;
			// Native objects can keep using this raw material after UW's tiles unload.
			if (reflection->GetRefCount() != ownedReferences)
				return false;

			if (entry != entries.end())
				entries.erase(entry);
			RE::NiPointer<RE::NiRefObject> released{ owner.get() };
			defer(released);
			return true;
		});
	}

private:
	struct Owner : RE::NiRefObject
	{
		Owner(RE::TESWaterReflections* a_reflection, RE::BSWaterShaderProperty* a_materialOwner) :
			materialOwner(a_materialOwner), reflection(a_reflection)
		{}

		// Destroy the reflection before releasing the material its destructor may access.
		RE::NiPointer<RE::BSWaterShaderProperty> materialOwner;
		RE::NiPointer<RE::TESWaterReflections> reflection;
	};

	std::vector<RE::NiPointer<Owner>> owners;
	std::unordered_map<RE::BSMultiBoundNode*, Source> sources;
	const Source* selectedSource = nullptr;
	const RE::TESWorldSpace* sourceWorldSpace = nullptr;
	bool sourceResolved = false;
};
