#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <unordered_map>
#include <vector>

namespace
{
	std::size_t rttiReads = 0;

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Room ancestor assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}
}

namespace RE
{
	struct NiRTTI
	{
		const NiRTTI* base = nullptr;
	};
	inline const NiRTTI ordinaryRtti;
	inline const NiRTTI geometryRtti;
	inline const NiRTTI roomRtti;
	inline const NiRTTI portalRtti;
	inline const NiRTTI derivedRoomRtti{ &roomRtti };
	inline const NiRTTI* NiRTTI_BSMultiBoundRoom = &roomRtti;
	inline const NiRTTI* NiRTTI_BSPortalSharedNode = &portalRtti;

	struct NiNode;
	struct NiAVObject
	{
		explicit NiAVObject(const NiRTTI* a_rtti = &ordinaryRtti) : rtti(a_rtti) {}
		const NiRTTI* GetRTTI() const
		{
			++rttiReads;
			++reads;
			return rtti;
		}
		NiNode* parent = nullptr;
		const NiRTTI* rtti;
		mutable std::size_t reads = 0;
	};
	struct NiNode : NiAVObject
	{
		using NiAVObject::NiAVObject;
	};
	struct BSGeometry : NiAVObject
	{
		BSGeometry() : NiAVObject(&geometryRtti) {}
	};
	struct BSRenderPass
	{
		BSGeometry* geometry = nullptr;
	};
}

namespace REL
{
	template <class T>
	struct Relocation
	{
		explicit Relocation(T a_value) : value(a_value) {}
		T get() const { return value; }
		T value;
	};
}

namespace
{
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	};
	namespace globals
	{
		ShaderCache shaderStorage;
		ShaderCache* shaderCache = &shaderStorage;
	}
	struct LightLimitFix
	{
		struct StrictLightDataCB
		{
			unsigned NumStrictLights = 0;
			int RoomIndex = -1;
			unsigned ShadowBitMask = 0;
			std::array<std::uint32_t, 15> inactivePayload{};
			bool operator==(const StrictLightDataCB&) const = default;
		};
		StrictLightDataCB strictLightDataTemp;
		std::unordered_map<RE::NiNode*, std::uint8_t> roomNodes;
		void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass);
	};

#include "room_ancestor_clear_under_test.h"

