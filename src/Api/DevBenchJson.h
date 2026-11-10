#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json.hpp>
#	include <string>

namespace CSX::Api
{
	/** @brief Encodes replies/events with invalid UTF-8 replaced; allocation failures still propagate. */
	inline std::string DumpForDevBench(const nlohmann::json& a_json)
	{
		return a_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}
}
#endif
