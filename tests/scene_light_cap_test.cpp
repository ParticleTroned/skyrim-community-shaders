#include "Features/LightLimitFix/SceneLightSnapshot.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
	using uint = unsigned;

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Scene light cap assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	struct Vector3
	{
		float x = 0;
		float y = 0;
		float z = 0;
		bool operator==(const Vector3&) const = default;
	};

	enum class LightFlags : std::uint32_t
	{
		PortalStrict = 1u << 0,
		Shadow = 1u << 1,
		Simple = 1u << 2,
		AffectWater = 1u << 3,
		Initialised = 1u << 8,
		Disabled = 1u << 9,
		InverseSquare = 1u << 10
	};

	struct Flags
	{
		Flags& operator=(LightFlags a_value)
		{
			value = static_cast<std::uint32_t>(a_value);
			return *this;
		}
		void set(LightFlags a_flag) { value |= static_cast<std::uint32_t>(a_flag); }
		void reset(LightFlags a_flag) { value &= ~static_cast<std::uint32_t>(a_flag); }
		std::uint32_t value = 0;
	};

	struct RoomFlags
	{
		void SetBit(std::uint8_t a_index, bool a_value)
		{
			Require(a_index < 128 && a_value);
			bits[a_index / 32] |= 1u << (a_index % 32);
		}
		std::array<std::uint32_t, 4> bits{};
	};

	struct LightData
	{
		Vector3 color;
		float fade = 1;
		float radius = 0;
		float invRadius = 0;
		float fadeZone = 0;
		float sizeBias = 0;
		std::array<Vector3, 2> positionWS;
		RoomFlags roomFlags;
		Flags lightFlags;
		std::uint32_t shadowMaskIndex = 0;
	};

	struct Calls
	{
		unsigned processing = 0;
		unsigned flags = 0;
		unsigned scaling = 0;
		unsigned position = 0;
		unsigned mask = 0;
		unsigned shadowType = 0;
	} calls;
}

namespace RE
{
	template <class T>
	using NiPointer = std::shared_ptr<T>;

	struct NiNode
	{};
	struct Portal
	{
		NiPointer<NiNode> portalSharedNode;
	};
	struct NiLight
	{
		struct RuntimeData
		{
			struct Color
			{
				float red = 0;
				float green = 0;
				float blue = 0;
			};
			Color diffuse;
			Color ambient;
			Vector3 radius{ 5, 0, 0 };
			float fade = 1;
		} runtime;
		struct World
		{
			Vector3 translate;
		} world;
		RuntimeData& GetLightRuntimeData() { return runtime; }
		unsigned nativeWrites = 0;
		bool hidden = false;
	};
	struct BSLight
	{
		virtual ~BSLight() = default;
		virtual bool IsShadowLight()
		{
			++calls.shadowType;
			return false;
		}
		NiPointer<NiLight> light;
		std::vector<NiNode*> rooms;
		std::vector<NiPointer<Portal>> portals;
		float lodDimmer = 0.5f;
		float jsonScale = 2;
		bool global = true;
		bool affectWater = false;
	};
	struct BSShadowLight : BSLight
	{
		bool IsShadowLight() override
		{
			++calls.shadowType;
			return true;
		}
		std::uint32_t ReadMaskIndex()
		{
			++calls.mask;
			return maskIndex;
		}
		std::uint32_t maskIndex = 7;
	};
	struct ShadowSceneNode
	{
		std::vector<NiPointer<BSLight>> activeLights;
		std::vector<NiPointer<BSShadowLight>> activeShadowLights;
		std::vector<NiPointer<BSLight>> lightQueueAdd;
		std::vector<NiPointer<BSLight>> lightQueueRemove;
		std::vector<NiPointer<BSLight>> unk190;
		ShadowSceneNode& GetRuntimeData() { return *this; }
	};
}

namespace globals::game
{
	bool isVR = false;
	struct Setting
	{
		struct Data
		{
			std::int32_t i = 19;
		} data;
	};
	struct SettingCollection
	{
		Setting* GetSetting(std::string_view a_name)
		{
			Require(a_name == "iMagicLightMaxCount");
			++lookups;
			return available ? &magic : nullptr;
		}
		Setting magic;
		unsigned lookups = 0;
		bool available = true;
	};
	SettingCollection* gameSettingCollection = nullptr;
}

namespace logger
{
	void info(const char*) {}
}