#include "room_ancestor_lookup_under_test.h"

	LightLimitFix::StrictLightDataCB SeedPayload()
	{
		LightLimitFix::StrictLightDataCB result;
		result.NumStrictLights = 15;
		result.RoomIndex = 73;
		result.ShadowBitMask = 0xffff;
		result.inactivePayload.fill(0xdecafbad);
		return result;
	}

	void CheckSetup(LightLimitFix& a_feature, RE::BSRenderPass* a_pass, int a_expectedRoom, std::size_t a_expectedReads)
	{
		const auto seeded = SeedPayload();
		a_feature.strictLightDataTemp = seeded;
		rttiReads = 0;
		a_feature.BSLightingShader_SetupGeometry_Before(a_pass);
		Require(a_feature.strictLightDataTemp.RoomIndex == a_expectedRoom);
		Require(a_feature.strictLightDataTemp.NumStrictLights == 0);
		Require(a_feature.strictLightDataTemp.ShadowBitMask == 0);
		Require(a_feature.strictLightDataTemp.inactivePayload == seeded.inactivePayload);
		Require(rttiReads == a_expectedReads);
	}

	void TestExactNearestType()
	{
		RE::NiNode room{ &RE::roomRtti };
		RE::NiNode portal{ &RE::portalRtti };
		RE::NiNode ordinary;
		RE::NiNode derived{ &RE::derivedRoomRtti };
		ordinary.parent = &room;
		room.parent = &portal;
		derived.parent = &portal;
		rttiReads = 0;
		Require(GetParentRoomNode(nullptr) == nullptr && rttiReads == 0);
		Require(GetParentRoomNode(&room) == &room && rttiReads == 1);
		rttiReads = 0;
		Require(GetParentRoomNode(&portal) == &portal && rttiReads == 1);
		rttiReads = 0;
		Require(GetParentRoomNode(&ordinary) == &room && rttiReads == 2);
		rttiReads = 0;
		Require(GetParentRoomNode(&derived) == &portal && rttiReads == 2);
		ordinary.parent = nullptr;
		rttiReads = 0;
		Require(GetParentRoomNode(&ordinary) == nullptr && rttiReads == 1);
	}

	void TestRoutingAndNearestIndex()
	{
		LightLimitFix feature;
		RE::NiNode room{ &RE::roomRtti };
		RE::NiNode portal{ &RE::portalRtti };
		RE::BSGeometry geometry;
		RE::BSRenderPass pass{ &geometry };
		geometry.parent = &room;
		room.parent = &portal;
		feature.roomNodes.emplace(&room, std::uint8_t{ 3 });
		feature.roomNodes.emplace(&portal, std::uint8_t{ 7 });
		CheckSetup(feature, &pass, 3, 1);
		Require(geometry.reads == 0);
		Require(portal.reads == 0);

		feature.roomNodes.erase(&room);
		CheckSetup(feature, &pass, -1, 1);
		Require(portal.reads == 0);
		feature.roomNodes.emplace(&room, std::uint8_t{ 127 });
		CheckSetup(feature, &pass, 127, 1);
		geometry.parent = &portal;
		CheckSetup(feature, &pass, 7, 1);
		Require(geometry.reads == 0);

		CheckSetup(feature, nullptr, -1, 0);
		RE::BSRenderPass missingGeometry;
		CheckSetup(feature, &missingGeometry, -1, 0);
		geometry.parent = nullptr;
		CheckSetup(feature, &pass, -1, 0);
		geometry.parent = &room;
		feature.roomNodes.clear();
		CheckSetup(feature, &pass, -1, 0);
		Require(geometry.reads == 0);

		feature.roomNodes.emplace(&room, std::uint8_t{ 9 });
		feature.strictLightDataTemp = SeedPayload();
		const auto before = feature.strictLightDataTemp;
		globals::shaderStorage.enabled = false;
		rttiReads = 0;
		feature.BSLightingShader_SetupGeometry_Before(&pass);
		Require(feature.strictLightDataTemp == before && rttiReads == 0);
		feature.BSLightingShader_SetupGeometry_Before(nullptr);
		Require(feature.strictLightDataTemp == before && rttiReads == 0);
		globals::shaderStorage.enabled = true;
	}

	void TestLiveTopologyAndRoomTable()
	{
		LightLimitFix feature;
		RE::NiNode firstRoom{ &RE::roomRtti };
		RE::NiNode secondRoom{ &RE::roomRtti };
		RE::NiNode middle;
		RE::NiNode upper;
		RE::BSGeometry geometry;
		RE::BSRenderPass pass{ &geometry };
		feature.roomNodes.emplace(&firstRoom, std::uint8_t{ 11 });
		feature.roomNodes.emplace(&secondRoom, std::uint8_t{ 23 });
		geometry.parent = &firstRoom;
		CheckSetup(feature, &pass, 11, 1);
		geometry.parent = &secondRoom;
		CheckSetup(feature, &pass, 23, 1);

		geometry.parent = &middle;
		middle.parent = &upper;
		upper.parent = &firstRoom;
		CheckSetup(feature, &pass, 11, 3);
		upper.parent = &secondRoom;
		CheckSetup(feature, &pass, 23, 3);
		middle.parent = &firstRoom;
		CheckSetup(feature, &pass, 11, 2);
		middle.parent = &upper;
		upper.parent = nullptr;
		CheckSetup(feature, &pass, -1, 2);
		upper.parent = &firstRoom;
		CheckSetup(feature, &pass, 11, 3);

		feature.roomNodes[&firstRoom] = std::uint8_t{ 127 };
		CheckSetup(feature, &pass, 127, 3);
		feature.roomNodes.erase(&firstRoom);
		CheckSetup(feature, &pass, -1, 3);
		feature.roomNodes.clear();
		feature.roomNodes.emplace(&firstRoom, std::uint8_t{ 4 });
		CheckSetup(feature, &pass, 4, 3);
		Require(geometry.reads == 0);
	}

	void TestDeepValidTree()
	{
		constexpr std::size_t depth = 32768;
		std::vector<RE::NiNode> ancestors(depth);
		RE::NiNode room{ &RE::roomRtti };
		for (std::size_t i = 0; i + 1 < depth; ++i)
			ancestors[i].parent = &ancestors[i + 1];
		ancestors.back().parent = &room;
		RE::BSGeometry geometry;
		geometry.parent = &ancestors.front();
		RE::BSRenderPass pass{ &geometry };
		LightLimitFix feature;
		feature.roomNodes.emplace(&room, std::uint8_t{ 12 });
		CheckSetup(feature, &pass, 12, depth + 1);
		Require(geometry.reads == 0);
		ancestors.back().parent = nullptr;
		CheckSetup(feature, &pass, -1, depth);
	}
}

int main()
{
	TestExactNearestType();
	TestRoutingAndNearestIndex();
	TestLiveTopologyAndRoomTable();
	TestDeepValidTree();
}
