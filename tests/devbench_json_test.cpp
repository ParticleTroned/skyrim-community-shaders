#include "Api/DevBenchJson.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	using json = nlohmann::json;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}
}

int main()
{
	const json valid{ { "path", "caf\xC3\xA9/\xE9\x9B\xAA/\xF0\x9F\x8C\x88" },
		{ "embeddedNull", std::string("a\0b", 3) }, { "items", { false, 0, nullptr } } };
	Check(CSX::Api::DumpForDevBench(valid) == valid.dump(), "Valid UTF-8 output must remain byte-identical");

	const std::string replacement = "\xEF\xBF\xBD";
	const std::string badKey = "field\x80";
	const json damaged{ { "requestId", "capture-17" }, { "ok", false },
		{ "error", { { "code", "filesystem_error" }, { "message", "path \xC3(" } } },
		{ "details", { "tail\xE2\x82", "byte\x80", "valid" } }, { badKey, 42 } };
	bool strictRejected = false;
	try {
		(void)damaged.dump();
	} catch (const json::type_error&) {
		strictRejected = true;
	}
	Check(strictRejected, "Fixture must reproduce strict serialization failure");
	const auto repaired = json::parse(CSX::Api::DumpForDevBench(damaged));
	Check(repaired.at("requestId") == "capture-17" && repaired.at("ok") == false,
		"Malformed text must not discard response identity or status");
	Check(repaired.at("error").at("code") == "filesystem_error" &&
			  repaired.at("error").at("message") == "path " + replacement + "(",
		"Original error context and valid adjacent characters must survive");
	Check(repaired.at("details") == json::array({ "tail" + replacement, "byte" + replacement, "valid" }) &&
			  repaired.at("field" + replacement) == 42,
		"Nested values, truncated sequences and keys must produce valid UTF-8");
	Check(damaged.at(badKey) == 42 && damaged.at("error").at("message") == "path \xC3(",
		"Output repair must not mutate stored results or events");

	const std::string malformedRequest = "{\"path\":\"\x80\"}";
	bool requestRejected = false;
	try {
		(void)json::parse(malformedRequest);
	} catch (const json::parse_error& error) {
		requestRejected = true;
		const json reply{ { "ok", false }, { "error", error.what() } };
		Check(json::parse(CSX::Api::DumpForDevBench(reply)).at("ok") == false,
			"A malformed request must still receive a serializable error");
	}
	Check(requestRejected, "Reply repair must not relax request parsing");
	return 0;
}
