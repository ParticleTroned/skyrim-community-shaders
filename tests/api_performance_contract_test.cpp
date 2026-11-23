#include "Api/JsonServiceDispatch.h"
#include "Api/PerformanceServicePolicy.h"
#include "VRAPI/CSperformanceapi.h"

#include <optional>
#include <stdexcept>
#include <type_traits>

using namespace CSX;
using nlohmann::json;
using PerformanceAPI::Status;
static_assert(std::is_standard_layout_v<PerformanceAPI::Interface001>);

void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("performance API contract failed");
}

int main()
{
	const json base{ { "contractMajor", 1 }, { "clientId", "test" }, { "commandId", "1" }, { "action", "status" } };
	Require(!Api::PerformancePolicy::Validate(base));
	for (const json& invalid : { json{}, json::array(), json{ { "action", "status" } } })
		Require(Api::PerformancePolicy::Validate(invalid));
	for (const auto& patch : { json{ { "action", "unknown" } }, json{ { "contractMajor", 1.0 } },
			 json{ { "clientId", "" } }, json{ { "commandId", std::string(129, 'x') } },
			 json{ { "recordFpsVR", true } }, json{ { "expectedBuildId", 42 } },
			 json{ { "action", "export_session" } }, json{ { "action", "set_feature_enabled" }, { "featureShortName", "Skin" } } }) {
		auto request = base;
		request.update(patch);
		Require(Api::PerformancePolicy::Validate(request));
	}

	int calls = 0;
	auto handler = [&](const json& request) -> std::optional<json> { ++calls; return std::optional<json>{ request }; };
	auto dispatch = [&](const std::string& bytes) {
		PerformanceAPI::Request001 request;
		request.jsonUtf8 = bytes.data();
		request.jsonBytes = static_cast<std::uint32_t>(bytes.size());
		PerformanceAPI::Response001 response;
		return Api::DispatchJsonService(&request, &response, PerformanceAPI::MaximumRequestBytes, handler);
	};
	Require(dispatch(base.dump()) == Status::kSuccess && calls == 1);
	Require(dispatch("{") == Status::kInvalidJson);
	Require(dispatch("[]") == Status::kInvalidJson);
	Require(dispatch(std::string(70, '[') + "0" + std::string(70, ']')) == Status::kInvalidJson);
	Require(dispatch(std::string(PerformanceAPI::MaximumRequestBytes + 1, ' ')) == Status::kRequestTooLarge);
	Require(calls == 1);
	PerformanceAPI::Request001 request;
	PerformanceAPI::Response001 response;
	request.structSize = 0;
	Require(Api::DispatchJsonService(&request, &response, 64, handler) == Status::kStructureTooSmall);
	Require(!response.jsonUtf8 && !response.jsonBytes);
	response.structSize = 0;
	Require(Api::DispatchJsonService(&request, &response, 64, handler) == Status::kStructureTooSmall);
}
