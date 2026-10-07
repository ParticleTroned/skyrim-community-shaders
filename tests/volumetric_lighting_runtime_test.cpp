#define NOMINMAX
#include "Features/VolumetricLightingRuntime.h"
#include "Features/VolumetricLightingTuning.h"
#include "LocationContext.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/RuntimeToggle.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	namespace RE
	{
		struct Cell
		{
			bool interior = false, attached = true, sun = true;
			bool IsAttached() const { return attached; }
			bool IsInteriorCell() const { return interior; }
		};
		struct TES
		{
			Cell* interiorCell = nullptr;
		};
		struct PlayerCharacter
		{
			Cell* testCell = nullptr;
			Cell* GetParentCell() const { return testCell; }
			static PlayerCharacter* GetSingleton();
		};
	}
	struct State
	{
		uint32_t frameCount = 0;
		bool safe = false, engineBusy = false, menu = false, pendingPostLoadRuntimeReset = false;
		bool IsSaveLoadSafeModeActive() const { return safe; }
		bool IsEngineSaveLoadActivityActive() const { return engineBusy; }
		bool IsMainOrLoadingMenuOpen() const { return menu; }
	};
	struct InteriorSun
	{
		bool enabled = true;
		struct Settings
		{
			bool Enabled = true;
			float InteriorShadowDistance = 100.0f;
		} settings;
		std::atomic<bool> runtimeSettingsDirty{ true }, runtimeEnabled{ true }, isInteriorWithSun{ false };
		float nativeDistance = 100.0f, vanillaInteriorShadowDistance = 60.0f;
		float* gInteriorShadowDistance = &nativeDistance;
		void PostPostLoad();
		void EarlyPrepass();
		void SetRuntimeEnabled(bool enabled);
		static void SetShadowDistance(bool interior);
		bool IsEnabled() const { return enabled && runtimeEnabled.load(); }
		static bool IsInteriorWithSun(RE::Cell* testCell) { return testCell && testCell->interior && testCell->sun; }
	};
	CRITICAL_SECTION rendererLock;
	DWORD renderThread;
	int qualityCalls, flagCalls, clearCalls, uploads;
	int nativeQuality;
	bool enableFlag, weatherFlag, raining;
	std::function<void()> duringApply;
	struct Renderer
	{
		struct RuntimeData
		{
			ID3D11DeviceContext* context = nullptr;
		} data;
		RuntimeData& GetRuntimeData() { return data; }
		CRITICAL_SECTION& GetLock() { return rendererLock; }
	} testRenderer;
	struct ShadowState
	{
		void* shadowSceneNode[1]{ reinterpret_cast<void*>(1) };
	} testShadowState;
	struct InverseSquareLighting
	{
		struct Settings
		{
			bool Enabled = true;
		} settings;
		std::atomic<bool> runtimeSettingsDirty{ true }, runtimeEnabled{ true };
		unsigned lightUpdates = 0;
		void PostPostLoad();
		void EarlyPrepass();
		void SetRuntimeEnabled(bool enabled);
		void ApplyRuntimeStateToActiveLights();
	};
	unsigned shadowDistanceUpdates = 0;
	State testState;
	RE::TES testTes;
	RE::Cell testCell;
	RE::PlayerCharacter testPlayer;
	RE::PlayerCharacter* RE::PlayerCharacter::GetSingleton() { return &testPlayer; }
	namespace globals
	{
		State* state = &::testState;
		namespace game
		{
			RE::TES* tes = &::testTes;
			ShadowState* smState = &testShadowState;
			bool isVR = true;
			bool* bEnableVolumetricLighting = &enableFlag;
			Renderer* renderer = &testRenderer;
		}
		namespace d3d
		{
			ID3D11DeviceContext* context = nullptr;
		}
		namespace features
		{
			InteriorSun interiorSun;
		}
	}
	void RequireRenderer()
	{
		Require(GetCurrentThreadId() == renderThread, "Graphics work escaped onto the API caller thread");
		Require(rendererLock.RecursionCount > 0, "Graphics work did not own the renderer");
	}
	void InteriorSun::SetShadowDistance(bool)
	{
		RequireRenderer();
		++shadowDistanceUpdates;
	}
	void InverseSquareLighting::ApplyRuntimeStateToActiveLights()
	{
		RequireRenderer();
		++lightUpdates;
	}
	void SetBooleanSettings(int flags, const std::string&, bool value)
	{
		RequireRenderer();
		++flagCalls;
		if (flags == 0)
			enableFlag = value;
		else
			weatherFlag = value;
		if (auto callback = std::exchange(duringApply, {}))
			callback();
	}
	bool IsRainTransitionActive() { return raining; }

	struct VolumetricLighting
	{
		using GodrayProfile = VolumetricLightingTuning::Profile;
#include "vl_runtime_settings.h"
		Settings settings, runtimeSettings;
		mutable std::mutex settingsMutex;
		std::atomic_bool runtimeResetRequested{ true };
		std::atomic_bool runtimeReady{ false };
		VolumetricLightingRuntime::Controller runtimeController;
		bool enabledAtBoot = true, runtimeEnabled = false, initialised = false, inInterior = false, blurDimensionsValid = false;
		TextureSize defaultSizeHigh, highSize;
		TextureSize* gVolumetricLightingSizeHigh = &highSize;
		int hiddenVREnableSettings = 0, hiddenVRWeatherUpdateSettings = 1;
		enum class Quality : uint8_t
		{
			Low,
			Medium,
			High,
			Custom
		};
		static int32_t ClampQualityIndex(int32_t quality);
		static TextureSize ClampTextureSize(const TextureSize& size);
		void SanitizeSettings();
		bool IsExteriorEnabled() const;
		void SetExteriorEnabled(bool enabled);
		void RequestRuntimeReset();
		bool IsPerformanceCostMeasurementReady() const;
		bool IsPerformanceCostMeasurementEnabled() const;
		bool IsPerformanceToggleEnabled() const;
		void SetPerformanceToggleEnabled(bool enabled);
		bool IsRuntimeTransitionBlocked() const;
		bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
		void EarlyPrepass();
		void ApplyRuntimeTarget(const VolumetricLightingRuntime::Target&, const VolumetricLightingRuntime::Changes&);
		std::string GetName() const { return "Volumetric Lighting"; }
		int GetVLDescriptor() const
		{
			RequireRenderer();
			return 0;
		}
		void SetVLQuality(int, int quality)
		{
			RequireRenderer();
			nativeQuality = quality;
			++qualityCalls;
		}
		void ClearVolumetricLightingTargets()
		{
			RequireRenderer();
			++clearCalls;
		}
		void UpdateBlurDimensions()
		{
			RequireRenderer();
			++uploads;
			blurDimensionsValid = true;
		}
	};

