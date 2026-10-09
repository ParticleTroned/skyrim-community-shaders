#include "Menu/SettingsPage.h"
#include "Profiler.h"
#include "Utils/FeatureProfiling.h"
#include "Utils/LegitProfiler.h"

#include <array>
#include <cmath>
#include <functional>
#include <imgui_internal.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unordered_map>

#define private public
#include "Menu/ProfilingRenderer.h"
#undef private

namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		explicit scope_exit(F f) : function(std::move(f)) {}
		~scope_exit() { function(); }
	};
}
namespace Util
{
	float GetUIScale() { return 1.0f; }
	bool HoverTooltipWrapper() { return false; }
	void AddTooltip(const char*) {}
}
namespace MenuUI
{
	int chosenMode = 1;
	int choiceDraws = 0;
	int ChoiceCards(const char*, int, std::span<const Choice>)
	{
		++choiceDraws;
		return chosenMode;
	}
	void SectionHeading(const char* text) { ImGui::TextUnformatted(text); }
	void DetailText(const char* text) { ImGui::TextWrapped("%s", text); }
	bool DetailNote(const char* text, const char*)
	{
		DetailText(text);
		return false;
	}
}
namespace globals
{
	struct TimingSource
	{
		bool enabled = true;
		bool initialized = true;
		int requests = 0, enableWrites = 0;
		std::vector<Profiler::TimerResult> results;
		bool IsUserEnabled() const { return enabled; }
		bool IsInitialized() const { return initialized; }
		void SetUserEnabled(bool value)
		{
			enabled = value;
			++enableWrites;
		}
		Profiler::CaptureMode requestedMode = Profiler::CaptureMode::None;
		std::vector<Profiler::ExternalGpuTiming> externalGpuTimings;
		void RequestCapture(Profiler::CaptureMode mode = Profiler::CaptureMode::Both)
		{
			++requests;
			requestedMode = mode;
		}
		auto GetExternalGpuTimings() const { return externalGpuTimings; }
		const auto& GetResults() const { return results; }
	} source;
	auto* profiler = &source;
}

// The extracted helpers also serve global graphs outside this feature-view test.
#pragma warning(push)
#pragma warning(disable: 4505)
#include "feature_profiling_under_test.h"
#pragma warning(pop)

namespace
{
	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	void AddTimer(const std::string& name, float gpu, float cpu, float gpuOutermost = -1, float cpuOutermost = -1)
	{
		static std::map<std::string, std::array<float, Profiler::kHistorySize>> histories;
		auto& gpuHistory = histories[name + "gpu"];
		auto& cpuHistory = histories[name + "cpu"];
		gpuHistory.fill(gpu);
		cpuHistory.fill(cpu);
		auto& gpuInclusive = histories[name + "gpuOutermost"];
		auto& cpuInclusive = histories[name + "cpuOutermost"];
		gpuInclusive.fill(gpuOutermost < 0 ? gpu : gpuOutermost);
		cpuInclusive.fill(cpuOutermost < 0 ? cpu : cpuOutermost);
		Profiler::TimerResult timer;
		timer.name = name;
		timer.valid = true;
		timer.activeGpu = timer.hasGpu = gpu >= 0;
		timer.activeCpu = timer.hasCpu = cpu >= 0;
		timer.historyBuffer = gpuHistory.data();
		timer.cpuHistoryBuffer = cpuHistory.data();
		timer.outermostGpuHistoryBuffer = gpuInclusive.data();
		timer.outermostCpuHistoryBuffer = cpuInclusive.data();
		timer.historyHead = timer.cpuHistoryHead = 60;
		timer.historyCount = timer.activeGpu ? 60 : 0;
		timer.cpuHistoryCount = timer.activeCpu ? 60 : 0;
		globals::source.results.push_back(timer);
	}

