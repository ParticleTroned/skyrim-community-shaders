#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

enum class ControllerDevice
{
	Primary,
	Secondary,
	Both
};
struct ButtonCombo
{
	ControllerDevice device;
	unsigned key;
	auto GetDevice() const { return device; }
	auto GetKey() const { return key; }
};
namespace logger
{
	template <class... T>
	void debug(T&&...)
	{}
}
namespace Util
{
	double GetNowSecs() { return 0; }
}
struct Menu
{
	struct KeyEvent
	{
		unsigned device = 0, keyCode = 0, eventType = 0;
		bool pressed = false, down = false;
		bool IsPressed() const { return pressed; }
		bool IsDown() const { return down; }
	};
};
namespace RE
{
	namespace INPUT_EVENT_TYPE
	{
		constexpr unsigned kButton = 0, kThumbstick = 1;
	}
	struct BSOpenVRControllerDevice
	{
		struct Keys
		{
			static constexpr unsigned kGrip = 1, kGripAlt = 2, kTrigger = 3, kJoystickTrigger = 4,
									  kTouchpadClick = 5, kTouchpadAlt = 6, kXA = 7, kBY = 8;
		};
		static inline bool leftHanded = false;
		static bool IsLeftHandedMode() { return leftHanded; }
		static bool IsPrimaryController(unsigned device) { return device == 0; }
		static bool IsSecondaryController(unsigned device) { return device == 1; }
		static bool IsGripButton(unsigned) { return false; }
		static bool IsTriggerButton(unsigned) { return false; }
		static bool IsStickClick(unsigned) { return false; }
		static bool IsTouchpadClick(unsigned) { return false; }
		static bool IsAButton(unsigned) { return false; }
		static bool IsBButton(unsigned) { return false; }
	};
	struct ButtonState
	{
		bool isPressed = false;
		void OnEvent(bool pressed, double) { isPressed = pressed; }
	};
}
namespace globals
{
	struct Menu
	{
		bool IsEnabled = true, overlayVisible = false;
		bool IsMenuSessionOpen() { return IsEnabled; }
	} menuInstance;
	auto* menu = &menuInstance;
	struct State
	{
		bool isMainMenuOpen = false;
	} stateInstance;
	auto* state = &stateInstance;
	namespace game
	{
		struct UI
		{
			bool IsMenuOpen(const char*) { return false; }
		} uiInstance;
		auto* ui = &uiInstance;
	}
	namespace features
	{
		struct Upscaling
		{
			unsigned toggles = 0;
			bool ToggleNeuralRendering()
			{
				++toggles;
				return true;
			}
		} upscaling;
	}
}
struct VR
{
	struct Settings
	{
		std::vector<ButtonCombo> VRNeuralRenderingToggleKeys;
		bool VRMenuControllerDiagnosticsTestMode = false;
	} settings;
	std::array<RE::ButtonState, 64> primaryControllerState{}, secondaryControllerState{};
	bool lastKnownLeftHandedMode = false;
	bool isCapturingCombo = false, neuralRenderingToggleHeld = false;
	void ResetMenuInputRuntimeState()
	{
		isCapturingCombo = false;
		settings.VRMenuControllerDiagnosticsTestMode = false;
	}
	bool IsControllerComboPressed(const std::vector<ButtonCombo>&) const;
	void UpdateNeuralRenderingToggleFromInput(bool);
	void ProcessVREvents(std::vector<Menu::KeyEvent>&);
	void ProcessVRButtonEvent(const Menu::KeyEvent&) {}
	void UpdateControllerState(const Menu::KeyEvent&) {}
};
#include "neural_controller_toggle_under_test.h"

int main()
{
	VR vr;
	auto& count = globals::features::upscaling.toggles;
	const auto require = [](bool value) { if (!value) throw std::runtime_error("NR controller toggle edge failed"); };
	vr.settings.VRNeuralRenderingToggleKeys = { { ControllerDevice::Both, 1 } };
	vr.primaryControllerState[1].isPressed = true;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 0);
	vr.secondaryControllerState[1].isPressed = true;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 1);
	vr.primaryControllerState[1].isPressed = false;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	vr.isCapturingCombo = true;
	vr.primaryControllerState[1].isPressed = true;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	vr.isCapturingCombo = false;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 1);
	vr.primaryControllerState[1].isPressed = false;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	vr.primaryControllerState[1].isPressed = true;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 2);
	vr.isCapturingCombo = true;
	vr.primaryControllerState[1].isPressed = false;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	vr.isCapturingCombo = false;
	vr.primaryControllerState[1].isPressed = true;
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 3);
	vr.settings.VRNeuralRenderingToggleKeys.clear();
	vr.UpdateNeuralRenderingToggleFromInput(true);
	require(count == 3);
	const auto events = [&](std::initializer_list<Menu::KeyEvent> input) {
		std::vector<Menu::KeyEvent> queue(input);
		vr.ProcessVREvents(queue);
	};
	vr.settings.VRNeuralRenderingToggleKeys = { { ControllerDevice::Primary, 1 } };
	events({ { 0, 1, 0, true, true }, { 0, 1, 0, false, false } });
	require(count == 4);
	events({ { 0, 1, 0, true, true }, { 0, 1, 0, true, false } });
	require(count == 5);
	// Menu state resets must not turn a held-button repeat into another toggle.
	vr.primaryControllerState = {};
	events({ { 0, 2, 0, true, true }, { 0, 1, 0, true, false } });
	require(count == 5);
	events({ { 0, 1, 0, false, false }, { 0, 1, 0, true, true } });
	require(count == 6);
	vr.settings.VRMenuControllerDiagnosticsTestMode = true;
	events({ { 0, 1, 0, false, false }, { 0, 1, 0, true, true } });
	vr.settings.VRMenuControllerDiagnosticsTestMode = false;
	events({ { 0, 1, 0, true, false } });
	require(count == 6);
	vr.isCapturingCombo = true;
	events({ { 0, 1, 0, false, false }, { 0, 1, 0, true, true } });
	vr.isCapturingCombo = false;
	events({ { 0, 1, 0, true, false } });
	require(count == 6);
	RE::BSOpenVRControllerDevice::leftHanded = true;
	events({ { 0, 1, 0, true, true } });
	require(count == 7);
}
