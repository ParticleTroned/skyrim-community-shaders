#include "LightAttach.h"

#include "Features/LightLimitFix.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UI.h"

#include <RE/B/BSLight.h>
#include <RE/N/NiLight.h>
#include <RE/S/ShadowSceneNode.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

namespace LightLimitFixDetail::LightAttach
{
	namespace
	{
		// Native purging checks NiLight's reference count; retain only its BSLight wrapper.
		using LightOwner = RE::NiPointer<RE::BSLight>;
		Tracker<LightOwner> tracker;
		std::array<LightOwner, MaximumTrackedLights> retired;
		std::mutex trackerMutex, updateMutex;
		std::atomic<std::uint32_t> refreshFrames{ 0 };
		std::atomic<std::uint64_t> generation{ 1 };
		std::atomic<std::uint64_t> publishedAt{ 0 };
		std::uint64_t appliedGeneration = 0;
		std::atomic_bool installed{ false }, hasTracking{ false };
		std::string installationError = "Not initialized";
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_uint64_t walked{ 0 }, skipped{ 0 }, bypassed{ 0 };
#endif

		bool Eligible(RE::ShadowSceneNode* a_node, RE::BSLight* a_light)
		{
			return a_node && a_light && a_light->light && a_light->pointLight && a_light->dynamic &&
			       !a_light->ambientLight && a_light->frustrumCull != 0xFF &&
			       !LightLimitFix::RequiresEngineLightPath(a_light) &&
			       a_node->GetRuntimeData().sceneGraphIndex == 0 && !a_node->GetRuntimeData().disableLightUpdate;
		}

		Signature MakeSignature(RE::ShadowSceneNode& a_node, RE::BSLight& a_light)
		{
			const auto point = [](const RE::NiPoint3& a_value) { return std::array{ a_value.x, a_value.y, a_value.z }; };
			const auto* source = a_light.light.get();
			const auto& data = source->GetLightRuntimeData();
			Signature result;
			result.scene = &a_node;
			result.source = source;
			result.object = a_light.objectNode.get();
			result.lightPortalGraph = a_light.portalGraph;
			result.scenePortalGraph = a_node.GetRuntimeData().portalGraph;
			result.position = point(source->world.translate);
			result.radius = point(data.radius);
			result.cachedPosition = point(a_light.worldTranslate);
			result.fade = data.fade;
			result.flags = source->GetFlags().underlying();
			result.lightFlags = std::uint32_t(a_light.affectLand) | (std::uint32_t(a_light.affectWater) << 1) |
			                    (std::uint32_t(a_light.lodFade) << 2);
			result.culling = a_light.frustrumCull;
			return result;
		}

		struct DynamicLightEnable
		{
			static void thunk(RE::ShadowSceneNode* a_node, RE::BSLight* a_light)
			{
				const auto interval = refreshFrames.load(std::memory_order_acquire);
				if (interval <= 1 || !globals::state || globals::state->IsSaveLoadSafeModeActive() ||
					!globals::shaderCache || !globals::shaderCache->IsEnabled()) {
					func(a_node, a_light);
					return;
				}
				const auto now = GetTickCount64();
				const auto published = publishedAt.load(std::memory_order_acquire);
				if (now < published || now - published > MaximumFrameAgeMilliseconds) {
					func(a_node, a_light);
					return;
				}
				if (!Eligible(a_node, a_light)) {
					{
						std::lock_guard lock(trackerMutex);
						tracker.Forget(a_light);
					}
#ifdef DEVBENCH_BRIDGE_ENABLED
					bypassed.fetch_add(1, std::memory_order_relaxed);
#endif
					func(a_node, a_light);
					return;
				}

				const auto epoch = generation.load(std::memory_order_acquire);
				const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
				const auto before = MakeSignature(*a_node, *a_light);
				bool walk = true;
				{
					std::lock_guard lock(trackerMutex);
					if (epoch == appliedGeneration && interval == refreshFrames.load(std::memory_order_acquire))
						walk = tracker.ShouldWalk(a_light, before, frame, interval);
				}
				if (!walk && epoch == generation.load(std::memory_order_acquire) && interval == refreshFrames.load(std::memory_order_acquire)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					skipped.fetch_add(1, std::memory_order_relaxed);
#endif
					return;
				}

				LightOwner owner{ a_light };
				func(a_node, a_light);
#ifdef DEVBENCH_BRIDGE_ENABLED
				walked.fetch_add(1, std::memory_order_relaxed);
#endif
				if (!Eligible(a_node, a_light)) {
					std::lock_guard lock(trackerMutex);
					tracker.Forget(a_light);
					return;
				}
				const auto after = MakeSignature(*a_node, *a_light);
				// A native source/object replacement needs a fresh walk before it can be skipped.
				if (after.source != before.source || after.object != before.object) {
					std::lock_guard lock(trackerMutex);
					tracker.Forget(a_light);
					return;
				}
				std::lock_guard lock(trackerMutex);
				if (epoch == generation.load(std::memory_order_acquire) && epoch == appliedGeneration &&
					interval == refreshFrames.load(std::memory_order_acquire)) {
					tracker.RecordWalk(owner, after, frame);
					hasTracking.store(true, std::memory_order_release);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		bool IsExpectedCall(std::uintptr_t a_call, std::uintptr_t a_target)
		{
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			if (a_call < text.address() || a_call - text.address() > text.size() ||
				text.size() - (a_call - text.address()) < 5 || a_target < text.address() || a_target - text.address() >= text.size())
				return false;
			std::array<std::uint8_t, 5> code{};
			SIZE_T read = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(a_call), code.data(), code.size(), &read) ||
				read != code.size() || code[0] != 0xE8)
				return false;
			std::int32_t displacement = 0;
			std::memcpy(&displacement, code.data() + 1, sizeof(displacement));
			const auto target = static_cast<std::intptr_t>(a_call + code.size()) + displacement;
			return target == static_cast<std::intptr_t>(a_target);
		}
	}

