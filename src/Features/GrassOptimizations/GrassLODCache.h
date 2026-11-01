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
		/** @brief Deduplicate loads, including failures, only while a resident needs the key. */
		template <class Lookup, class Load>
		const Value* FindOrLoad(const Lookup& key, Load&& load)
		{
			if (const auto found = entries.find(key); found != entries.end())
				return &found->second;
			const auto requested = demand.find(key);
			if (requested == demand.end())
				return nullptr;
			return &entries.emplace(*requested, std::forward<Load>(load)(*requested)).first->second;
		}
		const auto& Entries() const { return entries; }

	private:
		std::set<Key, std::less<>> demand;
		std::map<Key, Value, std::less<>> entries;
	};
}
