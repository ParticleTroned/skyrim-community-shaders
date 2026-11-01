#pragma once

#include "EngineFix.h"
#include "EngineFixes/SunReceiverPolicy.h"
#include <RE/B/BSShaderProperty.h>
#include <cstdint>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

namespace RE
{
	class BSLightingShaderProperty;
	class BSGeometry;
	class BSShaderAccumulator;
}

/** Restores missing sun reception at native lighting pass generation. */
struct SunShadowReceiverFix : EngineFix
{
	std::string GetName() override { return "Sun Shadow Receiver Fix"; }
	bool TryInstall() override;
};

namespace SunShadowReceiver
{
	using PassFunction = RE::BSShaderProperty::RenderPassArray* (*)(RE::BSLightingShaderProperty*, RE::BSGeometry*, std::uint32_t, RE::BSShaderAccumulator*);

	/** Capture current native receiver inputs for the shared production and diagnostic policy. */
	Policy::Input ReadInputs(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
		std::uint32_t mode, RE::BSShaderAccumulator* accumulator, std::uintptr_t& sun) noexcept;
	/** Apply a checked repair and count it for the bounded ordinary log. */
	bool Repair(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
		const Policy::Input& input, const Policy::Decision& decision) noexcept;
	/** Production dispatch, also used outside an explicit developer capture. */
	RE::BSShaderProperty::RenderPassArray* ProcessPasses(RE::BSLightingShaderProperty* property,
		RE::BSGeometry* geometry, std::uint32_t mode, RE::BSShaderAccumulator* accumulator, PassFunction original);
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect or change session-only enablement; saved settings are unaffected. */
	nlohmann::json Control(const nlohmann::json& args);
	/** Whether the shared native hook installed successfully. */
	bool Available() noexcept;
	/** Explain a refused native hook without retrying installation. */
	const std::string& InstallationError() noexcept;
	/** Read the primary accumulator identity for diagnostic attribution. */
	RE::BSShaderAccumulator* PrimaryAccumulator() noexcept;
#endif
}