	void Install()
	{
		if (installed.load(std::memory_order_acquire))
			return;
		try {
			const auto version = REL::Module::get().version();
			const bool known = REL::Module::IsVR() ? version == REL::Version{ 1, 4, 15, 0 } :
			                   REL::Module::IsAE() ? version == REL::Version{ 1, 6, 1170, 0 } || version == REL::Version{ 1, 7, 104, 0 } :
			                                         version == REL::Version{ 1, 5, 97, 0 };
			if (!known)
				throw std::runtime_error("Runtime has no verified dynamic light call site");
			const auto call = REL::Module::IsAE() ? REL::RelocationID(99701, 106335).address() + 0x6E2 :
			                                        REL::Offset(REL::Module::IsVR() ? 0x12FC2C3 : 0x12BE843).address();
			if (!IsExpectedCall(call, REL::RelocationID(99708, 106342).address()))
				throw std::runtime_error("Dynamic light call is modified or unsupported");
			stl::write_thunk_call<DynamicLightEnable>(call);
			installationError.clear();
			installed.store(true, std::memory_order_release);
			logger::info("[LightLimitFix] Installed optional light update gate");
		} catch (const std::exception& error) {
			installationError = error.what();
			logger::warn("[LightLimitFix] Light update gate unavailable: {}", installationError);
		}
	}

	void Invalidate() noexcept
	{
		refreshFrames.store(0, std::memory_order_release);
		generation.fetch_add(1, std::memory_order_acq_rel);
	}

	void Update(bool a_enabled, int a_refreshFrames, bool a_featureEnabled)
	{
		std::lock_guard updateLock(updateMutex);
		const bool allowed = installed.load(std::memory_order_acquire) && a_enabled && a_featureEnabled &&
		                     globals::state && !globals::state->IsSaveLoadSafeModeActive() &&
		                     globals::shaderCache && globals::shaderCache->IsEnabled() &&
		                     globals::game::ui && !globals::game::ui->GameIsPaused();
		const auto desired = allowed && a_refreshFrames > 1 ? static_cast<std::uint32_t>(std::clamp(a_refreshFrames, MinimumRefreshFrames, MaximumRefreshFrames)) : 0u;
		const auto previous = refreshFrames.load(std::memory_order_acquire);
		if (!desired && !previous && !hasTracking.load(std::memory_order_acquire))
			return;
		const auto now = GetTickCount64();
		const auto published = publishedAt.load(std::memory_order_acquire);
		if (desired != previous || now < published || now - published > MaximumFrameAgeMilliseconds)
			Invalidate();
		const auto epoch = generation.load(std::memory_order_acquire);
		const auto frame = globals::state ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0;
		std::size_t count = 0;
		{
			std::lock_guard lock(trackerMutex);
			count = tracker.Retire(frame, !desired || epoch != appliedGeneration, retired);
			appliedGeneration = epoch;
			hasTracking.store(tracker.Size() != 0, std::memory_order_release);
		}
		for (std::size_t i = 0; i < count; ++i)
			retired[i] = {};
		if (epoch == generation.load(std::memory_order_acquire)) {
			publishedAt.store(now, std::memory_order_release);
			refreshFrames.store(desired, std::memory_order_release);
		}
	}

