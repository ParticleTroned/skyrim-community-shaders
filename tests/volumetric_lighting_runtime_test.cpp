#define NOMINMAX
#include "Features/VolumetricLightingRuntime.h"
#include "Features/VolumetricLightingTuning.h"
#include "Utils/RendererOwnership.h"

#include <atomic>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <thread>

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
		bool IsEnabled() const { return enabled; }
		static bool IsInteriorWithSun(RE::Cell* testCell) { return testCell && testCell->interior && testCell->sun; }
	};
	CRITICAL_SECTION rendererLock;
	DWORD renderThread;
	int qualityCalls, flagCalls, clearCalls, uploads;
	bool enableFlag, raining;
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
			bool isVR = true;
			bool* bEnableVolumetricLighting = &enableFlag;
			int* renderer = &uploads;
		}
		namespace d3d
		{
			int* context = &uploads;
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
	void SetBooleanSettings(int flags, const std::string&, bool value)
	{
		RequireRenderer();
		++flagCalls;
		if (flags == 0)
			enableFlag = value;
	}
	bool IsRainTransitionActive() { return raining; }
	namespace LocationContext
	{
		bool AllowsEnabledLocations(bool interior, bool exterior, bool inside) { return inside ? interior : exterior; }
		template <class T>
		T SelectInteriorExterior(bool interior, const T& inside, const T& outside)
		{
			return interior ? inside : outside;
		}
	}
}

namespace Util
{
	CRITICAL_SECTION* GetRendererContextLock(int* renderer, int* context)
	{
		return renderer && context ? &rendererLock : nullptr;
	}
}

namespace
{
	struct VolumetricLighting
	{
		using GodrayProfile = VolumetricLightingTuning::Profile;
#include "vl_runtime_settings.h"
		Settings settings, runtimeSettings;
		mutable std::mutex settingsMutex;
		std::atomic_bool runtimeResetRequested{ true };
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
		bool TryGetActiveGodrayProfile(GodrayProfile& profile) const;
		void EarlyPrepass();
		void ApplyRuntimeTarget(const VolumetricLightingRuntime::Target&, const VolumetricLightingRuntime::Changes&);
		std::string GetName() const { return "Volumetric Lighting"; }
		int GetVLDescriptor() const
		{
			RequireRenderer();
			return 0;
		}
		void SetVLQuality(int, int)
		{
			RequireRenderer();
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
			globals::game::renderer = &uploads;
			globals::game::isVR = true;
			globals::d3d::context = &uploads;
			enableFlag = raining = false;
			qualityCalls = flagCalls = clearCalls = uploads = 0;
		}
		void Frame()
		{
			++testState.frameCount;
			vl.EarlyPrepass();
		}
		void NoEngineWork(const char* message)
		{
			const auto before = std::array{ qualityCalls, flagCalls, clearCalls, uploads };
			Frame();
			Require(before == std::array{ qualityCalls, flagCalls, clearCalls, uploads }, message);
		}
	};

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
			f.NoEngineWork("Save/load/menu gate allowed graphics mutation");
			*gate = false;
		}
		testPlayer.testCell = nullptr;
		f.NoEngineWork("Missing cell was treated as exterior");
		testPlayer.testCell = &testCell;
		testCell.attached = false;
		f.NoEngineWork("Detached destination was applied");
		testCell.attached = true;
		testCell.interior = true;
		f.NoEngineWork("Mismatched interior handoff was applied");
		testTes.interiorCell = &testCell;
		f.Frame();
		Require(enableFlag && f.vl.runtimeResetRequested == false && qualityCalls == 2, "Load reset was lost during blocked frames");
		testCell.interior = false;
		f.NoEngineWork("Stale TES interior was accepted as exterior");
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
		std::latch locked(1), release(1);
		std::thread owner([&] {
			Util::RendererOwnership ownership(&rendererLock, true);
			locked.count_down();
			release.wait();
		});
		locked.wait();
		f.NoEngineWork("Renderer lock contention did not defer");
		release.count_down();
		owner.join();
		Require(f.vl.runtimeResetRequested, "Failed ownership consumed reset request");
		f.Frame();
		Require(enableFlag && qualityCalls == 1, "Deferred request was lost");
		globals::d3d::context = nullptr;
		f.NoEngineWork("Absent context allowed graphics mutation");
	}

	void TestQualityRainAndRuntimeCompatibility()
	{
		Fixture f;
		f.Frame();
		f.vl.settings.DisableWeatherInteractionDuringRain = true;
		raining = true;
		f.Frame();
		Require(enableFlag && clearCalls == 1 && qualityCalls == 1, "Rain suppression changed main enable or quality");
		f.Frame();
		Require(clearCalls == 1, "Stable rain cleared history every frame");
		f.vl.settings.ExteriorQuality = 3;
		f.vl.settings.ExteriorCustomSize = { -1, 99999, 99999 };
		f.Frame();
		Require(f.vl.highSize == VolumetricLighting::TextureSize{ 32, 640, 640 } && qualityCalls == 2, "Unsafe custom dimensions reached native quality setter");
		f.vl.enabledAtBoot = false;
		f.Frame();
		Require(!enableFlag, "VR enabled VL without startup prerequisites");
		globals::game::isVR = false;
		f.Frame();
		Require(enableFlag, "SE/AE inherited VR-only startup restriction");
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
	try {
		TestCoalescingAndFrameBoundary();
		TestLoadAndDestinationGuards();
		TestRendererContention();
		TestQualityRainAndRuntimeCompatibility();
		TestConcurrentRequests();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		DeleteCriticalSection(&rendererLock);
		return 1;
	}
	DeleteCriticalSection(&rendererLock);
	return 0;
}
