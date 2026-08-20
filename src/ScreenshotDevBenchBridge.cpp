#include "ScreenshotDevBenchBridge.h"

#include "Features/ScreenshotFeature.h"
#include "Globals.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <future>
#	include <functional>

namespace
{
	using json = nlohmann::json;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return { { "ok", false }, { "error", { { "code", "dispatcher_unavailable" }, { "message", "SKSE task interface unavailable" } } } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		tasks->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "ok", false }, { "error", { { "code", "dispatcher_failed" }, { "message", e.what() } } } });
			} catch (...) {
				promise->set_value(json{ { "ok", false }, { "error", { { "code", "dispatcher_failed" }, { "message", "unknown main-thread failure" } } } });
			}
		});
		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "ok", false }, { "error", { { "code", "dispatcher_timeout" }, { "message", "main thread did not run within 5000ms" }, { "retryable", true } } } };
		}
		return future.get();
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json request = json::object();
			if (a_argsJson && *a_argsJson)
				request = json::parse(a_argsJson);
			if (!request.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = RunOnMainThread([request = std::move(request)]() {
				return globals::features::screenshotFeature.HandleApiRequest(request);
			});
		} catch (const std::exception& e) {
			output = { { "ok", false }, { "error", { { "code", "invalid_json" }, { "message", e.what() } } } };
		} catch (...) {
			output = { { "ok", false }, { "error", { { "code", "invalid_json" }, { "message", "unknown request parse failure" } } } };
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace ScreenshotDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ScreenshotDevBenchBridge: devbench host not present; screenshot tool not registered");
			return;
		}
		static constexpr const char* descriptor = R"({"description":"Versioned asynchronous CSX screenshot and frame-sequence API. Every request uses contractMajor 1 plus clientId and commandId. Capture acceptance returns a stable requestId; request_get and events_poll communicate source, queue, encoding, artifact, sequence, and terminal outcomes. status is read-only.","inputSchema":{"type":"object","required":["contractMajor","action","clientId","commandId"],"properties":{"contractMajor":{"type":"integer","const":1},"contractMinor":{"type":"integer","minimum":0},"action":{"type":"string","enum":["capabilities","status","settings_get","settings_validate","settings_apply","capture","sequence_start","sequence_stop","request_get","request_list","request_cancel","events_poll","acknowledge"]},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"requestId":{"type":"string"},"useSettings":{"type":"boolean"},"capture":{"type":"object"},"sequence":{"type":"object"},"patch":{"type":"object"},"scope":{"type":"string","enum":["runtime_session","persistent_user"]},"afterEventId":{"type":"integer","minimum":0},"throughEventId":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1}}}})";
		devBench->RegisterTool("communityshaders.screenshot", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("ScreenshotDevBenchBridge: registered communityshaders.screenshot with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace ScreenshotDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
