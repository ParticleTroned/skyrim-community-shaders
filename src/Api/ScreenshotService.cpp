#include "Api/ScreenshotService.h"

#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceRegistry.h"
#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include "VRAPI/CSserviceapi.h"
#include "VRAPI/CSscreenshotapi.h"

#include <SKSE/SKSE.h>
#include <nlohmann/json.hpp>

#include <limits>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace
{
	using CSX::ScreenshotAPI::Interface001;
	using CSX::ScreenshotAPI::Request001;
	using CSX::ScreenshotAPI::Response001;
	using CSX::ScreenshotAPI::Status;
	constexpr auto kMainThreadTimeout = std::chrono::seconds(5);

	std::optional<nlohmann::json> HandleOnRuntimeMainThread(nlohmann::json a_request)
	{
		auto handle = [request = std::move(a_request)]() mutable {
			return globals::features::screenshotFeature.HandleApiRequest(request);
		};
		if (CSX::Api::IsRuntimeMainThread())
			return handle();
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return std::nullopt;
		auto promise = std::make_shared<std::promise<nlohmann::json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		try {
			tasks->AddTask([promise, cancelled, handle = std::move(handle)]() mutable {
				CSX::Api::EnterRuntimeMainThreadTask();
				if (cancelled->load(std::memory_order_acquire))
					return;
				try {
					promise->set_value(handle());
				} catch (...) {
					try {
						promise->set_exception(std::current_exception());
					} catch (...) {
					}
				}
			});
		} catch (...) {
			return std::nullopt;
		}
		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return std::nullopt;
		}
		try {
			return future.get();
		} catch (...) {
			return std::nullopt;
		}
	}

	Status Dispatch(const void*, const Request001* a_request, Response001* a_response) noexcept
	{
		if (!a_response)
			return Status::kInvalidArgument;
		if (a_response->structSize < sizeof(Response001))
			return Status::kStructureTooSmall;
		a_response->status = Status::kInternalError;
		a_response->jsonUtf8 = nullptr;
		a_response->jsonBytes = 0;

		if (!a_request) {
			a_response->status = Status::kInvalidArgument;
			return a_response->status;
		}
		if (a_request->structSize < sizeof(Request001)) {
			a_response->status = Status::kStructureTooSmall;
			return a_response->status;
		}
		if (!a_request->jsonUtf8 || a_request->jsonBytes == 0) {
			a_response->status = Status::kInvalidArgument;
			return a_response->status;
		}
		try {
			const auto request = nlohmann::json::parse(
				a_request->jsonUtf8,
				a_request->jsonUtf8 + a_request->jsonBytes);
			if (!request.is_object()) {
				a_response->status = Status::kInvalidJson;
				return a_response->status;
			}

			const auto response = HandleOnRuntimeMainThread(request);
			if (!response) {
				a_response->status = Status::kServiceUnavailable;
				return a_response->status;
			}
			thread_local std::string responseStorage;
			responseStorage = response->dump();
			if (responseStorage.size() > std::numeric_limits<std::uint32_t>::max()) {
				a_response->status = Status::kInternalError;
				return a_response->status;
			}
			a_response->jsonUtf8 = responseStorage.c_str();
			a_response->jsonBytes = static_cast<std::uint32_t>(responseStorage.size());
			a_response->status = Status::kSuccess;
			return a_response->status;
		} catch (const nlohmann::json::parse_error&) {
			a_response->status = Status::kInvalidJson;
			return a_response->status;
		} catch (...) {
			a_response->status = Status::kInternalError;
			return a_response->status;
		}
	}

	Interface001& ServiceInterface()
	{
		static Interface001 service{
			.structSize = sizeof(Interface001),
			.major = CSX::ScreenshotAPI::ServiceMajor,
			.minor = CSX::ScreenshotAPI::ServiceMinor,
			.schemaRevision = CSX::ScreenshotAPI::SchemaRevision,
			.context = nullptr,
			.Dispatch = Dispatch,
		};
		return service;
	}
}

namespace CSX::Api
{
	void InitializeScreenshotService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const std::uint64_t capabilities =
				ServiceAPI::kCapabilityInspection |
				ServiceAPI::kCapabilityRuntimeMutation |
				ServiceAPI::kCapabilityPersistentMutation |
				ServiceAPI::kCapabilityAsynchronousOperations |
				ServiceAPI::kCapabilityEventStream;
			const auto status = GetProcessServiceRegistry().Register({
				ScreenshotAPI::ServiceName,
				ScreenshotAPI::ServiceMajor,
				ScreenshotAPI::ServiceMinor,
				ScreenshotAPI::SchemaRevision,
				capabilities,
				&ServiceInterface(),
			});
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register CSX screenshot service ({})", static_cast<std::uint32_t>(status));
			else
				logger::info("Registered CSX screenshot service ABI {}.{}", ScreenshotAPI::ServiceMajor, ScreenshotAPI::ServiceMinor);
		});
	}

	const ScreenshotAPI::Interface001* GetScreenshotService001()
	{
		InitializeScreenshotService();
		return &ServiceInterface();
	}

	nlohmann::json DispatchScreenshotServiceRequest(const nlohmann::json& a_request)
	{
		const auto requestText = a_request.dump();
		ScreenshotAPI::Request001 request;
		request.jsonUtf8 = requestText.data();
		request.jsonBytes = static_cast<std::uint32_t>(requestText.size());
		ScreenshotAPI::Response001 response;
		const auto* service = GetScreenshotService001();
		const auto status = service->Dispatch(service->context, &request, &response);
		if (status != ScreenshotAPI::Status::kSuccess || !response.jsonUtf8)
			return {
				{ "ok", false },
				{ "error", {
					{ "code", "transport_error" },
					{ "transportStatus", static_cast<std::uint32_t>(status) },
				} },
			};
		return nlohmann::json::parse(response.jsonUtf8, response.jsonUtf8 + response.jsonBytes);
	}
}
