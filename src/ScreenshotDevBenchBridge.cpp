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
	void ScreenshotCaptureHandler(void* context, const char* args, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		json result;
		try {
			const auto request = json::parse(args ? args : "{}");
			auto* host = static_cast<DevBenchAPI::IDevBenchInterface001*>(context);
			if (!request.is_object())
				throw std::invalid_argument("capture arguments must be an object");
			const auto action = request.value("action", std::string("capture"));
			if (action != "capture" && action != "burst" && action != "burstStatus")
				throw std::invalid_argument("capture action must be capture, burst or burstStatus");
			result = RunOnMainThread([request, host, action] {
				if (action != "capture") {
					return globals::features::screenshotFeature.HandleProviderBurst(request, [host](const json& completion) {
						const auto text = completion.dump();
						host->EmitEvent(completion.at("done").get<bool>() ? "capture.burst.ready" : "capture.burst.progress", text.c_str());
					});
				}
				return globals::features::screenshotFeature.HandleReferenceCapture(request, [host](const json& completion) {
					const auto text = completion.dump();
					host->EmitEvent("capture.ready", text.c_str());
				});
			});
			if (!result.value("ok", false))
				result = { { "error", result.at("error").is_string() ? result.at("error").get<std::string>() : result.at("error").value("message", "Capture failed") } };
			else if (action != "capture")
				result = result.at("result").get<json>();
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
		static constexpr const char* descriptor = R"({"description":"Versioned asynchronous CSX screenshot and frame-sequence API. Every request uses contractMajor 1 plus clientId and commandId. useSettings expands still captures from the CSX screenshot eye/format settings and sequences from the CSX frame eye/format/count/cadence settings. Capture acceptance returns a stable requestId; request_get and events_poll communicate source, queue, encoding, artifact, sequence, and terminal outcomes. status is read-only. Optional sequence.burst buffers native region atlases before encoding; require terminal continuity.complete for temporal qualification. When NR captureFrameEvidence is enabled, accepted stereo acquisitions pin exact CPU exposure and private-measurement companions. Terminal receipts and sequence children contain immutable actual.captureDiagnostics (schemaVersion 1), with transactionId, exposures, measurementBatches and explicit unavailable reasons in measurementRequests. Retention is bounded to 32 keys per publisher, releases at terminal, performs no GPU waits and never changes frozen acquisition evidence. Sequence cadence starts at the first unpaused Present. Pause/loading holds scheduling; missed slots are explicit drops and clock regression stops the sequence. Already acquired frames drain; pending acquisitions interrupted by pause are dropped.","inputSchema":{"type":"object","required":["contractMajor","action","clientId","commandId"],"properties":{"contractMajor":{"type":"integer","const":1},"contractMinor":{"type":"integer","minimum":0},"action":{"type":"string","enum":["capabilities","status","settings_get","settings_validate","settings_apply","capture","sequence_start","sequence_stop","request_get","request_list","request_cancel","events_poll","acknowledge"]},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"requestId":{"type":"string"},"useSettings":{"type":"boolean","description":"For capture, expand current CSX screenshot settings. For sequence_start, set sequence.useSettings to expand current CSX frame-capture settings."},"capture":{"type":"object"},"sequence":{"type":"object","properties":{"burst":{"type":"object","description":"Native region atlas, every rendered frame; at most 240 frames, SDR8 only. Buffer GPU copies, then read back and encode after acquisition. Regions have equal widths, are relative to each oriented native eye, and stack vertically in array order. maximumBytes bounds raw staging payload, not driver/codec overhead. Any gap invalidates continuity.complete.","required":["regions"],"properties":{"maximumBytes":{"type":"integer","minimum":1,"maximum":536870912,"default":536870912},"regions":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"object","required":["x","y","width","height"],"properties":{"x":{"type":"integer","minimum":0,"maximum":16383},"y":{"type":"integer","minimum":0,"maximum":16383},"width":{"type":"integer","minimum":1,"maximum":16384},"height":{"type":"integer","minimum":1,"maximum":16384}},"additionalProperties":false}}},"additionalProperties":false},"schedule":{"type":"object","description":"Cadence starts at the first unpaused render observation. Paused/loading time is held. Missed slots are reported as drops, never captured in a catch-up burst. game_frames targets Present dispatch frames; wall_clock uses the first Present within each interval. Accepted VR eyes arrive separately and retain actual acquisition/lateness metadata.","properties":{"basis":{"enum":["game_frames","wall_clock"]},"intervalFrames":{"type":"integer","minimum":1},"intervalMs":{"type":"integer","minimum":1},"startDelayFrames":{"type":"integer","minimum":0},"startDelayMs":{"type":"integer","minimum":0},"pausePolicy":{"const":"hold","description":"Hold sequence scheduling during pause/loading; acquired images drain. An interrupted pending acquisition is reported as dropped. Bursts fail continuity if interrupted."}},"additionalProperties":false}}},"patch":{"type":"object"},"scope":{"type":"string","enum":["runtime_session","persistent_user"]},"afterEventId":{"type":"integer","minimum":0},"throughEventId":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1}}},"outputSchema":{"type":"object","properties":{"result":{"type":"object","properties":{"actual":{"type":"object","properties":{"captureDiagnostics":{"type":"object","properties":{"schemaVersion":{"const":1},"finalized":{"const":true},"transactionId":{"type":"string"},"exposures":{"type":"array","maxItems":9},"measurementBatches":{"type":"array","maxItems":8},"measurementRequests":{"type":"array","maxItems":8},"reason":{"type":"string"}}}}}}}}}})";
		devBench->RegisterTool("communityshaders.screenshot", descriptor, &ToolHandler, nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* referenceDesc = R"({"description":"Native PNG capture provider for CSX. action=capture (default) preserves the single reference capture contract. action=burst records 1..600 full native PNG frames through the same scheduler, admission, encoder and retention as communityshaders.screenshot. VR uses same-cycle side-by-side eyes; SE/AE uses desktop. Fields: requestId, outputDir (absolute), prefix (safe ASCII stem), count, everyNFrames (1..600, default 1), optional manifestPath (absolute .json). Frames are prefix_0000.png etc.; files are never replaced. Emits capture.burst.progress and capture.burst.ready; ready waits for verified frame and manifest publication. burstStatus uses the returned runId. apiRequestId works with communityshaders.screenshot request_get, sequence_stop and request_cancel. Repeating an identical requestId replays the retained operation; changed parameters conflict. IDs expire with the API session/retention. UI exclusion is not guaranteed (uiExcluded=false). Burst subrect and scaling are unsupported; scale must be 1. Pause/loading holds scheduling, and encoder pressure or missed slots can drop frames. This is an ordinary sequence, not the strict buffered sequence.burst continuity mode. Capture overhead is incurred only when requested. Single captures support normalized subrect {x,y,w,h}. Stop/cancel during final manifest publication returns alreadyFinalizing=true and leaves its immutable result unchanged. Host manifest file entries are relative to outputDir; gameTimeMs aliases monotonic wallMs, not simulation time. Combined cadence is bounded by the CSX API: (count-1)*everyNFrames must not exceed 216000 frames.","inputSchema":{"type":"object","properties":{"outputPath":{"type":"string"},"requestId":{"type":"string","minLength":1,"maxLength":128},"subrect":{"type":["object","null"],"description":"Finite normalized rectangle contained within the image: x+w and y+h must not exceed 1. Omitted coordinates use their defaults.","properties":{"x":{"type":"number","minimum":0,"maximum":1,"default":0},"y":{"type":"number","minimum":0,"maximum":1,"default":0},"w":{"type":"number","exclusiveMinimum":0,"maximum":1,"default":1},"h":{"type":"number","exclusiveMinimum":0,"maximum":1,"default":1}}},"action":{"enum":["capture","burst","burstStatus"],"default":"capture"},"outputDir":{"type":"string"},"prefix":{"type":"string","minLength":1,"maxLength":128},"count":{"type":"integer","minimum":1,"maximum":600},"everyNFrames":{"type":"integer","minimum":1,"maximum":600,"default":1,"description":"(count-1)*everyNFrames must be at most 216000, matching the CSX sequence span limit."},"manifestPath":{"type":"string"},"excludeUi":{"type":"boolean","default":true,"description":"Requested only; actual uiExcluded is false."},"scale":{"type":"number","const":1},"runId":{"type":"integer","minimum":1,"maximum":9007199254740991}},"allOf":[{"if":{"properties":{"action":{"const":"burst"}},"required":["action"]},"then":{"required":["requestId","outputDir","prefix","count"],"properties":{"subrect":{"type":"null"}}},"else":{"if":{"properties":{"action":{"const":"burstStatus"}},"required":["action"]},"then":{"required":["runId"]},"else":{"required":["outputPath","requestId"],"properties":{"subrect":{"type":"object"}}}}}]},"outputSchema":{"type":"object","properties":{"queued":{"type":"boolean"},"runId":{"type":"integer","minimum":1},"apiRequestId":{"type":"string","description":"Use with communityshaders.screenshot request_get, sequence_stop or request_cancel."},"done":{"type":"boolean"},"ok":{"type":"boolean","description":"Burst completion success, after both manifests commit; false while pending."},"manifest":{"type":["string","null"]},"error":{"type":["string","object"]}}}})";
			devBench->RegisterToolExtension("capture", "communityshaders", referenceDesc, &ScreenshotCaptureHandler, devBench);
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
