#include "EngineFixes/SunShadowReceiverFix.h"

#include "Compatibility.h"
#include "EngineFixes/SunReceiverLog.h"
#include "Globals.h"
#include "State.h"
#include "Utils/Game.h"
#include "Utils/VirtualFunctionHook.h"
#include <RE/B/BSShaderPropertyLightData.h>
#include <RE/B/BSShadowDirectionalLight.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/SunReceiverTrace.h"
#	include <nlohmann/json.hpp>
#endif

namespace SunShadowReceiver
{
	namespace
	{
		struct Runtime
		{
			RE::BSShaderAccumulator** primaryAccumulator{};
			const float* shadowDistance{};
			const bool* nativeShadowSuppressed{};
			const bool* vrDrawShadows{};
			PassFunction original{};
			bool installed{};
			std::string installationError;
			std::atomic_bool enabled{ true }, logging{ true };
			RepairLog repairs;
			std::shared_ptr<spdlog::logger> log;
			PTP_TIMER timer{};
			std::atomic_uint64_t reportFailures{};
		};
		Runtime* runtime{};
		static_assert(Policy::ReceiveShadows == static_cast<std::uint64_t>(RE::BSShaderProperty::EShaderPropertyFlag::kReceiveShadows));

		void ScheduleReport(PTP_TIMER timer) noexcept
		{
			LARGE_INTEGER relative{};
			relative.QuadPart = -static_cast<LONGLONG>(LogIntervalMilliseconds) * 10000;
			FILETIME due{ relative.LowPart, static_cast<DWORD>(relative.HighPart) };
			SetThreadpoolTimer(timer, &due, 0, 0);
		}

		void CALLBACK Report(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER timer) noexcept
		{
			auto& state = *static_cast<Runtime*>(context);
			if (state.logging.load(std::memory_order_relaxed) && state.log->should_log(spdlog::level::info)) {
				try {
					state.repairs.Report(
						[&](std::uint64_t delta, std::uint64_t total) {
							state.log->info("[Sun Shadow Fix] repairs_since_last_summary={} total_repairs={} (calls, not unique meshes)", delta, total);
						},
						[&](std::size_t ordinal, const RepairExample& example) {
							state.log->info("[Sun Shadow Fix] example={}/{} frame={} reference={:08X} mesh=\"{}\" geometry=0x{:X} property=0x{:X} sun_mask=0x{:08X} mask=0x{:08X}->0x{:08X} distance_squared={} range_squared={}",
								ordinal, MaximumLogExamples, example.frame, example.referenceFormId, example.name.data(), example.geometry, example.property,
								example.sunMask, example.maskBefore, example.maskAfter, example.distanceSquared, example.rangeSquared);
						});
				} catch (...) {
					state.reportFailures.fetch_add(1, std::memory_order_relaxed);
					OutputDebugStringA("CSX Sun Shadow Fix: periodic log report failed; counters retained for retry.\n");
				}
			}
			// Rearm after completion so delayed file I/O cannot queue a burst of reports.
			ScheduleReport(timer);
		}

