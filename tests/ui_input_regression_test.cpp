#include "Menu/SettingsActions.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <imgui_internal.h>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <vector>

static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
// Only runtime services are substituted; production input policy uses real ImGui.
namespace vr
{
	using TrackedDeviceIndex_t = std::uint32_t;
	constexpr TrackedDeviceIndex_t k_unTrackedDeviceIndexInvalid = ~0u;
}
struct Vector3
{
	float x = 0, y = 0, z = 0;
	static const Vector3 Zero;
	Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }
	float LengthSquared() const { return Dot(*this); }
	float Dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
};
const Vector3 Vector3::Zero{};
enum class ControllerDevice
{
	Primary,
	Secondary
};
static std::size_t GetControllerSlot(ControllerDevice hand) { return hand == ControllerDevice::Secondary ? 1u : 0u; }
#include "ui_wand_types.h"
static std::array<WandPoseHistory, 2> g_wandPoseHistory{};
#include "ui_wand_pose.h"

namespace RE
{
	enum class INPUT_DEVICE
	{
		kKeyboard,
		kVivePrimary,
		kViveSecondary,
		kOculusPrimary,
		kOculusSecondary,
		kWMRPrimary,
		kWMRSecondary
	};
}

struct Menu
{
	struct KeyEvent
	{
		RE::INPUT_DEVICE device;
		int sequence;
	};
	std::vector<KeyEvent> _keyEventQueue;
	std::shared_mutex _inputEventMutex;
	std::atomic<std::int64_t> _directInputWheelDelta = 0;
	std::vector<KeyEvent> nonVREventsHandled;
	void ProcessInputEventQueue();
	void addToEventQueue(KeyEvent);
	bool escapeConsumed = false;
	bool ConsumeNestedEscape(bool down, bool up);
	struct Theme
	{
		bool UseMonochromeIcons = false;
		struct
		{
			ImVec4 Text{ 1, 1, 1, 1 };
		} Palette;
	} theme;
	const Theme& GetTheme() const { return theme; }
};
#include "ui_escape.h"
namespace globals
{
	Menu storage;
	auto* menu = &storage;
	namespace features
	{
		struct
		{
			bool compatible = true;
			std::function<void(std::vector<Menu::KeyEvent>&)> onInput;
			bool IsOpenVRCompatible() const { return compatible; }
			void ProcessVREvents(std::vector<Menu::KeyEvent>& events) { onInput(events); }
		} vr;
	}
}
constexpr int WHEEL_DELTA = 120;
// Keep the production batch boundary and VR callback; unrelated hotkeys are substituted.
#include "ui_input_queue.h"
nonVREventsHandled.insert(nonVREventsHandled.end(), nonVREvents.begin(), nonVREvents.end());
#include "ui_input_queue_finish.h"
#include "ui_input_queue_push.h"

