#include "Diagnostics/EngineStutterCapture.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <winrt/base.h>
using namespace CSX::Diagnostics::Stutters;
using json = nlohmann::json;
void Check(bool ok, const char* message)
{
	if (!ok)
		throw std::runtime_error(message);
}
template <class Predicate>
json Await(Capture& capture, Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
	json status;
	do {
		status = capture.Dispatch({ { "action", "events" } });
		if (predicate(status))
			return status;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	} while (std::chrono::steady_clock::now() < deadline);
	throw std::runtime_error("capture condition timed out: " + status.dump());
}
json Stopped(Capture& capture)
{
	return Await(capture, [](const json& status) { return status.at("workerDone") == true; });
}
std::uint64_t VerifyJournal(const json& status, bool needScope)
{
	const auto path = status.at("journalPath").get<std::string>();
	std::ifstream journal(std::filesystem::path(std::u8string(path.begin(), path.end())));
	Check(journal.is_open(), "journal missing");
	std::string line;
	std::uint64_t sequence = 0;
	bool completed = false;
	json last;
	while (std::getline(journal, line)) {
		auto event = json::parse(line);
		Check(event.at("sequence") == ++sequence, "journal lost ordering");
		Check(event.at("captureId") == status.at("captureId"), "journal producer session mismatch");
		completed |= event.at("kind") == "slow_scope";
		last = std::move(event);
	}
	Check(last.at("kind") == "capture_stopped", "missing terminal record");
	Check(status.at("lastRetainedSequence") == sequence, "history differs from journal");
	Check(!needScope || completed, "completed stall not durably recorded");
	return sequence;
}
int main()
{
	try {
		auto directory = std::filesystem::current_path() / ("stutter-fixture-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::create_directories(directory);
		std::atomic_bool fixtureShutdown{ false }, fixtureFailure{ false };
		Capture capture({ [&] { return std::optional(directory); }, [] { return json{ { "buildId", "fixture" } }; },
			[](const json& args) -> std::optional<json> {
				if (args.contains("expectedBuildId") && args.at("expectedBuildId") != "fixture")
					return json{ { "error", "producer mismatch" } };
				return std::nullopt;
			},
			[&] {
				if (fixtureFailure.load())
					throw std::runtime_error("fixture watchdog failure");
				return fixtureShutdown.load();
			} });
		const auto descriptor = Capture::Descriptor();
		Check(descriptor.at("inputSchema").at("properties").at("thresholdMs").at("minimum") == 10, "descriptor range differs");
		for (const auto* request : { "[]", "{", R"({"action":7})", R"({"unexpected":0})", R"({"thresholdMs":"50"})", R"({"durationSeconds":601})", R"({"thresholdMs":0})", R"({"action":"bogus"})", R"({"action":"start","expectedBuildId":"wrong"})" }) {
			const auto result = capture.Request(request);
			Check(result.contains("error") && result.at("producer").at("buildId") == "fixture", "bad input lost error or producer identity");
		}
		Check(capture.Request(std::string(8193, 'x').c_str()).contains("error"), "unbounded request accepted");
		struct Sink
		{
			unsigned calls = 0;
			std::string response;
		} sink;
		capture.Respond("{", &sink, [](void* context, const char* response) {
			auto& output = *static_cast<Sink*>(context);
			++output.calls;
			output.response = response;
		});
		Check(sink.calls == 1 && json::parse(sink.response).contains("error") && json::parse(sink.response).contains("producer"), "bridge callback did not return exactly one attributed error");
		sink.calls = 0;
		capture.Respond("{}", &sink, [](void* context, const char*) {
			++static_cast<Sink*>(context)->calls;
			throw std::runtime_error("fixture response writer failure");
		});
		Check(sink.calls == 1, "failed bridge writer was retried");
		Check(!capture.recorder.Enabled(), "invalid requests enabled recorder");
		auto start = capture.Request(R"({"action":"start","thresholdMs":10,"durationSeconds":2,"expectedBuildId":"fixture"})");
		Check(start.at("active") == true, "capture failed to start");
		Check(capture.Dispatch({ { "action", "start" } }).contains("error"), "active capture replaced");
		auto phase = capture.recorder.Begin(GetCurrentThreadId(), "Fixture::Outer", ReadCounter());
		auto shortChild = capture.recorder.Begin(GetCurrentThreadId(), "Fixture::ShortChild", ReadCounter());
		capture.recorder.End(shortChild, ReadCounter());
		auto live = Await(capture, [](const json& status) {
			for (const auto& event : status.at("events"))
				if (event.at("kind") == "active_stall_sample")
					return true;
			return false;
		});
		bool sample = false;
		for (const auto& event : live.at("events"))
			if (event.at("kind") == "active_stall_sample") {
				sample = true;
				Check(event.at("phase") == "Fixture::Outer" && event.at("threadId") == GetCurrentThreadId(), "wrong observed outer scope");
				Check(event.at("waitChain").contains("status") && event.at("threadCpu").contains("status"), "live evidence missing");
			}
		Check(sample, "watchdog waited for stalled thread to resume");
		capture.recorder.End(phase, ReadCounter());
		Await(capture, [](const json& status) {
			for (const auto& event : status.at("events"))
				if (event.at("kind") == "slow_scope")
					return true;
			return false;
		});
		const auto stopBegin = std::chrono::steady_clock::now();
		capture.Dispatch({ { "action", "stop" } });
		Check(std::chrono::steady_clock::now() - stopBegin < std::chrono::milliseconds(250), "stop blocked on OS cleanup");
		Check(!capture.recorder.Enabled(), "stop did not immediately freeze hook recording");
		auto stopped = Stopped(capture);
		Check(stopped.at("active") == false && stopped.at("journalFinalized") == true && stopped.at("failure").is_null(), "stop did not finalize cleanly");
		Check(stopped.at("finishReason") == "requested", "wrong stop reason");
		VerifyJournal(stopped, true);
		const auto oldPath = stopped.at("journalPath"), oldId = stopped.at("captureId");
		Check(capture.Dispatch({ { "action", "start" }, { "durationSeconds", 1 } }).at("active") == true, "restart failed");
		stopped = Stopped(capture);
		Check(stopped.at("finishReason") == "duration_limit" && stopped.at("journalFinalized") == true, "idle deadline failed");
		Check(stopped.at("journalPath") != oldPath && stopped.at("captureId") != oldId, "restart overwrote evidence");
		VerifyJournal(stopped, false);
		winrt::handle mutex(CreateMutexW(nullptr, TRUE, nullptr));
		Check(static_cast<bool>(mutex), "mutex creation failed");
		Check(capture.Dispatch({ { "action", "start" }, { "thresholdMs", 10 }, { "durationSeconds", 2 } }).at("active") == true, "mutex capture failed");
		std::atomic<DWORD> blockedThread{ 0 };
		std::jthread blocked([&] {
			auto token = capture.recorder.Begin(GetCurrentThreadId(), "Fixture::MutexWait", ReadCounter());
			blockedThread.store(GetCurrentThreadId(), std::memory_order_release);
			if (WaitForSingleObject(mutex.get(), 2000) == WAIT_OBJECT_0)
				ReleaseMutex(mutex.get());
			capture.recorder.End(token, ReadCounter());
		});
		auto chainStatus = Await(capture, [](const json& status) {
			for (const auto& event : status.at("events"))
				if (event.at("kind") == "wait_chain_result" || event.at("kind") == "wait_chain_timeout")
					return true;
			return false;
		});
		bool mutexDetail = false;
		for (const auto& event : chainStatus.at("events"))
			if (event.at("kind") == "wait_chain_result" && event.at("status") == "captured") {
				bool owner = false, lock = false;
				Check(event.at("threadId") == blockedThread.load(), "wait-chain result belongs to wrong thread");
				for (const auto& node : event.at("nodes")) {
					owner |= node.value("threadId", DWORD{ 0 }) == GetCurrentThreadId();
					lock |= node.at("typeName") == "mutex";
				}
				Check(owner && lock, "captured chain did not identify mutex owner");
				mutexDetail = true;
			}
		ReleaseMutex(mutex.get());
		blocked.join();
		capture.Dispatch({ { "action", "stop" } });
		stopped = Stopped(capture);
		Check(stopped.at("failure").is_null(), "mutex capture failed");
		std::cout << "Mutex wait chain: " << (mutexDetail ? "captured owner and lock" : "explicit unavailable/timeout evidence") << '\n';
		VerifyJournal(stopped, false);
		Check(capture.Dispatch({ { "action", "start" }, { "thresholdMs", 10 }, { "durationSeconds", 5 } }).at("active") == true, "record-bound capture failed");
		const auto frequency = capture.Dispatch(json::object()).at("qpcFrequency").get<Tick>();
		for (unsigned packet = 0; packet < 40 && capture.recorder.Enabled(); ++packet) {
			for (unsigned i = 0; i < 200; ++i) {
				auto token = capture.recorder.Begin(GetCurrentThreadId(), "Fixture::RecordLimit", ReadCounter());
				capture.recorder.End(token, token.phase.begin + frequency / 100);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		stopped = Stopped(capture);
		Check(stopped.at("finishReason") == "record_limit" && stopped.at("retainedEventCapacity") == 256, "record bound missing");
		Check(stopped.at("events").size() == 256 && stopped.at("firstRetainedSequence").get<unsigned>() > 1, "history was not bounded");
		Check(VerifyJournal(stopped, true) == 4096, "maximum journal record count exceeded or terminal record omitted");
		fixtureFailure.store(true);
		Check(!capture.Dispatch({ { "action", "start" } }).contains("error"), "failure-path capture did not start");
		stopped = Stopped(capture);
		Check(stopped.at("finishReason") == "capture_failed" && stopped.at("failure") == "fixture watchdog failure" && stopped.at("journalFinalized") == false, "worker error was hidden or reported as finalized");
		fixtureFailure.store(false);
		fixtureShutdown.store(true);
		Check(!capture.Dispatch({ { "action", "start" } }).contains("error"), "restart after worker failure failed");
		stopped = Stopped(capture);
		Check(stopped.at("finishReason") == "game_shutdown" && stopped.at("journalFinalized") == true, "shutdown lifecycle evidence missing");
		VerifyJournal(stopped, false);
		const auto retainedPath = stopped.at("journalPath");
		directory /= "missing-parent";
		auto failed = capture.Dispatch({ { "action", "start" } });
		Check(failed.contains("error") && failed.contains("producer"), "failed startup lost producer identity");
		Check(capture.Dispatch(json::object()).at("journalPath") == retainedPath, "failed file creation corrupted previous metadata");
		std::cout << "Direct capture API and synthetic Windows stutter tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
