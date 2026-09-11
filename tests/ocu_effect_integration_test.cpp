#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Defines = std::vector<std::pair<const char*, const char*>>;

struct ID3D11ComputeShader
{
	std::string filename;
	Defines defines;
};
namespace winrt
{
	template <class T>
	struct com_ptr : std::unique_ptr<T>
	{
		using std::unique_ptr<T>::operator=;
		void attach(T* value) { this->reset(value); }
	};
}
namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}
namespace logger
{
	template <class... Args>
	void warn(Args&&...)
	{}
}

bool HasDefine(const Defines& defines, std::string_view name)
{
	for (const auto& define : defines)
		if (name == define.first)
			return true;
	return false;
}
namespace Util
{
	struct ShaderCompileTiming
	{};
	int optionalCalls = 0;
	int failure = 0;
	bool failGI = false;
	bool failRequired = false;
	ID3D11ComputeShader* CompileShader(const wchar_t* path, const Defines& defines, const char*, const char*, ShaderCompileTiming*)
	{
		const auto filename = std::filesystem::path(path).filename().string();
		if (filename == "giOCUEffect.cs.hlsl") {
			++optionalCalls;
			if (HasDefine(defines, "GI") == failGI) {
				if (failure == 1)
					return nullptr;
				if (failure == 2)
					throw std::runtime_error("injected optional compiler failure");
			}
		} else if (failRequired && filename == "gi.cs.hlsl") {
			return nullptr;
		}
		return new ID3D11ComputeShader{ filename, defines };
	}
}

// Only compiler/engine boundaries are substituted; compile the production
// batch, setting transition and DevBench handler below without reimplementing them.
struct ScreenSpaceGI
{
	struct Settings
	{
		bool Enabled = true, ExperimentalOCUEffectFoveation = true;
		int ResolutionMode = 0;
		bool EnableTemporalDenoiser = true, EnableGI = true;
		bool EnableExperimentalSpecularGI = false, EnableAdaptiveSampling = true;
	} settings;
	bool loaded = true, resources = true, recompileFlag = false;
	std::atomic_bool queuedResetHistory{ false }, ocuEffectActive{ false };
	std::atomic<const char*> ocuEffectStatus{ "disabled" };
	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute, prefilterNormalCompute,
		prefilterRadianceCompute, radianceDisoccAOOnlyCompute, radianceDisoccCompute,
		giAOOnlyCompute, giCompute, centerGIMaskedAOOnlyCompute, centerGIMaskedCompute,
		upsampleAOOnlyCompute, upsampleCompute, centerBlendAOOnlyCompute, centerBlendCompute,
		giAOOnlyOCUEffectCompute, giOCUEffectCompute, giAOOnlyEye0OnlyCompute, giEye0OnlyCompute,
		reprojectAOOnlyCompute, reprojectCompute, stereoSyncAOOnlyCompute, stereoSyncCompute,
		centerStereoSyncAOOnlyCompute, centerStereoSyncCompute, blurCompute;
	bool HasGIResources() const { return resources; }
	bool CompileComputeShaders(Util::ShaderCompileTiming* timing = nullptr);
	void SetOCUEffectFoveationEnabled(bool enabled);
};
#include "ocu_setting_under_test.h"
#include "ocu_shader_batch_under_test.h"

namespace globals
{
	namespace features
	{
		ScreenSpaceGI screenSpaceGI;
	}
	namespace game
	{
		bool& isVR = REL::Module::vr;
	}
}
int mainThreadCalls = 0;
json RunOnMainThread(std::function<json()> run)
{
	++mainThreadCalls;
	return run();
}
#include "ocu_devbench_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void TestShaderFailures()
{
	for (bool gi : { false, true }) {
		for (int failure : { 1, 2 }) {
			ScreenSpaceGI effect;
			Util::failure = 0;
			Require(effect.CompileComputeShaders(), "initial compile failed");
			Util::failure = failure;
			Util::failGI = gi;
			effect.recompileFlag = true;
			Require(effect.CompileComputeShaders() && !effect.recompileFlag, "optional failure blocked native batch or caused retry loop");
			Require(effect.giCompute && effect.giAOOnlyCompute && effect.prefilterDepthsCompute, "native fallback was discarded");
			Require(static_cast<bool>(effect.giOCUEffectCompute) != gi && static_cast<bool>(effect.giAOOnlyOCUEffectCompute) == gi,
				"optional failure retained stale shader or discarded the independent variant");
		}
	}
	Util::failure = 0;
	ScreenSpaceGI effect;
	Require(effect.CompileComputeShaders(), "initial required batch failed");
	auto* previous = effect.giCompute.get();
	Util::failRequired = true;
	Util::optionalCalls = 0;
	Require(!effect.CompileComputeShaders() && effect.giCompute.get() == previous, "required batch lost transactional assignment");
	Require(!effect.giOCUEffectCompute && !effect.giAOOnlyOCUEffectCompute && !Util::optionalCalls, "failed required batch left optional permutations usable");
	Util::failRequired = false;
}

