#pragma once

#include "Api/MainThreadDispatchState.h"
#include "Api/RuntimeThreadAffinity.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace CSX::Api
{
	enum class DevBenchDispatchErrorFormat
	{
		standard,
		profiler,
		screenshot,
	};

	// Keep the existing wire error shapes while sharing the admission protocol.
	template <class TaskInterface>
	nlohmann::json RunDevBenchMainThreadTask(
		TaskInterface* a_tasks,
		std::function<nlohmann::json()> a_run,
		DevBenchDispatchErrorFormat a_format = DevBenchDispatchErrorFormat::standard,
		std::chrono::milliseconds a_timeout = std::chrono::milliseconds(5000))
	{
		using json = nlohmann::json;
		auto failure = [a_format](const char* a_code, const char* a_message, const char* a_detail = nullptr) -> json {
			if (a_format == DevBenchDispatchErrorFormat::profiler)
				return { { "_dispatchError", a_detail ? a_detail : a_message } };
			if (a_format == DevBenchDispatchErrorFormat::screenshot) {
				json error{ { "code", a_code }, { "message", a_detail ? a_detail : a_message } };
				if (std::string_view(a_code) == "dispatcher_timeout")
					error["retryable"] = true;
				return { { "ok", false }, { "error", std::move(error) } };
			}
			json error{ { "error", a_message } };
			if (a_detail)
				error["detail"] = a_detail;
			return error;
		};
		if (!a_tasks)
			return failure("dispatcher_unavailable", "SKSE task interface unavailable");
		try {
			return DispatchMainThreadTask(
				[a_tasks](auto a_task) { a_tasks->AddTask(std::move(a_task)); },
				[run = std::move(a_run)] {
					EnterRuntimeMainThreadTask();
					return run();
				},
				a_timeout);
		} catch (const MainThreadDispatchRejected&) {
			return failure("dispatcher_failed", "SKSE task queue rejected the main-thread task");
		} catch (const MainThreadDispatchTimeout&) {
			const auto message = "main thread did not run within " + std::to_string(a_timeout.count()) + "ms";
			return failure("dispatcher_timeout", message.c_str());
		} catch (const std::exception& e) {
			return failure("dispatcher_failed", "main-thread task failed", e.what());
		} catch (...) {
			return failure("dispatcher_failed", a_format == DevBenchDispatchErrorFormat::standard ? "main-thread task failed" : "unknown main-thread failure");
		}
	}
}
