#include "Api/ServiceFoundation.h"
#include "service_retry_test.h"

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
			{ "contractMajor", 1 },
			{ "action", std::move(a_action) },
			{ "clientId", "foundation-test" },
			{ "commandId", std::move(a_commandId) },
		};
	}

	void CheckCommandCapacity()
	{
		ServiceLimits limits;
		limits.maximumCommands = 3;
		ServiceFoundation service({ "csx.capacity.test" }, limits);
		const auto outerRequest = Request("start", "long-running");
		uint32_t calls = 0;
		const auto success = [&](const json& command) {
			++calls;
			return service.MakeEnvelope(command, true);
		};

		// Keep the oldest reservation pending while newer commands finish. This
		// deterministically exercises the same ordering as concurrent dispatch.
		const auto outer = service.Dispatch(outerRequest, [&](const json& command) {
			for (uint32_t index = 0; index < 64; ++index) {
				const auto request = Request("start", "completed-" + std::to_string(index));
				Check(service.Dispatch(request, success)["ok"].get<bool>(), "completed records blocked new commands");
				Check(service.JournalStatus()["retainedCommands"] <= limits.maximumCommands,
					"a pending oldest command defeated the retention bound");
				const auto replay = service.Dispatch(request, success);
				Check(replay["idempotentReplay"].get<bool>(), "capacity trimming lost the newest completed command");
			}
			const auto pending = service.Dispatch(outerRequest, success);
			Check(pending["error"]["code"] == "command_in_progress", "capacity trimming evicted a pending reservation");
			return service.MakeEnvelope(command, true);
		});
		Check(outer["ok"].get<bool>(), "capacity sweep failed: " + outer.dump());
		Check(calls == 64, "capacity pressure duplicated a handler");

		ServiceFoundation occupied({ "csx.occupied.test" }, limits);
		const auto overflowRequest = Request("start", "overflow");
		uint32_t overflowCalls = 0;
		const auto overflowHandler = [&](const json& command) {
			++overflowCalls;
			return occupied.MakeEnvelope(command, true);
		};
		std::function<json(uint32_t)> reserve = [&](uint32_t depth) {
			const auto request = Request("start", "pending-" + std::to_string(depth));
			return occupied.Dispatch(request, [&](const json& command) {
				if (depth + 1 < limits.maximumCommands) {
					Check(reserve(depth + 1)["ok"].get<bool>(), "available reservation capacity was rejected");
				} else {
					const auto overflow = occupied.Dispatch(overflowRequest, overflowHandler);
					Check(!overflow["ok"].get<bool>() && overflow["error"]["code"] == "service_busy" &&
							  overflow["error"]["retryable"].get<bool>(),
						"full pending capacity must reject with a retryable error");
					Check(overflowCalls == 0, "rejected command executed its handler");
					Check(occupied.JournalStatus()["retainedCommands"] == limits.maximumCommands,
						"full pending capacity exceeded the retention bound");
					const auto pending = occupied.Dispatch(request, overflowHandler);
					Check(pending["error"]["code"] == "command_in_progress", "full capacity changed duplicate semantics");
					auto conflicting = request;
					conflicting["different"] = true;
					const auto conflict = occupied.Dispatch(conflicting, overflowHandler);
					Check(conflict["error"]["code"] == "idempotency_conflict", "full capacity changed conflict semantics");
				}
				return occupied.MakeEnvelope(command, true);
			});
		};
		Check(reserve(0)["ok"].get<bool>(), "pending reservation completion failed");
		Check(occupied.Dispatch(overflowRequest, overflowHandler)["ok"].get<bool>(), "rejected command could not retry after capacity recovered");
		Check(occupied.Dispatch(overflowRequest, overflowHandler)["idempotentReplay"].get<bool>() && overflowCalls == 1,
			"retried command lost idempotent replay");
	}
}

int RunTest()
{
	CheckCommandCapacity();
	ServiceLimits limits;
	limits.maximumCommands = 8;
	limits.maximumEvents = 3;
	ServiceFoundation service({ "csx.test", 1, 2, 3 }, limits);
	service.SetServerMetadataProvider([] { return json{ { "testServer", true } }; });
	CheckRetryableCommands(service, Check);

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

	auto firstTuple = Request("tuple", "gamma");
	firstTuple["clientId"] = "alpha\nbeta";
	const auto firstTupleResponse = service.Dispatch(firstTuple, [&](const json& command) {
		auto response = service.MakeEnvelope(command, true);
		response["result"] = { { "tuple", 1 } };
		return response;
	});
	auto secondTuple = Request("tuple", "beta\ngamma");
	secondTuple["clientId"] = "alpha";
	const auto secondTupleResponse = service.Dispatch(secondTuple, [&](const json& command) {
		auto response = service.MakeEnvelope(command, true);
		response["result"] = { { "tuple", 2 } };
		return response;
	});
	Check(firstTupleResponse["ok"].get<bool>() && secondTupleResponse["ok"].get<bool>(),
		"distinct client and command tuples collided");
	Check(firstTupleResponse["result"]["tuple"] == 1 && secondTupleResponse["result"]["tuple"] == 2,
		"structured idempotency identity returned the wrong command");

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
