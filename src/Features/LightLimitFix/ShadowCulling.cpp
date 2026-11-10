#include "ShadowCulling.h"
#include "ActorShadowHistory.h"
#include "EngineFixes/VRShadowBatchPolicy.h"
#include "Globals.h"
#include "ShadowCullingDrawScope.h"
#include "ShadowCullingSnapshot.h"
#include "State.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <RE/B/BSCullingProcess.h>
#include <RE/B/BSRenderPass.h>
#include <RE/B/BSShader.h>
#include <RE/B/BSShadowDirectionalLight.h>
#include <algorithm>
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
			unsigned cascade = 0;
			RE::NiPoint3 lightPosition{};
			float lightRadius = 0;
		};
		struct Snapshot
		{
			Settings settings;
			RE::NiPointer<RE::ShadowSceneNode> scene;
			// The retained scene directly owns its sun; intrusive release would delete it prematurely.
			const RE::BSShadowLight* sunIdentity = nullptr;
			std::shared_ptr<ActorShadowHistory> actorPartHistory;
			std::array<Camera, 72> cameras;
			std::array<std::array<double, 3>, 2> eyes{};
			unsigned cameraCount = 0, eyeCount = 0;
			double sunSine = 0;
			bool interior = false;
			std::uint64_t sampledAt = 0;
			std::uint32_t frame = 0;
		};
		SnapshotPublication<Snapshot> snapshot;
		std::atomic_bool available{ false }, active{ false };
		bool conflict = false;
		std::string failure;
		using AppendFunction = void (*)(RE::BSCullingProcess*, RE::BSGeometry&, std::int32_t);
		AppendFunction appendOriginal = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_uint64_t sunSkipped{ 0 }, torchSkipped{ 0 }, actorSkipped{ 0 }, pointDrawsTested{ 0 };
		std::atomic_uint64_t actorPartDrawsSkipped{ 0 };
