#define NOMINMAX
#include "Features/VR/ViewRefresh.h"
#include "Features/VR/ViewRefreshPolicy.h"
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace globals
{
	struct State
	{
		uint32_t frameCount = 40;
		uint64_t generation = 8;
		uint64_t GetCompletedRenderTargetResourcePublicationGeneration() const { return generation; }
	} stateStorage;
	State* state = &stateStorage;
	namespace d3d
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(77);
	}
}
uint32_t currentThread = 99;
uint32_t ViewRefreshTestThreadId() { return currentThread; }
namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}

namespace CSX::ViewRefresh
{
	using json = nlohmann::json;
	struct Output
	{
		struct Handle
		{
			void* get() const { return reinterpret_cast<void*>(123); }
		} resource;
	};
	struct Runtime
	{
		std::mutex mutex;
		Settings settings;
		std::atomic<bool> serviceRequired{ false }, pendingPair{ false };
		uint64_t revision = 1, poseCycle = 100;
		SceneIdentity source, prepared, pairIdentity;
		ID3D11Device* device = nullptr;
		const char* reason = "Disabled";
		bool pairActive = false, pairRefreshed = false, reused = false;
		bool graphicsFailed = false, retryGraphics = false;
		StereoReceipt receipt;
		vr::HmdMatrix34_t displayedPose{};
		struct Eye
		{
			std::unique_ptr<Output> output;
		};
		std::array<Eye, 2> eyes;
		uint64_t renderedPairs = 0, reusedPairs = 0, correctedPairs = 0, failedPairs = 0;
	} runtime;
	uint32_t releases = 0;
	void ReleaseGraphics()
	{
		if (runtime.pairActive || runtime.pendingPair.load())
			throw std::runtime_error("Released graphics inside a prepared stereo transaction");
		++releases;
		runtime.source = runtime.prepared = {};
		runtime.device = nullptr;
		runtime.eyes = {};
	}
#define GetCurrentThreadId ViewRefreshTestThreadId
#include "view_refresh_identity.h"
#undef GetCurrentThreadId
#include "view_refresh_configure.h"
#include "view_refresh_disabled.h"
#include "view_refresh_submission.h"
	json Status() { return { { "enabled", GetSettings().enabled } }; }
#include "view_refresh_request.h"
}

