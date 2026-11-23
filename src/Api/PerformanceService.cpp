#include "Api/PerformanceService.h"

#include "Api/DevBenchMainThreadDispatch.h"
#include "Api/JsonServiceDispatch.h"
#include "Api/PerformanceServicePolicy.h"
#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceFoundation.h"
#include "Api/ServiceRegistry.h"
#include "BuildProvenance.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceResultsUI.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Profiler.h"

#include <SKSE/SKSE.h>
#include <mutex>
#include <optional>

namespace
{
	using json = nlohmann::json;
	using namespace CSX;

	Api::ServiceFoundation& Foundation()
	{
		static Api::ServiceFoundation value({ PerformanceAPI::ServiceName, PerformanceAPI::ServiceMajor,
												PerformanceAPI::ServiceMinor, PerformanceAPI::SchemaRevision },
			{ .maximumCommands = 64, .maximumEvents = 0 });
		static std::once_flag initialized;
		std::call_once(initialized, [&] { value.SetServerMetadataProvider(BuildProvenance::GetProducer); });
		return value;
	}

	json ExecuteAction(const json& request)
	{
		if (!globals::menu || !globals::menu->initialized || !ImGui::GetCurrentContext())
			return { { "error", "Performance controls are not initialized." } };
		const auto action = request.at("action").get<std::string_view>();
		if (action == "status")
			return PerformanceTuningRenderer::GetApiMeasurementStatus(0, 0);
		if (action == "start_quick_scan" || action == "start_quick_scan_batch")
			return PerformanceTuningRenderer::StartApiQuickScan(action == "start_quick_scan_batch", request.value("recordFpsVR", false));
		if (action == "start_feature_cost")
			return PerformanceTuningRenderer::StartApiFeatureCostMeasurement(request.at("featureShortName").get<std::string>());
		if (action == "start_feature_costs")
			return PerformanceTuningRenderer::StartApiFeatureCostBatch();
		if (action == "start_upscaling_sweep")
			return PerformanceTuningRenderer::StartApiUpscalingCostSweep(request.value("matrix", "auto"), request.value("dlssPreset", ""));
		if (action == "set_feature_enabled")
			return PerformanceTuningRenderer::SetApiFeatureEnabled(request.at("featureShortName").get<std::string>(), request.at("enabled").get<bool>());
		if (action == "cancel")
			return PerformanceTuningRenderer::CancelApiMeasurements();
		return MenuUI::PerformanceResultsPage::Action(request);
	}

	json Execute(const json& request)
	{
		static std::string measurementOwner;
		static std::string ownedRun;
		const auto action = request.at("action").get<std::string_view>();
		const auto client = request.at("clientId").get<std::string>();
		const auto previousRun = PerformanceTuningRenderer::GetApiMeasurementRunId();
		if (action == "cancel" && (client != measurementOwner || ownedRun.empty() || ownedRun != previousRun))
			return { { "accepted", false }, { "error", "Only the measurement owner may cancel this run." } };
		if ((action.starts_with("start_") || action == "set_feature_enabled") && globals::profiler &&
			globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running)
			return { { "accepted", false }, { "errorCode", "profiler_busy" } };
		auto result = ExecuteAction(request);
		const auto currentRun = PerformanceTuningRenderer::GetApiMeasurementRunId();
		// Failed starts can still own a restoration phase; ownership follows the run.
		if (action.starts_with("start_") && !currentRun.empty() && currentRun != previousRun) {
			measurementOwner = client;
			ownedRun = currentRun;
		}
		if (action == "cancel" && result.value("accepted", false))
			measurementOwner.clear();
		return result;
	}

	std::optional<json> Handle(const json& request)
	{
		auto& foundation = Foundation();
		if (const auto* error = Api::PerformancePolicy::Validate(request))
			return foundation.MakeError(request, "invalid_request", error);
		if (const auto mismatch = BuildProvenance::ValidateExpectedBuild(request))
			return foundation.MakeError(request, "producer_mismatch", "Loaded CSX does not match expectedBuildId.");
		const auto invoke = [&](const json& command) {
			const auto run = [command] { return Execute(command); };
			auto result = Api::IsRuntimeMainThread() ? run() : Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), run);
			if (result.contains("error") && !result.contains("accepted"))
				return foundation.MakeError(command, "dispatch_failed", result.dump(), "dispatch", false);
			auto response = foundation.MakeEnvelope(command, true);
			response["result"] = std::move(result);
			return response;
		};
		// Status is fresh and creates no command journal entry; mutations can be retried safely.
		return Api::PerformancePolicy::ReadOnly(request.at("action").get<std::string_view>()) ?
		           invoke(request) :
		           foundation.Dispatch(request, invoke);
	}

	PerformanceAPI::Status Dispatch(const void*, const PerformanceAPI::Request001* request, PerformanceAPI::Response001* response) noexcept
	{
		return Api::DispatchJsonService(request, response, PerformanceAPI::MaximumRequestBytes, Handle);
	}
}

namespace CSX::Api
{
	void InitializePerformanceService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			static PerformanceAPI::Interface001 service;
#ifdef DEVBENCH_BRIDGE_ENABLED
			service.instrumentationFlags |= PerformanceAPI::kDeveloperDiagnostics;
#endif
#ifdef TRACY_SUPPORT
			service.instrumentationFlags |= PerformanceAPI::kTracy;
#endif
			service.Dispatch = Dispatch;
			const auto status = GetProcessServiceRegistry().Register({ PerformanceAPI::ServiceName,
				PerformanceAPI::ServiceMajor, PerformanceAPI::ServiceMinor, PerformanceAPI::SchemaRevision,
				ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityRuntimeMutation |
					ServiceAPI::kCapabilityPersistentMutation | ServiceAPI::kCapabilityAsynchronousOperations,
				&service });
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register CSX performance API ({})", static_cast<std::uint32_t>(status));
		});
	}
}
