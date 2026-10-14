#pragma once

#include "Features/TextureStreaming/StatusAccounting.h"

namespace TextureStreamingStatusChecks
{
	template <class Record>
	StreamingTextures::InventoryStatusCounters Contribution(const Record& record)
	{
		StreamingTextures::InventoryStatusCounters result;
		result.records = 1;
		result.fullLogicalBytes = record.bytes[0];
		result.residentLogicalBytes = record.bytes[record.drop];
		result.protectedRecords = record.protectedConsumer ? 1 : 0;
		result.reducedTextures = record.drop != 0 ? 1 : 0;
		result.desiredShrinks = record.desired > record.drop ? 1 : 0;
		result.desiredRestores = record.desired < record.drop ? 1 : 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		result.terrainRecords = record.terrainConsumer ? 1 : 0;
		result.terrainReducedTextures = record.terrainConsumer && record.drop != 0 ? 1 : 0;
		result.terrainDesiredShrinks = record.terrainConsumer && record.desired > record.drop ? 1 : 0;
#endif
		return result;
	}

	inline bool Equal(const StreamingTextures::InventoryStatusCounters& actual,
		const StreamingTextures::InventoryStatusCounters& expected)
	{
		return actual.records == expected.records && actual.fullLogicalBytes == expected.fullLogicalBytes &&
		       actual.residentLogicalBytes == expected.residentLogicalBytes && actual.protectedRecords == expected.protectedRecords &&
		       actual.reducedTextures == expected.reducedTextures && actual.desiredShrinks == expected.desiredShrinks &&
		       actual.desiredRestores == expected.desiredRestores
#ifdef DEVBENCH_BRIDGE_ENABLED
		       && actual.terrainRecords == expected.terrainRecords && actual.terrainReducedTextures == expected.terrainReducedTextures &&
		       actual.terrainDesiredShrinks == expected.terrainDesiredShrinks
#endif
			;
	}

	template <class Records>
	StreamingTextures::InventoryStatusCounters Aggregate(const Records& records)
	{
		StreamingTextures::InventoryStatusCounters result;
		for (const auto& [id, record] : records) {
			(void)id;
			result.records += 1;
			result.fullLogicalBytes += record.bytes[0];
			result.residentLogicalBytes += record.bytes[record.drop];
			result.protectedRecords += record.protectedConsumer ? 1 : 0;
			result.reducedTextures += record.drop != 0 ? 1 : 0;
			result.desiredShrinks += record.desired > record.drop ? 1 : 0;
			result.desiredRestores += record.desired < record.drop ? 1 : 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
			result.terrainRecords += record.terrainConsumer ? 1 : 0;
			result.terrainReducedTextures += record.terrainConsumer && record.drop != 0 ? 1 : 0;
			result.terrainDesiredShrinks += record.terrainConsumer && record.desired > record.drop ? 1 : 0;
#endif
		}
		return result;
	}
}