static void CheckInputQueue()
{
	Menu menu;
	menu.addToEventQueue({ RE::INPUT_DEVICE::kVivePrimary, 1 });
	menu.addToEventQueue({ RE::INPUT_DEVICE::kKeyboard, 2 });
	menu.addToEventQueue({ RE::INPUT_DEVICE::kOculusSecondary, 3 });
	std::vector<int> dispatched;
	bool producerWasUnblocked = false;
	globals::features::vr.onInput = [&](std::vector<Menu::KeyEvent>& events) {
		for (const auto& event : events)
			dispatched.push_back(event.sequence);
		std::thread producer([&] {
			std::unique_lock<std::shared_mutex> probe(menu._inputEventMutex, std::try_to_lock);
			producerWasUnblocked = probe.owns_lock();
			if (producerWasUnblocked) {
				probe.unlock();
				menu.addToEventQueue({ RE::INPUT_DEVICE::kWMRPrimary, 4 });
			}
		});
		producer.join();
	};
	menu.ProcessInputEventQueue();
	Require(producerWasUnblocked, "runtime callbacks must not hold the input producer's mutex");
	Require(dispatched == std::vector<int>{ 1, 3 }, "the captured VR batch retains event order");
	Require(menu.nonVREventsHandled.size() == 1 && menu.nonVREventsHandled[0].sequence == 2,
		"desktop input remains in its captured batch");
	Require(menu._keyEventQueue.size() == 1 && menu._keyEventQueue[0].sequence == 4,
		"input arriving during dispatch must survive for the next frame");
	globals::features::vr.onInput = [&](std::vector<Menu::KeyEvent>& events) {
		for (const auto& event : events)
			dispatched.push_back(event.sequence);
	};
	menu.ProcessInputEventQueue();
	menu.ProcessInputEventQueue();
	Require(dispatched == std::vector<int>{ 1, 3, 4 } && menu._keyEventQueue.empty(),
		"a newly queued event is dispatched exactly once on the next frame");
	globals::features::vr.compatible = false;
	menu.addToEventQueue({ RE::INPUT_DEVICE::kVivePrimary, 5 });
	menu.ProcessInputEventQueue();
	Require(dispatched.size() == 3 && menu.nonVREventsHandled.back().sequence == 5,
		"flat runtimes must not dispatch OpenVR callbacks");
	globals::features::vr.compatible = true;
	globals::features::vr.onInput = {};
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F f;
		explicit scope_exit(F value) : f(std::move(value)) {}
		~scope_exit() { f(); }
	};
}
namespace MenuUI
{
	bool blocked = false;
	const char* GetGlobalSettingsActionBlockReason(GlobalSettingsAction) { return blocked ? "Blocked" : nullptr; }
	const char* GetGlobalSettingsActionHelp(GlobalSettingsAction) { return "Save"; }
}
namespace Util
{
	struct DisableGuard
	{
		explicit DisableGuard(bool disabled) { ImGui::BeginDisabled(disabled); }
		~DisableGuard() { ImGui::EndDisabled(); }
	};
	bool IsButtonFlashActive(const char*) { return false; }
	ImVec4 GetButtonFlashColor(ImVec4 value) { return value; }
	void TriggerButtonFlash(const char*) {}
	void AddTooltip(const char*, ImGuiHoveredFlags) {}
}
struct ThemeManager
{
	struct Constants
	{
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.5f;
		static constexpr float DOCKED_ICON_SPACING = 8;
		static constexpr float DOCKED_RIGHT_MARGIN = 45;
	};
};
#include "ui_docked_icon_size.h"
struct MenuHeaderRenderer
{
	struct ActionIcon
	{
		enum class Glyph
		{
			Texture,
			Save
		};
		const char* id;
		const char* flashId;
		void* texture;
		const char* tooltip;
		std::function<void()> callback;
		Glyph glyph = Glyph::Save;
		MenuUI::GlobalSettingsAction action = MenuUI::GlobalSettingsAction::Save;
	};
	static void DrawActionButton(const ActionIcon&, ImVec2, ImVec2, float = 0);
	static void RenderDockedIcons(const std::vector<ActionIcon>&, float);
	static void DrawActionIcon(ImDrawList*, const ActionIcon&, ImVec2, ImVec2, ImU32) {}
};
#include "ui_docked_header.h"
#include "ui_header_button.h"

static void CheckPose()
{
	auto sample = [](ControllerDevice hand, float x, bool force = false) {
		return GetWandPoseUpdateMode(hand, force, hand == ControllerDevice::Primary ? 1 : 2, { x, 0, 0 }, { 0, 0, 1 });
	};
	for (auto hand : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
		Require(sample(hand, 0) == WandPoseUpdateMode::Active, "initial pose is active");
		Require(sample(hand, 0) == WandPoseUpdateMode::Active, "repeated consumer retains activity");
	}
	for (int frame = 0; frame < 30; ++frame) {
		++GImGui->FrameCount;
		for (auto hand : { ControllerDevice::Primary, ControllerDevice::Secondary }) {
			const auto mode = sample(hand, 0);
			const auto remaining = g_wandPoseHistory[GetControllerSlot(hand)].activeFramesRemaining;
			Require(sample(hand, 0) == mode, "every consumer sees the same keepalive mode");
			Require(remaining == g_wandPoseHistory[GetControllerSlot(hand)].activeFramesRemaining, "keepalive decrements once per frame");
		}
	}
	Require(sample(ControllerDevice::Secondary, 0) == WandPoseUpdateMode::None, "stationary hand expires");
	++GImGui->FrameCount;
	Require(sample(ControllerDevice::Secondary, .02f) == WandPoseUpdateMode::Active, "right hand movement wakes pointer");
	Require(sample(ControllerDevice::Primary, 0) == WandPoseUpdateMode::None, "hand histories remain independent");
	Require(sample(ControllerDevice::Secondary, .02f) == WandPoseUpdateMode::Active, "ownership consumer sees right movement");
	++GImGui->FrameCount;
	Require(sample(ControllerDevice::Secondary, .02f, true) == WandPoseUpdateMode::Active, "trigger forces active cursor");
	Require(sample(ControllerDevice::Secondary, .02f) == WandPoseUpdateMode::Active, "trigger activity survives subsequent pose lookup");
	g_wandPoseHistory = {};
	sample(ControllerDevice::Primary, 0);
	bool slowMotionActive = false;
	for (int frame = 1; frame <= 10; ++frame) {
		++GImGui->FrameCount;
		slowMotionActive |= sample(ControllerDevice::Primary, frame * .001f) == WandPoseUpdateMode::Active;
	}
	Require(slowMotionActive, "slow movement accumulates across samples");
}

