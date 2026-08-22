#include "Api/ServiceFoundation.h"

#include <iostream>
#include <stdexcept>
#include <string>

using CSX::Api::ServiceFoundation;
using CSX::Api::ServiceLimits;
using json = nlohmann::json;

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	json Request(std::string a_action, std::string a_commandId)
	{
		return {
			{ "contractMajor", 1 }, { "action", std::move(a_action) },
			{ "clientId", "foundation-test" }, { "commandId", std::move(a_commandId) },
		};
	}
}

int RunTest()
{
	ServiceLimits limits;
	limits.maximumCommands = 8;
	limits.maximumEvents = 3;
	ServiceFoundation service({ "csx.test", 1, 2, 3 }, limits);
	service.SetServerMetadataProvider([] { return json{ { "testServer", true } }; });

	const auto invalid = service.Dispatch(json::object(), [](const json&) { return json::object(); });
	Check(!invalid["ok"].get<bool>(), "missing contract major must be rejected");
	Check(invalid["error"]["code"] == "unsupported_contract_version", "wrong validation error code");
	auto wrongActionType = Request("ignored", "wrong-action-type");
	wrongActionType["action"] = 7;
	const auto wrongAction = service.Dispatch(wrongActionType, [](const json&) { return json::object(); });
	Check(!wrongAction["ok"].get<bool>(), "non-string action must be rejected");
	Check(wrongAction["error"]["code"] == "missing_field", "typed-field validation did not return an envelope");

	uint32_t calls = 0;
	const auto request = Request("start", "one");
	const auto first = service.Dispatch(request, [&](const json& command) {
		++calls;
		auto response = service.MakeEnvelope(command, true);
		response["result"] = { { "requestId", "request-1" }, { "state", "accepted" } };
		return response;
	});
	Check(first["ok"].get<bool>(), "initial command failed");
	Check(first["contract"]["minor"] == 2, "contract metadata was not preserved");
	Check(first["server"]["testServer"].get<bool>(), "server metadata provider was not used");

	const auto replay = service.Dispatch(
		request,
		[&](const json&) {
			++calls;
			return json::object();
		},
		[](std::string_view id) {
			return json{ { "requestId", id }, { "state", "completed" } };
		});
	Check(calls == 1, "idempotent replay executed the handler twice");
	Check(replay["result"]["state"] == "completed", "replay did not refresh the retained receipt");
	Check(replay["result"]["idempotentReplay"].get<bool>(), "replay was not identified");

	auto conflictRequest = request;
	conflictRequest["different"] = true;
	const auto conflict = service.Dispatch(conflictRequest, [](const json&) { return json::object(); });
	Check(!conflict["ok"].get<bool>(), "idempotency conflict was accepted");
	Check(conflict["error"]["code"] == "idempotency_conflict", "wrong idempotency error code");

	service.AppendEvent("request-1", 1, "request.accepted");
	service.AppendEvent("request-1", 2, "request.running");
	service.AppendEvent("request-2", 1, "request.accepted");
	service.AppendEvent("request-1", 3, "request.completed");
	const auto filtered = service.PollEvents(0, 1, "request-1");
	Check(filtered["events"].size() == 1, "event filter or limit was not applied");
	Check(filtered["cursorExpired"].get<bool>() == false, "zero cursor must not expire");
	Check(filtered["moreAvailable"].get<bool>(), "filtered pagination did not report remaining events");

	const auto journal = service.JournalStatus();
	Check(journal["retainedEvents"] == 3, "event retention limit was not enforced");
	const auto acknowledged = service.AcknowledgeEvents(journal["latestEventId"].get<uint64_t>());
	Check(acknowledged == journal["latestEventId"].get<uint64_t>(), "event acknowledgement did not advance");
	Check(service.JournalStatus()["retainedEvents"] == 0, "acknowledged events were not trimmed");

	Check(ServiceFoundation::NewId() != ServiceFoundation::NewId(), "generated identities collided");
	Check(ServiceFoundation::TimestampUtc().ends_with('Z'), "timestamp is not UTC-qualified");
	return 0;
}

int main()
{
	try {
		return RunTest();
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
