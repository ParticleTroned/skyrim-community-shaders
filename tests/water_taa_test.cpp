#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace RE
{
	struct Setting
	{
		struct
		{
			bool b = true;
		} data;
	};
	Setting waterSetting;
	bool settingAvailable = false;
	unsigned settingLookups = 0;
	Setting* GetINISetting(const char* name)
	{
		++settingLookups;
		return settingAvailable && std::string_view(name) == "bTAAWater:Display" ? &waterSetting : nullptr;
	}

	struct TemporalAA
	{
		std::byte prefix[0x18]{};
		bool taaEnabled = false;
		std::byte padding[0x1f]{};
		bool waterTAAEnabled = false;
	};
	static_assert(offsetof(TemporalAA, waterTAAEnabled) == 0x38);
	struct ImageSpaceManager
	{
		TemporalAA* BSImagespaceShaderISTemporalAA = nullptr;
		static ImageSpaceManager* instance;
		static unsigned lookups;
		static ImageSpaceManager* GetSingleton()
		{
			++lookups;
			return instance;
		}
	};
	ImageSpaceManager* ImageSpaceManager::instance = nullptr;
	unsigned ImageSpaceManager::lookups = 0;
	struct RENDER_TARGETS
	{
		enum
		{
			kTEMPORAL_AA_WATER_1 = 86,
			kTEMPORAL_AA_WATER_2 = 87
		};
	};
	struct RenderTargetData
	{
		void* texture = nullptr;
		void* RTV = nullptr;
		void* SRV = nullptr;
	};
	struct Renderer
	{
		struct RuntimeData
		{
			std::array<RenderTargetData, 88> renderTargets;
		} data;
		RuntimeData& GetRuntimeData() { return data; }
	};
}

namespace globals::game
{
	RE::Renderer* renderer = nullptr;
}
#define GET_INSTANCE_MEMBER(member, owner) auto member = (owner)->member

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	UpscaleMethod method = UpscaleMethod::kDLSS;
	UpscaleMethod GetRuntimeUpscaleMethod() const { return method; }
	void ConfigureTAA();
};

#include "water_taa_under_test.h"

int main(int argc, char** argv)
{
	int failures = 0;
	const auto check = [&](bool condition, const char* message) {
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	};
	Upscaling upscaling;
	using Method = Upscaling::UpscaleMethod;
	RE::TemporalAA taa;
	RE::ImageSpaceManager manager{ &taa };
	RE::Renderer renderer;
	int resource = 0;
	const RE::RenderTargetData complete{ &resource, &resource, &resource };
	const auto restoreTargets = [&] {
		renderer.data.renderTargets[86] = complete;
		renderer.data.renderTargets[87] = complete;
	};
	if (argc == 2 && std::string_view(argv[1]) == "--repro") {
		RE::ImageSpaceManager::instance = &manager;
		globals::game::renderer = &renderer;
		RE::settingAvailable = true;
		RE::waterSetting.data.b = false;
		upscaling.ConfigureTAA();
		check(!taa.waterTAAEnabled, "Crash reproduction: DLSS must not enable water TAA with its setting off and both histories absent");
		return failures ? 1 : 0;
	}

	upscaling.ConfigureTAA();
	RE::ImageSpaceManager::instance = &manager;
	manager.BSImagespaceShaderISTemporalAA = nullptr;
	upscaling.ConfigureTAA();
	manager.BSImagespaceShaderISTemporalAA = &taa;
	globals::game::renderer = &renderer;
	restoreTargets();
	upscaling.ConfigureTAA();
	check(!taa.waterTAAEnabled, "A missing setting must not enable water TAA");
	RE::settingAvailable = true;
	upscaling.ConfigureTAA();
	check(taa.waterTAAEnabled, "A late setting must be retried, not cached as unavailable");
	const auto cachedLookups = RE::settingLookups;

	for (const auto method : { Method::kFSR, Method::kDLSS }) {
		upscaling.method = method;
		RE::waterSetting.data.b = false;
		taa.waterTAAEnabled = true;
		upscaling.ConfigureTAA();
		check(!taa.waterTAAEnabled, "Disabled water TAA must stay off even with allocated targets");
		RE::waterSetting.data.b = true;
		for (const auto target : { 86, 87 }) {
			for (const auto member : { &RE::RenderTargetData::texture, &RE::RenderTargetData::RTV, &RE::RenderTargetData::SRV }) {
				restoreTargets();
				renderer.data.renderTargets[target].*member = nullptr;
				taa.waterTAAEnabled = true;
				upscaling.ConfigureTAA();
				check(!taa.waterTAAEnabled, "Either incomplete history must disable water TAA");
				restoreTargets();
				upscaling.ConfigureTAA();
				check(taa.waterTAAEnabled, "Recreated histories must restore eligible water TAA");
			}
		}
		globals::game::renderer = nullptr;
		upscaling.ConfigureTAA();
		check(!taa.waterTAAEnabled, "A missing renderer must disable water TAA");
		globals::game::renderer = &renderer;
		renderer.data.renderTargets[86] = {};
		renderer.data.renderTargets[87] = {};
		taa.waterTAAEnabled = true;
		taa.taaEnabled = false;
		upscaling.ConfigureTAA();
		check(!taa.waterTAAEnabled && taa.taaEnabled, "Absent water histories must not disable the upscaler's TAA wrapper");
		restoreTargets();
	}
	check(RE::settingLookups == cachedLookups, "An available setting must not be looked up every frame");

	upscaling.method = Method::kTAA;
	taa.waterTAAEnabled = true;
	taa.taaEnabled = false;
	upscaling.ConfigureTAA();
	check(!taa.waterTAAEnabled && taa.taaEnabled, "CSX TAA must retain its own TAA path");
	upscaling.method = Method::kNONE;
	const auto nativeLookups = RE::ImageSpaceManager::lookups;
	for (const bool nativeTAA : { false, true }) {
		for (const bool nativeWaterTAA : { false, true }) {
			taa.taaEnabled = nativeTAA;
			taa.waterTAAEnabled = nativeWaterTAA;
			upscaling.ConfigureTAA();
			check(taa.taaEnabled == nativeTAA && taa.waterTAAEnabled == nativeWaterTAA,
				"None must preserve valid vanilla TAA choices");
		}
	}
	for (const bool nativeTAA : { false, true }) {
		taa.taaEnabled = nativeTAA;
		taa.waterTAAEnabled = true;
		renderer.data.renderTargets[87] = {};
		upscaling.ConfigureTAA();
		check(taa.taaEnabled == nativeTAA && taa.waterTAAEnabled,
			"None must not permanently change vanilla intent during target recreation");
		restoreTargets();
	}
	RE::waterSetting.data.b = false;
	taa.waterTAAEnabled = true;
	upscaling.ConfigureTAA();
	check(taa.waterTAAEnabled, "None must leave native water-setting reconciliation to vanilla");
	check(RE::ImageSpaceManager::lookups == nativeLookups, "None must retain its early return without new per-frame lookups");
	return failures ? 1 : 0;
}
