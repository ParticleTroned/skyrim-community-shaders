#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace VRPipelineDiagnostics
{
	enum class Source
	{
		CS
	};

	struct Event
	{
		Source source = Source::CS;
		std::string type;
		std::string reason;
		nlohmann::json data = nlohmann::json::object();
	};

	void Emit(const Event& event, bool writeStructured, std::string_view textPayload);
	void ResetSession();
}
