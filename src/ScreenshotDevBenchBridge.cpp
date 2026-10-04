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
		static constexpr const char* descriptor = R"({"description":"Versioned asynchronous CSX screenshot and frame-sequence API. Every request uses contractMajor 1 plus clientId and commandId. useSettings expands still captures from the CSX screenshot eye/format settings and sequences from the CSX frame eye/format/count/cadence settings. Capture acceptance returns a stable requestId; request_get and events_poll communicate source, queue, encoding, artifact, sequence, and terminal outcomes. status is read-only and reports independent bounded destination-preparation and manifest-publication lanes. A stop or cancellation during active synchronous filesystem I/O remains pending until that call returns. Optional sequence.burst buffers native region atlases before encoding; require terminal continuity.complete for temporal qualification.","inputSchema":{"type":"object","required":["contractMajor","action","clientId","commandId"],"properties":{"contractMajor":{"type":"integer","const":1},"contractMinor":{"type":"integer","minimum":0},"action":{"type":"string","enum":["capabilities","status","settings_get","settings_validate","settings_apply","capture","sequence_start","sequence_stop","request_get","request_list","request_cancel","events_poll","acknowledge"]},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"requestId":{"type":"string"},"useSettings":{"type":"boolean","description":"For capture, expand current CSX screenshot settings. For sequence_start, set sequence.useSettings to expand current CSX frame-capture settings."},"capture":{"type":"object"},"sequence":{"type":"object","properties":{"burst":{"type":"object","description":"Native region atlas, every rendered frame; at most 240 frames, SDR8 only. Buffer GPU copies, then read back and encode after acquisition. Regions have equal widths, are relative to each oriented native eye, and stack vertically in array order. maximumBytes bounds raw staging payload, not driver/codec overhead. Any gap invalidates continuity.complete.","required":["regions"],"properties":{"maximumBytes":{"type":"integer","minimum":1,"maximum":536870912,"default":536870912},"regions":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"object","required":["x","y","width","height"],"properties":{"x":{"type":"integer","minimum":0,"maximum":16383},"y":{"type":"integer","minimum":0,"maximum":16383},"width":{"type":"integer","minimum":1,"maximum":16384},"height":{"type":"integer","minimum":1,"maximum":16384}},"additionalProperties":false}}},"additionalProperties":false}},"description":"sequence_start returns preparing before destination I/O. Preparation has a separate bounded worker; capabilities publishes lane capacities. Relative sequence destinations traverse from a retained approved root without following reparse components; resolvedDirectory records the retained parent. Queued preparation may be cancelled before I/O; active I/O must finish before terminal receipt."},"patch":{"type":"object"},"scope":{"type":"string","enum":["runtime_session","persistent_user"]},"afterEventId":{"type":"integer","minimum":0},"throughEventId":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1}}}})";
		devBench->RegisterTool("communityshaders.screenshot", descriptor, &ToolHandler, nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* referenceDesc = R"({"description":"Native SDR reference PNG from Screenshot: same-cycle side-by-side eyes in VR, desktop in SE/AE. Honors the host's absolute outputPath and requestId, never replaces files, and emits capture.ready after verified publication. UI exclusion is not guaranteed. Use the base capture tool's golden/threshold/regions or replay goldens for scoring; temporal sequences use communityshaders.screenshot.","inputSchema":{"type":"object","required":["outputPath","requestId"],"properties":{"outputPath":{"type":"string"},"requestId":{"type":"string","minLength":1,"maxLength":128}}}})";
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
