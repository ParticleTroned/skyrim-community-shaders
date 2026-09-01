#pragma once

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

/**
 * Serializes PBR metadata against material loading, culling and destruction.
 * Callbacks must not reenter the registry, call the engine or retain entries.
 * The engine still owns material lifetimes outside a registry visit.
 */
template <class Material, class Extensions>
class PBRMaterialRegistry
{
public:
	/** Checks membership without dereferencing the material. */
	bool Contains(Material* material) const
	{
		const std::shared_lock lock(mutex);
		return entries.contains(material);
	}

	/** Registers a new material without overwriting existing metadata. */
	void Register(Material* material)
	{
		const std::unique_lock lock(mutex);
		entries.try_emplace(material);
	}

	/** Removes metadata before the material's members are destroyed. */
	void Unregister(Material* material)
	{
		const std::unique_lock lock(mutex);
		entries.erase(material);
	}

	/** Copies metadata atomically, defaulting an unregistered source. */
	void Copy(Material* destination, Material* source)
	{
		const std::unique_lock lock(mutex);
		const auto it = entries.find(source);
		const auto extensions = it != entries.end() ? it->second : Extensions{};
		entries.insert_or_assign(destination, extensions);
	}

	/** Updates metadata under exclusive ownership, registering if necessary. */
	template <class Callback>
	void Update(Material* material, Callback&& callback)
	{
		const std::unique_lock lock(mutex);
		callback(entries[material]);
	}

	/** Updates a registered material without recreating a removed entry. */
	template <class Callback>
	bool TryUpdate(Material* material, Callback&& callback)
	{
		const std::unique_lock lock(mutex);
		const auto it = entries.find(material);
		if (it == entries.end()) {
			return false;
		}
		callback(it->second);
		return true;
	}

	/** Visits materials while excluding metadata changes and unregistration. */
	template <class Callback>
	void ForEach(Callback&& callback)
	{
		const std::unique_lock lock(mutex);
		for (auto& [material, extensions] : entries) {
			callback(material, extensions);
		}
	}

private:
	mutable std::shared_mutex mutex;
	std::unordered_map<Material*, Extensions> entries;
};