	std::string Draw(const char* feature, int mode, float panelWidth = 1500, float fontSize = 13, bool selectMode = true)
	{
		MenuUI::chosenMode = selectMode ? mode : -1;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize({ panelWidth, 1000 });
		ImGui::Begin("Profiling test", nullptr, ImGuiWindowFlags_NoSavedSettings);
		ImGui::PushFont(ImGui::GetFont(), fontSize);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { fontSize * .75f, fontSize * .5f });
		ImGui::LogToBuffer();
		if (mode < 0)
			ProfilingRenderer::RenderFeaturePerformanceSummary(feature);
		else
			ProfilingRenderer::RenderFeatureTimers(feature);
		const std::string text = GImGui->LogBuffer.c_str();
		ImGui::LogFinish();
		if (panelWidth < 1500) {
			for (int i = 0; i < GImGui->Tables.GetBufSize(); ++i) {
				const auto* table = GImGui->Tables.GetByIndex(i);
				if (!table || table->LastFrameActive != ImGui::GetFrameCount())
					continue;
				for (int column = 1; column < table->ColumnsCount; ++column)
					Check(table->Columns[column].WidthGiven >= ImGui::CalcTextSize("20.000").x,
						"shared timing metrics clip at the measurement panel's body size");
			}
		}
		ImGui::PopStyleVar();
		ImGui::PopFont();
		ImGui::End();
		ImGui::Render();
		return text;
	}
}

