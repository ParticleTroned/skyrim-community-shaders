#pragma once

#include "Categories.h"
#include "Policy.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace StreamingTextures
{
	inline constexpr bool DefaultEnabled = false;
	/** Persistent residency controls shared by the menu, saved configuration and automation. */
	struct Options
	{
		std::uint32_t reserveMiB = Util::TextureMemoryPolicy::DefaultReserveMiB;
		bool rememberedSizes = true;
		std::uint32_t maximumSize = 0;
		bool operator==(const Options&) const = default;
	};
	inline void ValidateOptions(const Options& value)
	{
		if (value.reserveMiB < Util::TextureMemoryPolicy::ReserveStepMiB || value.reserveMiB > Util::TextureMemoryPolicy::MaximumReserveMiB ||
			value.reserveMiB % Util::TextureMemoryPolicy::ReserveStepMiB)
			throw std::invalid_argument("reserveMiB must be 256 through 8192 in steps of 256");
		if (value.maximumSize != 0 && value.maximumSize != 1024 && value.maximumSize != 2048 && value.maximumSize != 4096 && value.maximumSize != 8192)
			throw std::invalid_argument("maximumSize must be 0 (unlimited), 1024, 2048, 4096 or 8192");
	}
	inline Options ParseOptions(const nlohmann::json& value)
	{
		if (!value.is_object() || value.size() != 3 || !value.contains("reserveMiB") || !value.contains("maximumSize") ||
			!value.contains("rememberedSizes") || !value.at("reserveMiB").is_number_integer() || !value.at("maximumSize").is_number_integer() ||
			!value.at("rememberedSizes").is_boolean() || value.at("reserveMiB") < 0 || value.at("reserveMiB") > UINT32_MAX ||
			value.at("maximumSize") < 0 || value.at("maximumSize") > UINT32_MAX)
			throw std::invalid_argument("Options requires reserveMiB, rememberedSizes and maximumSize");
		Options result{ value.at("reserveMiB").get<std::uint32_t>(), value.at("rememberedSizes").get<bool>(), value.at("maximumSize").get<std::uint32_t>() };
		ValidateOptions(result);
		return result;
	}
	inline nlohmann::json SerializeOptions(const Options& value)
	{
		return { { "reserveMiB", value.reserveMiB }, { "rememberedSizes", value.rememberedSizes }, { "maximumSize", value.maximumSize } };
	}

	/** Independent persistent triggers; continuous streaming still respects detail and allocation limits. */
	struct Modes
	{
		bool onPressure = true;
		bool continuous = false;
		bool operator==(const Modes&) const = default;
		[[nodiscard]] bool Enabled() const noexcept { return onPressure || continuous; }
	};
	inline Modes ParseModes(const nlohmann::json& value)
	{
		if (!value.is_object() || value.size() != 2 || !value.contains("onPressure") || !value.contains("continuous") ||
			!value.at("onPressure").is_boolean() || !value.at("continuous").is_boolean())
			throw std::invalid_argument("Modes requires onPressure and continuous booleans");
		return { value.at("onPressure").get<bool>(), value.at("continuous").get<bool>() };
	}
	inline nlohmann::json SerializeModes(const Modes& value)
	{
		return { { "onPressure", value.onPressure }, { "continuous", value.continuous } };
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct PressureSimulationRequest
	{
		std::uint32_t durationMilliseconds = 0, freeMiB = 0;
	};
	/** Reject malformed pressure tests before changing any session state. */
	inline PressureSimulationRequest ParsePressureSimulation(const nlohmann::json& value)
	{
		if (!value.is_object() || !value.contains("durationMilliseconds") || value.size() != 1u + value.contains("freeMiB"))
			throw std::invalid_argument("pressureSimulation requires durationMilliseconds and optional freeMiB");
		const auto integer = [&](const char* name, std::uint32_t maximum) {
			const auto& number = value.at(name);
			if (!number.is_number_integer() || (!number.is_number_unsigned() && number.get<std::int64_t>() < 0) || number.get<std::uint64_t>() > maximum)
				throw std::invalid_argument(std::string(name) + " is outside the bounded integer range");
			return number.get<std::uint32_t>();
		};
		PressureSimulationRequest result;
		result.durationMilliseconds = integer("durationMilliseconds", Util::TextureMemoryPolicy::MaximumPressureSimulationMilliseconds);
		if (bool(result.durationMilliseconds) != value.contains("freeMiB"))
			throw std::invalid_argument("Starting pressure requires freeMiB; clearing accepts only zero durationMilliseconds");
		if (result.durationMilliseconds)
			result.freeMiB = integer("freeMiB", Util::TextureMemoryPolicy::MaximumPressureSimulationHeadroomMiB);
		return result;
	}
	/** Validate the legacy continuous-streaming alias before any DevBench settings mutation. */
	inline std::optional<bool> ParseDebugForceStreaming(const nlohmann::json& args)
	{
		if (!args.contains("debugForceStreaming"))
			return std::nullopt;
		const auto& enabled = args.at("debugForceStreaming");
		if (args.value("action", std::string{}) != "texture_streaming" || !enabled.is_boolean())
			throw std::invalid_argument("debugForceStreaming requires the texture_streaming action and a boolean");
		return enabled.get<bool>();
	}
#endif
	/** Reject fractional, negative and overflowing mip limits before conversion. */
	inline std::uint32_t ParseMaximumMipDrop(const nlohmann::json& value)
	{
		if (!value.is_number_integer() || value < 1 || value > TextureStreamingPolicy::MaximumDrop)
			throw std::invalid_argument("MaximumMipDrop must be an integer between 1 and 3");
		return value.get<std::uint32_t>();
	}
	/** Saved settings and DevBench use the same strict optional-category boundary. */
	inline Categories ParseCategories(const nlohmann::json& value)
	{
		if (!value.is_object())
			throw std::invalid_argument("Texture categories must be an object");
		Categories result{ false, false, false, false, false };
		for (const auto& [name, enabled] : value.items()) {
			if (!enabled.is_boolean())
				throw std::invalid_argument("Texture category values must be boolean");
			if (name == "landscapeStatics")
				result.landscapeStatics = enabled.get<bool>();
			else if (name == "alphaTestedStatics")
				result.alphaTestedStatics = enabled.get<bool>();
			else if (name == "emissiveStatics")
				result.emissiveStatics = enabled.get<bool>();
			else if (name == "terrain")
				result.terrain = enabled.get<bool>();
			else if (name == "parallax")
				result.parallax = enabled.get<bool>();
			else
				throw std::invalid_argument("Unknown texture category: " + name);
		}
		return result;
	}
	inline nlohmann::json SerializeCategories(const Categories& value)
	{
		return { { "landscapeStatics", value.landscapeStatics }, { "alphaTestedStatics", value.alphaTestedStatics },
			{ "emissiveStatics", value.emissiveStatics }, { "terrain", value.terrain }, { "parallax", value.parallax } };
	}
}
