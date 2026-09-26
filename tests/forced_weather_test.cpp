#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

namespace RE
{
	struct TESWeather
	{};
	struct Property
	{
		virtual ~Property() = default;
	};
	struct BSRenderPass
	{
		bool blendPass = true;
	};
	struct BSSkyShaderProperty : Property
	{
		float blend = 0.5f;
		std::int32_t lastRenderPassState = 0;
		std::shared_ptr<BSRenderPass> cachedPass = std::make_shared<BSRenderPass>();
		int clears = 0, rebuilds = 0;
		void DoClearRenderPasses()
		{
			cachedPass.reset();
			lastRenderPassState = (std::numeric_limits<std::int32_t>::max)();
			++clears;
		}
		bool UsesBlendPass()
		{
			if (lastRenderPassState == (std::numeric_limits<std::int32_t>::max)()) {
				cachedPass = std::make_shared<BSRenderPass>(blend > 0.0f);
				lastRenderPassState = 0;
				++rebuilds;
			}
			return cachedPass->blendPass;
		}
	};
	struct Geometry
	{
		struct
		{
			std::shared_ptr<Property> shaderProperty;
		} data;
		auto& GetGeometryRuntimeData() { return data; }
	};
	struct Clouds
	{
		std::array<std::shared_ptr<Geometry>, 32> clouds{};
	};
	struct NiNode
	{
		std::shared_ptr<NiNode> child;
		int detaches = 0;
		void DetachChild(NiNode* node)
		{
			Check(child.get() == node, "detached a different sky node");
			child.reset();
			++detaches;
		}
	};
	struct ModelDBHandle
	{
		struct U_Entry
		{
			int references = 1;
		};
		U_Entry* entry = nullptr;
		U_Entry* get() const { return entry; }
		explicit operator bool() const { return entry != nullptr; }
	};
	struct Sky
	{
		static inline Sky* singleton = nullptr;
		static Sky* GetSingleton() { return singleton; }
		std::shared_ptr<NiNode> root = std::make_shared<NiNode>(), auroraRoot;
		ModelDBHandle auroraModel;
		Clouds* clouds = nullptr;
		TESWeather* currentWeather = nullptr;
		TESWeather* overrideWeather = nullptr;
		TESWeather* defaultWeather = nullptr;
		float lastWeatherUpdate = 0.0f;
		int nativeForces = 0, entryCalls = 0, nativeSets = 0;
		bool lastAccelerate = false;
		void ForceWeather(TESWeather*, bool);
	};
	struct TESGlobal
	{
		float value = 0.0f;
	};
	struct Calendar
	{
		static inline Calendar* singleton = nullptr;
		static Calendar* GetSingleton() { return singleton; }
		TESGlobal* gameHour = nullptr;
	};
}

namespace globals::game
{
	RE::Calendar* calendar = nullptr;
	RE::Sky* sky = nullptr;
}

namespace ImGui
{
	bool changed = false;
	float requestedHour = 0.0f;
	int sliderCalls = 0;
	bool SliderFloat(const char*, float* value, float, float, const char*)
	{
		++sliderCalls;
		if (changed)
			*value = requestedHour;
		return changed;
	}
}

template <class T, class U>
T skyrim_cast(U* value)
{
	return dynamic_cast<T>(value);
}

namespace
{
	enum class Runtime
	{
		SE,
		AE,
		VR
	};
	Runtime runtime = Runtime::SE;
	bool hooksInstalled = false;
	RE::Sky* nativeSky = nullptr;
	std::array<int, 3> releaseCalls{};

	void ReleaseModel(RE::ModelDBHandle::U_Entry* entry)
	{
		Check(runtime == Runtime::AE, "AE release used on SE/VR");
		Check(nativeSky && !nativeSky->auroraModel, "AE release retained the handle");
		Check(entry && entry->references == 1, "invalid or repeated model release");
		--entry->references;
		++releaseCalls[static_cast<int>(runtime)];
	}

	RE::ModelDBHandle* ResetModel(RE::ModelDBHandle* handle, RE::ModelDBHandle::U_Entry* replacement)
	{
		Check(runtime != Runtime::AE && replacement == nullptr, "incorrect model reset runtime or replacement");
		Check(handle->entry && handle->entry->references == 1, "invalid or repeated model reset");
		--handle->entry->references;
		handle->entry = nullptr;
		++releaseCalls[static_cast<int>(runtime)];
		return handle;
	}
}

