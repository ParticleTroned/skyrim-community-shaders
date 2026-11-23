#include "Api/ScreenshotService.h"
#include "Api/JsonServiceDispatch.h"

#include "Api/MainThreadDispatchState.h"
#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceRegistry.h"
#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include "VRAPI/CSscreenshotapi.h"
#include "VRAPI/CSserviceapi.h"

#include <SKSE/SKSE.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>
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
		try {
			return CSX::Api::DispatchMainThreadTask(
				[tasks](auto a_task) { tasks->AddTask(std::move(a_task)); },
				[handle = std::move(handle)]() mutable {
					CSX::Api::EnterRuntimeMainThreadTask();
					return handle();
				},
				kMainThreadTimeout);
		} catch (...) {
			return std::nullopt;
		}
	}

	Status Dispatch(const void*, const Request001* request, Response001* response) noexcept
	{
		return CSX::Api::DispatchJsonService(request, response, CSX::ScreenshotAPI::MaximumRequestBytes, HandleOnRuntimeMainThread);
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