#endif
		struct Caster
		{
			bool actor = false, dynamic = true, protectedGeometry = true;
			const RE::NiAVObject* actorRoot = nullptr;
			RE::TESObjectREFR* actorReference = nullptr;
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
			if (result.protectedGeometry)
				return result;
			result.dynamic = data.skinInstance != nullptr || result.protectedGeometry ||
			                 netimmerse_cast<const RE::BSDynamicTriShape*>(&geometry) != nullptr;
			unsigned depthCounter = 0;
			for (const RE::NiAVObject* object = &geometry; object; object = object->parent) {
				result.dynamic |= object->GetControllers() != nullptr;
				if (auto* reference = Util::GetOwnUserData(object)) {
					result.actor = reference->IsActor();
					if (result.actor) {
						result.actorRoot = object;
						result.actorReference = reference;
					} else {
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
		std::shared_ptr<const Snapshot> CurrentSnapshot()
		{
			if (!active.load(std::memory_order_acquire) || !globals::state || globals::state->IsSaveLoadSafeModeActive())
				return {};
			thread_local SnapshotPublication<Snapshot>::Reader reader{ snapshot };
			const auto captured = reader.Acquire();
			const auto now = GetTickCount64();
			if (!captured || now < captured->sampledAt || now - captured->sampledAt > SnapshotMaxAgeMs)
				return {};
			return captured;
		}
		std::shared_ptr<ActorShadowHistory> ActorPartHistoryFor(const Snapshot& captured, const std::shared_ptr<const Snapshot>& previous)
		{
			if (!captured.settings.ActorPartsEnabled())
				return {};
			if (previous && previous->settings == captured.settings && previous->scene.get() == captured.scene.get() &&
				previous->actorPartHistory && captured.frame >= previous->frame &&
				captured.sampledAt >= previous->sampledAt && captured.sampledAt - previous->sampledAt <= SnapshotMaxAgeMs)
				return previous->actorPartHistory;
			return std::make_shared<ActorShadowHistory>(ActorPartDistances(captured.settings));
		}
		bool BeyondActorPartDistance(const Snapshot& captured, const Caster& caster)
		{
			if (!captured.actorPartHistory || !caster.actorReference || caster.protectedGeometry)
				return false;
			const auto position = caster.actorReference->GetPosition();
			return captured.actorPartHistory->Beyond(caster.actorReference->GetHandle().native_handle(),
				{ position.x, position.y, position.z }, captured.eyes, captured.eyeCount, captured.sampledAt, captured.frame);
		}
		bool Skip(const Snapshot& captured, const Camera& matched, RE::BSGeometry& geometry, bool actorPart = false)
		{
			Demand demand;
			demand.pass = matched.pass;
			demand.cascade = matched.cascade;
			demand.eyeCount = captured.eyeCount;
			demand.sunElevationSine = captured.sunSine;
			demand.interior = captured.interior;
			demand.radius = geometry.worldBound.radius;
			demand.actorPart = actorPart;
			Caster caster;
			if (actorPart) {
				if (!PassMayCull(captured.settings, demand) || !captured.actorPartHistory ||
					!std::isfinite(demand.radius) || demand.radius <= 0 || demand.radius >= ShadowRadiusLimit(captured.settings, true, true) ||
					!geometry.GetGeometryRuntimeData().skinInstance)
					return false;
				caster = Classify(geometry);
				// Observe nearby actors too, so returning inside the band clears every part's distance decision.
				if (!BeyondActorPartDistance(captured, caster))
					return false;
			}
			const std::array<double, 3> center{ geometry.worldBound.center.x, geometry.worldBound.center.y, geometry.worldBound.center.z };
			if (!BoundsMayCull(captured.settings, demand, center, captured.eyes))
				return false;
			if (matched.pass == Pass::Point) {
				const auto* light = matched.pointOwner.get();
				if (!light || light->light.get() != matched.pointSource.get() ||
					light->worldTranslate != matched.lightPosition || !light->light ||
					light->light->GetLightRuntimeData().radius.x != matched.lightRadius)
					return false;
			}
			if (!actorPart)
				caster = Classify(geometry);
			demand.actor = caster.actor;
			demand.dynamic = caster.dynamic;
			demand.protectedGeometry = caster.protectedGeometry;
			const auto minimumShadowRadius = demand.pass == Pass::Sun ? demand.radius / demand.sunElevationSine : demand.radius;
			if (!CasterMayCull(captured.settings, demand) || minimumShadowRadius >= ShadowRadiusLimit(captured.settings, demand.actor, actorPart))
				return false;
			std::array<double, 3> actorRoot{};
			if (caster.actorRoot) {
				const auto& point = caster.actorRoot->world.translate;
				actorRoot = { point.x, point.y, point.z };
			}
			const auto distance = [&](const std::array<double, 3>& position) {
				return SurfaceDistance(center, demand.radius, position, caster.actorRoot ? &actorRoot : nullptr);
			};
			for (unsigned i = 0; i < demand.eyeCount; ++i)
				demand.eyeDistance[i] = distance(captured.eyes[i]) - EyeDistanceMargin;
			if (demand.pass == Pass::Point) {
				demand.lightDistance = distance({ matched.lightPosition.x, matched.lightPosition.y, matched.lightPosition.z });
				demand.lightRadius = matched.lightRadius;
			}
			const bool skip = ShouldCull(captured.settings, demand);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (skip) {
				if (actorPart)
					actorPartDrawsSkipped.fetch_add(1, std::memory_order_relaxed);
				else {
					(demand.pass == Pass::Sun ? sunSkipped : torchSkipped).fetch_add(1, std::memory_order_relaxed);
					if (demand.actor)
						actorSkipped.fetch_add(1, std::memory_order_relaxed);
				}
			}
#endif
			return skip;
		}
		void Append(RE::BSCullingProcess* culler, RE::BSGeometry& geometry, std::int32_t alphaGroup)
		{
			if (culler) {
				if (const auto captured = CurrentSnapshot()) {
					for (unsigned i = 0; i < captured->cameraCount; ++i) {
						const auto& camera = captured->cameras[i];
						if (camera.owner.get() != culler->camera)
							continue;
						// Point-light collection also assigns illumination; only sun casters may be omitted here.
						if (camera.pass == Pass::Sun && Skip(*captured, camera, geometry))
							return;
						break;
					}
				}
			}
			appendOriginal(culler, geometry, alphaGroup);
		}
		bool HasEligibleCamera(const Snapshot& captured) noexcept
		{
			Demand demand;
			demand.eyeCount = captured.eyeCount;
			demand.sunElevationSine = captured.sunSine;
			demand.interior = captured.interior;
			demand.dynamic = false;
			// Keep the bounded camera order intact, including first-match identity semantics.
			return std::any_of(captured.cameras.begin(), captured.cameras.begin() + captured.cameraCount, [&](const Camera& camera) {
				demand.pass = camera.pass;
				demand.cascade = camera.cascade;
				demand.actorPart = true;
				demand.actor = true;
				if (PassMayCull(captured.settings, demand) && CasterMayCull(captured.settings, demand))
					return true;
				demand.actorPart = false;
				if (!PassMayCull(captured.settings, demand))
					return false;
				demand.actor = false;
				if (CasterMayCull(captured.settings, demand))
					return true;
				demand.actor = true;
				return CasterMayCull(captured.settings, demand);
			});
		}
		void CaptureLight(Snapshot& output, RE::BSShadowLight* light, Pass pass)
		{
			if (!light)
				return;
			const auto append = [&](const RE::NiPointer<RE::NiCamera>& camera, unsigned cascade) {
				if (!camera || output.cameraCount >= output.cameras.size())
					return;
				auto& target = output.cameras[output.cameraCount++];
				target.owner = camera;
				target.pass = pass;
				target.cascade = cascade;
				if (pass == Pass::Point) {
					target.pointOwner.reset(light);
					target.pointSource = light->light;
				}
				target.lightPosition = light->worldTranslate;
				if (light->light)
					target.lightRadius = light->light->GetLightRuntimeData().radius.x;
			};
			unsigned cascade = 0;
			if (globals::game::isVR) {
				for (const auto& descriptor : light->GetVRRuntimeData().shadowmapDescriptors) {
					if (descriptor.isEnabled)
						for (const auto& camera : descriptor.camera)
							append(camera, cascade);
					++cascade;
				}
			} else {
				for (const auto& descriptor : light->GetRuntimeData().shadowmapDescriptors) {
					if (descriptor.isEnabled)
						append(descriptor.camera, cascade);
					++cascade;
				}
			}
		}
	}
	bool NeedsSunDrawScope()
	{
		const auto captured = CurrentSnapshot();
		return captured && captured->settings.ActorPartsEnabled() && captured->sunIdentity;
	}
	bool SkipShadowDraw(const RE::BSRenderPass* pass, std::uint32_t technique)
	{
		const auto* light = DrawScope::Current();
		if (!light || !pass)
			return false;
		// Inactive or unknown light scopes leave queued render-pass state untouched.
		const auto captured = CurrentSnapshot();
		if (!captured)
			return false;
		Camera sunCamera;
		sunCamera.pass = Pass::Sun;
		const bool sun = captured->sunIdentity == light;
		if (sun && !captured->settings.ActorPartsEnabled())
			return false;
		const Camera* camera = sun ? &sunCamera : nullptr;
		if (!sun) {
			for (unsigned i = 0; i < captured->cameraCount; ++i) {
				const auto& candidate = captured->cameras[i];
				if (candidate.pass == Pass::Point && candidate.pointOwner.get() == light) {
					camera = &candidate;
					break;
				}
			}
		}
		if (!camera || !pass->shader || !pass->geometry ||
			!VRShadowBatch::IsIsolatedShadowPass(pass->shader->shaderType.get() == RE::BSShader::Type::Utility,
				technique, pass->numLights, pass->numShadowLights))
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!sun)
			pointDrawsTested.fetch_add(1, std::memory_order_relaxed);
#endif
		return (!sun && Skip(*captured, *camera, *pass->geometry)) ||
		       (captured->settings.ActorPartsEnabled() && Skip(*captured, *camera, *pass->geometry, true));
	}
	void Install()
	{
		if (appendOriginal || conflict)
			return;
		conflict = GetModuleHandleW(L"SPS.dll") != nullptr;
		if (conflict) {
			logger::warn("[LightLimitFix] CSX native shadow culling disabled because SPS.dll is loaded; SPS retains its hooks");
			return;
		}
		try {
			REL::Relocation<std::uintptr_t> normal{ RE::VTABLE_BSCullingProcess[0] };
			appendOriginal = reinterpret_cast<AppendFunction>(normal.write_vfunc(0x18, &Append));
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
			captured->frame = globals::state->frameCount;
			captured->eyeCount = globals::game::isVR ? 2 : 1;
			for (unsigned i = 0; i < captured->eyeCount; ++i) {
				const auto position = Util::GetEyePosition(i);
				captured->eyes[i] = { position.x, position.y, position.z };
				const auto view = Util::GetCameraData(i);
				if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
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
			if (settings.ActorPartsEnabled())
				captured->actorPartHistory = ActorPartHistoryFor(*captured, snapshot.Read());
			captured->interior = globals::game::smState->interior;
			auto& data = scene->GetRuntimeData();
			{
				const RE::BSSpinLockGuard lock{ data.lightQueueLock };
				if (const auto* sun = data.sunShadowDirLight) {
					const auto& direction = sun->GetShadowDirectionalLightRuntimeData().sunVector;
					const double length = std::hypot(double(direction.x), double(direction.y), double(direction.z));
					captured->sunSine = std::isfinite(length) && length > 0 ? std::abs(direction.z) / length : 0;
					CaptureLight(*captured, data.sunShadowDirLight, Pass::Sun);
					if (captured->cameraCount)
						captured->sunIdentity = data.sunShadowDirLight;
				}
				for (const auto& light : data.activeShadowLights)
					if (light && light.get() != data.sunShadowDirLight)
						CaptureLight(*captured, light.get(), Pass::Point);
			}
			if (!HasEligibleCamera(*captured)) {
				active.store(false, std::memory_order_release);
				snapshot.Publish({});
				return;
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
		Util::Widgets::SliderFloat("Sun shadow distance", &settings.sunDistanceMeters, MinimumDistanceMeters, MaximumDistanceMeters, "%.0f m");
		Util::AddTooltip("Minimum distance from the caster's bound. Default: 20 m. The nearest sun cascade and an additional stereo movement margin remain protected.");
		ImGui::Spacing();
		Util::Widgets::Checkbox("Torch shadows", &settings.torch);
		Util::AddTooltip("Skip tiny distant torch/fire shadow casters. Objects close to a light and large projected shadows remain protected.");
		Util::Widgets::SliderFloat("Torch shadow distance", &settings.torchDistanceMeters, MinimumDistanceMeters, MaximumDistanceMeters, "%.0f m");
		Util::AddTooltip("Minimum distance from the caster's bound. Default: 15 m. Light magnification and an additional stereo movement margin can retain more shadows.");
		ImGui::Spacing();
		Util::Widgets::Checkbox("Distant NPC shadows", &settings.actors);
		Util::AddTooltip("Also omit distant NPC shadows. Characters and their texture detail stay visible.");
		Util::Widgets::SliderFloat("NPC distance", &settings.actorDistanceMeters, MinimumDistanceMeters, MaximumDistanceMeters, "%.0f m");
		Util::AddTooltip("Default: 50 m. Nearby actor roots and large projected shadows remain protected.");
		if (advanced) {
			ImGui::Spacing();
			Util::Widgets::Checkbox("Small actor part shadows", &settings.smallActorParts);
			Util::AddTooltip("Omit tiny skinned actor-part shadows only while drawing shadow maps. Separate cutoff and return distances reduce repeated switching; both eyes, nearby lights and large projected shadows remain protected. On by default.");
			Util::Widgets::SliderFloat("Small part radius", &settings.actorPartRadiusCentimeters, 0, MaximumActorPartRadiusCentimeters, "%.2f cm", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("0 disables small actor-part shadow culling. Positive values set the maximum bounding-sphere radius of an eligible part and its projected shadow, not its diameter. Default: about 11.42 cm. Larger values allow more parts to qualify. Distance, both-eye, near-light and projected-size safeguards still apply.");
			if (Util::Widgets::SliderFloat("Small part cutoff distance", &settings.actorPartDistanceMeters, 0, MaximumDistanceMeters, "%.2f m", ImGuiSliderFlags_AlwaysClamp) && settings.actorPartDistanceMeters > 0)
				settings.actorPartReturnDistanceMeters = std::min(settings.actorPartReturnDistanceMeters, settings.actorPartDistanceMeters);
			Util::AddTooltip("0 disables small actor-part shadow culling without changing the return distance. Positive values start omitting eligible shadows beyond this actor distance, plus the fixed movement margin. Default: 7.14 m before the margin. A positive cutoff below the return distance also lowers the return distance. All three small-part sliders must be positive to enable this rule.");
			Util::Widgets::SliderFloat("Small part return distance", &settings.actorPartReturnDistanceMeters, 0, settings.actorPartDistanceMeters > 0 ? settings.actorPartDistanceMeters : MaximumDistanceMeters, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("0 disables small actor-part shadow culling. Positive values restore shadows when the actor returns within this distance, plus the fixed movement margin. Default: 6.426 m before the margin. Equal positive cutoff and return distances remove the return band. All three small-part sliders must be positive to enable this rule.");
			if (settings.ActorPartsEnabled())
				ImGui::TextWrapped("With movement margin: cutoff %.2f m / return %.2f m. Other shadow safeguards may retain shadows.",
					settings.actorPartDistanceMeters + EyeDistanceMargin * MetersPerUnit, settings.actorPartReturnDistanceMeters + EyeDistanceMargin * MetersPerUnit);
			else
				ImGui::TextWrapped("Small actor-part culling is off. Enable its checkbox and set all three sliders above 0.");
			Util::Widgets::Checkbox("Indoor torches", &settings.interiorTorches);
			Util::AddTooltip("Allow torch, NPC and small actor-part shadow culling indoors. Near lights and large projected shadows remain protected.");
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
		result["pointDrawsTested"] = pointDrawsTested.load();
		result["actorPartDrawsSkipped"] = actorPartDrawsSkipped.load();
		result["npcSkipped"] = actorSkipped.load();
#endif
		return result;
	}
}
