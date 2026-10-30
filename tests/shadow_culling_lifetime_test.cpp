#include "Features/LightLimitFix/ActorShadowHistory.h"
#include "Features/LightLimitFix/ShadowCullingSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "RE/N/NiSmartPointer.h"

void Require(bool condition, std::source_location location = std::source_location::current())
{
	if (!condition) {
		std::cerr << "Shadow lifetime assertion failed at line " << location.line() << '\n';
		std::abort();
	}
}

namespace RE
{
	struct RefObject
	{
		virtual ~RefObject() = default;
		void IncRefCount() { ++references; }
		void DecRefCount()
		{
			Require(references > 0);
			if (--references == 0)
				delete this;
		}
		unsigned references = 0;
	};
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
	};
	struct NiCamera : RefObject
	{};
	struct NiLight : RefObject
	{
		NiPoint3 radius{ 1000, 0, 0 };
		const NiLight& GetLightRuntimeData() const { return *this; }
	};
	struct BSShadowLight : RefObject
	{
		template <class Camera>
		struct Descriptor
		{
			bool isEnabled = true;
			Camera camera;
		};
		template <class Camera>
		struct Data
		{
			std::array<Descriptor<Camera>, 2> shadowmapDescriptors;
		};
		Data<NiPointer<NiCamera>> flat;
		Data<std::array<NiPointer<NiCamera>, 2>> vr;
		NiPointer<NiLight> light = make_nismart<NiLight>();
		NiPoint3 worldTranslate;
		auto& GetRuntimeData() { return flat; }
		auto& GetVRRuntimeData() { return vr; }
	};
	unsigned sunsDestroyed = 0, scenesDestroyed = 0;
	struct BSShadowDirectionalLight : BSShadowLight
	{
		~BSShadowDirectionalLight() override { ++sunsDestroyed; }
		NiPoint3 sunVector{ 0, 0, 1 };
		const auto& GetShadowDirectionalLightRuntimeData() const { return *this; }
	};
	struct ShadowSceneNode : RefObject
	{
		~ShadowSceneNode() override { ++scenesDestroyed; }
		std::unique_ptr<BSShadowDirectionalLight> sun = std::make_unique<BSShadowDirectionalLight>();
		BSShadowDirectionalLight* sunShadowDirLight = sun.get();
		std::vector<NiPointer<BSShadowLight>> activeShadowLights;
		std::mutex lightQueueLock;
		auto& GetRuntimeData() { return *this; }
	};
	using BSSpinLockGuard = std::lock_guard<std::mutex>;
	struct BSCullingProcess;
	struct BSGeometry;
	struct NiAVObject;
	struct TESObjectREFR;
}
namespace globals
{
	struct State
	{
		bool loading = false;
		std::uint32_t frameCount = 1;
		bool IsSaveLoadSafeModeActive() const { return loading; }
	} stateStorage;
	auto* state = &stateStorage;
	namespace game
	{
		bool isVR = false;
		struct ShaderManager
		{
			std::array<RE::ShadowSceneNode*, 1> shadowSceneNode{};
			bool interior = false;
		} manager;
		auto* smState = &manager;
		const bool shadowState = true;
		struct UI
		{
			bool paused = false;
			bool GameIsPaused() const { return paused; }
		} uiStorage;
		auto* ui = &uiStorage;
	}
}
namespace Util
{
	bool validEyes = true;
	RE::NiPoint3 GetEyePosition(unsigned) { return {}; }
	struct CameraData
	{
		struct
		{
			float _11, _22;
		} projMatrixUnjittered;
	};
	CameraData GetCameraData(unsigned) { return { { validEyes ? 1.0f : 0.0f, 1 } }; }
}
namespace logger
{
	void warn(const char*, const std::string&) { Require(false); }
}
std::uint64_t GetTickCount64() { return 1000; }
namespace LightLimitFixDetail::ShadowCulling
{
// Captured state must precede the extracted function definitions.
#include "shadow_culling_state.h"

#include "shadow_culling_capture.h"
#include "shadow_culling_history.h"
#include "shadow_culling_update.h"
}
namespace C = LightLimitFixDetail::ShadowCulling;