		RE::BSShaderProperty::RenderPassArray* Passes(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
			std::uint32_t mode, RE::BSShaderAccumulator* accumulator)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			return Diagnostics::SunReceiverTrace::ProcessPasses(property, geometry, mode, accumulator, runtime->original);
#else
			return ProcessPasses(property, geometry, mode, accumulator, runtime->original);
#endif
		}
	}

	Policy::Input ReadInputs(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
		std::uint32_t mode, RE::BSShaderAccumulator* accumulator, std::uintptr_t& sun) noexcept
	{
		Policy::Input input;
		input.primaryPass = mode == 0 && accumulator && runtime && runtime->primaryAccumulator && accumulator == *runtime->primaryAccumulator;
		input.identityStable = true;
		if (!input.primaryPass)
			return input;
		const auto* lightData = property ? property->lightData : nullptr;
		input.lightData = reinterpret_cast<std::uintptr_t>(lightData);
		input.available = lightData && geometry && accumulator->camera && globals::state &&
		                  !globals::state->IsSaveLoadSafeModeActive() && globals::game::smState;
		if (!input.available)
			return input;
		input.flags = property->flags.underlying();
		input.activeMask = Policy::ReadMask(lightData->activeLightMask);
		input.shadowsEnabled = !globals::game::smState->interior && runtime->nativeShadowSuppressed && !*runtime->nativeShadowSuppressed &&
		                       (!runtime->vrDrawShadows || *runtime->vrDrawShadows);
		if (!(input.flags & Policy::ReceiveShadows) || (input.flags & Policy::ExcludedLod) || !input.shadowsEnabled)
			return input;
		if (const auto* scene = globals::game::smState->shadowSceneNode[0]) {
			const auto& data = scene->GetRuntimeData();
			sun = reinterpret_cast<std::uintptr_t>(data.sunShadowDirLight);
			input.sunMask = Policy::SunMask(data.shadowLightsAccum.data(), data.shadowLightsAccum.size(),
				static_cast<RE::BSShadowLight*>(data.sunShadowDirLight), globals::game::isVR);
		}
		if (!input.sunMask || (input.activeMask & input.sunMask))
			return input;
		input.shadowDistance = runtime->shadowDistance ? *runtime->shadowDistance : 0;
		const auto& center = geometry->worldBound.center;
		const auto& camera = accumulator->camera->world.translate;
		input.center = { center.x, center.y, center.z };
		input.camera = { camera.x, camera.y, camera.z };
		input.radius = geometry->worldBound.radius;
		return input;
	}

	bool Repair(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
		const Policy::Input& input, const Policy::Decision& decision) noexcept
	{
		auto* lightData = property ? property->lightData : nullptr;
		if (!runtime || !lightData || !Policy::Apply(lightData->activeLightMask, reinterpret_cast<std::uintptr_t>(lightData), input, decision, true))
			return false;
		runtime->repairs.Record([&]() noexcept {
			RepairExample example;
			example.frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
			example.geometry = reinterpret_cast<std::uintptr_t>(geometry);
			example.property = reinterpret_cast<std::uintptr_t>(property);
			example.maskBefore = input.activeMask;
			example.maskAfter = input.activeMask | input.sunMask;
			example.sunMask = input.sunMask;
			example.distanceSquared = decision.distanceSquared;
			example.rangeSquared = decision.rangeSquared;
			if (geometry) {
				if (const auto* name = geometry->name.c_str())
					for (std::size_t i = 0; i + 1 < example.name.size() && name[i]; ++i) {
						const auto value = static_cast<unsigned char>(name[i]);
						example.name[i] = value < 32 || value == 127 ? '?' : name[i];
					}
				const RE::NiAVObject* node = geometry;
				for (std::size_t depth = 0; node && depth < 32; ++depth, node = node->parent)
					if (const auto* reference = Util::GetOwnUserData(node)) {
						example.referenceFormId = reference->GetFormID();
						break;
					}
			}
			return example;
		});
		return true;
	}

	RE::BSShaderProperty::RenderPassArray* ProcessPasses(RE::BSLightingShaderProperty* property,
		RE::BSGeometry* geometry, std::uint32_t mode, RE::BSShaderAccumulator* accumulator, PassFunction original)
	{
		if (runtime->enabled.load(std::memory_order_relaxed)) {
			std::uintptr_t sun{};
			const auto input = ReadInputs(property, geometry, mode, accumulator, sun);
			const auto decision = Policy::Evaluate(input);
			if (decision.reason == Policy::Reason::candidate)
				(void)Repair(property, geometry, input, decision);
		}
		return original(property, geometry, mode, accumulator);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	bool Available() noexcept { return runtime && runtime->installed; }
	const std::string& InstallationError() noexcept { return runtime->installationError; }
	RE::BSShaderAccumulator* PrimaryAccumulator() noexcept { return runtime->primaryAccumulator ? *runtime->primaryAccumulator : nullptr; }

	nlohmann::json Control(const nlohmann::json& args)
	{
		if (const auto settings = args.find("sunReceiver"); settings != args.end()) {
			if (!settings->is_object())
				throw std::invalid_argument("sunReceiver must be an object");
			for (const auto& [key, value] : settings->items())
				if ((key != "enabled" && key != "logging") || !value.is_boolean())
					throw std::invalid_argument("sunReceiver accepts only boolean enabled and logging fields");
			if (!Available())
				throw std::invalid_argument("Sun receiver fix is unavailable");
			if (settings->value("logging", false) && !runtime->timer)
				throw std::invalid_argument("Periodic receiver logging could not be initialized");
			if (settings->contains("enabled"))
				runtime->enabled.store(settings->at("enabled").get<bool>(), std::memory_order_relaxed);
			if (settings->contains("logging"))
				runtime->logging.store(settings->at("logging").get<bool>(), std::memory_order_relaxed);
		}
		return { { "available", Available() }, { "installationError", runtime ? runtime->installationError : "Not initialized" },
			{ "enabled", Available() && runtime->enabled.load() }, { "logging", runtime && runtime->logging.load() },
			{ "loggingAvailable", runtime && runtime->timer }, { "loggingFailures", runtime ? runtime->reportFailures.load() : 0 },
			{ "totalRepairs", runtime ? runtime->repairs.Total() : 0 }, { "countsAreUniqueObjects", false },
			{ "logIntervalMilliseconds", LogIntervalMilliseconds }, { "maximumExamples", MaximumLogExamples },
			{ "detailedCaptureActive", Diagnostics::SunReceiverTrace::Active() },
			{ "capturePolicy", "An active developer trace replaces production repair globally; ordinary enablement resumes afterward." } };
	}
#endif
}

