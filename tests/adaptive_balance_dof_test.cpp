#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "Features/AdaptiveBalanceDepthOfField.h"
#include "Utils/Finite.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace REX
{
	template <class Enum, class Storage>
	struct EnumSet
	{
		Storage value{};
		EnumSet() = default;
		EnumSet(Enum a_value) : value(static_cast<Storage>(a_value)) {}
		Storage underlying() const { return value; }
	};
}
#include "adaptive_balance_dof_engine_types.h"

namespace REL
{
	std::array<float, 7> autoFocusValues{};
	bool missingAutoFocusValues = false;
	struct VariantOffset
	{
		std::size_t index;
		VariantOffset(std::uintptr_t, std::uintptr_t, std::uintptr_t)
		{
			static std::size_t nextIndex = 0;
			index = nextIndex++;
			assert(index < autoFocusValues.size());
		}
		std::uintptr_t address() const
		{
			return missingAutoFocusValues ? 0 : reinterpret_cast<std::uintptr_t>(&autoFocusValues[index]);
		}
	};
}
namespace RE
{
	struct ImageSpaceModData
	{
		enum
		{
			kDOFStrength,
			kDOFDistance,
			kDOFRange,
			kDOFMode
		};
		std::array<float, 4> data{};
	};
	struct ImageSpaceManager
	{
		struct
		{
			ImageSpaceModData modData;
		} data;
		struct BaseData
		{
			ImageSpaceBaseData::DepthOfField depthOfField{};
		};
		BaseData* currentBaseData = nullptr;
		BaseData* underwaterBaseData = nullptr;
		static inline ImageSpaceManager* instance = nullptr;
		static ImageSpaceManager* GetSingleton() { return instance; }
	};
	struct PlayerCharacter
	{
		bool hasCell = true;
		static PlayerCharacter* GetSingleton()
		{
			static PlayerCharacter player;
			return &player;
		}
	};
}
#define GET_INSTANCE_MEMBER(a_member, a_source) auto& a_member = a_source->a_member
const void* GetCurrentPlayerCell(RE::PlayerCharacter* a_player) { return a_player->hasCell ? a_player : nullptr; }

struct AdaptiveBrightness
{
	struct
	{
		bool enabled = true;
		AdaptiveBalanceDepthOfField::Settings depthOfField;
	} settings;
	bool loaded = true;
	bool performanceCostMeasurementEnabled = true;
	bool IsRuntimeAvailable() const;
	bool IsRuntimeEnabled() const;
};
namespace globals
{
	struct State
	{
		bool menuOpen = false;
		bool IsMainOrLoadingMenuOpen() const { return menuOpen; }
	} stateValue;
	auto* state = &stateValue;
	namespace features
	{
		AdaptiveBrightness adaptiveBrightness;
	}
}
#include "adaptive_balance_dof_under_test.h"

namespace Dof = AdaptiveBalanceDepthOfField;
using json = nlohmann::json;

void CheckRuntimeGates()
{
	auto& balance = globals::features::adaptiveBrightness;
	for (unsigned gates = 0; gates < 128; ++gates) {
		balance.loaded = (gates & 1) != 0;
		balance.settings.enabled = (gates & 2) != 0;
		balance.performanceCostMeasurementEnabled = (gates & 4) != 0;
		globals::stateValue.menuOpen = (gates & 8) != 0;
		RE::PlayerCharacter::GetSingleton()->hasCell = (gates & 16) != 0;
		balance.settings.depthOfField.enabled = (gates & 32) != 0;
		balance.settings.depthOfField.fixUnderwaterFogDofBlur = (gates & 64) != 0;
		const json before = balance.settings.depthOfField;
		const bool masterEnabled = (gates & 31) == 23;
		assert(Dof::IsRuntimeEnabled() == (masterEnabled && ((gates & 32) != 0)));
		assert(Dof::IsCorrectionEnabled() == (masterEnabled && ((gates & 64) != 0)));
		assert(json(balance.settings.depthOfField) == before);
	}
	balance = {};
	globals::stateValue.menuOpen = false;
	RE::PlayerCharacter::GetSingleton()->hasCell = true;
	balance.settings.depthOfField.fixUnderwaterFogDofBlur = true;
	assert(!Dof::IsRuntimeEnabled() && Dof::IsCorrectionEnabled());
	balance.settings.depthOfField.enabled = true;
	balance.settings.depthOfField.fixUnderwaterFogDofBlur = false;
	assert(Dof::IsRuntimeEnabled() && !Dof::IsCorrectionEnabled());
	balance = {};
}