#define MAXINT32 std::numeric_limits<std::int32_t>::max()
#define GET_INSTANCE_MEMBER(name, object) const auto name = object->ReadMaskIndex()
#define CS_PROFILE_CPU_SCOPE(...) static_cast<void>(0)

#include "scene_light_cap_constant_under_test.h"
static_assert(MAX_LIGHTS == 1024);

struct InverseSquareLighting
{
	void ProcessLight(LightData& a_output, RE::BSLight*, RE::NiLight* a_native)
	{
		++calls.processing;
		++a_native->nativeWrites;
		a_output.lightFlags.set(LightFlags::Initialised);
		if (enabled)
			a_output.lightFlags.set(LightFlags::InverseSquare);
		else
			a_output.lightFlags.reset(LightFlags::InverseSquare);
		a_output.radius = a_native->runtime.radius.x;
		a_output.invRadius = a_output.radius > 0 ? 1 / a_output.radius : 0;
		a_output.fade = a_native->runtime.fade;
	}
	bool loaded = true;
	bool enabled = true;
};

struct LightLimitFix
{
	using Snapshot = LightLimitFixDetail::SceneLightSnapshot<RE::NiPointer<RE::BSLight>>;

	static bool IsValidLight(RE::BSLight* a_light) { return a_light && a_light->light && !a_light->light->hidden; }
	static bool IsGlobalLight(RE::BSLight* a_light) { return a_light->global; }
	static void SetEngineLightFlags(LightData& a_output, RE::BSLight* a_light)
	{
		++calls.flags;
		a_output.lightFlags.reset(LightFlags::PortalStrict);
		a_output.lightFlags.reset(LightFlags::Shadow);
		a_output.lightFlags.reset(LightFlags::AffectWater);
		if (a_light->affectWater)
			a_output.lightFlags.set(LightFlags::AffectWater);
	}
	static void ApplyJsonPlacedLightIntensityScale(LightData& a_output, RE::BSLight* a_light, RE::NiLight*, bool, bool)
	{
		++calls.scaling;
		a_output.fade *= a_light->jsonScale;
	}
	static void SetLightPosition(LightData& a_output, Vector3 a_position)
	{
		++calls.position;
		a_output.positionWS = { a_position, Vector3{ a_position.x + 1, a_position.y, a_position.z } };
	}

	void Run(RE::ShadowSceneNode* shadowSceneNode)
	{
		Snapshot retained;
		if (globals::game::isVR)
			retained.RetainScene(*shadowSceneNode);
		const auto* snapshot = &retained;
		const bool isInterior = true;
		lightsData.clear();
		roomNodes.clear();
#include "scene_light_cap_under_test.h"
	}

	void DataLoaded();
	InverseSquareLighting isl;
	std::vector<LightData> lightsData;
	std::unordered_map<RE::NiNode*, std::uint8_t> roomNodes;
};

#include "scene_light_cap_native_limit_under_test.h"

namespace
{
	template <class T = RE::BSLight>
	std::shared_ptr<T> MakeLight(unsigned a_id)
	{
		auto light = std::make_shared<T>();
		light->light = std::make_shared<RE::NiLight>();
		light->light->runtime.diffuse = { static_cast<float>(a_id), 0.25f, 0.5f };
		light->light->runtime.ambient.red = std::bit_cast<float>(static_cast<std::uint32_t>(LightFlags::Simple));
		light->light->world.translate = { static_cast<float>(a_id), 2, 3 };
		light->affectWater = a_id % 2 != 0;
		return light;
	}

	void CheckOutput(const LightData& a_output, unsigned a_id, bool a_shadow, bool a_islLoaded, bool a_islEnabled, bool a_strict)
	{
		Require(a_output.color == Vector3{ static_cast<float>(a_id), 0.25f, 0.5f });
		Require(a_output.fade == 1 && a_output.radius == 5);
		Require(a_output.invRadius == (a_islLoaded ? 0.2f : 0));
		Require(a_output.fadeZone == 0 && a_output.sizeBias == 0);
		Require(a_output.positionWS[0] == Vector3{ static_cast<float>(a_id), 2, 3 });
		Require(a_output.positionWS[1] == Vector3{ static_cast<float>(a_id + 1), 2, 3 });
		auto flags = static_cast<std::uint32_t>(LightFlags::Simple);
		if (a_islLoaded)
			flags |= static_cast<std::uint32_t>(LightFlags::Initialised);
		if (a_islLoaded && a_islEnabled)
			flags |= static_cast<std::uint32_t>(LightFlags::InverseSquare);
		if (a_id % 2)
			flags |= static_cast<std::uint32_t>(LightFlags::AffectWater);
		if (a_shadow)
			flags |= static_cast<std::uint32_t>(LightFlags::Shadow);
		if (a_strict)
			flags |= static_cast<std::uint32_t>(LightFlags::PortalStrict);
		Require(a_output.lightFlags.value == flags);
		Require(a_output.shadowMaskIndex == (a_shadow ? 7u : 0u));
		Require(a_output.roomFlags.bits == std::array<std::uint32_t, 4>{ a_strict ? 1u : 0u, 0, 0, 0 });
	}