int main()
{
	try {
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.DisplaySize = { 1500, 1000 };
		io.DeltaTime = 1.0f / 60;
		io.IniFilename = nullptr;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		for (const auto& view : Util::FeatureProfiling::views)
			Check(ProfilingRenderer::CanProfileFeature(view.feature), "registered view is hidden before samples exist");
		Check(!ProfilingRenderer::GetProfilingDisabledReason() && ProfilingRenderer::featureTimingModes.empty() && globals::source.requests == 0, "overview availability query must not create modes or start capture");
		Check(Draw("ImageBasedLighting", 0).contains("profiling is off"), "feature off mode starts profiling");
		Draw("ImageBasedLighting", 1);
		globals::source.enabled = false;
		const auto inactiveRequests = globals::source.requests;
		const auto inactiveChoices = MenuUI::choiceDraws;
		const auto retainedModes = ProfilingRenderer::featureTimingModes;
		Check(ProfilingRenderer::GetProfilingDisabledReason(), "globally disabled profiling appears available");
		for (const auto& view : Util::FeatureProfiling::views)
			for (const int mode : { 0, 1, 2 })
				Check(Draw(std::string(view.feature).c_str(), mode).contains("main Profiling page"), "disabled feature view must explain the main profiling switch");
		Check(!globals::source.enabled && globals::source.enableWrites == 0 && globals::source.requests == inactiveRequests && MenuUI::choiceDraws == inactiveChoices && ProfilingRenderer::featureTimingModes == retainedModes, "disabled feature views must not change modes, expose controls, start capture or enable the main switch");
		globals::source.enabled = true;
		Draw("ImageBasedLighting", 1, 1500, 13, false);
		Check(globals::source.requests > inactiveRequests && ProfilingRenderer::featureTimingModes == retainedModes, "main-switch reactivation preserves the feature timing mode");
		Draw("ImageBasedLighting", 2);
		Check(ProfilingRenderer::featureTimingModes.at("ImageBasedLighting") == ProfilingRenderer::FeatureTimingMode::CPU && globals::source.enableWrites == 0, "feature mode selection must not write the main profiling switch");
		globals::source.initialized = false;
		Check(ProfilingRenderer::GetProfilingDisabledReason(), "uninitialized profiling appears available");
		globals::profiler = nullptr;
		Check(ProfilingRenderer::GetProfilingDisabledReason(), "missing profiler appears available");
		for (const auto& view : Util::FeatureProfiling::views)
			Check(ProfilingRenderer::CanProfileFeature(view.feature), "missing profiler hides a registered view");
		globals::profiler = &globals::source;
		globals::source.initialized = true;
		// NR uses two timer namespaces; unrelated upscaling and lookalike names stay excluded.
		for (const char* evaluation : { "Upscaling::DLSSNeuralRendering", "Upscaling::DLSSNeuralRenderingStereo", "Upscaling::DLSSNeuralRenderingSequentialStereo" }) {
			globals::source.results.clear();
			AddTimer("Upscaling::DLSS", 4, .4f);
			AddTimer(evaluation, 2, .2f, 0, 0);
			Check(ProfilingRenderer::HasFeatureTimers("NeuralRendering"), "NR evaluation alone does not expose live feature timings");
			const auto gpu = ProfilingRenderer::CollectFeatureTimingData(std::string("NeuralRendering"), false);
			const auto cpu = ProfilingRenderer::CollectFeatureTimingData(std::string("NeuralRendering"), true);
			Check(gpu.entries.size() == 1 && gpu.entries[0].colorKey == evaluation && std::abs(gpu.totalAvg - 2) < .00001f,
				"NR GPU evaluation nested in Upscaling is missing or includes ordinary DLSS cost");
			Check(cpu.entries.size() == 1 && std::abs(cpu.totalAvg - .2f) < .00001f,
				"NR CPU submission timing is missing from its view");
			Draw("NeuralRendering", 1);
			Check(Draw("NeuralRendering", 1).contains(evaluation), "NR evaluation does not reach the profiling table");
		}
		globals::source.externalGpuTimings.push_back({ "NeuralRendering::Inference", {}, 1 });
		for (int frame = 0; frame < 60; ++frame)
			globals::source.externalGpuTimings.back().history.PushSample(1.25f);
		for (const auto panelWidth : { 600.f, 1500.f }) {
			const auto gpuText = Draw("NeuralRendering", 1, panelWidth);
			Check(gpuText.contains("NR inference GPU timings (D3D12, ms)") && gpuText.contains("1.250"), "independent NR inference timing is missing from the GPU view");
			Check(globals::source.requestedMode == Profiler::CaptureMode::GPU, "NR GPU view requests CPU timing unnecessarily");
			const auto cpuText = Draw("NeuralRendering", 2, panelWidth);
			Check(!cpuText.contains("NR inference GPU timings") && globals::source.requestedMode == Profiler::CaptureMode::CPU, "NR CPU-only view records or displays inference GPU timing");
		}
		const auto offRequests = globals::source.requests;
		Draw("NeuralRendering", 0);
		Check(globals::source.requests == offRequests, "NR off mode still requests capture");
		globals::source.externalGpuTimings.clear();
		globals::source.results.clear();
		AddTimer("Upscaling::NeuralFinalLdrPreUi", .1f, .01f, 3.6f, .36f);
		AddTimer("Upscaling::DLSSNeuralRenderingStereo", 2, .2f, 0, 0);
		AddTimer("Upscaling::NRColorPrepare", .3f, .03f, 0, 0);
		AddTimer("Upscaling::NRColorReconstruct", .4f, .04f, 0, 0);
		AddTimer("Upscaling::DLSS5CharacterMask", .5f, .05f, 0, 0);
		AddTimer("NeuralRendering::ActorProtection", .2f, .02f, .3f, .03f);
		AddTimer("NeuralRendering::ModelResolutionPrepare", .1f, .01f, 0, 0);
		AddTimer("Upscaling::DLSS5CharacterRoiSetup", -1, .06f, -1, 0);
		AddTimer("Upscaling::DLSS", 10, 1);
		AddTimer("Upscaling::FoveatedMaskVisualization", 20, 2);
		AddTimer("Upscaling::DLSSNeuralRenderingOther", 30, 3);
		AddTimer("NeuralRenderingExtra::Pass", 40, 4);
		const auto nrGpu = ProfilingRenderer::CollectFeatureTimingData(std::string("NeuralRendering"), false);
		const auto nrCpu = ProfilingRenderer::CollectFeatureTimingData(std::string("NeuralRendering"), true);
		Check(nrGpu.entries.size() == 7 && std::abs(nrGpu.totalAvg - 3.6f) < .00001f && std::abs(nrGpu.totalP95 - 3.6f) < .00001f && std::abs(nrGpu.totalP99 - 3.6f) < .00001f,
			"nested NR GPU passes are omitted or counted more than once");
		Check(nrCpu.entries.size() == 8 && std::abs(nrCpu.totalAvg - .42f) < .00001f,
			"NR CPU-only actor preparation is lost or unrelated upscaling leaked into the subtotal");
		globals::source.results.clear();
		AddTimer("Upscaling::DLSS", 1, .1f);
		AddTimer("Upscaling::DLSSNeuralRenderingOther", 1, .1f);
		Check(!ProfilingRenderer::HasFeatureTimers("NeuralRendering") && ProfilingRenderer::CollectFeatureTimingData(std::string("NeuralRendering"), false).entries.empty(),
			"NR matching accepts ordinary upscaling or similarly named effects");
		globals::source.results.clear();
		AddTimer("IBL::EnvDiffuseIBL", .2f, .01f);
		AddTimer("UnderwaterDepthOfField::InputFog", .3f, .02f);
		AddTimer("Wetterness::UpdateWeatherState", -1, .01f);
		AddTimer("InteriorSun::PrepareShadowJobs", -1, .03f, -1, .04f);
		AddTimer("InteriorSun::SelectShadowCasters", -1, .01f, -1, 0);
		AddTimer("CloudShadows::CopyCubemap", .02f, .001f);
		AddTimer("SharedScene::World", 8, .8f, 20, 2);
		AddTimer("SharedScene::DirectionalShadows", 2, .2f);
		AddTimer("Water::RenderWaterEffects", 1, .1f);
		AddTimer("DeferredComposite", .5f, .05f);
		AddTimer("VR::StereoBlend", .4f, .04f);
		AddTimer("ScreenSpaceGI::GI", .6f, .06f);
		AddTimer("Screenshot::Stage", 3, .3f);

		Draw("ImageBasedLighting", 1);
		Check(Draw("ImageBasedLighting", 1).contains("EnvDiffuseIBL"), "IBL page does not display its mapped timer");
		Check(Draw("CSUtility", 1).contains("InputFog"), "CS Utility does not display underwater DOF timing");
		const auto gpuWetness = Draw("Wetterness", 1);
		Check(gpuWetness.contains("Shared GPU pass timings") && gpuWetness.contains("SharedScene::World") && !gpuWetness.contains("Instrumented subtotal") && !gpuWetness.contains("UpdateWeatherState"), "wetness shared GPU passes were presented as owned cost");
		const auto cpuWetness = Draw("Wetterness", 2);
		Check(cpuWetness.contains("UpdateWeatherState") && cpuWetness.contains("Shared CPU pass timings"), "wetness CPU updates or shared context are missing");
		const auto ownedCpu = ProfilingRenderer::CollectFeatureTimingData(std::string("Wetterness"), true);
		Check(std::abs(ownedCpu.totalAvg - .01f) < .00001f, "wetness CPU subtotal includes unrelated shared rendering time");
		Check(ProfilingRenderer::CollectFeatureTimingData(std::string("Wetterness"), false).entries.empty(), "CPU-only update produced an owned GPU timer");
		Check(Draw("InteriorSun", 2).contains("PrepareShadowJobs"), "interior shadow job timing is missing");
		const auto shadowCpu = ProfilingRenderer::CollectFeatureTimingData(std::string("InteriorSun"), true);
		Check(shadowCpu.entries.size() == 2 && std::abs(shadowCpu.totalAvg - .04f) < .00001f, "nested caster selection double-counted in CPU subtotal");
		const auto shared = ProfilingRenderer::CollectFeatureTimingData(
			std::vector<std::string>{ "SharedScene::World", "DeferredComposite" }, false, true, ProfilingRenderer::TimingAttribution::Shared);
		Check(shared.entries.size() == 2 && shared.totalAvg == 0 && shared.totalP95 == 0 && shared.totalP99 == 0, "shared stages produced an aggregate feature cost");
		Check(shared.maxAvg == 8 && shared.maxP95 == 8 && shared.maxP99 == 8, "shared table heat colours include an invisible inclusive aggregate");
		Check(Draw("InteriorSun", 1).contains("DirectionalShadows"), "interior sun shared GPU shadow timing is missing");
		Check(Draw("CloudShadows", 1).contains("CopyCubemap"), "cloud copy timing is missing");
		for (const auto feature : { "TruePBR", "ExtendedMaterials", "TerrainVariation", "ExtendedTranslucency", "FoliageLighting", "GrassLighting", "HairSpecular", "WaterEffects" }) {
			const auto output = Draw(feature, 1);
			Check(output.contains("Shared GPU pass timings") && !output.contains("Instrumented subtotal"), "shared shader page claims a feature cost subtotal");
		}
		const auto vr = Draw("VR", 1);
		Check(vr.contains("StereoBlend") && vr.contains("ScreenSpaceGI::GI") && vr.contains("Partial VR coverage"), "VR lost stereo or partial shared coverage");
		Draw("TruePBR", -1, 650, 27);
		Draw("TruePBR", -1, 650, 27);
		Draw("TruePBR", -1, 350, 27);
		Draw("TruePBR", -1, 350, 27);
		const auto summaryRequests = globals::source.requests;
		const auto integrated = Draw("Wetterness", -1);
		Check(integrated.contains("Shared GPU pass timings") && integrated.contains("Feature CPU timings"), "measurement summary lost CPU or shared GPU attribution");
		Check(integrated.contains("Instrumented average: 0.010 ms"), "summary average includes shared rendering cost");
		Check(Draw("TruePBR", -1).contains("Shared CPU pass timings") && !Draw("TruePBR", -1).contains("Instrumented average"), "shared-only summary claims an isolated feature cost");
		int sharedTables = 0;
		for (int i = 0; i < GImGui->Tables.GetBufSize(); ++i) {
			const auto* table = GImGui->Tables.GetByIndex(i);
			if (table && table->LastFrameActive == ImGui::GetFrameCount())
				++sharedTables;
		}
		Check(sharedTables == 2, "CPU and GPU summaries must retain independent table identities");
		Check(Draw("ImageBasedLighting", -1).contains("Feature GPU timings") && Draw("ImageBasedLighting", -1).contains("Feature CPU timings"), "summary must display both owned profiles");
		Check(globals::source.requests == summaryRequests, "summary must leave capture ownership to its measurement suite");
		globals::source.enabled = false;
		Check(Draw("ImageBasedLighting", -1).contains("Enable it on the main Profiling page") && !globals::source.enabled, "performance summaries display the main-switch explanation instead of stale timings while profiling is off");
		globals::source.enabled = true;
		Check(Draw("Screenshot", -1).empty(), "excluded feature reopened the performance summary");
		AddTimer("TextureStreaming::Upload", .1f, .01f);
		const auto requests = globals::source.requests;
		Check(!Util::FeatureProfiling::Find("TextureStreaming") && !ProfilingRenderer::CanProfileFeature("TextureStreaming") && !ProfilingRenderer::HasFeatureTimers("TextureStreaming"), "retained texture upload samples reopened profiling");
		Check(Draw("TextureStreaming", 1).empty() && Draw("TextureStreaming", -1).empty() && globals::source.requests == requests, "excluded texture streaming started profiling or displayed timing controls");
		Check(Draw("Screenshot", 1).empty() && !ProfilingRenderer::HasFeatureTimers("Screenshot"), "stale screenshot samples reopened profiling");
		Check(Draw("Wetterness", 0).contains("profiling is off") && globals::source.requests == requests, "off mode or excluded feature requested capture");
		const auto retained = globals::source.results;
		globals::source.results.clear();
		AddTimer("Wetterness::GpuOnly", .5f, -1);
		Check(!ProfilingRenderer::HasFeatureTimers("Wetterness"), "CPU-only ownership accepted unrelated GPU samples");
		globals::source.results.clear();
		AddTimer("IBLExtra::Pass", .5f, .1f);
		Check(!ProfilingRenderer::HasFeatureTimers("ImageBasedLighting"), "feature mapping crossed a timer namespace boundary");
		Check(Draw("ImageBasedLighting", 1).contains("No samples"), "empty mapped view claimed timing coverage");
		std::string label;
		Check(!TryMatchTimingPrefix("", "", true, label), "empty timer name matched an empty feature prefix");
		globals::source.results = retained;
		globals::source.initialized = false;
		const auto unavailableRequests = globals::source.requests;
		Check(Draw("Wetterness", 1).contains("Profiler is unavailable"), "uninitialized profiler requested capture");
		Check(!ProfilingRenderer::HasFeatureTimers("CSUtility"), "uninitialized profiler exposed retained samples");
		Check(ProfilingRenderer::CollectFeatureTimingData(std::string("CSUtility"), false).entries.empty(), "uninitialized profiler returned stale samples");
		globals::profiler = nullptr;
		Check(Draw("ImageBasedLighting", 1).contains("Profiler is unavailable"), "missing profiler crashed a registered view");
		Check(Draw("ImageBasedLighting", -1).contains("Profiler is unavailable"), "missing profiler crashed the integrated summary");
		Check(ProfilingRenderer::CollectFeatureTimingData(std::string("ImageBasedLighting"), false).entries.empty(), "missing profiler was dereferenced by timing collection");
		Check(!ProfilingRenderer::RenderFeatureOverview(), "missing profiler rendered an overview");
		Check(globals::source.requests == unavailableRequests, "unavailable profiler received capture requests");
		globals::profiler = &globals::source;
		globals::source.initialized = true;
		ImGui::DestroyContext();
		std::cout << "Feature profiling attribution and native UI checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