Dof::DepthOfFieldSettings MakeManualValues()
{
	Dof::DepthOfFieldSettings result;
	result.strength = 0.8f;
	result.distance = 1500.0f;
	result.range = 250.0f;
	result.mode = 1;
	result.excludeSky = true;
	result.autoFocus = true;
	result.blurRadius = 7;
	result.autoFocusSettings = { 15, 25, 35, 45, 0.2f, 0.3f, 0.4f };
	return result;
}

void CheckSerialization()
{
	const Dof::Settings defaults;
	assert(!defaults.enabled && !defaults.fixUnderwaterFogDofBlur);
	assert(json(json::object().get<Dof::Settings>()) == json(defaults));
	assert(json(json(nullptr).get<Dof::Settings>()) == json(defaults));
	Dof::Settings settings;
	settings.enabled = true;
	settings.fixUnderwaterFogDofBlur = true;
	settings.sceneDof = { true, MakeManualValues(), {} };
	settings.underwaterDof = { true, {}, MakeManualValues() };
	const json saved = settings;
	assert(json(saved.get<Dof::Settings>()) == saved);
	const auto partial = json{ { "sceneDof", { { "locked", true }, { "values", { { "strength", 0.5 } } } } } }.get<Dof::Settings>();
	assert(partial.sceneDof.locked && partial.sceneDof.values.strength == 0.5f);
	assert(!partial.enabled && !partial.fixUnderwaterFogDofBlur && !partial.underwaterDof.locked);
	assert(partial.sceneDof.values.autoFocusSettings.blurMultiplier == 1.0f);
	auto invalidStrength = saved;
	invalidStrength["sceneDof"]["values"]["strength"] = "0.5";
	for (const auto& invalid : std::vector<json>{ json::array(), json{ { "enabled", "true" } }, invalidStrength }) {
		bool rejected = false;
		try {
			static_cast<void>(invalid.get<Dof::Settings>());
		} catch (const json::exception&) {
			rejected = true;
		}
		assert(rejected);
	}
}

void CheckSanitization()
{
	Dof::DepthOfFieldSettings values;
	values.strength = 3.0f;
	values.distance = -1.0f;
	values.range = 60000.0f;
	values.mode = std::numeric_limits<uint32_t>::max();
	values.blurRadius = std::numeric_limits<uint32_t>::max();
	Dof::SanitizeDepthOfFieldSettings(values);
	assert(values.strength == 1.0f && values.distance == 0.0f && values.range == 50000.0f);
	assert(values.mode == 3 && values.blurRadius == 7);
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		values.strength = values.distance = values.range = invalid;
		for (const auto& definition : kDofAutoFocusSettingDefinitions)
			values.autoFocusSettings.*definition.member = invalid;
		Dof::SanitizeDepthOfFieldSettings(values);
		assert(values.strength == 0 && values.distance == 0 && values.range == 0);
		assert(json(values.autoFocusSettings) == json(Dof::DepthOfFieldAutoFocusSettings{}));
	}
	for (const auto& definition : kDofAutoFocusSettingDefinitions) {
		values.autoFocusSettings.*definition.member = definition.minValue - 1;
		Dof::SanitizeDepthOfFieldSettings(values);
		assert(values.autoFocusSettings.*definition.member == definition.minValue);
		values.autoFocusSettings.*definition.member = definition.maxValue + 1;
		Dof::SanitizeDepthOfFieldSettings(values);
		assert(values.autoFocusSettings.*definition.member == definition.maxValue);
	}
	Dof::Settings settings;
	for (auto* target : { &settings.sceneDof.values, &settings.sceneDof.baseline, &settings.underwaterDof.values, &settings.underwaterDof.baseline })
		target->strength = -1;
	Dof::SanitizeSettings(settings);
	for (auto* target : { &settings.sceneDof.values, &settings.sceneDof.baseline, &settings.underwaterDof.values, &settings.underwaterDof.baseline })
		assert(target->strength == 0);
	for (const float invalid : { -1.0f, 4294967296.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
		const auto decoded = DecodeDofPackedValue(0.5f, 100.0f, 200.0f, invalid);
		assert(decoded.mode == 0 && decoded.blurRadius == 0 && !decoded.excludeSky && !decoded.autoFocus);
	}
	for (uint32_t mode = 0; mode < 4; ++mode) {
		for (uint32_t radius = 0; radius < 8; ++radius) {
			for (unsigned flags = 0; flags < 4; ++flags) {
				values = MakeManualValues();
				values.mode = mode;
				values.blurRadius = radius;
				values.autoFocus = (flags & 1) != 0;
				values.excludeSky = (flags & 2) != 0;
				const auto decoded = DecodeDofPackedValue(values.strength, values.distance, values.range, static_cast<float>(EncodeDofPackedValue(values)));
				assert(decoded.mode == mode && decoded.blurRadius == radius);
				assert(decoded.autoFocus == values.autoFocus && decoded.excludeSky == values.excludeSky);
			}
		}
	}
}

