#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	std::vector<std::string> events;
	bool helperAvailable = true;
	bool worldContent = false;
	bool captureRequested = false;
	int demandFrame = 0;
	int tickCount = 0;
	int hookCount = 0;

	struct State
	{
		int frame = 0;
		void Reset()
		{
			++frame;
			events.emplace_back("reset");
		}
	};
	struct Menu
	{
		bool open = false;
		int uiFrames = 0;
		void DrawOverlay()
		{
			events.emplace_back("menu");
			if (!open)
				return;
			++uiFrames;
		}
	};
	struct VR
	{
		bool compatible = true;
		bool IsOpenVRCompatible() const { return compatible; }
		bool InstallSubmitHook()
		{
			++hookCount;
			events.emplace_back("install");
			return true;
		}
	};
}

namespace globals
{
	State stateStorage;
	State* state = &stateStorage;
	Menu menuStorage;
	Menu* menu = &menuStorage;
	namespace game
	{
		bool isVR = true;
	}
	namespace d3d
	{
		void* context = &stateStorage;
	}
	namespace features
	{
		VR vr;
	}
}

namespace CSX::Api
{
	void AdvanceAcceptedDrawFrame(void*) { events.emplace_back("advance"); }
}

namespace ImGuiVRHelperHost
{
	bool IsAvailable() { return helperAvailable; }
	void Tick()
	{
		++tickCount;
		events.emplace_back("tick");
		if (!helperAvailable || !globals::d3d::context)
			return;
		captureRequested = worldContent;
		demandFrame = globals::state->frame;
	}
}

namespace
{
	void PresentFrame()
	{
		auto* state = globals::state;
		auto* menu = globals::menu;
#include "imgui_vr_helper_render_lifecycle_under_test.h"
	}

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Reset()
	{
		events.clear();
		globals::stateStorage = {};
		globals::menuStorage = {};
		globals::game::isVR = true;
		globals::d3d::context = &globals::stateStorage;
		globals::features::vr.compatible = true;
		helperAvailable = true;
		worldContent = false;
		captureRequested = false;
		demandFrame = tickCount = hookCount = 0;
	}

	void ClosedMenuStillAdvancesHostedClients()
	{
		Reset();
		for (bool content : { false, true, false, true }) {
			events.clear();
			worldContent = content;
			PresentFrame();
			Require(captureRequested == content, "Closed menu prevented content-demand refresh");
			Require(demandFrame == globals::state->frame, "Demand was sampled before the current frame advanced");
			Require(hookCount == globals::state->frame, "Closed menu prevented Submit hook installation");
			Require(events == std::vector<std::string>{ "reset", "advance", "tick", "install", "menu" },
				"Hosted lifecycle depends on UI drawing or runs in the wrong order");
			Require(globals::menu->uiFrames == 0, "Hosted clients opened or rendered a CSX menu");
		}
		globals::menu->open = true;
		PresentFrame();
		Require(tickCount == 5 && hookCount == 5 && globals::menu->uiFrames == 1,
			"Opening the menu duplicated or skipped hosted lifecycle work");
	}

	void RuntimeAndAvailabilityRemainIndependent()
	{
		Reset();
		globals::game::isVR = false;
		worldContent = true;
		PresentFrame();
		Require(tickCount == 0 && hookCount == 0 && !captureRequested, "Flat runtime entered VR hosting");
		Require(events == std::vector<std::string>{ "reset", "menu" }, "Flat lifecycle changed");

		Reset();
		helperAvailable = false;
		worldContent = true;
		PresentFrame();
		Require(hookCount == 0 && !captureRequested, "Missing helper installed hosting hooks");
		helperAvailable = true;
		PresentFrame();
		Require(hookCount == 1 && captureRequested, "Closed menu prevented newly available hosting");

		Reset();
		globals::d3d::context = nullptr;
		worldContent = true;
		PresentFrame();
		Require(hookCount == 0 && !captureRequested, "Missing D3D context started hosted rendering");

		Reset();
		globals::features::vr.compatible = false;
		worldContent = true;
		PresentFrame();
		Require(hookCount == 0, "Incompatible OpenVR runtime installed Submit hooks");
		globals::features::vr.compatible = true;
		PresentFrame();
		Require(hookCount == 1 && captureRequested && globals::menu->uiFrames == 0,
			"Compatible OpenVR runtime could not recover with every menu closed");
	}
}

int main()
{
	try {
		ClosedMenuStillAdvancesHostedClients();
		RuntimeAndAvailabilityRemainIndependent();
		std::cout << "PASS ImGui VR Helper UI-independent render lifecycle\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL ImGui VR Helper render lifecycle: " << error.what() << '\n';
		return 1;
	}
}
