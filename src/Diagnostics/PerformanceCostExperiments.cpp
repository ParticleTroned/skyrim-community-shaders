#include "Diagnostics/PerformanceCostExperiments.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/PerformanceOverlay/ABTesting/ABComparisonRunner.h"
#	include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#	include "Globals.h"
#	include "Features/GrassCollision.h"
#	include "Features/Upscaling.h"
#	include "Features/Upscaling/NeuralRendering/Renderer.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "ShaderCache.h"
#	include "Profiler.h"
#	include "State.h"

namespace CSX::Diagnostics::PerformanceCosts
{
	namespace
	{
		std::atomic_uint64_t transitionEpoch{ 0 };

		struct Applying
		{
			Applying() { transitionEpoch.fetch_add(1, std::memory_order_acq_rel); }
			~Applying() { transitionEpoch.fetch_add(1, std::memory_order_release); }
		};

		bool MeasurementActive()
		{
			return (globals::profiler && globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running) ||
			       PerformanceTuningRenderer::HasActiveMeasurements() || ABComparisonRunner::Get().IsRunning() ||
			       ABTestingManager::GetSingleton()->IsEnabled();
		}

		nlohmann::json SettingsJson(uint32_t a_mask)
		{
			auto result = nlohmann::json::object();
			for (const auto& entry : Entries)
				result[entry.name] = (a_mask & static_cast<uint32_t>(entry.point)) == 0;
			return result;
		}

		void InvalidateShaders(uint32_t a_changed)
		{
			if (a_changed & (CSX_PERF_FUR_CONTACT_REJECT | CSX_PERF_SNOW_WEATHER_RANGE | CSX_PERF_POINT_LIGHT_COMPOSITION))
				globals::shaderCache->Clear(RE::BSShader::Type::Lighting);
			if (a_changed & (CSX_PERF_SNOW_WEATHER_RANGE | CSX_PERF_POINT_LIGHT_COMPOSITION))
				globals::shaderCache->Clear(RE::BSShader::Type::Grass);
			if (a_changed & CSX_PERF_POINT_LIGHT_COMPOSITION) {
				globals::shaderCache->Clear(RE::BSShader::Type::Water);
				globals::shaderCache->Clear(RE::BSShader::Type::Effect);
				globals::shaderCache->Clear(RE::BSShader::Type::Particle);
			}
			if (a_changed & CSX_PERF_GRASS_TILE_FILTER)
				globals::features::grassCollision.ClearShaderCache();
			if (a_changed & CSX_PERF_SNOW_WEATHER_RANGE)
				globals::shaderCache->Clear(RE::BSShader::Type::DistantTree);
			if (a_changed & CSX_PERF_NEURAL_PREPARATION)
				NeuralRendering::Renderer::Instance().ResetReferenceShaders();
		}
	}

	nlohmann::json Control(const nlohmann::json& a_args)
	{
		for (const auto& [name, value] : a_args.items()) {
			(void)value;
			if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
				name != "expectedBuildId" && name != "performanceCosts")
				throw std::invalid_argument("unknown performance_costs argument: " + name);
		}
		if (a_args.contains("performanceCosts")) {
			const auto next = ParseSettings(selection.Requested(), a_args.at("performanceCosts"));
			if (next != selection.Requested() && MeasurementActive())
				throw std::invalid_argument("performance references cannot change during an active measurement");
			selection.Request(next);
		}
		const auto epoch = transitionEpoch.load(std::memory_order_acquire);
		const auto active = selection.Active();
		const auto requested = selection.Requested();
		const bool compiling = globals::shaderCache->IsCompiling();
		const auto failed = globals::shaderCache->GetFailedTasks();
		const bool nrPending = NeuralRendering::Renderer::Instance().HasPendingShaderInvalidation();
		const bool applying = (epoch & 1) != 0 || epoch != transitionEpoch.load(std::memory_order_acquire);
		const bool pending = applying || active != requested;
		nlohmann::json conditions = nlohmann::json::object();
		for (const auto& entry : Entries)
			conditions[entry.name] = entry.conditions;
		return { { "settings", SettingsJson(active) }, { "requestedSettings", SettingsJson(requested) },
			{ "referenceMask", active }, { "pending", pending }, { "sessionOnly", true },
			{ "transitionEpoch", epoch }, { "shaderCompiling", compiling }, { "failedShaderTasks", failed },
			{ "nrShaderInvalidationPending", nrPending },
			{ "ready", !pending && !compiling && failed == 0 && (!globals::features::upscaling.IsNeuralRenderingEnabled() || !nrPending) },
			{ "requiredConditions", std::move(conditions) } };
	}

	void ProcessPendingChanges()
	{
		if (!selection.Pending() || globals::shaderCache->IsCompiling() || MeasurementActive())
			return;
		const auto before = selection.Active();
		Applying applying;
		if (!selection.Apply())
			return;
		if ((before ^ selection.Active()) & ShaderPoints) {
			globals::state->SetDefines(globals::state->GetShaderDefinesSnapshot()->authoredText);
			InvalidateShaders(before ^ selection.Active());
		}
	}
}
#endif
