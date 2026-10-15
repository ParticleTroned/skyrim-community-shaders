#include "ShadowCulling.h"
#include "Globals.h"
#include "ShadowCullingSnapshot.h"
#include "State.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <RE/B/BSCullingProcess.h>
#include <RE/B/BSParabolicCullingProcess.h>
#include <RE/B/BSShadowDirectionalLight.h>
#include <atomic>
#include <memory>

namespace LightLimitFixDetail::ShadowCulling
{
	namespace
	{
		struct Camera
		{
			RE::NiPointer<RE::NiCamera> owner;
			RE::NiPointer<RE::BSShadowLight> pointOwner;
			RE::NiPointer<RE::NiLight> pointSource;
			Pass pass = Pass::Unknown;
			RE::NiPoint3 lightPosition{};
			float lightRadius = 0;
		};
		struct Snapshot
		{
			Settings settings;
			RE::NiPointer<RE::ShadowSceneNode> scene;
			std::array<Camera, 72> cameras;
			std::array<RE::NiPoint3, 2> eyes{};
			unsigned cameraCount = 0, eyeCount = 0;
			double sunSine = 0;
			bool interior = false;
			std::uint64_t sampledAt = 0;
		};
		SnapshotPublication<Snapshot> snapshot;
		std::atomic_bool available{ false }, active{ false };
		bool conflict = false;
		std::string failure;
		using AppendFunction = void (*)(RE::BSCullingProcess*, RE::BSGeometry&, std::int32_t);
		AppendFunction appendOriginal = nullptr, parabolicOriginal = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_uint64_t sunSkipped{ 0 }, torchSkipped{ 0 }, actorSkipped{ 0 };
#endif
		struct Caster
		{
			bool actor = false, dynamic = true, protectedGeometry = true;
			const RE::NiAVObject* actorRoot = nullptr;
		};
		Caster Classify(const RE::BSGeometry& geometry)
		{
			Caster result;
			const auto& data = geometry.GetGeometryRuntimeData();
			const auto* property = data.shaderProperty.get();
			if (!property || !property->material)
				return result;
			using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
			result.protectedGeometry = property->material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
			                           property->GetControllers() != nullptr || geometry.GetControllers() != nullptr ||
			                           property->flags.any(Flag::kTreeAnim, Flag::kBillboard, Flag::kProjectedUV, Flag::kRefraction, Flag::kTempRefraction);
			result.dynamic = data.skinInstance != nullptr || result.protectedGeometry ||
			                 netimmerse_cast<const RE::BSDynamicTriShape*>(&geometry) != nullptr;
			unsigned depthCounter = 0;
			for (const RE::NiAVObject* object = &geometry; object; object = object->parent) {
				result.dynamic |= object->GetControllers() != nullptr;
				if (const auto* reference = Util::GetOwnUserData(object)) {
					result.actor = reference->IsActor();
					if (result.actor)
						result.actorRoot = object;
					else {
						const auto* base = reference->GetBaseObject();
						result.dynamic |= !base || (base->GetFormType() != RE::FormType::Static && base->GetFormType() != RE::FormType::Land);
					}
					return result;
				}
				if (++depthCounter >= 32)
					break;
			}
			result.dynamic = result.protectedGeometry = true;
			return result;
		}
		bool Skip(RE::BSCullingProcess* culler, RE::BSGeometry& geometry, const RE::NiCamera* shadowCamera = nullptr)
		{
			if (!active.load(std::memory_order_acquire) || !culler || !globals::state || globals::state->IsSaveLoadSafeModeActive())
				return false;
			thread_local SnapshotPublication<Snapshot>::Reader reader{ snapshot };
			const auto captured = reader.Acquire();
			const auto now = GetTickCount64();
			if (!captured || now < captured->sampledAt || now - captured->sampledAt > 250)
				return false;
			const auto* camera = shadowCamera ? shadowCamera : culler->camera;
			const Camera* matched = nullptr;
			for (unsigned i = 0; i < captured->cameraCount; ++i)
				if (captured->cameras[i].owner.get() == camera) {
					matched = &captured->cameras[i];
					break;
				}
			if (!matched)
				return false;
			if (matched->pass == Pass::Point) {
				const auto* light = matched->pointOwner.get();
				if (!light || light->light.get() != matched->pointSource.get() ||
					light->worldTranslate != matched->lightPosition || !light->light ||
					light->light->GetLightRuntimeData().radius.x != matched->lightRadius)
					return false;
			}
			Demand demand;
			demand.pass = matched->pass;
			demand.eyeCount = captured->eyeCount;
			demand.sunElevationSine = captured->sunSine;
			demand.interior = captured->interior;
			demand.radius = geometry.worldBound.radius;
			const auto caster = Classify(geometry);
			demand.actor = caster.actor;
			demand.dynamic = caster.dynamic;
			demand.protectedGeometry = caster.protectedGeometry;
			const std::array<double, 3> center{ geometry.worldBound.center.x, geometry.worldBound.center.y, geometry.worldBound.center.z };
			std::array<double, 3> actorRoot{};
			if (caster.actorRoot) {
				const auto& point = caster.actorRoot->world.translate;
				actorRoot = { point.x, point.y, point.z };
			}
			const auto distance = [&](const RE::NiPoint3& position) {
				return SurfaceDistance(center, demand.radius, { position.x, position.y, position.z }, caster.actorRoot ? &actorRoot : nullptr);
			};
			for (unsigned i = 0; i < demand.eyeCount; ++i)
				demand.eyeDistance[i] = distance(captured->eyes[i]) - 512;
			demand.lightDistance = distance(matched->lightPosition);
			demand.lightRadius = matched->lightRadius;
			const bool skip = ShouldCull(captured->settings, demand);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (skip) {
				(demand.pass == Pass::Sun ? sunSkipped : torchSkipped).fetch_add(1, std::memory_order_relaxed);
				if (demand.actor)
					actorSkipped.fetch_add(1, std::memory_order_relaxed);
			}
#endif
			return skip;
		}
		void Append(RE::BSCullingProcess* culler, RE::BSGeometry& geometry, std::int32_t alphaGroup)
		{
			if (!Skip(culler, geometry))
				appendOriginal(culler, geometry, alphaGroup);
		}
		void Parabolic(RE::BSCullingProcess* culler, RE::BSGeometry& geometry, std::int32_t alphaGroup)
		{
			const auto* camera = static_cast<RE::BSParabolicCullingProcess*>(culler)->lightCamera.get();
			if (!Skip(culler, geometry, camera))
				parabolicOriginal(culler, geometry, alphaGroup);
		}
		void CaptureLight(Snapshot& output, RE::BSShadowLight* light, Pass pass)
		{
			if (!light)
				return;
			const auto append = [&](const RE::NiPointer<RE::NiCamera>& camera) {
				if (!camera || output.cameraCount >= output.cameras.size())
					return;
				auto& target = output.cameras[output.cameraCount++];
				target.owner = camera;
				target.pass = pass;
				if (pass == Pass::Point) {
					target.pointOwner.reset(light);
					target.pointSource = light->light;
				}
				target.lightPosition = light->worldTranslate;
				if (light->light)
					target.lightRadius = light->light->GetLightRuntimeData().radius.x;
			};
			if (globals::game::isVR) {
				for (const auto& descriptor : light->GetVRRuntimeData().shadowmapDescriptors)
					if (descriptor.isEnabled)
						for (const auto& camera : descriptor.camera)
							append(camera);
			} else {
				for (const auto& descriptor : light->GetRuntimeData().shadowmapDescriptors)
					if (descriptor.isEnabled)
						append(descriptor.camera);
			}
		}
	}
	void Install()
	{
		if (appendOriginal || parabolicOriginal || conflict)
			return;
		conflict = GetModuleHandleW(L"SPS.dll") != nullptr;
		if (conflict) {
			logger::warn("[LightLimitFix] CSX native shadow culling disabled because SPS.dll is loaded; SPS retains its hooks");
			return;
		}
		try {
			REL::Relocation<std::uintptr_t> normal{ RE::VTABLE_BSCullingProcess[0] }, parabolic{ RE::VTABLE_BSParabolicCullingProcess[0] };
			appendOriginal = reinterpret_cast<AppendFunction>(normal.write_vfunc(0x18, &Append));
			parabolicOriginal = reinterpret_cast<AppendFunction>(parabolic.write_vfunc(0x18, &Parabolic));
			available = true;
		} catch (const std::exception& error) {
			failure = error.what();
			logger::warn("[LightLimitFix] Native shadow culling unavailable: {}", failure);
		}
	}
	void Update(const Settings& settings, bool featureEnabled)
	{
		if (!available.load() || !featureEnabled || !settings.Active() || !globals::game::smState || !globals::game::shadowState || !globals::state ||
			globals::state->IsSaveLoadSafeModeActive() || (globals::game::ui && globals::game::ui->GameIsPaused())) {
			active.store(false, std::memory_order_release);
			snapshot.Publish({});
			return;
		}
		try {
			auto captured = std::make_shared<Snapshot>();
			captured->settings = settings;
			captured->sampledAt = GetTickCount64();
			captured->eyeCount = globals::game::isVR ? 2 : 1;
			for (unsigned i = 0; i < captured->eyeCount; ++i) {
				captured->eyes[i] = Util::GetEyePosition(i);
				const auto view = Util::GetCameraData(i);
				if (!std::isfinite(captured->eyes[i].x) || !std::isfinite(captured->eyes[i].y) || !std::isfinite(captured->eyes[i].z) ||
					!std::isfinite(view.projMatrixUnjittered._11) || view.projMatrixUnjittered._11 == 0 ||
					!std::isfinite(view.projMatrixUnjittered._22) || view.projMatrixUnjittered._22 == 0) {
					active.store(false, std::memory_order_release);
					snapshot.Publish({});
					return;
				}
			}
			auto* scene = globals::game::smState->shadowSceneNode[0];
			if (!scene) {
				active.store(false, std::memory_order_release);
				snapshot.Publish({});
				return;
			}
			captured->scene.reset(scene);
			captured->interior = globals::game::smState->interior;
			auto& data = scene->GetRuntimeData();
			{
				const RE::BSSpinLockGuard lock{ data.lightQueueLock };
				if (const auto* sun = data.sunShadowDirLight) {
					const auto& direction = sun->GetShadowDirectionalLightRuntimeData().sunVector;
					const double length = std::hypot(double(direction.x), double(direction.y), double(direction.z));
					captured->sunSine = std::isfinite(length) && length > 0 ? std::abs(direction.z) / length : 0;
					CaptureLight(*captured, data.sunShadowDirLight, Pass::Sun);
				}
				for (const auto& light : data.activeShadowLights)
					if (light && light.get() != data.sunShadowDirLight)
						CaptureLight(*captured, light.get(), Pass::Point);
			}
			snapshot.Publish(std::move(captured));
			active.store(true, std::memory_order_release);
		} catch (const std::exception& error) {
			active.store(false, std::memory_order_release);
			snapshot.Publish({});
			failure = error.what();
			available = false;
			logger::warn("[LightLimitFix] Native shadow culling suspended: {}", failure);
		}
	}
	void DrawSettings(Settings& settings, bool advanced)
	{
		const bool unavailable = conflict || !available.load();
		{
			const auto disabled = Util::DisableGuard(unavailable);
			ImGui::SeparatorText("Shadow culling");
		}
		if (conflict)
			Util::Text::WrappedWarning("SPS owns shadow culling. Disable SPS and restart to use CSX native culling.");
		else if (!failure.empty())
			ImGui::TextWrapped("Native shadow culling is unavailable: %s", failure.c_str());
		const auto disabled = Util::DisableGuard(unavailable);
		ImGui::TextWrapped("Native shadow maps; independent of contact shadows.");
		Util::Widgets::Checkbox("Sun shadows", &settings.sun);
		Util::AddTooltip("Skip tiny distant sun-shadow casters. Both VR eyes and long shadows near the horizon remain protected.");
		ImGui::Spacing();
		Util::Widgets::Checkbox("Torch shadows", &settings.torch);
		Util::AddTooltip("Skip tiny distant torch/fire shadow casters. Objects close to a light and large projected shadows remain protected.");
		ImGui::Spacing();
		Util::Widgets::Checkbox("Distant NPC shadows", &settings.actors);
		Util::AddTooltip("Also omit distant NPC shadows. Characters and their texture detail stay visible.");
		if (advanced) {
			ImGui::Spacing();
			Util::Widgets::Checkbox("Indoor torches", &settings.interiorTorches);
			Util::AddTooltip("Allow torch and NPC shadow culling indoors. Near lights and large projected shadows remain protected.");
			Util::Widgets::SliderFloat("Object distance", &settings.minimumDistanceMeters, 30, 300, "%.0f m");
			Util::Widgets::SliderFloat("NPC distance", &settings.actorDistanceMeters, 50, 300, "%.0f m");
		}
	}
	nlohmann::json Status(const Settings& settings)
	{
		nlohmann::json result{ { "settings", settings }, { "available", available.load() }, { "spsConflict", conflict }, { "error", failure } };
		const auto captured = snapshot.Read();
		result["active"] = active.load() && captured != nullptr;
		result["shadowCameras"] = captured ? captured->cameraCount : 0;
		result["eyes"] = captured ? captured->eyeCount : 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		result["sunSkipped"] = sunSkipped.load();
		result["torchSkipped"] = torchSkipped.load();
		result["npcSkipped"] = actorSkipped.load();
#endif
		return result;
	}
}
