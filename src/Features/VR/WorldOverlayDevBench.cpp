#include "WorldOverlayInternal.h"
#include "WorldOverlayRenderer.h"
#include <nlohmann/json.hpp>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include <DevBenchAPI.h>
#endif

namespace CSX::WorldOverlays
{
	nlohmann::json Diagnostics()
	{
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		std::uint64_t bytes = 0;
		std::uint32_t clients = 0, quads = 0;
		for (auto& c : s.clients) {
			clients += c.token ? 1u : 0u;
			quads += c.count;
			for (auto& p : c.pages) bytes += std::uint64_t(p.width) * p.height * 4;
		}
		return { { "service", API::ServiceName }, { "abiMajor", API::Major }, { "ready", SceneCaptured(s.scene) },
			{ "worldEpoch", s.epoch }, { "resourceGeneration", s.scene.generation }, { "sceneFrame", s.scene.frame },
			{ "cycle", s.scene.cycle }, { "clients", clients }, { "quads", quads }, { "atlasBytes", bytes },
			{ "atlasLeases", s.atlasLeases }, { "atlasUpdates", s.atlasUpdates }, { "publications", s.publications }, { "eyeDraws", s.draws },
			{ "rejected", s.rejected }, { "acceptedStereoPairs", s.acceptedPairs }, { "compositionCopies", s.compositionCopies }, { "captureCopies", s.captureCopies }, { "syntheticEnabled", s.syntheticEnabled } };
	}
	bool SetSynthetic(bool enabled, const API::Quad001& quad)
	{
		if (enabled && !Policy::ValidQuad(quad))
			return false;
		static std::mutex controlMutex;
		std::lock_guard controlLock(controlMutex);
		API::Client client = 0;
		std::uint64_t epoch;
		{
			std::lock_guard lock(Shared().mutex);
			client = Shared().syntheticClient;
			epoch = Shared().epoch;
		}
		if (enabled && !client && Interface().RegisterClient("CSX.DevBench.WorldOverlay", &client) != API::Status::Success)
			return false;
		if (!enabled && client)
			Interface().ClearBatch(client);
		std::lock_guard lock(Shared().mutex);
		Shared().syntheticEnabled = enabled && epoch == Shared().epoch;
		Shared().syntheticClient = client;
		Shared().synthetic = quad;
		UpdatePresence();
		return !enabled || Shared().syntheticEnabled;
	}
	void ServiceSynthetic()
	{
		API::Client client;
		API::Quad001 quad;
		{
			std::lock_guard lock(Shared().mutex);
			if (!Shared().syntheticEnabled)
				return;
			client = Shared().syntheticClient;
			quad = Shared().synthetic;
		}
		API::AtlasRequest001 request;
		request.width = request.height = 2;
		request.device = globals::d3d::device;
		request.immediateContext = globals::d3d::context;
		API::AtlasLease001 lease;
		if (Interface().BeginAtlasUpdate(client, &request, &lease, nullptr) != API::Status::Success)
			return;
		bool stillEnabled;
		{
			std::lock_guard lock(Shared().mutex);
			stillEnabled = Shared().syntheticEnabled && Shared().epoch == lease.worldEpoch;
		}
		if (!stillEnabled) {
			Interface().CancelAtlasUpdate(client, lease.contentRevision);
			return;
		}
		const float white[]{ 1, 1, 1, 1 };
		globals::d3d::context->ClearRenderTargetView(lease.target, white);
		static std::uint64_t sequence = 0;
		API::Batch001 batch;
		batch.count = 1;
		batch.sequence = ++sequence;
		batch.quads = &quad;
		batch.contentRevision = lease.contentRevision;
		batch.worldEpoch = lease.worldEpoch;
		batch.resourceGeneration = lease.resourceGeneration;
		if (Interface().PublishBatch(client, &batch, nullptr) != API::Status::Success)
			Interface().CancelAtlasUpdate(client, lease.contentRevision);
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	namespace
	{
		void Handler(void*, const char* args, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			try {
				const auto input = nlohmann::json::parse(args ? args : "{}");
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(input)) {
					write(sink, mismatch->dump().c_str());
					return;
				}
				const auto action = input.at("action").get<std::string>();
				bool ok = true;
				if (action == "synthetic") {
					API::Quad001 q;
					q.id = 1;
					const bool enabled = input.at("enabled").get<bool>();
					if (enabled) {
						const auto center = input.at("center").get<std::array<double, 3>>();
						std::copy(center.begin(), center.end(), q.center);
						q.width = input.value("width", 50.f);
						q.height = input.value("height", 15.f);
						q.occlusion = input.value("depth", true) ? API::Occlusion::SceneDepth : API::Occlusion::Disabled;
					}
					ok = SetSynthetic(enabled, q);
				} else if (action != "status")
					ok = false;
				auto result = Diagnostics();
				result["ok"] = ok;
				write(sink, result.dump().c_str());
			} catch (const std::exception& e) {
				const auto error = nlohmann::json{ { "ok", false }, { "error", e.what() } }.dump();
				write(sink, error.c_str());
			} catch (...) {
				write(sink, R"({"ok":false,"error":"world overlay command failed"})");
			}
		}
	}
#endif
	void InstallDevBench()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		static bool installed = false;
		if (installed)
			return;
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host)
			return;
		host->RegisterTool("communityshaders.world_overlays", R"({
"description":"Inspect direct VR world-overlay publication and rendering. Synthetic queues a white world-space billboard through the same atlas service; center, width and height use Skyrim units. Depth defaults on; depth=false is a diagnostic comparison. Disable explicitly after testing. No helper or NPC dialogue required.",
"inputSchema":{"type":"object","required":["action"],"properties":{
"action":{"type":"string","enum":["status","synthetic"]},"expectedBuildId":{"type":"string"},
"enabled":{"type":"boolean"},"center":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},
"width":{"type":"number","exclusiveMinimum":0,"maximum":100000},"height":{"type":"number","exclusiveMinimum":0,"maximum":100000},"depth":{"type":"boolean"}}}
})",
			Handler, nullptr);
		installed = true;
#endif
	}
}
