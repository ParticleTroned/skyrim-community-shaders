#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spdlog::level
{
	enum level_enum
	{
		trace,
		debug,
		info,
		warn,
		err,
		critical,
		off
	};
}
struct State
{
	spdlog::level::level_enum level = spdlog::level::info;
	spdlog::level::level_enum GetLogLevel() const { return level; }
	bool IsDeveloperMode();
};
namespace globals
{
	State* state = nullptr;
	namespace features
	{
		struct Upscaling
		{
			unsigned draws = 0;
			int GetRuntimeUpscaleMethod() const { return 0; }
			void DrawNeuralRenderingSettings(int) { ++draws; }
		} upscaling;
	}
}
namespace ImGui
{
	std::vector<std::string> items;
	std::string clicked;
	int treeDepth = 0;
	bool openTrees = true;
	void Record(const char* label) { items.emplace_back(label); }
	bool Seen(std::string_view label)
	{
		return std::find(items.begin(), items.end(), label) != items.end();
	}
	void Clear(std::string_view click = {})
	{
		items.clear();
		clicked = click;
		treeDepth = 0;
	}
	template <class... Args>
	void TextWrapped(const char* label, Args&&...)
	{
		Record(label);
	}
	void TextUnformatted(const char* label) { Record(label); }
	void SeparatorText(const char* label) { Record(label); }
	void Separator() {}
	void PushID(int) {}
	void PopID() {}
	bool Button(const char* label)
	{
		Record(label);
		return clicked == label;
	}
	bool Checkbox(const char* label, bool* value)
	{
		if (!Button(label))
			return false;
		*value = !*value;
		return true;
	}
	template <class... Args>
	bool Combo(const char* label, Args&&...)
	{
		Record(label);
		return false;
	}
	bool SliderFloat(const char* label, float*, float, float)
	{
		Record(label);
		return false;
	}
	bool TreeNode(const char* label)
	{
		Record(label);
		if (openTrees)
			++treeDepth;
		return openTrees;
	}
	void TreePop() { --treeDepth; }
}
#define IM_ARRAYSIZE(value) (sizeof(value) / sizeof((value)[0]))

namespace NeuralRendering::Color
{
	struct Registry
	{
		Configuration configuration{};
		bool accept = true;
		static Registry& Instance()
		{
			static Registry instance;
			return instance;
		}
		Configuration Snapshot() const { return configuration; }
		bool Configure(const Settings& settings, const Experiments& experiments, std::uint64_t revision)
		{
			if (!accept || revision != configuration.revision || !Valid(settings) || !Valid(experiments))
				return false;
			configuration.settings = settings;
			configuration.experiments = experiments;
			++configuration.revision;
			return true;
		}
	};
}
using namespace NeuralRendering::Color;
unsigned statusReads = 0, assetReads = 0;
nlohmann::json StatusJson()
{
	++statusReads;
	return nlohmann::json::object();
}
nlohmann::json AssetsJson()
{
	++assetReads;
	return nlohmann::json::object();
}
struct NeuralRenderingFeature
{
	void DrawSettings();
};
#include "neural_rendering_ui_under_test.h"

int main()
{
	const auto require = [](bool condition, const char* message) {
		if (!condition)
			throw std::runtime_error(message);
	};
	State state;
	globals::state = &state;
	NeuralRenderingFeature feature;
	auto& registry = Registry::Instance();
	registry.configuration.settings.mode = Mode::PreserveSource;
	registry.configuration.settings.detailStrength = 1.25f;
	const auto draw = [&](std::string_view click = {}) {
		ImGui::Clear(click);
		feature.DrawSettings();
		require(ImGui::treeDepth == 0, "UI tree scopes must be balanced");
	};
	for (auto level : { spdlog::level::info, spdlog::level::warn, spdlog::level::err,
			 spdlog::level::critical, spdlog::level::off }) {
		state.level = level;
		draw();
		require(ImGui::Seen("Enable colour processing") && ImGui::Seen("Colour mode") &&
					ImGui::Seen("Detail contribution"),
			"Production colour controls remain available");
		require(!ImGui::Seen("Colour experiments and diagnostics") && statusReads == 0 && assetReads == 0,
			"Info and quieter levels cannot expose or generate diagnostics");
	}
	for (auto level : { spdlog::level::debug, spdlog::level::trace }) {
		state.level = level;
		draw();
		for (const auto* control : { "Apply neural edit (A/B; inference stays running)", "Capture engine HDR exposure",
				 "Capture HMD frame provenance", "Source-domain candidate", "Transport bypass (skip neural evaluation)",
				 "Bounded asynchronous colour samples", "Colour diagnostics", "Check installed colour shaders" })
			require(ImGui::Seen(control), "Debug and Trace expose the complete assessment controls");
	}
	require(statusReads == 2 && assetReads == 0, "Asset checks run only on explicit request");
	draw("Check installed colour shaders");
	require(assetReads == 1, "Explicit shader check must run");
	state.level = spdlog::level::info;
	registry.configuration.experiments.transportBypass = true;
	registry.configuration.experiments.applyModelEdit = false;
	const auto before = registry.Snapshot();
	draw();
	require(registry.configuration.experiments == before.experiments && registry.configuration.revision == before.revision,
		"Hiding controls must not silently cancel active experiments");
	require(ImGui::Seen("Restore normal colour processing") && !ImGui::Seen("Colour experiments and diagnostics"),
		"Hidden output overrides retain a production recovery action");
	registry.accept = false;
	draw("Restore normal colour processing");
	require(registry.configuration.experiments == before.experiments, "Rejected reset must retain active settings");
	require(ImGui::Seen("Settings changed concurrently; retry after the next UI refresh."), "Rejected reset must be visible");
	registry.accept = true;
	draw("Restore normal colour processing");
	require(registry.configuration.experiments == Experiments{} && registry.configuration.settings == before.settings,
		"Recovery clears only experiments, preserving colour mode and tuning");
	registry.configuration.experiments.captureFrameEvidence = true;
	registry.configuration.experiments.diagnostics = true;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Capture-only diagnostics do not claim an image override");
	registry.configuration.experiments.profiles[0].domain = Domain::Linear;
	registry.configuration.experiments.profiles[0].transform = Transform::LinearToSRGB;
	draw();
	require(ImGui::Seen("Restore normal colour processing"), "Hidden colour calibration must remain recoverable");
	registry.configuration.settings.enabled = false;
	draw();
	require(!ImGui::Seen("Restore normal colour processing"), "Disabled processing cannot claim active profile overrides");
	globals::state = nullptr;
	draw();
	require(!ImGui::Seen("Colour experiments and diagnostics"), "Missing state fails closed for diagnostics");
	require(globals::features::upscaling.draws > 0, "Feature must retain the main NR controls");
}