namespace
{
	using namespace CSX::ViewRefresh;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Prepare(bool refreshed = false, bool reused = false)
	{
		globals::state = &globals::stateStorage;
		globals::stateStorage = {};
		globals::d3d::device = reinterpret_cast<ID3D11Device*>(77);
		currentThread = 99;
		runtime.settings = { true, true, refreshed };
		runtime.revision = 1;
		runtime.poseCycle = 100;
		runtime.source = runtime.prepared = runtime.pairIdentity = CurrentIdentity();
		runtime.device = globals::d3d::device;
		runtime.pairActive = true;
		runtime.pairRefreshed = refreshed;
		runtime.reused = reused;
		runtime.graphicsFailed = runtime.retryGraphics = false;
		runtime.serviceRequired = runtime.pendingPair = true;
		runtime.receipt = { 100 };
		runtime.renderedPairs = runtime.reusedPairs = runtime.correctedPairs = runtime.failedPairs = 0;
		for (auto& eye : runtime.eyes)
			eye.output = std::make_unique<Output>();
		releases = 0;
	}
	void AcceptedEyes(bool refreshed)
	{
		RecordSubmission(vr::Eye_Right, 100, true, refreshed);
		RecordSubmission(vr::Eye_Left, 100, true, refreshed);
	}
	void Completion()
	{
		for (bool refreshed : { false, true }) {
			Prepare(refreshed);
			AcceptedEyes(refreshed);
			EndSubmitPair(100);
			Require(runtime.source.complete && runtime.renderedPairs == 1 && runtime.correctedPairs == (refreshed ? 1u : 0u) && runtime.failedPairs == 0,
				"Actual accepted route did not determine fresh-pair completion");
		}
		Prepare(true, true);
		AcceptedEyes(true);
		EndSubmitPair(100);
		Require(!runtime.source.complete && runtime.reusedPairs == 1, "A reused image became source history");
		Prepare(true);
		RecordSubmission(vr::Eye_Left, 100, true, true);
		RecordSubmission(vr::Eye_Right, 100, true, false);
		EndSubmitPair(100);
		Require(!runtime.source.complete && runtime.failedPairs == 1 && runtime.correctedPairs == 0,
			"A fallback route counted as a corrected stereo pair");
		for (uint32_t changed = 0; changed < 6; ++changed) {
			Prepare(true);
			AcceptedEyes(true);
			switch (changed) {
			case 0:
				++globals::state->frameCount;
				break;
			case 1:
				++globals::state->generation;
				break;
			case 2:
				++currentThread;
				break;
			case 3:
				++runtime.poseCycle;
				break;
			case 4:
				globals::d3d::device = reinterpret_cast<ID3D11Device*>(78);
				break;
			case 5:
				globals::state = nullptr;
				break;
			}
			vr::VRTextureWithPose_t eye{};
			Require(!GetSubmission(vr::Eye_Left, 100, eye), "Prepared image survived a changed frame owner");
			EndSubmitPair(100);
			Require(!runtime.source.complete && runtime.failedPairs == 1, "Source completion survived a changed frame owner");
		}
	}
	void SettingsAndCleanup()
	{
		Prepare(true);
		Configure(GetSettings());
		Require(runtime.revision == 1 && !runtime.retryGraphics && releases == 0, "Unchanged settings invalidated graphics");
		Require(releases == 0 && !ServiceDisabled(), "Disabled service misclassified active work");
	}
	void DisablePreparedPair()
	{
		Prepare(true);
		Configure({});
		Require(ServiceDisabled() && releases == 0, "Disabling released an in-flight stereo pair");
		vr::VRTextureWithPose_t eye{};
		Require(GetSubmission(vr::Eye_Left, 100, eye) && eye.handle, "A toggle interrupted a prepared eye");
		AcceptedEyes(true);
		EndSubmitPair(100);
		Require(releases == 1 && !runtime.serviceRequired && !runtime.pendingPair && !runtime.source.complete,
			"Disabled resources were retained after the native pair completed");
		Prepare();
		runtime.pairActive = false;
		runtime.pendingPair = false;
		Configure({ true, false, false });
		Require(ServiceDisabled() && releases == 1 && !runtime.serviceRequired, "No selected modes retained graphics");
		Prepare(true);
		runtime.pairActive = false;
		Configure({});
		++globals::state->frameCount;
		Require(ServiceDisabled() && releases == 1 && !runtime.pendingPair,
			"An abandoned prepared pair retained disabled resources");
		Prepare();
		runtime.graphicsFailed = true;
		Configure(GetSettings());
		Require(runtime.retryGraphics && !runtime.graphicsFailed && runtime.revision == 2, "Failed graphics could not be retried");
	}
	void Requests()
	{
		Prepare();
		const auto original = GetSettings();
		for (const auto& request : { json::array(), json{ { "action", 1 } }, json{ { "action", "configure" }, { "enabled", 1 } },
				 json{ { "action", "status" }, { "enabled", false } }, json{ { "unknown", true } } }) {
			Require(Execute(request).contains("error") && GetSettings() == original, "Malformed request changed settings");
		}
		Require(!Execute({ { "action", "status" } }).contains("error"), "Read-only status request failed");
		REL::Module::vr = false;
		Require(Execute({ { "action", "configure" }, { "enabled", false } }).contains("error") && GetSettings() == original,
			"A flat-runtime request configured VR rendering");
		REL::Module::vr = true;
		Require(!Execute({ { "action", "configure" }, { "latePose", true } }).contains("error") && GetSettings().latePose,
			"Valid VR configuration failed");
	}
}

int main()
{
	try {
		Completion();
		SettingsAndCleanup();
		DisablePreparedPair();
		Requests();
		std::cout << "View Refresh real lifecycle: identity, routes, toggles, cleanup, retry and requests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
