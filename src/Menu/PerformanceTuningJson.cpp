#include "Menu/PerformanceTuningJson.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>

namespace PerformanceTuning
{
	namespace
	{
		bool AreNumbersEquivalent(
			const nlohmann::json& lhs,
			const nlohmann::json& rhs)
		{
			const bool lhsIsFloat = lhs.is_number_float();
			const bool rhsIsFloat = rhs.is_number_float();
			if (!lhsIsFloat && !rhsIsFloat)
				return lhs == rhs;

			const double lhsValue = lhs.get<double>();
			const double rhsValue = rhs.get<double>();
			if (!std::isfinite(lhsValue) || !std::isfinite(rhsValue))
				return lhsValue == rhsValue;

			// Float round-trip tolerance applies only when both stored values are
			// floating point. A mixed float/integer comparison must be exact so a
			// value such as 3.0000001 cannot verify an integer setting restored as 3.
			if (lhsIsFloat != rhsIsFloat) {
				const auto& floating = lhsIsFloat ? lhs : rhs;
				const auto& integral = lhsIsFloat ? rhs : lhs;
				const double floatingValue = floating.get<double>();
				if (std::trunc(floatingValue) != floatingValue)
					return false;

				if (integral.is_number_unsigned()) {
					constexpr double kUnsignedLimit = 0x1p64;
					if (floatingValue < 0.0 || floatingValue >= kUnsignedLimit)
						return false;
					const auto integralValue =
						integral.get<nlohmann::json::number_unsigned_t>();
					const auto converted =
						static_cast<nlohmann::json::number_unsigned_t>(floatingValue);
					return converted == integralValue &&
					       static_cast<double>(converted) == floatingValue;
				}

				constexpr double kSignedLimit = 0x1p63;
				if (floatingValue < -kSignedLimit || floatingValue >= kSignedLimit)
					return false;
				const auto integralValue =
					integral.get<nlohmann::json::number_integer_t>();
				const auto converted =
					static_cast<nlohmann::json::number_integer_t>(floatingValue);
				return converted == integralValue &&
				       static_cast<double>(converted) == floatingValue;
			}

			const double scale =
				std::max({ 1.0, std::abs(lhsValue), std::abs(rhsValue) });
			const double tolerance =
				4.0 *
				static_cast<double>(
					std::numeric_limits<float>::epsilon()) *
				scale;
			return std::abs(lhsValue - rhsValue) <= tolerance;
		}
	}

	bool AreJsonValuesEquivalent(
		const nlohmann::json& lhs,
		const nlohmann::json& rhs)
	{
		if (lhs.is_number() && rhs.is_number())
			return AreNumbersEquivalent(lhs, rhs);

		if (lhs.type() != rhs.type())
			return false;

		if (lhs.is_object()) {
			if (lhs.size() != rhs.size())
				return false;
			for (const auto& [key, lhsValue] : lhs.items()) {
				const auto rhsIt = rhs.find(key);
				if (rhsIt == rhs.end() ||
					!AreJsonValuesEquivalent(lhsValue, *rhsIt)) {
					return false;
				}
			}
			return true;
		}

		if (lhs.is_array()) {
			if (lhs.size() != rhs.size())
				return false;
			for (std::size_t index = 0; index < lhs.size(); ++index) {
				if (!AreJsonValuesEquivalent(lhs[index], rhs[index]))
					return false;
			}
			return true;
		}

		return lhs == rhs;
	}
}