	void DrawSettings(bool& a_enabled, int& a_refreshFrames)
	{
		const auto disabled = Util::DisableGuard(!installed.load(std::memory_order_acquire));
		ImGui::SeparatorText("Experimental light updates");
		if (Util::Widgets::Checkbox("Skip Unchanged Light Updates", &a_enabled))
			Invalidate();
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Skips repeated light checks to save work. Moving objects, including your hands, may briefly be lit incorrectly. Off keeps normal updates. Experimental; performance benefit is unmeasured.");
		const auto intervalDisabled = Util::DisableGuard(!a_enabled);
		if (Util::Widgets::SliderInt("Light Update Interval (frames)", &a_refreshFrames, MinimumRefreshFrames, MaximumRefreshFrames, "%d", ImGuiSliderFlags_AlwaysClamp))
			Invalidate();
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("How often unchanged lights recheck nearby objects. 1 checks every frame; higher values save work but may delay lighting changes on moving objects.");
		if (!installed.load(std::memory_order_acquire))
			ImGui::TextWrapped("Unavailable: %s", installationError.c_str());
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Control(const nlohmann::json& a_request)
	{
		using json = nlohmann::json;
		for (const auto& [name, value] : a_request.items()) {
			(void)value;
			if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
				name != "expectedBuildId" && name != "lightAttach")
				throw std::invalid_argument("Unknown light_attach argument");
		}
		auto& feature = globals::features::lightLimitFix;
		if (const auto patch = a_request.find("lightAttach"); patch != a_request.end()) {
			if (!patch->is_object())
				throw std::invalid_argument("lightAttach must be an object");
			bool enabled = feature.settings.SkipRedundantLightAttach;
			int interval = feature.settings.LightAttachRefreshFrames;
			for (const auto& [name, value] : patch->items()) {
				if (name == "enabled" && value.is_boolean())
					enabled = value.get<bool>();
				else if (name == "refreshFrames" && value.is_number_integer() && value >= MinimumRefreshFrames && value <= MaximumRefreshFrames)
					interval = value.get<int>();
				else
					throw std::invalid_argument("lightAttach accepts only boolean enabled and integer refreshFrames in 1..600");
			}
			if (enabled && (!installed.load(std::memory_order_acquire) || !feature.loaded || !globals::state || globals::state->IsFeatureDisabled(feature.GetShortName())))
				throw std::invalid_argument("Light update gate is unavailable");
			feature.settings.SkipRedundantLightAttach = enabled;
			feature.settings.LightAttachRefreshFrames = interval;
			Update(enabled, interval, feature.loaded && globals::state && !globals::state->IsFeatureDisabled(feature.GetShortName()));
		}
		std::size_t retained = 0;
		{
			std::lock_guard lock(trackerMutex);
			retained = tracker.Size();
		}
		const auto now = GetTickCount64();
		const auto published = publishedAt.load(std::memory_order_acquire);
		return json{ { "lightAttach", { { "available", installed.load(std::memory_order_acquire) }, { "enabled", feature.settings.SkipRedundantLightAttach },
										  { "active", refreshFrames.load(std::memory_order_acquire) > 1 && globals::state && !globals::state->IsSaveLoadSafeModeActive() &&
														  globals::shaderCache && globals::shaderCache->IsEnabled() && now >= published && now - published <= MaximumFrameAgeMilliseconds },
										  { "refreshFrames", feature.settings.LightAttachRefreshFrames },
										  { "experimental", true }, { "sessionOnly", true }, { "installationError", installationError },
										  { "walked", walked.load(std::memory_order_relaxed) }, { "skipped", skipped.load(std::memory_order_relaxed) },
										  { "bypassed", bypassed.load(std::memory_order_relaxed) }, { "trackedLights", retained },
										  { "maximumTrackedLights", MaximumTrackedLights }, { "trackingStorageBytes", sizeof(tracker) + sizeof(retired) } } } };
	}

	nlohmann::json GetControlSchemas()
	{
		using json = nlohmann::json;
		const json settings{ { "type", "object" }, { "additionalProperties", false }, { "properties", { { "enabled", { { "type", "boolean" }, { "default", true } } }, { "refreshFrames", { { "type", "integer" }, { "minimum", MinimumRefreshFrames }, { "maximum", MaximumRefreshFrames }, { "default", DefaultRefreshFrames } } } } } };
		json properties = settings.at("properties");
		for (const auto* name : { "available", "active", "experimental", "sessionOnly" })
			properties[name] = { { "type", "boolean" } };
		properties["installationError"] = { { "type", "string" } };
		for (const auto* name : { "walked", "skipped", "bypassed", "trackedLights", "maximumTrackedLights", "trackingStorageBytes" })
			properties[name] = { { "type", "integer" }, { "minimum", 0 } };
		return { { "settings", settings }, { "status", { { "type", "object" }, { "properties", properties } } } };
	}
#endif
}
