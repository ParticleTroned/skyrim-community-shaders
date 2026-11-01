#include "LightListSearchFix.h"

#include "EngineFixes/LightListSearchPolicy.h"
#include "Utils/PointerSearch.h"
#include "Utils/VirtualFunctionHook.h"

#include <RE/B/BSShaderPropertyLightData.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#	include <nlohmann/json.hpp>
#	include <stdexcept>
#endif

namespace
{
	bool installed = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool enabled{ true };
	std::string installationError = "Not initialized";
#endif

	bool Decline(const std::string& a_reason)
	{
		logger::warn("[Light List Search] {}; retaining native search", a_reason);
#ifdef DEVBENCH_BRIDGE_ENABLED
		installationError = a_reason;
#endif
		return false;
	}

	struct AddLight
	{
		static void thunk(RE::BSShaderPropertyLightData* a_data, RE::BSLight* a_light)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (!enabled.load(std::memory_order_relaxed)) {
				func(a_data, a_light);
				return;
			}
#endif
			const auto count = a_data->lights.size();
			if (LightListSearch::Policy::ShouldSearch(a_data->lightListFence, count) &&
				Util::PointerSearch::ContainsAvx2(a_data->lights.data(), count, a_light))
				return;
			func(a_data, a_light);
		}
		static inline decltype(&thunk) func = nullptr;
	};
}

bool LightListSearchFix::TryInstall()
{
	static_assert(sizeof(RE::BSShaderPropertyLightData) == 0x28);
	static_assert(offsetof(RE::BSShaderPropertyLightData, lights) == 0x00);
	static_assert(offsetof(RE::BSShaderPropertyLightData, lightListFence) == 0x18);
	if (installed)
		return true;
	if (!Util::PointerSearch::HasAvx2())
		return Decline("AVX2 CPU/OS support is unavailable");

	const auto target = REL::RelocationID(100990, 107777).address();
	const auto text = REL::Module::get().segment(REL::Segment::textx);
	std::array<std::uint8_t, LightListSearch::Policy::NativeCodeSize> bytes{};
	SIZE_T read = 0;
	if (target < text.address() || target - text.address() > text.size() ||
		text.size() - (target - text.address()) < bytes.size() ||
		!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(target), bytes.data(), bytes.size(), &read) ||
		read != bytes.size() || !LightListSearch::Policy::MatchesNativeCode(bytes))
		return Decline("Native duplicate-search code is modified or unsupported");

	const auto result = Util::InstallDetour(target, reinterpret_cast<void**>(&AddLight::func), reinterpret_cast<void*>(&AddLight::thunk));
	if (result.error != NO_ERROR)
		return Decline("Detour installation failed (" + std::to_string(result.error) + ")");
	installed = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
	installationError.clear();
#endif
	return true;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
nlohmann::json LightListSearch::Control(const nlohmann::json& a_args)
{
	for (const auto& [name, value] : a_args.items()) {
		(void)value;
		if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
			name != "expectedBuildId" && name != "lightListSearch")
			throw std::invalid_argument("Unknown light_list_search argument");
	}
	if (const auto settings = a_args.find("lightListSearch"); settings != a_args.end()) {
		if (!settings->is_object() || settings->size() != 1 || !settings->contains("enabled") || !settings->at("enabled").is_boolean())
			throw std::invalid_argument("lightListSearch requires exactly one boolean enabled field");
		if (!installed)
			throw std::invalid_argument("Light list search hook is unavailable");
		enabled.store(settings->at("enabled").get<bool>(), std::memory_order_relaxed);
	}
	return { { "available", installed }, { "enabled", installed && enabled.load(std::memory_order_relaxed) },
		{ "avx2Supported", Util::PointerSearch::HasAvx2() }, { "installationError", installationError },
		{ "minimumLightCount", Policy::MinimumVectorCount }, { "sessionOnly", true } };
}
#endif