bool SunShadowReceiverFix::TryInstall()
{
	using namespace SunShadowReceiver;
	if (runtime)
		return runtime->installed;
	// Native hooks and timer callbacks live until process exit; never join under the loader lock.
	runtime = new Runtime;
	try {
		const bool vr = REL::Module::IsVR();
		if (vr && REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
			throw std::runtime_error("Sun receiver repair requires verified Skyrim VR 1.4.15");
		if (Compatibility::IsLoaded(Compatibility::shadowFixes))
			throw std::runtime_error("ShadowFixes.dll already owns sun receiver repair; CSX repair is disabled");
		const auto table = RE::VTABLE_BSLightingShaderProperty[0].address();
		const auto expected = REL::VariantID(99871, 106516, 0x13045F0).address();
		if (reinterpret_cast<const std::uintptr_t*>(table)[0x2A] != expected)
			throw std::runtime_error("Lighting GetRenderPasses differs; sun receiver hook refused");
		runtime->primaryAccumulator = reinterpret_cast<RE::BSShaderAccumulator**>(REL::VariantID(528064, 415009, 0x34859E0).address());
		runtime->shadowDistance = reinterpret_cast<const float*>(REL::RelocationID(528314, 415263).address());
		runtime->nativeShadowSuppressed = reinterpret_cast<const bool*>(REL::RelocationID(528095, 415040).address());
		if (vr)
			runtime->vrDrawShadows = reinterpret_cast<const bool*>(REL::Module::get().base() + 0x1ED3CB0);
		const auto hook = Util::InstallNativeVirtualFunctionHook(table, 0x2A, reinterpret_cast<void**>(&runtime->original), reinterpret_cast<void*>(&Passes));
		if (hook.error != NO_ERROR)
			throw std::runtime_error("Sun receiver hook installation failed: " + std::to_string(hook.error));
		runtime->installed = true;
		runtime->log = spdlog::default_logger();
		if (runtime->log)
			runtime->timer = CreateThreadpoolTimer(&Report, runtime, nullptr);
		if (runtime->timer) {
			ScheduleReport(runtime->timer);
			logger::info("[Sun Shadow Fix] Enabled; CSX.log summaries every {} ms when counts change, first {} repair examples per process", LogIntervalMilliseconds, MaximumLogExamples);
		} else {
			runtime->logging = false;
			logger::warn("[Sun Shadow Fix] Repair enabled, but periodic logging is unavailable (Win32 error {})", GetLastError());
		}
		return true;
	} catch (const std::exception& error) {
		runtime->enabled = false;
		runtime->installationError = error.what();
		logger::warn("[Sun Shadow Fix] {}", runtime->installationError);
		return false;
	}
}