	void TestCapAndOrder(bool a_vr, bool a_islLoaded, bool a_islEnabled)
	{
		globals::game::isVR = a_vr;
		calls = {};
		LightLimitFix fix;
		fix.isl.loaded = a_islLoaded;
		fix.isl.enabled = a_islEnabled;
		RE::ShadowSceneNode node;
		RE::NiNode sharedRoom;
		std::vector<RE::NiPointer<RE::BSLight>> valid;
		node.activeLights.push_back(nullptr);
		node.activeLights.push_back(std::make_shared<RE::BSLight>());
		auto hidden = MakeLight(70000);
		hidden->light->hidden = true;
		node.activeLights.push_back(hidden);
		auto dim = MakeLight(70001);
		dim->light->runtime.diffuse = {};
		node.activeLights.push_back(dim);
		auto zero = MakeLight(70002);
		zero->light->runtime.radius.x = 0;
		node.activeLights.push_back(zero);
		for (unsigned id = 1; id <= 750; ++id) {
			auto light = MakeLight(id);
			node.activeLights.push_back(light);
			valid.push_back(light);
		}
		auto inactive = MakeLight<RE::BSShadowLight>(70003);
		inactive->maskIndex = 255;
		node.activeShadowLights.push_back(inactive);
		for (unsigned id = 751; id <= 1064; ++id) {
			auto light = MakeLight<RE::BSShadowLight>(id);
			light->global = false;
			light->rooms = { &sharedRoom };
			node.activeShadowLights.push_back(light);
			valid.push_back(light);
		}
		fix.Run(&node);
		Require(fix.lightsData.size() == MAX_LIGHTS);
		for (unsigned index = 0; index < MAX_LIGHTS; ++index)
			CheckOutput(fix.lightsData[index], index + 1, index >= 750, a_islLoaded, a_islEnabled, index >= 750);
		Require(fix.roomNodes.size() == 1 && fix.roomNodes.at(&sharedRoom) == 0);
		Require(calls.processing == (a_islLoaded ? 1067u : 0u));
		Require(calls.flags == 1027 && calls.scaling == 1027 && calls.position == 1026);
		Require(calls.mask == 275 && calls.shadowType == 1027);
		for (const auto& light : valid)
			Require(light->light->nativeWrites == (a_islLoaded ? 1u : 0u));
		Require(hidden->light->nativeWrites == 0);
		Require(inactive->light->nativeWrites == (a_islLoaded ? 1u : 0u));
	}

	void TestRejectedRoomRegistration(bool a_vr)
	{
		globals::game::isVR = a_vr;
		calls = {};
		LightLimitFix fix;
		RE::ShadowSceneNode node;
		std::array<RE::NiNode, 6> rooms;
		auto dim = MakeLight(1);
		dim->light->runtime.diffuse = {};
		dim->global = false;
		dim->rooms = { &rooms[0], nullptr };
		node.activeLights.push_back(dim);
		auto zero = MakeLight(2);
		zero->light->runtime.radius.x = 0;
		zero->global = false;
		zero->rooms = { &rooms[1] };
		node.activeLights.push_back(zero);
		auto global = MakeLight(3);
		global->rooms = { &rooms[5] };
		node.activeLights.push_back(global);
		auto inactive = MakeLight<RE::BSShadowLight>(4);
		inactive->maskIndex = 255;
		inactive->global = false;
		inactive->rooms = { &rooms[2] };
		node.activeShadowLights.push_back(inactive);
		auto accepted = MakeLight<RE::BSShadowLight>(5);
		accepted->global = false;
		accepted->rooms = { &rooms[3], &rooms[0] };
		auto portal = std::make_shared<RE::Portal>();
		portal->portalSharedNode = std::shared_ptr<RE::NiNode>(&rooms[4], [](auto*) {});
		accepted->portals = { nullptr, std::make_shared<RE::Portal>(), portal };
		node.activeShadowLights.push_back(accepted);
		fix.Run(&node);
		Require(fix.lightsData.size() == 2 && fix.roomNodes.size() == 5);
		for (unsigned index = 0; index < 5; ++index)
			Require(fix.roomNodes.at(&rooms[index]) == index);
		Require(!fix.roomNodes.contains(&rooms[5]));
		Require(fix.lightsData[0].roomFlags.bits == std::array<std::uint32_t, 4>{});
		Require(fix.lightsData[1].roomFlags.bits == std::array<std::uint32_t, 4>{ 0b11001, 0, 0, 0 });
	}

