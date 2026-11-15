#pragma once

#include <functional>
#include <map>
#include <set>
#include <utility>

namespace GrassLOD
{
	/** @brief Render-thread cache whose successful and negative entries require live demand. */
	template <class Key, class Value>
	class Cache
	{
	public:
		/** @brief Release all assets and demand without allocating during memory-pressure recovery. */
		void Clear()
		{
			entries.clear();
			demand.clear();
		}
		/** @brief Retire unreferenced assets outside the native capture lock. */
		void SetDemand(std::set<Key, std::less<>> next)
		{
			std::erase_if(entries, [&](const auto& entry) { return !next.contains(entry.first); });
			demand = std::move(next);
		}
		/** @brief Return an owned request key without constructing a lookup string. */
		template <class Lookup>
		const Key* Requested(const Lookup& key) const
		{
			const auto found = demand.find(key);
			return found == demand.end() ? nullptr : &*found;
		}
		/** @brief Read only completed assets; an absent entry keeps native full detail. */
		template <class Lookup>
		const Value* Find(const Lookup& key) const
		{
			const auto found = entries.find(key);
			return found == entries.end() ? nullptr : &found->second;
		}
		/** @brief Publish a completed positive or negative result only while still requested. */
		template <class Lookup>
		void Publish(const Lookup& key, Value&& value)
		{
			if (const auto requested = Requested(key))
				entries.try_emplace(*requested, std::move(value));
		}
		const auto& Entries() const { return entries; }

	private:
		std::set<Key, std::less<>> demand;
		std::map<Key, Value, std::less<>> entries;
	};
}
