#pragma once

#include "FoveatedMaskCalibration.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace FoveatedMaskCalibration
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Geometry, scale, horizontalScale, centers)

	inline void to_json(nlohmann::json& json, const Reference& value)
	{
		json = { { "version", value.version }, { "outer", value.outer }, { "leftToRight", value.leftToRight },
			{ "peripheryTaa", value.peripheryTaa }, { "fullImage", value.fullImage },
			{ "centerScale", value.centerScale }, { "feather", value.feather } };
	}

	/** Reject incomplete or corrupt references before they can alter live geometry. */
	inline void from_json(const nlohmann::json& json, Reference& value)
	{
		Reference parsed;
		const auto& version = json.at("version");
		if (!version.is_number_integer() || (version != 0 && version != 1))
			throw std::invalid_argument("Unsupported FOV calibration version");
		parsed.version = version.get<uint32_t>();
		if (parsed.version != 0) {
			const auto& centers = json.at("outer").at("centers");
			const auto& projection = json.at("leftToRight");
			if (!centers.is_array() || centers.size() != 4 || !projection.is_array() || projection.size() != 9)
				throw std::invalid_argument("Invalid FOV calibration dimensions");
			const auto requireNumber = [](const nlohmann::json& number) {
				if (!number.is_number())
					throw std::invalid_argument("FOV calibration geometry requires numeric values");
			};
			for (const auto* number : { &json.at("outer").at("scale"), &json.at("outer").at("horizontalScale"),
					 &json.at("centerScale"), &json.at("feather") })
				requireNumber(*number);
			for (const auto& number : centers)
				requireNumber(number);
			for (const auto& number : projection)
				requireNumber(number);
			json.at("outer").get_to(parsed.outer);
			json.at("leftToRight").get_to(parsed.leftToRight);
			json.at("peripheryTaa").get_to(parsed.peripheryTaa);
			json.at("fullImage").get_to(parsed.fullImage);
			json.at("centerScale").get_to(parsed.centerScale);
			json.at("feather").get_to(parsed.feather);
			if (!IsValid(parsed))
				throw std::invalid_argument("Invalid FOV calibration reference");
		}
		value = parsed;
	}
}