#include "runtime.h"

	struct Fixture
	{
		VolumetricLighting vl;
		Fixture()
		{
			testState = {};
			testTes = {};
			testCell = {};
			testPlayer.testCell = &testCell;
			globals::state = &testState;
			globals::game::tes = &testTes;
			globals::game::renderer = &testRenderer;
			globals::game::isVR = true;
			globals::game::bEnableVolumetricLighting = &enableFlag;
			globals::features::interiorSun.enabled = true;
			globals::d3d::context = reinterpret_cast<ID3D11DeviceContext*>(&uploads);
			testRenderer.data.context = globals::d3d::context;
			enableFlag = weatherFlag = raining = false;
			duringApply = {};
			nativeQuality = -1;
			qualityCalls = flagCalls = clearCalls = uploads = 0;
		}
		void Frame()
		{
			++testState.frameCount;
			vl.EarlyPrepass();
		}
		void DeferredTransition(const char* message, bool rendererAvailable = true)
		{
			const auto before = std::array{ qualityCalls, flagCalls, clearCalls };
			const auto previousUploads = uploads;
			Frame();
			Require(before == std::array{ qualityCalls, flagCalls, clearCalls }, message);
			Require(uploads == previousUploads + (rendererAvailable ? 1 : 0), "Deferred toggle interrupted frame blur maintenance");
			Require(vl.blurDimensionsValid == rendererAvailable, "Blur validity does not match renderer ownership");
			Require(!vl.IsPerformanceCostMeasurementReady(), "Deferred frame remained measurement-ready");
		}
	};

	void TestDisabledNativeStartup()
	{
		Fixture f;
		InteriorSun sun;
		InverseSquareLighting inverse;
		sun.SetRuntimeEnabled(false);
		inverse.SetRuntimeEnabled(false);
		sun.PostPostLoad();
		inverse.PostPostLoad();
		testState.menu = true;
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(!sun.IsEnabled() && !inverse.runtimeEnabled, "native hooks must honor disabled settings before the first safe frame");
		Require(sun.runtimeSettingsDirty && inverse.runtimeSettingsDirty, "startup must retain deferred engine reconciliation");
	}

	void TestNativeLightingToggles()
	{
		Fixture f;
		InteriorSun sun;
		InverseSquareLighting inverse;
		shadowDistanceUpdates = 0;
		sun.SetRuntimeEnabled(false);
		inverse.SetRuntimeEnabled(false);
		Require(shadowDistanceUpdates == 0 && inverse.lightUpdates == 0, "native setters must only request changes");
		for (bool* guard : { &testState.safe, &testState.engineBusy, &testState.menu, &testState.pendingPostLoadRuntimeReset }) {
			*guard = true;
			sun.EarlyPrepass();
			inverse.EarlyPrepass();
			Require(sun.runtimeEnabled && inverse.runtimeEnabled, "native toggles must defer protected engine state");
			*guard = false;
		}
		testCell.attached = false;
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(shadowDistanceUpdates == 0 && inverse.lightUpdates == 0, "unattached destination must retain native changes");
		testCell.attached = true;
		testTes.interiorCell = &testCell;
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(shadowDistanceUpdates == 0 && inverse.lightUpdates == 0, "inconsistent destination must retain native changes");
		testTes.interiorCell = nullptr;
		auto* context = globals::d3d::context;
		globals::d3d::context = nullptr;
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(shadowDistanceUpdates == 0 && inverse.lightUpdates == 0, "missing renderer must retain native changes");
		globals::d3d::context = context;
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(shadowDistanceUpdates == 1 && inverse.lightUpdates == 1 && sun.nativeDistance == 60, "safe render boundary must apply native changes");
		sun.SetRuntimeEnabled(true);
		sun.SetRuntimeEnabled(false);
		inverse.SetRuntimeEnabled(true);
		inverse.SetRuntimeEnabled(false);
		sun.EarlyPrepass();
		inverse.EarlyPrepass();
		Require(shadowDistanceUpdates == 1 && inverse.lightUpdates == 1, "opposing native requests must avoid redundant work");
	}

	void TestNativeLocationDuringDeferral()
	{
		Fixture f;
		InteriorSun sun;
		testCell.interior = true;
		testTes.interiorCell = &testCell;
		sun.EarlyPrepass();
		Require(sun.isInteriorWithSun, "interior sun must classify the active cell");
		testState.safe = true;
		sun.SetRuntimeEnabled(false);
		sun.EarlyPrepass();
		Require(sun.isInteriorWithSun && sun.runtimeEnabled, "saving must preserve applied interior lighting");
		testCell.interior = false;
		testTes.interiorCell = nullptr;
		sun.EarlyPrepass();
		Require(!sun.isInteriorWithSun && sun.runtimeEnabled, "destination classification must advance while the toggle is deferred");
	}

	void TestCoalescingAndFrameBoundary()
	{
		Fixture f;
		f.vl.SetExteriorEnabled(false);
		f.Frame();
		const auto before = std::array{ qualityCalls, flagCalls, clearCalls };
		std::thread worker([&] { f.vl.SetExteriorEnabled(true); f.vl.SetExteriorEnabled(false); });
		worker.join();
		Require(!f.vl.IsExteriorEnabled(), "Getter must reflect pending requested state");
		f.Frame();
		Require(before == std::array{ qualityCalls, flagCalls, clearCalls }, "Cancelled toggle burst performed graphics work");
		f.vl.SetExteriorEnabled(true);
		Require(!f.vl.IsPerformanceCostMeasurementReady(), "Measurement admitted an unapplied toggle");
		f.vl.EarlyPrepass();
		Require(!enableFlag, "Second prepass changed the same frame");
		f.Frame();
		Require(enableFlag && qualityCalls == 1 && clearCalls == 1, "Enable repeated quality setup or history clear");
		Require(f.vl.IsPerformanceCostMeasurementReady(), "Applied toggle never became measurement-ready");
		f.vl.SetExteriorEnabled(false);
		VolumetricLighting::GodrayProfile profile;
		Require(f.vl.TryGetActiveGodrayProfile(profile), "Pending disable changed the current frame profile");
		f.Frame();
		Require(!enableFlag && clearCalls == 2 && qualityCalls == 1, "Disable did not clear exactly once");
		Require(!f.vl.TryGetActiveGodrayProfile(profile), "Disabled runtime retained its profile");
	}

	void TestLoadAndDestinationGuards()
	{
		Fixture f;
		f.Frame();
		f.vl.RequestRuntimeReset();
		f.vl.SetExteriorEnabled(false);
		for (auto* gate : { &testState.safe, &testState.engineBusy, &testState.menu, &testState.pendingPostLoadRuntimeReset }) {
			*gate = true;
			f.DeferredTransition("Save/load/menu gate allowed graphics mutation");
			*gate = false;
		}
		testPlayer.testCell = nullptr;
		f.DeferredTransition("Missing cell was treated as exterior");
		globals::game::tes = nullptr;
		f.DeferredTransition("Missing TES allowed a transition");
		globals::game::tes = &testTes;
		testPlayer.testCell = &testCell;
		testCell.attached = false;
		f.DeferredTransition("Detached destination was applied");
		testCell.attached = true;
		testCell.interior = true;
		f.DeferredTransition("Mismatched interior handoff was applied");
		testTes.interiorCell = &testCell;
		f.Frame();
		Require(enableFlag && f.vl.runtimeResetRequested == false && qualityCalls == 2, "Load reset was lost during blocked frames");
		testCell.interior = false;
		f.DeferredTransition("Stale TES interior was accepted as exterior");
		testTes.interiorCell = nullptr;
		f.Frame();
		Require(!enableFlag && clearCalls == 1, "Interior-to-exterior transition did not apply once");
		f.vl.RequestRuntimeReset();
		f.Frame();
		Require(qualityCalls == 3 && clearCalls == 2, "Same-location load did not reconcile state");
	}

	void TestRendererContention()
	{
		Fixture f;
		std::latch locked(1);
		{
			std::jthread owner([&](std::stop_token stop) {
				Util::RendererOwnership ownership(&rendererLock, true);
				std::mutex waitMutex;
				std::condition_variable_any release;
				std::unique_lock waitLock(waitMutex);
				locked.count_down();
				release.wait(waitLock, stop, [] { return false; });
			});
			locked.wait();
			f.DeferredTransition("Renderer lock contention did not defer", false);
		}
		Require(f.vl.runtimeResetRequested, "Failed ownership consumed reset request");
		f.Frame();
		Require(enableFlag && qualityCalls == 1, "Deferred request was lost");
		globals::d3d::context = nullptr;
		f.DeferredTransition("Absent context allowed graphics mutation", false);
		globals::d3d::context = reinterpret_cast<ID3D11DeviceContext*>(&qualityCalls);
		f.DeferredTransition("Mismatched native context allowed graphics mutation", false);
		globals::d3d::context = testRenderer.data.context;
		globals::game::renderer = nullptr;
		f.DeferredTransition("Missing renderer allowed graphics mutation", false);
	}

	void TestReadinessDuringDeferral()
	{
		Fixture f;
		f.Frame();
		for (auto* gate : { &testState.engineBusy, &testState.menu, &testState.pendingPostLoadRuntimeReset, &testState.safe }) {
			Require(f.vl.IsPerformanceCostMeasurementReady(), "Settled frame was not ready");
			*gate = true;
			Require(!f.vl.IsPerformanceCostMeasurementReady(), "Measurement ignored an active lifecycle guard");
			f.DeferredTransition("Unchanged settings bypassed the lifecycle guard");
			*gate = false;
			Require(!f.vl.IsPerformanceCostMeasurementReady(), "Measurement resumed before a safe prepass");
			f.Frame();
		}
		testCell.attached = false;
		f.DeferredTransition("Detached cell allowed settled-state measurement");
		testCell.attached = true;
		f.Frame();
		Require(f.vl.IsPerformanceCostMeasurementReady(), "Ready destination never recovered");
	}

	void TestRequestsDuringApplication()
	{
		Fixture f;
		f.vl.SetExteriorEnabled(false);
		f.Frame();
		f.vl.SetExteriorEnabled(true);
		duringApply = [&] {
			auto worker = std::async(std::launch::async, [&] {
				f.vl.SetExteriorEnabled(false);
				f.vl.RequestRuntimeReset();
			});
			worker.get();
			Require(!f.vl.IsPerformanceCostMeasurementReady(), "In-progress transaction was measurement-ready");
		};
		f.Frame();
		Require(enableFlag && !f.vl.IsExteriorEnabled(), "Request during application changed the current transaction");
		Require(f.vl.runtimeResetRequested && !f.vl.IsPerformanceCostMeasurementReady(), "Publication lost the concurrent reset");
		f.Frame();
		Require(!enableFlag && qualityCalls == 2 && !f.vl.runtimeResetRequested, "Concurrent request did not reconcile on the next frame");
	}

	void TestQualityRainAndRuntimeCompatibility()
	{
		Fixture f;
		f.Frame();
		f.vl.settings.DisableWeatherInteractionDuringRain = true;
		raining = true;
		f.Frame();
		Require(enableFlag && !weatherFlag && clearCalls == 1 && qualityCalls == 1, "Rain suppression changed main enable or quality");
		f.Frame();
		Require(clearCalls == 1, "Stable rain cleared history every frame");
		f.vl.settings.ExteriorQuality = 3;
		f.vl.settings.ExteriorCustomSize = { -1, 99999, 99999 };
		f.Frame();
		Require(f.vl.highSize == VolumetricLighting::TextureSize{ 32, 640, 640 } && qualityCalls == 2 && nativeQuality == 2, "Unsafe custom dimensions reached native quality setter");
		f.vl.enabledAtBoot = false;
		f.Frame();
		Require(!enableFlag, "VR enabled VL without startup prerequisites");
		Require(!f.vl.IsPerformanceCostMeasurementEnabled(), "VR measured an unavailable startup configuration");
		globals::game::isVR = false;
		f.Frame();
		Require(enableFlag, "SE/AE inherited VR-only startup restriction");
		Require(f.vl.IsPerformanceCostMeasurementEnabled(), "Active SE/AE lighting was not measurable");
		testCell.interior = true;
		testCell.sun = false;
		testTes.interiorCell = &testCell;
		f.Frame();
		Require(!enableFlag && !f.vl.IsPerformanceCostMeasurementEnabled(), "Interior without sunlight was measured as enabled");
	}

	void TestConcurrentRequests()
	{
		Fixture f;
		std::atomic_bool done{ false };
		std::thread worker([&] {
			for (int i = 0; i < 10000; ++i) {
				f.vl.SetExteriorEnabled(i % 2 == 0);
				(void)f.vl.IsExteriorEnabled();
			}
			f.vl.SetExteriorEnabled(false);
			done.store(true);
		});
		while (!done.load())
			f.Frame();
		worker.join();
		f.Frame();
		Require(!enableFlag && !f.vl.IsExteriorEnabled(), "Concurrent final request was overwritten by render publication");
	}
}

int main()
{
	InitializeCriticalSection(&rendererLock);
	renderThread = GetCurrentThreadId();
	bool passed = true;
	for (const auto& [name, test] : std::array{
			 std::pair{ "Disabled native startup", &TestDisabledNativeStartup },
			 std::pair{ "Native location during deferral", &TestNativeLocationDuringDeferral },
			 std::pair{ "Native lighting toggles", &TestNativeLightingToggles },
			 std::pair{ "Coalescing and frame boundary", &TestCoalescingAndFrameBoundary },
			 std::pair{ "Load and destination guards", &TestLoadAndDestinationGuards },
			 std::pair{ "Renderer contention", &TestRendererContention },
			 std::pair{ "Readiness during deferral", &TestReadinessDuringDeferral },
			 std::pair{ "Requests during application", &TestRequestsDuringApplication },
			 std::pair{ "Quality, rain and runtime compatibility", &TestQualityRainAndRuntimeCompatibility },
			 std::pair{ "Concurrent requests", &TestConcurrentRequests } }) {
		try {
			test();
		} catch (const std::exception& error) {
			std::cerr << name << ": " << error.what() << '\n';
			passed = false;
		}
	}
	DeleteCriticalSection(&rendererLock);
	return passed ? 0 : 1;
}