namespace REL
{
	struct Module
	{
		static bool IsAE() { return runtime == Runtime::AE; }
	};
	struct ID
	{
		int value;
		explicit ID(int id) : value(id) {}
	};
	template <class T>
	struct Relocation
	{
		using Pointer = std::conditional_t<std::is_pointer_v<T>, T, std::add_pointer_t<T>>;
		Pointer target = nullptr;
		Relocation() = default;
		explicit Relocation(ID id)
		{
			if constexpr (std::is_same_v<Pointer, decltype(&ReleaseModel)>) {
				Check(id.value == 15443, "wrong AE model-release ID");
				target = &ReleaseModel;
			} else {
				Check(id.value == 25746, "wrong SE/VR model-reset ID");
				target = &ResetModel;
			}
		}
		std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(target); }
		template <class... Args>
		auto operator()(Args... args) const
		{
			return target(args...);
		}
	};
}

class EditorWindow
{
public:
	static constexpr float kGameHourMax = 23.99f;
	static void ForceWeather(RE::Sky*, RE::TESWeather*, bool);
	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");
};

namespace Util
{
	void RefreshForcedWeatherSky(RE::Sky*);
}
namespace
{
#include "forced_weather_hooks.h"

	void NativeForceWeather(RE::Sky* sky, RE::TESWeather* weather, bool isOverride)
	{
		Check(sky != nullptr, "null sky reached the engine");
		nativeSky = sky;
		++sky->nativeForces;
		sky->currentWeather = weather;
		sky->overrideWeather = isOverride ? weather : nullptr;
		sky->defaultWeather = isOverride ? nullptr : weather;
		if (sky->clouds) {
			for (const auto& cloud : sky->clouds->clouds) {
				if (cloud) {
					if (auto* property = dynamic_cast<RE::BSSkyShaderProperty*>(cloud->data.shaderProperty.get()))
						property->blend = 0.0f;
				}
			}
		}
	}

	void NativeSetWeather(RE::Sky* sky, RE::TESWeather* weather, bool isOverride, bool accelerate)
	{
		++sky->nativeSets;
		sky->currentWeather = weather;
		sky->overrideWeather = isOverride ? weather : nullptr;
		sky->lastAccelerate = accelerate;
	}

	void SetHookState(int state)
	{
		// Zero is before hook setup, one is a failed installation, two is installed.
		hooksInstalled = state == 2;
		ForceWeatherHook::func.target = state != 0 ? &NativeForceWeather : nullptr;
		SetWeatherHook::func.target = &NativeSetWeather;
		g_lockedWeather = nullptr;
		g_weatherLockActive = false;
		nativeSky = nullptr;
		releaseCalls = {};
	}
}

void RE::Sky::ForceWeather(TESWeather* weather, bool isOverride)
{
	Check(++entryCalls < 10, "force-weather detour recursion");
	if (hooksInstalled)
		ForceWeatherHook::thunk(this, weather, isOverride);
	else
		NativeForceWeather(this, weather, isOverride);
}

#include "forced_weather_under_test.h"

namespace
{
	struct Fixture
	{
		RE::Sky sky;
		RE::Clouds clouds;
		RE::TESWeather first, second;
		RE::ModelDBHandle::U_Entry request;
		std::weak_ptr<RE::NiNode> oldModel;
		std::array<std::weak_ptr<RE::BSRenderPass>, 32> queuedPasses;

		Fixture()
		{
			sky.clouds = &clouds;
			sky.currentWeather = &first;
			sky.auroraRoot = sky.root->child = std::make_shared<RE::NiNode>();
			oldModel = sky.auroraRoot;
			sky.auroraModel.entry = &request;
			for (int index : { 0, 31 }) {
				clouds.clouds[index] = std::make_shared<RE::Geometry>();
				clouds.clouds[index]->data.shaderProperty = std::make_shared<RE::BSSkyShaderProperty>();
				queuedPasses[index] = static_cast<RE::BSSkyShaderProperty*>(clouds.clouds[index]->data.shaderProperty.get())->cachedPass;
			}
			clouds.clouds[1] = std::make_shared<RE::Geometry>();
			clouds.clouds[2] = std::make_shared<RE::Geometry>();
			clouds.clouds[2]->data.shaderProperty = std::make_shared<RE::Property>();
		}

