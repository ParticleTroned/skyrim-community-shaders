#pragma once

#include "../../package/Shaders/Common/PerformanceCostReference.hlsli"
#include <cstdint>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <array>
#	include <atomic>
#	include <nlohmann/json.hpp>
#	include <stdexcept>
#	include <string_view>
#	include <utility>
#endif

namespace CSX::Diagnostics::PerformanceCosts
{
	/** Independent references retain feature settings while restoring previous calculations. */
	enum class Point : uint32_t
	{
		GrassTileFilter = CSX_PERF_GRASS_TILE_FILTER,
		FurContactReject = CSX_PERF_FUR_CONTACT_REJECT,
		SnowWeatherRange = CSX_PERF_SNOW_WEATHER_RANGE,
		PointLightComposition = CSX_PERF_POINT_LIGHT_COMPOSITION,
		NeuralPreparation = CSX_PERF_NEURAL_PREPARATION,
		ReverseZBindings = CSX_PERF_REVERSE_Z_BINDINGS
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	struct Entry
	{
		std::string_view name;
		Point point;
		std::string_view conditions;
	};
	inline constexpr std::array Entries{
		Entry{ "grassTileFilter", Point::GrassTileFilter, "Advanced Grass Collision with more than four actor boxes" },
		Entry{ "furContactReject", Point::FurContactReject, "Visible fur with Touch enabled and out-of-range contact capsules" },
		Entry{ "snowWeatherRange", Point::SnowWeatherRange, "Snow Cover with zero snowfall accumulation" },
		Entry{ "pointLightComposition", Point::PointLightComposition, "Nonneutral shared and subtype point-light colour controls" },
		Entry{ "neuralPreparation", Point::NeuralPreparation, "NR empty character selections or zero-detail/zero-appearance preserve-source reconstruction" },
		Entry{ "reverseZBindings", Point::ReverseZBindings, "Reverse Z active with unchanged physical state during convention transitions" }
	};
	inline constexpr uint32_t AllPoints = [] {
		uint32_t mask = 0;
		for (const auto& entry : Entries)
			mask |= static_cast<uint32_t>(entry.point);
		return mask;
	}();
	inline constexpr uint32_t ShaderPoints = AllPoints & ~CSX_PERF_REVERSE_Z_BINDINGS;

	/** Schema and validation use the same independent optimization names. */
	inline nlohmann::json SettingsSchema()
	{
		nlohmann::json properties = nlohmann::json::object();
		for (const auto& entry : Entries)
			properties[entry.name] = { { "type", "boolean" }, { "default", true }, { "description", entry.conditions } };
		return { { "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false }, { "properties", std::move(properties) } };
	}

	/** Validates an entire partial update before changing any requested reference bits. */
	inline uint32_t ParseSettings(uint32_t a_current, const nlohmann::json& a_patch)
	{
		if (!a_patch.is_object() || a_patch.empty())
			throw std::invalid_argument("performanceCosts must be a nonempty object");
		for (const auto& [name, value] : a_patch.items()) {
			bool known = false;
			for (const auto& entry : Entries) {
				if (entry.name != name)
					continue;
				known = true;
				if (!value.is_boolean())
					throw std::invalid_argument("performanceCosts values must be boolean");
				const auto bit = static_cast<uint32_t>(entry.point);
				a_current = value.get<bool>() ? a_current & ~bit : a_current | bit;
				break;
			}
			if (!known)
				throw std::invalid_argument("unknown performanceCosts setting: " + name);
		}
		return a_current;
	}

	/** Queued references become visible together at the render-frame boundary. */
	class Selection
	{
	public:
		uint32_t Active() const { return active.load(std::memory_order_acquire); }
		uint32_t Requested() const { return requested.load(std::memory_order_acquire); }
		bool Pending() const { return Active() != Requested(); }
		void Request(uint32_t a_mask)
		{
			if (a_mask & ~AllPoints)
				throw std::invalid_argument("unknown performance reference bits");
			requested.store(a_mask, std::memory_order_release);
		}
		bool Apply()
		{
			const auto next = Requested();
			return active.exchange(next, std::memory_order_acq_rel) != next;
		}

	private:
		std::atomic_uint32_t active{ 0 };
		std::atomic_uint32_t requested{ 0 };
	};

	inline Selection selection;
	inline bool ReferenceEnabled(Point a_point) { return (selection.Active() & static_cast<uint32_t>(a_point)) != 0; }
	inline uint32_t ShaderReferenceMask() { return selection.Active() & ShaderPoints; }

	/** Stages unsaved references or inspects their applied state and shader readiness. */
	nlohmann::json Control(const nlohmann::json& a_args);
	/** Applies queued references on the render thread before features reset for the next frame. */
	void ProcessPendingChanges();
#else
	constexpr bool ReferenceEnabled(Point) { return false; }
#endif
}
