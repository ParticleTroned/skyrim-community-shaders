#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

namespace WaterAppearanceMigration
{
	template <std::size_t N>
	bool ContainsAnyKey(
		const nlohmann::json& a_source,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_source.is_object())
			return false;

		for (const auto key : a_keys) {
			if (a_source.contains(key.data()))
				return true;
		}
		return false;
	}

	template <std::size_t N>
	bool MoveValues(
		nlohmann::json& a_source,
		nlohmann::json& a_destination,
		std::string_view a_forceGlobalKey,
		bool a_forceGlobal,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_source.is_object() || !a_destination.is_object())
			return false;

		bool changed = false;
		const auto forceIt = a_destination.find(a_forceGlobalKey.data());
		if (forceIt == a_destination.end() ||
			!forceIt->is_boolean() ||
			forceIt->get<bool>() != a_forceGlobal) {
			a_destination[std::string(a_forceGlobalKey)] = a_forceGlobal;
			changed = true;
		}

		for (const auto key : a_keys) {
			auto sourceIt = a_source.find(key.data());
			if (sourceIt == a_source.end())
				continue;

			const auto destinationIt = a_destination.find(key.data());
			if (sourceIt->is_number() &&
				(destinationIt == a_destination.end() || !destinationIt->is_number())) {
				a_destination[std::string(key)] = *sourceIt;
			}

			// Adaptive Balance is the sole owner after migration. Valid destination
			// values win, and malformed legacy values are discarded rather than
			// surviving as a second source of truth.
			a_source.erase(sourceIt);
			changed = true;
		}

		return changed;
	}
}