		void CheckRefreshed(int expectedRebuilds = 1)
		{
			Check(oldModel.expired() && !sky.auroraRoot && sky.root->detaches == 1,
				"outgoing model remained attached or was detached twice");
			Check(!sky.auroraModel && request.references == 0, "model request was not released");
			for (int index : { 0, 31 }) {
				auto* property = static_cast<RE::BSSkyShaderProperty*>(clouds.clouds[index]->data.shaderProperty.get());
				Check(property->clears == 0 && !queuedPasses[index].expired(),
					"weather refresh freed a pass borrowed by the current draw queue");
				Check(queuedPasses[index].lock()->blendPass == (expectedRebuilds == 1),
					"queued pass changed before the next accumulation");
				Check(!property->UsesBlendPass() && property->rebuilds == expectedRebuilds,
					"clouds did not rebuild once at the next accumulation");
				Check(queuedPasses[index].expired(), "old pass was retained after accumulation rebuilt it");
				Check(!property->UsesBlendPass() && property->rebuilds == expectedRebuilds,
					"unchanged cloud pass rebuilt again");
				queuedPasses[index] = property->cachedPass;
			}
		}
	};

	void DirectAndConsoleCalls()
	{
		for (auto selectedRuntime : { Runtime::SE, Runtime::AE, Runtime::VR }) {
			runtime = selectedRuntime;
			for (int hookState : { 0, 1, 2 }) {
				SetHookState(hookState);
				Fixture f;
				EditorWindow::ForceWeather(&f.sky, &f.second, false);
				Check(f.sky.nativeForces == 1 && f.sky.currentWeather == &f.second &&
						  !f.sky.overrideWeather && f.sky.defaultWeather == &f.second,
					"preview force semantics changed");
				Check(f.sky.entryCalls == (hookState == 0 ? 1 : 0), "saved native entry was not used");
				f.CheckRefreshed();
				Check(releaseCalls[static_cast<int>(runtime)] == 1, "model release used the wrong runtime");

				EditorWindow::ForceWeather(&f.sky, nullptr, false);
				Check(!f.sky.currentWeather && !f.sky.overrideWeather, "null weather reset was lost");
				f.CheckRefreshed(2);
				Check(releaseCalls[static_cast<int>(runtime)] == 1, "empty model handle was released again");
			}

			SetHookState(2);
			Fixture f;
			f.sky.ForceWeather(&f.second, true);
			Check(f.sky.entryCalls == 1 && f.sky.nativeForces == 1 && f.sky.overrideWeather == &f.second,
				"external forced weather recursed or changed its override");
			f.CheckRefreshed();
		}
	}

	void LockEnforcement()
	{
		for (int hookState : { 0, 1, 2 }) {
			SetHookState(hookState);
			Fixture f;
			g_lockedWeather = &f.first;
			g_weatherLockActive = true;
			EditorWindow::ForceWeather(&f.sky, &f.second, false);
			Check(f.sky.currentWeather == &f.first && f.sky.overrideWeather == &f.first,
				"forced weather escaped the active lock");
			f.CheckRefreshed();
			ReapplyWeatherLock(&f.sky, &f.first);
			f.CheckRefreshed(2);
			Check(f.sky.nativeForces == 2, "lock repair forced weather twice");

			SetWeatherHook::thunk(&f.sky, &f.second, false, false);
			Check(f.sky.nativeForces == 2 && f.sky.nativeSets == 0, "unchanged lock was unnecessarily refreshed");
			f.sky.currentWeather = &f.second;
			SetWeatherHook::thunk(&f.sky, &f.second, false, false);
			Check(f.sky.nativeForces == 3 && f.sky.currentWeather == &f.first,
				"drifted lock did not repair through one native force");
			f.CheckRefreshed(3);
			SetWeatherHook::thunk(&f.sky, &f.first, false, true);
			Check(f.sky.nativeSets == 1 && f.sky.lastAccelerate && f.sky.overrideWeather == &f.first,
				"allowed SetWeather changed its acceleration or override");
			Check(f.sky.nativeForces == 3, "ordinary SetWeather forced visual refresh");
		}
	}