	void TestOverflowBookkeeping(bool a_vr)
	{
		globals::game::isVR = a_vr;
		calls = {};
		LightLimitFix fix;
		fix.isl.enabled = false;
		RE::ShadowSceneNode node;
		for (unsigned id = 1; id <= MAX_LIGHTS; ++id)
			node.activeLights.push_back(MakeLight(id));
		std::array<RE::NiNode, 132> rooms;
		std::vector<RE::NiPointer<RE::BSShadowLight>> overflow;
		for (unsigned index = 0; index < 130; ++index) {
			auto light = MakeLight<RE::BSShadowLight>(2000 + index);
			light->global = false;
			light->rooms = { &rooms[index] };
			if (index == 0) {
				light->maskIndex = 255;
				auto portal = std::make_shared<RE::Portal>();
				portal->portalSharedNode = std::shared_ptr<RE::NiNode>(&rooms[1], [](auto*) {});
				light->portals = { nullptr, std::make_shared<RE::Portal>(), portal };
			}
			if (index == 64)
				light->rooms.push_back(&rooms[0]);
			if (index == 1)
				light->light->runtime.diffuse = {};
			if (index == 2)
				light->light->runtime.radius.x = 0;
			node.activeShadowLights.push_back(light);
			overflow.push_back(light);
		}
		auto global = MakeLight<RE::BSShadowLight>(3000);
		global->rooms = { &rooms[130] };
		node.activeShadowLights.push_back(global);
		auto hidden = MakeLight<RE::BSShadowLight>(3001);
		hidden->light->hidden = true;
		hidden->global = false;
		hidden->rooms = { &rooms[131] };
		node.activeShadowLights.push_back(hidden);
		node.activeShadowLights.push_back(nullptr);
		fix.Run(&node);
		Require(fix.lightsData.size() == MAX_LIGHTS && fix.roomNodes.size() == 128);
		for (unsigned index = 0; index < 128; ++index)
			Require(fix.roomNodes.at(&rooms[index]) == index);
		for (unsigned index = 128; index < rooms.size(); ++index)
			Require(!fix.roomNodes.contains(&rooms[index]));
		Require(calls.processing == MAX_LIGHTS + 131 && calls.flags == MAX_LIGHTS);
		Require(calls.scaling == MAX_LIGHTS && calls.position == MAX_LIGHTS);
		Require(calls.mask == 0 && calls.shadowType == MAX_LIGHTS);
		for (const auto& light : overflow)
			Require(light->light->nativeWrites == 1);
		Require(global->light->nativeWrites == 1 && hidden->light->nativeWrites == 0);
		for (unsigned index = 0; index < MAX_LIGHTS; ++index)
			CheckOutput(fix.lightsData[index], index + 1, false, true, false, false);
	}

	void TestNativeMagicLimit()
	{
		LightLimitFix fix;
		globals::game::SettingCollection settings;
		globals::game::gameSettingCollection = nullptr;
		fix.DataLoaded();
		Require(settings.lookups == 0 && settings.magic.data.i == 19);
		globals::game::gameSettingCollection = &settings;
		settings.available = false;
		fix.DataLoaded();
		Require(settings.lookups == 1 && settings.magic.data.i == 19);
		settings.available = true;
		fix.DataLoaded();
		Require(settings.lookups == 2 && settings.magic.data.i == std::numeric_limits<std::int32_t>::max());
		globals::game::gameSettingCollection = nullptr;
	}
}

int main()
{
	for (const bool vr : { false, true }) {
		TestCapAndOrder(vr, false, false);
		TestCapAndOrder(vr, true, false);
		TestCapAndOrder(vr, true, true);
		TestRejectedRoomRegistration(vr);
		TestOverflowBookkeeping(vr);
	}
	TestNativeMagicLimit();
	std::cout << "Scene output cap, source order, native writes, room bookkeeping and native magic limit passed\n";
}
