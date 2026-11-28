#pragma once

#include "RE/B/BSMultiBoundNode.h"
#include "RE/N/NiSmartPointer.h"

#include <atomic>
#include <unordered_map>

/** @brief Retains native terrain water while replacement nodes are attached; requires the water-system lock. */
class UnifiedWaterLOD
{
public:
	/// Pins both nodes until the replacement is detached and its original restored.
	void Register(RE::BSMultiBoundNode* replacement, RE::BSMultiBoundNode* original)
	{
		originals.insert_or_assign(replacement, Entry{ RE::NiPointer<RE::BSMultiBoundNode>{ replacement }, RE::NiPointer<RE::BSMultiBoundNode>{ original } });
		replacement->SetAppCulled(IsCulled());
	}

	/// Allows unchanged visibility to skip the water-system lock and node traversal.
	bool IsCulled() const { return culled.load(std::memory_order_acquire); }

	/// Updates owned roots only; callers hold the water-system lock.
	void SetCulled(bool value)
	{
		if (IsCulled() == value)
			return;
		for (const auto& [replacement, entry] : originals)
			entry.replacement->SetAppCulled(value);
		culled.store(value, std::memory_order_release);
	}

	/// Recognizes the replacement root and its directly attached generated tiles.
	bool Contains(const RE::NiAVObject* water) const
	{
		return water && (originals.contains(water) || originals.contains(water->parent));
	}

	/// Keeps the native node alive while detach restores it to the terrain chunk.
	RE::NiPointer<RE::BSMultiBoundNode> GetOriginal(const RE::NiAVObject* replacement) const
	{
		const auto entry = originals.find(replacement);
		if (entry == originals.end())
			return {};
		return entry->second.original;
	}
	/// Releases ownership after the replacement is detached and native geometry is restored.
	void Remove(const RE::NiAVObject* replacement) { originals.erase(replacement); }

private:
	struct Entry
	{
		RE::NiPointer<RE::BSMultiBoundNode> replacement;
		RE::NiPointer<RE::BSMultiBoundNode> original;
	};
	std::unordered_map<const RE::NiAVObject*, Entry> originals;
	std::atomic_bool culled{ false };
};