	void PendingAndAbsentModels()
	{
		for (auto selectedRuntime : { Runtime::SE, Runtime::AE, Runtime::VR }) {
			runtime = selectedRuntime;
			SetHookState(2);
			RE::Sky sky;
			RE::TESWeather weather;
			RE::ModelDBHandle::U_Entry request;
			sky.auroraModel.entry = &request;
			EditorWindow::ForceWeather(&sky, &weather, true);
			Check(!sky.auroraModel && request.references == 0 && sky.root->detaches == 0,
				"pending model was not canceled without an attached node");
			sky.root.reset();
			sky.auroraRoot = std::make_shared<RE::NiNode>();
			std::weak_ptr<RE::NiNode> orphan = sky.auroraRoot;
			EditorWindow::ForceWeather(&sky, &weather, false);
			Check(orphan.expired() && !sky.auroraRoot, "missing sky root prevented model cleanup");
			EditorWindow::ForceWeather(nullptr, &weather, false);
			Util::RefreshForcedWeatherSky(nullptr);
			ReapplyWeatherLock(nullptr, &weather);
			ReapplyWeatherLock(&sky, nullptr);
			Check(sky.nativeForces == 2 && releaseCalls[static_cast<int>(runtime)] == 1,
				"null inputs or empty handles invoked extra engine work");
		}
	}

	void ClockEdits()
	{
		EditorWindow editor;
		for (bool cachedAccessors : { false, true }) {
			SetHookState(2);
			RE::TESGlobal hour{ 12.0f };
			RE::Calendar calendar{ &hour };
			RE::Sky sky;
			RE::TESWeather locked, other;
			RE::Calendar::singleton = &calendar;
			RE::Sky::singleton = &sky;
			globals::game::calendar = cachedAccessors ? &calendar : nullptr;
			globals::game::sky = cachedAccessors ? &sky : nullptr;
			sky.currentWeather = sky.overrideWeather = g_lockedWeather = &locked;
			g_weatherLockActive = true;
			sky.lastWeatherUpdate = hour.value;
			ImGui::changed = true;
			for (float target : { 11.9f, 11.8f, 6.0f, 0.1f, 23.99f, 0.0f, 18.0f }) {
				ImGui::requestedHour = target;
				Check(editor.DrawGameHourSlider() && hour.value == target, "valid hour edit was lost");
				float elapsed = hour.value - sky.lastWeatherUpdate;
				if (elapsed < 0.0f)
					elapsed += 24.0f;
				if (elapsed > 1.0f)
					sky.overrideWeather = nullptr;
				Check(sky.overrideWeather == &locked, "clock edit expired the locked weather");
				SetWeatherHook::thunk(&sky, &other, false, false);
				Check(sky.nativeForces == 0, "clock edit forced a weather reload to repair the lock");
			}

			sky.lastWeatherUpdate = 3.0f;
			ImGui::changed = false;
			Check(editor.DrawGameHourSlider() && hour.value == 18.0f && sky.lastWeatherUpdate == 3.0f,
				"drawing an idle slider changed game time or the weather timer");
			ImGui::changed = true;
			for (float invalid : { -1.0f, 24.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
				ImGui::requestedHour = invalid;
				Check(editor.DrawGameHourSlider() && hour.value == 18.0f && sky.lastWeatherUpdate == 3.0f,
					"invalid clock input changed game time or the weather timer");
			}
			for (bool activeWithoutWeather : { false, true }) {
				g_weatherLockActive = activeWithoutWeather;
				g_lockedWeather = activeWithoutWeather ? nullptr : &locked;
				ImGui::requestedHour = activeWithoutWeather ? 5.0f : 9.0f;
				Check(editor.DrawGameHourSlider() && hour.value == ImGui::requestedHour && sky.lastWeatherUpdate == 3.0f,
					"unlocked edit or missing lock target changed the weather timer");
			}

			g_weatherLockActive = true;
			g_lockedWeather = &locked;
			globals::game::sky = RE::Sky::singleton = nullptr;
			ImGui::requestedHour = 7.0f;
			Check(editor.DrawGameHourSlider() && hour.value == 7.0f, "missing sky blocked a valid clock edit");
			const int calls = ImGui::sliderCalls;
			calendar.gameHour = nullptr;
			Check(!editor.DrawGameHourSlider() && ImGui::sliderCalls == calls, "missing hour reached the slider");
			globals::game::calendar = RE::Calendar::singleton = nullptr;
			Check(!editor.DrawGameHourSlider() && ImGui::sliderCalls == calls, "missing calendar reached the slider");
		}
		SetHookState(0);
	}
}

int main()
{
	try {
		DirectAndConsoleCalls();
		LockEnforcement();
		PendingAndAbsentModels();
		ClockEdits();
		std::cout << "Forced-weather refresh checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
