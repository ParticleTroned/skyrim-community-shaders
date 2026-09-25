#include "Features/VRDepthCullingCacheRefreshPolicy.h"
#include "Features/VRDepthCullingEnablePolicy.h"
#include "Features/VRDepthCullingTemporal.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::fprintf(stderr, "%s\n", a_message);
			std::exit(EXIT_FAILURE);
		}
	}

	struct Control
	{
		std::string kind;
		std::string path;
		std::string label;
		bool disabled = false;
		bool selected = false;
	};

	std::vector<Control> controls;
	std::vector<std::string> ids;
	std::vector<std::string> text;
	std::unordered_map<std::string, bool> checkboxEdits;
	std::unordered_map<std::string, float> sliderEdits;
	std::string radioClick;
	bool controlsDisabled = false;
	int openTables = 0;

	std::string CurrentPath()
	{
		std::string result;
		for (const auto& id : ids)
			result += "/" + id;
		return result;
	}

	struct State
	{
		bool developerMode = false;
		bool IsDeveloperMode() const { return developerMode; }
	} state;
}

namespace globals
{
	State* state = &::state;
}

namespace LocationContext
{
	bool interior = false;
	bool HasInteriorCell() { return interior; }
}

namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}

namespace VRDepthCullingTemporal
{
	Mode publishedMode = Mode::Balanced;
	unsigned modePublications = 0;
	bool publishedEnabled = false;
	void SetMode(Mode a_mode)
	{
		publishedMode = a_mode;
		++modePublications;
	}
	void SetCullingEnabled(bool a_enabled) { publishedEnabled = a_enabled; }
}

constexpr int ImGuiTableFlags_SizingStretchSame = 1;
namespace ImGui
{
	void PushID(const char* a_id) { ids.emplace_back(a_id); }
	void PopID()
	{
		Require(!ids.empty(), "Unbalanced ImGui PopID");
		ids.pop_back();
	}
	void SeparatorText(const char*) {}
	bool BeginTable(const char*, int a_columns, int)
	{
		Require(a_columns == 2, "Depth controls must use two columns");
		++openTables;
		return true;
	}
	void EndTable() { --openTables; }
	void TableNextColumn() {}
	void SetNextItemWidth(float) {}
	void TextUnformatted(const char* a_text) { text.emplace_back(a_text); }
	bool Checkbox(const char* a_label, bool* a_value)
	{
		const auto path = CurrentPath();
		controls.push_back({ "checkbox", path, a_label, controlsDisabled, *a_value });
		const auto edit = checkboxEdits.find(path);
		if (controlsDisabled || edit == checkboxEdits.end() || *a_value == edit->second)
			return false;
		*a_value = edit->second;
		return true;
	}
	bool SliderFloat(const char* a_label, float* a_value, float a_minimum, float a_maximum, const char*)
	{
		Require(a_minimum == VRDepthCullingEnablePolicy::kMinimumExtent &&
					a_maximum == VRDepthCullingEnablePolicy::kMaximumExtent,
			"Extent slider bounds differ from the production policy");
		const auto path = CurrentPath();
		controls.push_back({ "slider", path, a_label, controlsDisabled });
		const auto edit = sliderEdits.find(path);
		if (controlsDisabled || edit == sliderEdits.end() || *a_value == edit->second)
			return false;
		*a_value = edit->second;
		return true;
	}
	bool RadioButton(const char* a_label, bool a_selected)
	{
		controls.push_back({ "radio", CurrentPath(), a_label, controlsDisabled, a_selected });
		return !controlsDisabled && radioClick == a_label;
	}
}

namespace Util
{
	class DisableGuard
	{
	public:
		explicit DisableGuard(bool a_disabled) : previous(controlsDisabled)
		{
			controlsDisabled = controlsDisabled || a_disabled;
		}
		~DisableGuard() { controlsDisabled = previous; }

	private:
		bool previous;
	};
	bool HoverTooltipWrapper() { return false; }
}

struct VR
{
	struct Settings
	{
#include "vr_depth_culling_settings_members.h"
		unsigned clampCalls = 0;
		void ClampToValidRanges() { ++clampCalls; }
	} settings;

	bool nativeEnabled = false;
	float nativeExtent = -1.0f;
	bool* gDepthBufferCulling = &nativeEnabled;
	float* gMinOccludeeBoxExtent = &nativeExtent;
	std::atomic_bool depthCullingCacheRefreshCompleted{ false };
	std::atomic_bool depthCullingCacheRefreshPending{ false };

	void UpdateDepthBufferCulling();
	void SetDepthCullingMode(VRDepthCullingTemporal::Mode a_mode);
	VRDepthCullingTemporal::Mode GetDepthCullingMode() const;
};

#include "vr_depth_culling_settings_ui_under_test.h"

namespace
{
	void BeginFrame(bool a_developerMode)
	{
		Require(ids.empty() && openTables == 0 && !controlsDisabled, "Previous UI frame leaked state");
		state.developerMode = a_developerMode;
		controls.clear();
		text.clear();
		checkboxEdits.clear();
		sliderEdits.clear();
		radioClick.clear();
	}

	void Draw(VR& a_vr)
	{
		DrawDepthCullingSettings(a_vr, "DepthTest");
		Require(ids.empty() && openTables == 0 && !controlsDisabled, "Depth UI leaked ImGui state");
	}