bool SameDepth(const RE::ImageSpaceBaseData::DepthOfField& left, const RE::ImageSpaceBaseData::DepthOfField& right)
{
	return left.strength == right.strength && left.distance == right.distance && left.range == right.range &&
	       left.flags == right.flags && left.skyBlurRadius.underlying() == right.skyBlurRadius.underlying();
}

void CheckScopedRestoration()
{
	auto& balance = globals::features::adaptiveBrightness;
	balance = {};
	auto& settings = balance.settings.depthOfField;
	settings.enabled = true;
	settings.sceneDof = { true, MakeManualValues(), {} };
	settings.underwaterDof = { true, MakeManualValues(), {} };
	RE::ImageSpaceManager manager;
	RE::ImageSpaceManager::instance = &manager;
	RE::ImageSpaceManager::BaseData scene;
	RE::ImageSpaceManager::BaseData underwater;
	manager.currentBaseData = &scene;
	manager.underwaterBaseData = &underwater;
	manager.data.modData.data = { 0.1f, 100, 200, 18 };
	underwater.depthOfField = { 0.15f, 300, 400, 0xFF, RE::ImageSpaceBaseData::DepthOfField::SkyBlurRadius::kRadius2 };
	REL::autoFocusValues = { 31, 32, 33, 34, 0.35f, 0.36f, 0.37f };
	const auto sceneBefore = manager.data.modData.data;
	const auto underwaterBefore = underwater.depthOfField;
	const auto autofocusBefore = REL::autoFocusValues;
	const auto checkRestored = [&] {
		assert(manager.data.modData.data == sceneBefore);
		assert(SameDepth(underwater.depthOfField, underwaterBefore));
		assert(REL::autoFocusValues == autofocusBefore);
	};
	for (bool failInsideHook : { false, true }) {
		try {
			DepthOfFieldOverrideScope scope;
			assert(manager.data.modData.data[RE::ImageSpaceModData::kDOFStrength] == settings.sceneDof.values.strength);
			assert(manager.data.modData.data[RE::ImageSpaceModData::kDOFMode] == static_cast<float>(EncodeDofPackedValue(settings.sceneDof.values)));
			assert(underwater.depthOfField.strength == settings.underwaterDof.values.strength && underwater.depthOfField.flags == 0);
			assert(underwater.depthOfField.skyBlurRadius.underlying() == static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius7));
			for (std::size_t i = 0; i < kDofAutoFocusSettingDefinitions.size(); ++i)
				assert(REL::autoFocusValues[i] == settings.sceneDof.values.autoFocusSettings.*kDofAutoFocusSettingDefinitions[i].member);
			const auto outerScene = manager.data.modData.data;
			{
				settings.sceneDof.values.strength = 0.6f;
				DepthOfFieldOverrideScope nested;
				assert(manager.data.modData.data[RE::ImageSpaceModData::kDOFStrength] == 0.6f);
			}
			assert(manager.data.modData.data == outerScene);
			settings.sceneDof.values.strength = 0.8f;
			if (failInsideHook)
				throw std::runtime_error("injected image-space hook failure");
		} catch (const std::runtime_error&) {
			assert(failInsideHook);
		}
		checkRestored();
	}
	manager.currentBaseData = &underwater;
	{
		DepthOfFieldOverrideScope scope;
		assert(REL::autoFocusValues == autofocusBefore);
	}
	checkRestored();
	manager.currentBaseData = &scene;
	manager.underwaterBaseData = nullptr;
	{
		DepthOfFieldOverrideScope scope;
		assert(SameDepth(underwater.depthOfField, underwaterBefore));
	}
	checkRestored();
	REL::missingAutoFocusValues = true;
	{
		DepthOfFieldOverrideScope scope;
		assert(REL::autoFocusValues == autofocusBefore);
	}
	REL::missingAutoFocusValues = false;
	checkRestored();
	for (unsigned bypass = 0; bypass < 4; ++bypass) {
		balance.settings.enabled = bypass != 0;
		settings.enabled = bypass != 1;
		settings.sceneDof.locked = settings.underwaterDof.locked = bypass != 2;
		RE::ImageSpaceManager::instance = bypass == 3 ? nullptr : &manager;
		DepthOfFieldOverrideScope scope;
		checkRestored();
	}
	RE::ImageSpaceManager::instance = nullptr;
	balance = {};
}

int main()
{
	CheckRuntimeGates();
	CheckSerialization();
	CheckSanitization();
	CheckScopedRestoration();
	std::cout << "Adaptive Balance DOF gates, serialization, bounds and hook state restoration passed\n";
}