void TestPermutations()
{
	for (bool vr : { false, true })
		for (bool enabled : { false, true })
			for (bool resources : { false, true })
				for (int resolution : { 0, 1, 2 })
					for (bool temporal : { false, true })
						for (bool adaptive : { false, true }) {
							REL::Module::vr = vr;
							ScreenSpaceGI effect;
							effect.resources = resources;
							effect.settings = { true, enabled, resolution, temporal, true, false, adaptive };
							Require(effect.CompileComputeShaders(), "permutation compile failed");
							Require(static_cast<bool>(effect.giOCUEffectCompute) == (vr && enabled && resources), "GI variant eligibility wrong");
							Require(static_cast<bool>(effect.giAOOnlyOCUEffectCompute) == (vr && enabled), "AO variant eligibility wrong");
							for (auto* shader : { effect.giOCUEffectCompute.get(), effect.giAOOnlyOCUEffectCompute.get() }) {
								if (!shader)
									continue;
								Require(HasDefine(shader->defines, "VR") && HasDefine(shader->defines, "HALF_RES") == (resolution == 1) &&
											HasDefine(shader->defines, "QUARTER_RES") == (resolution == 2) &&
											HasDefine(shader->defines, "TEMPORAL_DENOISER") == temporal && HasDefine(shader->defines, "ADAPTIVE_SAMPLING") == adaptive &&
											HasDefine(shader->defines, "GI") == (shader == effect.giOCUEffectCompute.get()),
									"optional and native compilation policies diverged");
							}
							effect.settings.ExperimentalOCUEffectFoveation = false;
							Require(effect.CompileComputeShaders() && !effect.giOCUEffectCompute && !effect.giAOOnlyOCUEffectCompute, "disabled permutation retained optional shaders");
						}
}

void TestDevBench()
{
	auto& effect = globals::features::screenSpaceGI;
	REL::Module::vr = true;
	effect.settings.ExperimentalOCUEffectFoveation = false;
	json request = { { "action", "set_ocu_foveation" } };
	Require(BuildOCUEffectFoveationResult(request).contains("error") && mainThreadCalls == 0, "missing boolean accepted");
	for (const json& invalid : { json(nullptr), json(1), json("true"), json::array(), json::object() }) {
		request["enabled"] = invalid;
		Require(BuildOCUEffectFoveationResult(request).contains("error") && mainThreadCalls == 0, "invalid boolean reached mutation");
	}
	request["enabled"] = true;
	for (bool vr : { false, true }) {
		REL::Module::vr = vr;
		effect.loaded = !vr;
		Require(BuildOCUEffectFoveationResult(request).contains("error") && !effect.settings.ExperimentalOCUEffectFoveation, "unavailable runtime accepted mutation");
	}
	effect.loaded = true;
	Require(BuildOCUEffectFoveationResult(request).at("requested") == true && effect.recompileFlag && effect.queuedResetHistory,
		"enable did not stage shaders and history reset");
	effect.recompileFlag = false;
	effect.queuedResetHistory = false;
	BuildOCUEffectFoveationResult(request);
	Require(!effect.recompileFlag && !effect.queuedResetHistory, "idempotent request recompiled shaders");
	effect.ocuEffectActive = true;
	const auto status = BuildOCUEffectFoveationResult({ { "action", "ocu_foveation" } });
	Require(status.at("active") == true && !effect.recompileFlag, "inspection mutated setting");
	request["enabled"] = false;
	Require(BuildOCUEffectFoveationResult(request).at("active") == false && !effect.ocuEffectActive && effect.recompileFlag && effect.queuedResetHistory,
		"disable retained active telemetry or history");
}

int main()
{
	try {
		TestShaderFailures();
		TestPermutations();
		TestDevBench();
		std::cout << "PASS: optional failure isolation, 96 shader permutations and DevBench setting lifecycle\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
