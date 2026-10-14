#pragma once

#include <cstdint>

namespace StreamingTextures
{
	/** Exact logical inventory totals; callers serialize record mutations and accounting. */
	struct InventoryStatusCounters
	{
		std::uint64_t records = 0, fullLogicalBytes = 0, residentLogicalBytes = 0;
		std::uint64_t protectedRecords = 0, reducedTextures = 0, desiredShrinks = 0, desiredRestores = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::uint64_t terrainRecords = 0, terrainReducedTextures = 0, terrainDesiredShrinks = 0;
#endif

		/** Describe one initialized record with a valid resident mip index. */
		template <class Record>
		[[nodiscard]] static InventoryStatusCounters From(const Record& record) noexcept
		{
			InventoryStatusCounters result;
			result.records = 1;
			result.fullLogicalBytes = record.bytes[0];
			result.residentLogicalBytes = record.bytes[record.drop];
			result.protectedRecords = record.protectedConsumer;
			result.reducedTextures = record.drop != 0;
			result.desiredShrinks = record.desired > record.drop;
			result.desiredRestores = record.desired < record.drop;
#ifdef DEVBENCH_BRIDGE_ENABLED
			result.terrainRecords = record.terrainConsumer;
			result.terrainReducedTextures = record.terrainConsumer && record.drop != 0;
			result.terrainDesiredShrinks = record.terrainConsumer && record.desired > record.drop;
#endif
			return result;
		}

		/** Add contributions only after their records have joined the inventory. */
		void Add(const InventoryStatusCounters& contribution) noexcept
		{
			records += contribution.records;
			fullLogicalBytes += contribution.fullLogicalBytes;
			residentLogicalBytes += contribution.residentLogicalBytes;
			protectedRecords += contribution.protectedRecords;
			reducedTextures += contribution.reducedTextures;
			desiredShrinks += contribution.desiredShrinks;
			desiredRestores += contribution.desiredRestores;
#ifdef DEVBENCH_BRIDGE_ENABLED
			terrainRecords += contribution.terrainRecords;
			terrainReducedTextures += contribution.terrainReducedTextures;
			terrainDesiredShrinks += contribution.terrainDesiredShrinks;
#endif
		}

		/** Remove an already accounted contribution before its record leaves the inventory. */
		void Remove(const InventoryStatusCounters& contribution) noexcept
		{
			records -= contribution.records;
			fullLogicalBytes -= contribution.fullLogicalBytes;
			residentLogicalBytes -= contribution.residentLogicalBytes;
			protectedRecords -= contribution.protectedRecords;
			reducedTextures -= contribution.reducedTextures;
			desiredShrinks -= contribution.desiredShrinks;
			desiredRestores -= contribution.desiredRestores;
#ifdef DEVBENCH_BRIDGE_ENABLED
			terrainRecords -= contribution.terrainRecords;
			terrainReducedTextures -= contribution.terrainReducedTextures;
			terrainDesiredShrinks -= contribution.terrainDesiredShrinks;
#endif
		}

		/** Reconcile one record mutation, including partially completed exception paths. */
		void Replace(const InventoryStatusCounters& before, const InventoryStatusCounters& after) noexcept
		{
			Remove(before);
			Add(after);
		}
	};
}
