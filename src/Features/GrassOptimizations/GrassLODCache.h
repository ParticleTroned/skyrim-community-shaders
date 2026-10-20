#pragma once

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
		/** @brief Retire unreferenced assets outside the native capture lock. */
		void SetDemand(std::set<Key> next)
		{
			std::erase_if(entries, [&](const auto& entry) { return !next.contains(entry.first); });
			demand = std::move(next);
		}
		/** @brief Deduplicate loads, including failures, only while a resident needs the key. */
		template <class Load>
		const Value* FindOrLoad(const Key& key, Load&& load)
		{
			if (!demand.contains(key))
				return nullptr;
			if (const auto found = entries.find(key); found != entries.end())
				return &found->second;
			return &entries.emplace(key, std::forward<Load>(load)()).first->second;
		}
		const std::map<Key, Value>& Entries() const { return entries; }

	private:
		std::set<Key> demand;
		std::map<Key, Value> entries;
	};
}
