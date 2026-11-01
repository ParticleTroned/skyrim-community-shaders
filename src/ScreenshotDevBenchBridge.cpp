#include "ScreenshotDevBenchBridge.h"

#include "Features/ScreenshotFeature.h"
#include "Globals.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchMainThreadDispatch.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <exception>
#	include <functional>

namespace
{
	using json = nlohmann::json;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run), CSX::Api::DevBenchDispatchErrorFormat::screenshot);
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
	void ScreenshotReferenceHandler(void* context, const char* args, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		json result;
		try {
			const auto request = json::parse(args ? args : "{}");
			auto* host = static_cast<DevBenchAPI::IDevBenchInterface001*>(context);
			result = RunOnMainThread([request, host] {
				return globals::features::screenshotFeature.HandleReferenceCapture(request, [host](const json& completion) {
					const auto text = completion.dump();
					host->EmitEvent("capture.ready", text.c_str());
				});
			});
			if (!result.value("ok", false))
				result = { { "error", result.at("error").is_string() ? result.at("error").get<std::string>() : result.at("error").value("message", "Reference capture failed") } };
		} catch (const std::exception& error) {
			result = { { "error", error.what() } };
		} catch (...) {
			result = { { "error", "Reference capture failed" } };
		}
		try {
			const auto text = result.dump();
			write(sink, text.c_str());
		} catch (...) {
			write(sink, R"({"error":"Reference result serialization failed"})");
		}
	}
}

namespace ScreenshotDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ScreenshotDevBenchBridge: devbench host not present; screenshot tool not registered");
			return;
		}
		// Do not consume the retry latch until the optional host is actually
		// present. DevBench and CSX may receive PostLoad in either order.
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		static constexpr const char* descriptor = R"({"description":"Versioned asynchronous SE/AE screenshot and image-sequence API. Contract v1 uses clientId and commandId for idempotency; request_get and events_poll expose source, queue, encoding, artifact and terminal receipts. Capture accepts one to four single-view desktop outputs, normalized crops, SDR PNG/BMP or HDR10 PNG. Sequence supports game-frame and wall-clock schedules, bounded backpressure, cancellation and verified manifests. Optional burst captures native SDR8 region atlases on consecutive accepted game frames, defers encoding until acquisition ends, and reports continuity.complete. Neural evidence freezes mono source-frame outcome and retains exact CPU exposure companions until terminal publication.","inputSchema":{"type":"object","required":["contractMajor","action","clientId","commandId"],"properties":{"contractMajor":{"const":1},"action":{"enum":["capabilities","status","settings_get","settings_validate","settings_apply","capture","sequence_start","sequence_stop","request_get","request_list","request_cancel","events_poll","acknowledge"]},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"requestId":{"type":"string"},"capture":{"type":"object"},"sequence":{"type":"object"},"useSettings":{"type":"boolean"},"patch":{"type":"object"},"scope":{"enum":["runtime_session","persistent_user"]},"afterEventId":{"type":"integer","minimum":0},"throughEventId":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1}}}})";
		devBench->RegisterTool("communityshaders.screenshot", descriptor, &ToolHandler, nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* referenceDesc = R"({"description":"Native desktop SDR reference PNG for SE/AE. Uses the absolute outputPath and requestId, never replaces files, and emits capture.ready after verified publication. Optional normalized subrect {x,y,w,h} crops native pixels without resizing. UI exclusion is reported from the actual source. HDR display capture is available through communityshaders.screenshot with hdr10_pq.","inputSchema":{"type":"object","required":["outputPath","requestId"],"properties":{"outputPath":{"type":"string"},"requestId":{"type":"string","minLength":1,"maxLength":128},"subrect":{"type":"object","properties":{"x":{"type":"number","minimum":0,"maximum":1},"y":{"type":"number","minimum":0,"maximum":1},"w":{"type":"number","exclusiveMinimum":0,"maximum":1},"h":{"type":"number","exclusiveMinimum":0,"maximum":1}}}}}})";
			devBench->RegisterToolExtension("capture", "communityshaders", referenceDesc, &ScreenshotReferenceHandler, devBench);
		}
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