static void CheckEscape()
{
	Menu menu;
	ImGui::NewFrame();
	ImGui::Begin("Escape");
	Require(!menu.ConsumeNestedEscape(true, false), "bare Escape can close menu");
	Require(!menu.ConsumeNestedEscape(false, true), "bare release is unconsumed");
	ImGui::SetActiveID(ImGui::GetID("editing"), ImGui::GetCurrentWindow());
	Require(menu.ConsumeNestedEscape(true, false), "editing consumes Escape press");
	ImGui::ClearActiveID();
	Require(menu.ConsumeNestedEscape(false, true), "editing cancellation also consumes release");
	Require(!menu.ConsumeNestedEscape(true, false), "next Escape can close menu");
	menu.ConsumeNestedEscape(false, true);
	ImGui::OpenPopup("Nested");
	Require(menu.ConsumeNestedEscape(true, false), "popup consumes Escape");
	ImGui::ClosePopupToLevel(0, true);
	Require(menu.ConsumeNestedEscape(false, true), "closed popup retains release consumption");
	ImGui::End();
	ImGui::Render();
}

static void CheckHeader()
{
	int clicks = 0;
	MenuHeaderRenderer::ActionIcon icon{ "Save", nullptr, nullptr, "Save", [&] { ++clicks; } };
	enum class Cover
	{
		None,
		Window,
		Modal,
		DockHost
	};
	Cover cover = Cover::None;
	bool openModal = false;
	ImGuiWindow* settings = nullptr;
	auto draw = [&] {
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 10, 10 });
		ImGui::SetNextWindowSize({ 400, 300 });
		ImGui::Begin("Header", nullptr, ImGuiWindowFlags_NoSavedSettings);
		settings = ImGui::GetCurrentWindow();
		ImGuiDockNode node(123);
		if (cover == Cover::DockHost) {
			node.HostWindow = ImGui::FindWindowByName("Dock host");
			settings->DockNode = &node;
			settings->DockTabIsVisible = true;
		}
		const auto hoveredBefore = GImGui->HoveredWindow;
		MenuHeaderRenderer::DrawActionButton(icon, { 40, 60 }, { 90, 110 });
		Require(GImGui->HoveredWindow == hoveredBefore, "header restores hovered window");
		if (cover == Cover::DockHost)
			settings->DockNode = nullptr;
		if (openModal) {
			ImGui::OpenPopup("Confirmation");
			openModal = false;
		}
		if (ImGui::BeginPopupModal("Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Confirm");
			ImGui::EndPopup();
		}
		ImGui::End();
		if (cover == Cover::Window || cover == Cover::DockHost) {
			ImGui::SetNextWindowPos({ 30, 50 });
			ImGui::SetNextWindowSize({ 140, 140 });
			ImGui::Begin(cover == Cover::Window ? "Cover" : "Dock host", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
			ImGui::TextUnformatted("Overlay");
			ImGui::End();
		}
		ImGui::Render();
	};
	auto click = [&] {
		ImGui::GetIO().AddMousePosEvent(65, 85);
		draw();
		draw();
		ImGui::GetIO().AddMouseButtonEvent(0, true);
		draw();
		ImGui::GetIO().AddMouseButtonEvent(0, false);
		draw();
	};
	draw();
	draw();
	click();
	Require(clicks == 1, "visible header invokes once on release");
	MenuUI::blocked = true;
	click();
	Require(clicks == 1, "disabled action cannot run");
	MenuUI::blocked = false;
	cover = Cover::Window;
	draw();
	draw();
	click();
	Require(clicks == 1, "overlapping window blocks click-through");
	cover = Cover::None;
	draw();
	draw();
	openModal = true;
	draw();
	draw();
	click();
	Require(clicks == 1, "modal blocks click-through");
	ImGui::ClosePopupToLevel(0, true);
	cover = Cover::DockHost;
	draw();
	draw();
	click();
	Require(clicks == 2, "dock host routes title action to visible tab");
}
static void CheckRealDockedHeader()
{
	int clicks = 0;
	std::vector<MenuHeaderRenderer::ActionIcon> icons{ { "RealDockSave", nullptr, nullptr, "Save", [&] { ++clicks; } } };
	ImGuiWindow* settings = nullptr;
	ImGuiWindow* other = nullptr;
	ImVec2 actionPosition{};
	bool setup = true, selectSettings = true, cover = false;
	auto draw = [&] {
		if (settings && settings->DockNode && settings->DockNode->TabBar)
			settings->DockNode->TabBar->NextSelectedTabId = selectSettings ? settings->TabId : other->TabId;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 10, 10 });
		ImGui::SetNextWindowSize({ 800, 500 });
		ImGui::Begin("Dock space", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
		const auto dockId = ImGui::GetID("Dock");
		if (setup) {
			ImGui::DockBuilderRemoveNode(dockId);
			ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockId, { 750, 450 });
			ImGui::DockBuilderDockWindow("Docked settings", dockId);
			ImGui::DockBuilderDockWindow("A deliberately long editor tab title that fills the docking strip and competes with actions", dockId);
			ImGui::DockBuilderFinish(dockId);
			setup = false;
		}
		ImGui::DockSpace(dockId);
		ImGui::End();
		const bool visible = ImGui::Begin("Docked settings");
		settings = ImGui::GetCurrentWindow();
		if (visible)
			MenuHeaderRenderer::RenderDockedIcons(icons, 1);
		ImGui::End();
		ImGui::Begin("A deliberately long editor tab title that fills the docking strip and competes with actions");
		other = ImGui::GetCurrentWindow();
		ImGui::TextUnformatted("Other tab content");
		ImGui::End();
		if (!settings->DockNode || !settings->DockNode->TabBar) {
			ImGui::Render();
			return;
		}
		const auto title = settings->DockNode->TabBar->BarRect;
		const float iconSize = GetDockedActionIconSize(title.GetHeight());
		actionPosition = { title.Max.x - ThemeManager::Constants::DOCKED_RIGHT_MARGIN - ThemeManager::Constants::DOCKED_ICON_SPACING - iconSize * .5f,
			(title.Min.y + title.Max.y) * .5f };
		if (cover) {
			ImGui::SetNextWindowPos({ actionPosition.x - 30, actionPosition.y - 20 });
			ImGui::SetNextWindowSize({ 100, 100 });
			ImGui::Begin("Dock cover", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
			ImGui::TextUnformatted("Cover");
			ImGui::End();
		}
		ImGui::Render();
	};
	auto click = [&] {
		ImGui::GetIO().AddMousePosEvent(actionPosition.x, actionPosition.y);
		draw();
		draw();
		ImGui::GetIO().AddMouseButtonEvent(0, true);
		draw();
		ImGui::GetIO().AddMouseButtonEvent(0, false);
		draw();
	};
	draw();
	draw();
	draw();
	Require(settings->DockNode && settings->DockNode->TabBar, "real dock tab bar exists");
	Require(settings->DockTabIsVisible, "settings dock tab is selected");
	click();
	Require(clicks == 1, "actual dock title action is wired");
	MenuUI::blocked = true;
	click();
	Require(clicks == 1, "actual dock action obeys disabled policy");
	MenuUI::blocked = false;
	selectSettings = false;
	draw();
	draw();
	Require(!settings->DockTabIsVisible, "other dock tab is selected");
	click();
	Require(clicks == 1, "inactive dock tab never invokes hidden actions");
	selectSettings = true;
	draw();
	draw();
	cover = true;
	draw();
	draw();
	click();
	Require(clicks == 1, "covering window blocks actual dock title action");
}
int main()
{
	try {
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = { 1024, 768 };
		io.Fonts->AddFontDefault();
		io.Fonts->Build();
		CheckInputQueue();
		CheckPose();
		ImGui::DestroyContext();
		ImGui::CreateContext();
		auto& frameIO = ImGui::GetIO();
		frameIO.IniFilename = nullptr;
		frameIO.DisplaySize = { 1024, 768 };
		frameIO.Fonts->AddFontDefault();
		frameIO.Fonts->Build();
		CheckEscape();
		CheckHeader();
		frameIO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		CheckRealDockedHeader();
		ImGui::DestroyContext();
		std::cout << "UI input regressions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