	void RequireLocationPairs()
	{
		Require(controls.size() >= 4, "Missing location controls");
		for (unsigned location = 0; location < 2; ++location) {
			const char* label = location == 0 ? "Exterior" : "Interior";
			const std::string path = std::string("/DepthTest/") + label;
			const auto& toggle = controls[location * 2];
			const auto& slider = controls[location * 2 + 1];
			Require(toggle.kind == "checkbox" && toggle.label == label && toggle.path == path,
				"Location enable control is missing or incorrectly scoped");
			Require(slider.kind == "slider" && slider.label == "Minimum Object Size" && slider.path == path,
				"Location extent slider is missing or incorrectly associated");
		}
	}

	void TestInfoControlsAndIndependentEdits()
	{
		VR vr;
		Require(vr.GetDepthCullingMode() == VRDepthCullingTemporal::Mode::Balanced,
			"Production defaults must select Advanced");
		BeginFrame(false);
		Draw(vr);
		RequireLocationPairs();
		Require(controls.size() == 4 && text.empty(), "Info exposes policy controls or extra widgets");
		Require(!controls[1].disabled && !controls[3].disabled, "Default location sliders should be enabled");
		Require(vr.settings.clampCalls == 0, "Drawing unchanged Info settings modified settings");

		LocationContext::interior = true;
		BeginFrame(false);
		checkboxEdits["/DepthTest/Exterior"] = false;
		sliderEdits["/DepthTest/Exterior"] = 800.0f;
		sliderEdits["/DepthTest/Interior"] = 42.0f;
		Draw(vr);
		RequireLocationPairs();
		Require(!vr.settings.EnableDepthBufferCullingExterior && vr.settings.EnableDepthBufferCullingInterior,
			"Disabling exterior changed the independent interior enable");
		Require(vr.settings.MinOccludeeBoxExtentExterior == VRDepthCullingEnablePolicy::kDefaultMinimumExtent &&
					vr.settings.MinOccludeeBoxExtentInterior == 42.0f,
			"Disabled exterior slider accepted edits or interior slider changed the wrong extent");
		Require(controls[1].disabled && !controls[3].disabled, "Disabling exterior disabled the wrong slider");
		Require(vr.nativeEnabled && vr.nativeExtent == 42.0f && vr.settings.clampCalls == 1,
			"Interior UI edit did not publish its production enable/extent selection");

		LocationContext::interior = false;
		vr.UpdateDepthBufferCulling();
		Require(!vr.nativeEnabled && vr.nativeExtent == VRDepthCullingEnablePolicy::kDefaultMinimumExtent,
			"Exterior did not select its independent values");
		BeginFrame(false);
		checkboxEdits["/DepthTest/Exterior"] = true;
		checkboxEdits["/DepthTest/Interior"] = false;
		sliderEdits["/DepthTest/Exterior"] = 85.0f;
		sliderEdits["/DepthTest/Interior"] = 900.0f;
		Draw(vr);
		Require(vr.nativeEnabled && vr.nativeExtent == 85.0f && vr.settings.MinOccludeeBoxExtentInterior == 42.0f,
			"Exterior edit changed the independent interior extent");
		Require(!controls[1].disabled && controls[3].disabled, "Disabling interior disabled the wrong slider");
		LocationContext::interior = true;
		vr.UpdateDepthBufferCulling();
		Require(!vr.nativeEnabled && vr.nativeExtent == 42.0f, "Interior retained exterior engine values");
	}

	void TestDebugSelectionSurvivesInfo()
	{
		VR vr;
		BeginFrame(true);
		Draw(vr);
		RequireLocationPairs();
		Require(controls.size() == 6 && text == std::vector<std::string>{ "Culling Method" },
			"Debug should expose exactly Advanced and Legacy policy controls");
		Require(controls[4].kind == "radio" && controls[4].label == "Advanced (Default)" && controls[4].selected &&
					controls[5].kind == "radio" && controls[5].label == "Legacy" && !controls[5].selected,
			"Debug policy labels or default selection are incorrect");

		BeginFrame(true);
		radioClick = "Legacy";
		Draw(vr);
		Require(vr.settings.DepthCullingLegacyMode && vr.GetDepthCullingMode() == VRDepthCullingTemporal::Mode::Legacy &&
					VRDepthCullingTemporal::publishedMode == VRDepthCullingTemporal::Mode::Legacy,
			"Debug Legacy click did not publish the production mode");
		const auto publications = VRDepthCullingTemporal::modePublications;
		BeginFrame(false);
		Draw(vr);
		Require(controls.size() == 4 && text.empty(), "Info exposed the selected Legacy policy controls");
		Require(vr.settings.DepthCullingLegacyMode && vr.GetDepthCullingMode() == VRDepthCullingTemporal::Mode::Legacy &&
					VRDepthCullingTemporal::publishedMode == VRDepthCullingTemporal::Mode::Legacy &&
					VRDepthCullingTemporal::modePublications == publications,
			"Returning to Info silently changed Legacy mode");

		BeginFrame(true);
		Draw(vr);
		Require(!controls[4].selected && controls[5].selected, "Returning to Debug lost the persisted Legacy selection");
		BeginFrame(true);
		radioClick = "Advanced (Default)";
		Draw(vr);
		Require(!vr.settings.DepthCullingLegacyMode && vr.GetDepthCullingMode() == VRDepthCullingTemporal::Mode::Balanced &&
					VRDepthCullingTemporal::publishedMode == VRDepthCullingTemporal::Mode::Balanced,
			"Debug Advanced click did not restore the default policy");
	}
}

int main()
{
	TestInfoControlsAndIndependentEdits();
	TestDebugSelectionSurvivesInfo();
	std::puts("Depth culling production UI tests passed");
	return 0;
}
