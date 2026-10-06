#include "Features/Upscaling/NeuralRendering/LifetimeDiagnosticsJson.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
}

int main()
{
	using namespace NeuralRendering;
	try {
		LifetimeDiagnostics history;
		for (std::uint32_t frame = 0; frame < 70; ++frame) {
			LifetimeRecord record;
			record.requestFrame = frame;
			record.succeeded = true;
			history.Record(record);
		}
		auto snapshot = history.Snapshot();
		Require(snapshot.records.size() == 64 && snapshot.overwrittenRecords == 6, "bounded retention failed");
		Require(snapshot.records.front().requestFrame == 6 && snapshot.records.back().requestFrame == 69, "ring order changed");
		LifetimeRecord failure;
		failure.requestFrame = 70;
		failure.failureObserved = true;
		failure.result = static_cast<std::int32_t>(0x887A0006);
		failure.regionCount = 1;
		failure.regions[0].resourceSerial = UINT64_MAX - 1;
		failure.regions[0].resources12[0] = UINT64_MAX - 2;
		failure.before.completedKnown = true;
		failure.before.completed = 0;
		failure.after.deviceRemoved = true;
		history.Record(failure);
		LifetimeRecord cleanup;
		cleanup.operation = LifetimeOperation::BackendRetirement;
		cleanup.succeeded = true;
		history.Record(cleanup);
		history.DiagnosticFailure();
		snapshot = history.Snapshot();
		Require(history.Frozen() && snapshot.totalRecords == 71 && snapshot.diagnosticFailures == 1, "failure evidence overwritten by cleanup");
		Require(snapshot.firstFailure && snapshot.firstFailure->requestFrame == 70, "first failure lost");
		const auto json = LifetimeDiagnosticsJson(snapshot, false);
		Require(!json["enabled"].get<bool>() && json["frozen"].get<bool>(), "disabling capture hid failure");
		const auto& pinned = json["firstFailure"];
		Require(pinned["before"]["completed"] == "0" && pinned["after"]["completed"].is_null(), "unknown completion or real zero misrepresented");
		Require(pinned["regions"][0]["resourceSerial"] == "18446744073709551614", "serial precision lost");
		Require(pinned["regions"][0]["resources12"][0] == "18446744073709551613", "resource identity precision lost");
		Require(pinned["regions"][0]["previousContext"].is_null(), "missing history presented as valid geometry");
		Require(pinned["modeValue"].is_null() && pinned["result"] == failure.result, "unknown mode or HRESULT lost");
		std::cout << "Lifetime retention, failure pinning and JSON identity checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