void CheckLifetime(bool vr, bool smallActorParts)
{
	globals::game::isVR = vr;
	const auto destroyed = RE::sunsDestroyed;
	const auto scenesDestroyed = RE::scenesDestroyed;
	auto scene = RE::make_nismart<RE::ShadowSceneNode>();
	globals::game::manager.shadowSceneNode[0] = scene.get();
	auto* sun = scene->sunShadowDirLight;
	auto camera = RE::make_nismart<RE::NiCamera>();
	auto point = RE::make_nismart<RE::BSShadowLight>();
	for (auto* light : { static_cast<RE::BSShadowLight*>(sun), point.get() }) {
		for (auto& descriptor : light->flat.shadowmapDescriptors)
			descriptor.camera = camera;
		for (auto& descriptor : light->vr.shadowmapDescriptors)
			descriptor.camera.fill(camera);
	}
	scene->activeShadowLights.push_back(point);
	const auto cameraReferences = camera->references;
	C::available = true;
	C::Settings enabled;
	enabled.smallActorParts = smallActorParts;
	C::Settings disabled;
	disabled.sun = disabled.torch = disabled.actors = disabled.smallActorParts = false;
	auto zeroSliders = enabled;
	zeroSliders.sun = zeroSliders.torch = zeroSliders.actors = false;
	zeroSliders.actorPartRadiusCentimeters = 0;
	const auto verifyRetired = [&] {
		Require(!C::active && !C::snapshot.Read());
		Require(RE::sunsDestroyed == destroyed && sun->references == 0);
		Require(scene->references == 1 && point->references == 2);
		Require(camera->references == cameraReferences);
	};
	enum class Retirement
	{
		Settings,
		ZeroSliders,
		Feature,
		Pause,
		Load,
		InvalidCamera,
		IneligibleSun
	};
	for (const auto reason : { Retirement::Settings, Retirement::ZeroSliders, Retirement::Feature, Retirement::Pause,
			 Retirement::Load, Retirement::InvalidCamera, Retirement::IneligibleSun }) {
		C::Update(enabled, true);
		{
			const auto captured = C::snapshot.Read();
			Require(C::active && captured);
			Require(captured->scene.get() == scene.get() && captured->sunIdentity == sun);
			Require(captured->cameraCount == (vr ? 8u : 4u) && captured->eyeCount == (vr ? 2u : 1u));
		}
		Require(scene->references == 2 && point->references > 2);
		// The engine's direct sun owner must never participate in intrusive retention.
		Require(sun->references == 0 && RE::sunsDestroyed == destroyed);
		C::Update(enabled, true);
		Require(scene->references == 2 && sun->references == 0);
		if (reason == Retirement::Pause)
			globals::game::uiStorage.paused = true;
		if (reason == Retirement::Load)
			globals::stateStorage.loading = true;
		if (reason == Retirement::InvalidCamera)
			Util::validEyes = false;
		if (reason == Retirement::IneligibleSun) {
			sun->sunVector = { 1, 0, 0 };
			scene->activeShadowLights.clear();
		}
		C::Update(reason == Retirement::Settings ? disabled : reason == Retirement::ZeroSliders ? zeroSliders :
																								  enabled,
			reason != Retirement::Feature);
		if (reason == Retirement::IneligibleSun) {
			sun->sunVector = { 0, 0, 1 };
			scene->activeShadowLights.push_back(point);
		}
		verifyRetired();
		globals::game::uiStorage.paused = globals::stateStorage.loading = false;
		Util::validEyes = true;
	}

	// Point-only captures cannot inherit a sun identity from an earlier publication.
	for (const bool sunPresent : { false, true }) {
		C::Update(enabled, true);
		scene->sunShadowDirLight = sunPresent ? sun : nullptr;
		for (auto& descriptor : sun->flat.shadowmapDescriptors)
			descriptor.isEnabled = false;
		for (auto& descriptor : sun->vr.shadowmapDescriptors)
			descriptor.isEnabled = false;
		C::Update(enabled, true);
		{
			const auto captured = C::snapshot.Read();
			Require(C::active && captured && !captured->sunIdentity);
			Require(captured->cameraCount == (vr ? 4u : 2u));
		}
		C::Update(disabled, true);
		verifyRetired();
		scene->sunShadowDirLight = sun;
		for (auto& descriptor : sun->flat.shadowmapDescriptors)
			descriptor.isEnabled = true;
		for (auto& descriptor : sun->vr.shadowmapDescriptors)
			descriptor.isEnabled = true;
	}

	C::Update(enabled, true);
	C::SnapshotPublication<C::Snapshot>::Reader reader{ C::snapshot };
	auto inFlight = reader.Acquire();
	Require(inFlight && inFlight->sunIdentity == sun);
	C::Update(disabled, true);
	Require(!C::active && !C::snapshot.Read() && !reader.Acquire());
	globals::game::manager.shadowSceneNode[0] = nullptr;
	scene.reset();
	Require(RE::sunsDestroyed == destroyed && RE::scenesDestroyed == scenesDestroyed);
	Require(inFlight->scene->references == 1 && sun->references == 0);
	inFlight.reset();
	Require(RE::sunsDestroyed == destroyed + 1 && RE::scenesDestroyed == scenesDestroyed + 1);
	Require(point->references == 1);
	point.reset();
	Require(camera->references == 1);
}

int main()
{
	for (const bool vr : { false, true })
		for (const bool smallActorParts : { false, true })
			CheckLifetime(vr, smallActorParts);
	std::cout << "Sun lifetime survives snapshot replacement, retirement and delayed readers\n";
}
