#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
bool widgetsDisabled = false, editRequested = true;
int menuDraws = 0, controlDraws = 0, desktopDraws = 0, popupDraws = 0, diagnosticDraws = 0, notifications = 0;
bool acceptRecording = false;
int popupCloses = 0;
namespace ImGui
{
	void OpenPopup(const char*) { ++popupDraws; }
	void CloseCurrentPopup() { ++popupCloses; }
}
struct VR
{
	bool loaded = false, renderingEnabled = false;
	struct Settings
	{
		int preference = 0, renderingPreference = 9;
		bool operator==(const Settings&) const = default;
	} settings;
	struct OpenVRInfo
	{
		bool isCompatible = true;
	} openVRInfo;
	bool isCapturingCombo = false;
	int saves = 0, recordingDraws = 0, cancellations = 0;
	static void DrawMenuInterfaceSettings();
	static void DrawControlInterfaceSettings();
	static void DrawDesktopInterfaceSettings();
	static void DrawInterfaceDiagnostics();
	void DrawComboRecordingPopup();
	void DrawComboRecordingContents()
	{
		++recordingDraws;
		if (acceptRecording) {
			++settings.preference;
			isCapturingCombo = false;
		}
	}
	void ResetComboRecordingState()
	{
		isCapturingCombo = false;
		++cancellations;
	}
	void SaveSettings(json& a_json)
	{
		++saves;
		settings.renderingPreference = 0;
		a_json = { { "preference", settings.preference } };
	}
};
namespace globals
{
	namespace game
	{
		bool isVR = false;
	}
	struct Menu
	{
		int dirtyChecks = 0;
		void RequestSettingsDirtyCheck() { ++dirtyChecks; }
	} menuStorage;
	Menu* menu = &menuStorage;
	int stateStorage = 0;
	int* state = &stateStorage;
	namespace features
	{
		VR vr;
	}
}
namespace Util
{
	bool CenteredPopupModal(const char*) { return true; }
	struct DisableGuard
	{
		bool previous;
		explicit DisableGuard(bool a_disabled) : previous(widgetsDisabled) { widgetsDisabled |= a_disabled; }
		~DisableGuard() { widgetsDisabled = previous; }
	};
}
namespace MenuUI
{
	void DetailText(const char*) {}
	struct ActionFeedback
	{
		std::string message;
		bool error = false;
		void Draw() const {}
	};
}
struct PerformanceTuningRenderer
{
	static inline bool busy = false, sceneOwned = false;
	static const char* GetFeatureSceneControlBlockReason(VR*) { return sceneOwned ? "scene owned" : nullptr; }
	static bool HasActiveMeasurements() { return busy; }
	static void NotifyFeatureSettingsChanged(VR*) { ++notifications; }
};
namespace
{
	void EditPreference()
	{
		if (!widgetsDisabled && editRequested)
			++globals::features::vr.settings.preference;
	}
	void DrawMenuSettings()
	{
		++menuDraws;
		EditPreference();
	}
	void DrawControlsSettings()
	{
		++controlDraws;
		EditPreference();
	}
	void DrawDesktopSettings()
	{
		++desktopDraws;
		EditPreference();
	}
	void DrawDebugSection() { ++diagnosticDraws; }
}
#include "vr_interface_settings_under_test.h"

int main()
try {
	const auto require = [](bool a_condition, const char* a_message) {
		if (!a_condition)
			throw std::runtime_error(a_message);
	};
	const auto drawAll = [] {
		VR::DrawMenuInterfaceSettings();
		VR::DrawControlInterfaceSettings();
		VR::DrawDesktopInterfaceSettings();
		VR::DrawInterfaceDiagnostics();
	};
	drawAll();
	require(menuDraws + controlDraws + desktopDraws + popupDraws + diagnosticDraws == 0, "SE/AE must not render VR interface controls");
	globals::game::isVR = true;
	for (bool loaded : { false, true }) {
		for (bool renderingEnabled : { false, true }) {
			auto& vr = globals::features::vr;
			vr.loaded = loaded;
			vr.renderingEnabled = renderingEnabled;
			vr.isCapturingCombo = true;
			const int before = vr.settings.preference, popupsBefore = popupDraws;
			drawAll();
			require(vr.settings.preference == before + (loaded ? 3 : 0) && popupDraws == popupsBefore + 1,
				"loaded interface settings remain editable independently of rendering, while unloaded preferences cannot be changed and lost on save");
			if (!loaded)
				require(!vr.isCapturingCombo && controllerBindingFeedback.error, "unloaded preferences cannot accept pending controller recordings");
			require(vr.saves == 0 && vr.settings.renderingPreference == 9, "drawing interface panels never invokes save-time rendering normalization");
		}
	}
	require(notifications == 6 && globals::menu->dirtyChecks == 6, "only persistable settings changes invalidate comparisons and request tracking");
	for (bool sceneOwned : { false, true }) {
		PerformanceTuningRenderer::sceneOwned = sceneOwned;
		PerformanceTuningRenderer::busy = !sceneOwned;
		auto& vr = globals::features::vr;
		vr.isCapturingCombo = true;
		acceptRecording = true;
		const int before = vr.settings.preference, checksBefore = globals::menu->dirtyChecks, recordingsBefore = vr.recordingDraws;
		drawAll();
		require(vr.settings.preference == before && globals::menu->dirtyChecks == checksBefore && !widgetsDisabled,
			"measurement and scene ownership disable mutation and restore UI state");
		require(!vr.isCapturingCombo && vr.recordingDraws == recordingsBefore && controllerBindingFeedback.error,
			"recording is cancelled before keyboard or timeout acceptance can bypass disabled widgets");
	}
	PerformanceTuningRenderer::busy = PerformanceTuningRenderer::sceneOwned = false;
	{
		auto& vr = globals::features::vr;
		vr.openVRInfo.isCompatible = false;
		vr.isCapturingCombo = true;
		const int recordingsBefore = vr.recordingDraws;
		vr.DrawComboRecordingPopup();
		require(!vr.isCapturingCombo && vr.recordingDraws == recordingsBefore && controllerBindingFeedback.error,
			"loss of controller compatibility cancels recording without applying pending input");
		vr.openVRInfo.isCompatible = true;
		vr.isCapturingCombo = true;
		const int preferenceBefore = vr.settings.preference;
		vr.DrawComboRecordingPopup();
		require(!vr.isCapturingCombo && vr.settings.preference == preferenceBefore + 1 && !controllerBindingFeedback.error,
			"valid recordings still apply and clear prior cancellation feedback");
	}
	editRequested = false;
	const int checksBefore = globals::menu->dirtyChecks;
	drawAll();
	require(globals::menu->dirtyChecks == checksBefore, "viewing controls does not dirty settings");
	globals::menu = nullptr;
	drawAll();
	require(!widgetsDisabled, "missing interface services fail without leaking UI state");
	std::cout << "VR interface runtime, rendering independence, recording and ownership checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
