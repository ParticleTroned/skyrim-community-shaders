#include "Menu/OverlayPolicy.h"

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width = 0, Height = 0;
};
struct ID3D11Texture2D
{
	D3D11_TEXTURE2D_DESC desc;
	void GetDesc(D3D11_TEXTURE2D_DESC* a_desc) { *a_desc = desc; }
};
struct TextureSlot
{
	ID3D11Texture2D* texture = nullptr;
	ID3D11Texture2D* get() const { return texture; }
};
struct DXGI_SWAP_CHAIN_DESC
{
	D3D11_TEXTURE2D_DESC BufferDesc;
	void* OutputWindow = nullptr;
};
struct SwapChain
{
	bool available = true;
	uint32_t height = 1024;
	unsigned queries = 0;
	int GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc)
	{
		++queries;
		a_desc->BufferDesc = { 1024, height };
		return available ? 0 : -1;
	}
};
constexpr bool SUCCEEDED(int a_result) { return a_result >= 0; }
struct VR
{
	struct Config
	{
#include "menu_canvas_constants_under_test.h"
	};
	struct Settings
	{
		enum class OverlayAttachMode
		{
			HMDOnly,
			ControllerOnly,
			Both,
			None
		};
	};
	bool available = true, compatible = true;
	mutable unsigned attachModeQueries = 0;
	std::function<void()> controllerInput;
	void ProcessControllerInputForImGui()
	{
		if (controllerInput)
			controllerInput();
	}
	Settings::OverlayAttachMode selectedAttachMode = Settings::OverlayAttachMode::HMDOnly;
	TextureSlot menuTexture, menuControllerTexture;
	bool IsOpenVRCompatible() const { return available && compatible; }
	Settings::OverlayAttachMode GetEffectiveMenuAttachMode() const
	{
		++attachModeQueries;
		return selectedAttachMode;
	}
	bool GetMenuCanvasSize(uint32_t&, uint32_t&) const;
};
struct Menu
{
	struct DisplaySize
	{
		float x = 0, y = 0;
		bool operator==(const DisplaySize&) const = default;
	} lastDisplaySize;
	bool resetLayout = false;
	struct ThemeSettings
	{
		float FontSize = 27;
		struct
		{
			ImVec4 Warning{ 1, 0.5f, 0, 1 }, Error{ 1, 0, 0, 1 };
		} StatusPalette;
	};
	struct Settings
	{
		bool UseResolutionFont = true, ShowCompilationHUDInVR = false;
		ThemeSettings Theme;
		std::vector<int> SkipCompilationKey;
	} settings;
	const Settings& GetSettings() const { return settings; }
	const ThemeSettings& GetTheme() const { return settings.Theme; }
	static Menu* GetSingleton()
	{
		static Menu menu;
		return &menu;
	}
};
struct ThemeManager
{
	struct Constants
	{
#include "menu_font_constants_under_test.h"
	};
	static float ResolveFontSize(const Menu&);
	static void SetupImGuiStyle(const Menu&) {}
};
struct State
{
	ImVec2 screenSize{ 10000, 10000 };
	bool IsDeveloperMode() const { return false; }
};
struct ShaderCache
{
	bool compiling = true, hide = false;
	mutable unsigned progressQueries = 0;
	uint64_t failed = 0;
	std::atomic<bool> backgroundCompilation{ false }, menuLoaded{ false };
	struct Pool
	{
		int get_tasks_running() const { return 1; }
		int get_thread_count() const { return 4; }
	} compilationPool;
	struct Rollback
	{
		bool featureSetRevertPending = false, featureSetChanged = false, diskCacheHeld = false;
		bool featureSetCacheBackedUp = false, previousDiskCacheAvailable = false;
	};
	uint64_t GetCurrentFailedCount() const { return failed; }
	bool IsHideErrors() const { return hide; }
	bool IsCompiling() const { return compiling; }
	uint64_t GetTotalTasks() const
	{
		++progressQueries;
		return 3161;
	}
	uint64_t GetCompletedTasks() const { return 418; }
	uint64_t GetFailedTasks() const { return failed; }
	uint64_t GetDiskHitTasks() const { return 0; }
	uint64_t GetSourceCompileTasks() const { return 1; }
	std::string GetShaderStatsString(bool) const { return "00:05:48/00:20:38"; }
	Rollback GetRollbackFlags() const { return {}; }
	std::pair<int, int> GetCompilationThreadLimits() const { return { 4, 4 }; }
	int GetHeavyTasksInFlight() const { return 0; }
	uint64_t GetSlowTasks() const { return 0; }
	uint64_t GetVerySlowTasks() const { return 0; }
};
struct RenderDoc
{
	bool available = true;
	bool upscalingCompatible = false;
	bool IsAvailable() const { return available; }
	bool IsUpscalingCompatible() const { return upscalingCompatible; }
	static RenderDoc* GetSingleton()
	{
		static RenderDoc renderdoc;
		return &renderdoc;
	}
	std::string GetOverlayWarningMessage() const;
};
namespace globals
{
	namespace game
	{
		bool isVR = true;
	}
	namespace features
	{
		VR vr;
		struct Overlay
		{
			void ResetWindowLayout() {}
		} performanceOverlay, weatherPicker;
	}
	namespace d3d
	{
		SwapChain* swapChain = nullptr;
	}
	State stateStorage;
	State* state = &stateStorage;
	ShaderCache cache;
	ShaderCache* shaderCache = &cache;
	Menu* menu = Menu::GetSingleton();
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return globals::game::isVR; }
	};
}
namespace FeatureIssues
{
	size_t CountLoadFailures() { return 0; }
	bool HasPotentialShaderModifyingFeatures() { return false; }
}
namespace Util
{
	namespace Colors
	{
		ImVec4 GetWarning() { return { 1, 0, .95f, 1 }; }
		ImVec4 GetError() { return GetWarning(); }
	}
#include "menu_ui_scale_under_test.h"
	unsigned GetPerformanceCoreCount() { return 4; }
	unsigned inputUpdates = 0;
	void UpdateImGuiInput(void*, float a_width, float a_height)
	{
		++inputUpdates;
		ImGui::GetIO().DisplaySize = { a_width, a_height };
	}
}
void ImGui_ImplDX11_NewFrame() {}
std::function<void()> win32Input;
void ImGui_ImplWin32_NewFrame()
{
	ImGui::GetIO().DisplaySize = { 1024, 768 };
	if (win32Input)
		win32Input();
}
ImVec2 GetDesktopMirrorCursor(void*, const ImVec2&, const ImVec2&) { return { 0, 0 }; }
namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}
struct EditorWindow
{
	bool resetLayout = false;
	static EditorWindow* GetSingleton()
	{
		static EditorWindow editor;
		return &editor;
	}
};
using float2 = Menu::DisplaySize;
namespace SKSE::stl
{
	template <class Function>
	struct scope_exit
	{
		Function function;
		explicit scope_exit(Function a_function) : function(a_function) {}
		~scope_exit() { function(); }
	};
}
using InputCombo = int;
struct OverlayRenderer
{
	static bool ApplyVROverlayDisplaySize();
	static void InitializeImGuiFrame(Menu&, bool);
	static void RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>&);
	static ImVec2 GetDefaultVRSettingsWindowSize(bool) { return { 900, 700 }; }
	static float GetDefaultVRLeftAnchorX(float) { return ImGui::GetMainViewport()->WorkPos.x + 300; }
};

#include "menu_gamepad_filter_under_test.h"

#include "menu_canvas_size_under_test.h"
#include "menu_font_size_under_test.h"
#include "menu_frame_initialization_under_test.h"
#include "menu_overlay_display_under_test.h"
#include "menu_renderdoc_warning_under_test.h"
#include "menu_status_visibility_under_test.h"

#include "menu_shader_failures_under_test.h"
#include "menu_shader_status_under_test.h"

void Require(bool a_condition, const char* a_message)
{
	if (!a_condition)
		throw std::runtime_error(a_message);
}

void CheckCanvasAndFontFallback()
{
	using AttachMode = VR::Settings::OverlayAttachMode;
	auto& vr = globals::features::vr;
	auto* menu = Menu::GetSingleton();
	SwapChain swapChain;
	globals::d3d::swapChain = &swapChain;
	ID3D11Texture2D retainedTexture{ { 4096, 4096 } };
	for (bool compatible : { false, true }) {
		for (bool available : { false, true }) {
			vr.compatible = compatible;
			vr.available = available;
			vr.menuTexture.texture = &retainedTexture;
			vr.attachModeQueries = 0;
			uint32_t width = 99, height = 99;
			if (!compatible || !available) {
				Require(!vr.GetMenuCanvasSize(width, height) && width == 0 && height == 0,
					"An unavailable VR runtime retained an HMD canvas");
				Require(vr.attachModeQueries == 0, "Desktop fallback queried headset presentation state");
				ImGui::GetIO().DisplaySize = { 1024, 1024 };
				Require(!OverlayRenderer::ApplyVROverlayDisplaySize() && ImGui::GetIO().DisplaySize.x == 1024,
					"Desktop fallback dimensions were replaced by a VR canvas");
				Require(std::abs(ThemeManager::ResolveFontSize(*menu) - 21.0f * 1024 / 1080) < 0.01f,
					"Desktop fallback font used the eye texture's height");
			}
		}
	}
	vr.menuTexture.texture = nullptr;
	for (auto mode : { AttachMode::HMDOnly, AttachMode::ControllerOnly, AttachMode::Both }) {
		vr.selectedAttachMode = mode;
		uint32_t width = 0, height = 0;
		Require(vr.GetMenuCanvasSize(width, height) && width == 1920 &&
					height == (mode == AttachMode::ControllerOnly ? 1080u : 1620u),
			"Compatible VR canvas dimensions changed");
		const auto queries = swapChain.queries;
		Require(std::abs(ThemeManager::ResolveFontSize(*menu) - (mode == AttachMode::ControllerOnly ? 27.0f : 37.8f)) < 0.01f &&
					swapChain.queries == queries,
			"Headset fonts changed or queried the desktop backbuffer");
	}
	vr.selectedAttachMode = AttachMode::None;
	swapChain.height = 2160;
	Require(ThemeManager::ResolveFontSize(*menu) == 42, "Desktop-only VR menu retained headset typography");
	swapChain.available = false;
	Require(ThemeManager::ResolveFontSize(*menu) == 21, "Missing desktop dimensions have no stable baseline");
	swapChain.available = true;
	swapChain.height = 0;
	Require(ThemeManager::ResolveFontSize(*menu) == 21, "Zero-height desktop dimensions changed the baseline");
	globals::d3d::swapChain = nullptr;
	Require(ThemeManager::ResolveFontSize(*menu) == 21, "Startup without a swapchain has no font baseline");
	globals::game::isVR = false;
	globals::state->screenSize.y = 2160;
	Require(ThemeManager::ResolveFontSize(*menu) == 42, "SE/AE automatic font sizing changed");
	for (const float height : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
		globals::state->screenSize.y = height;
		Require(ThemeManager::ResolveFontSize(*menu) == 21, "Invalid desktop dimensions produced an invalid font");
	}
	uint32_t width = 99, height = 99;
	Require(!vr.GetMenuCanvasSize(width, height) && width == 0 && height == 0, "SE/AE exposed a VR canvas");
	menu->settings.UseResolutionFont = false;
	menu->settings.Theme.FontSize = 32;
	Require(ThemeManager::ResolveFontSize(*menu) == 32, "Fixed user font size was overridden");
	menu->settings.UseResolutionFont = true;
	globals::game::isVR = true;
	vr.compatible = false;
	vr.selectedAttachMode = AttachMode::HMDOnly;
}

void CheckStatusBounds()
{
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImFontConfig font;
	font.SizePixels = 21;
	io.FontDefault = io.Fonts->AddFontDefault(&font);
	io.Fonts->Build();
	struct StatusCase
	{
		bool compiling;
		uint64_t failed;
		bool hideErrors;
		bool renderDoc;
		bool visible;
		bool upscalingCompatible = false;
	};
	constexpr StatusCase cases[] = {
		{ true, 0, false, true, true },
		{ false, 1, false, true, true },
		{ false, 0, false, true, true },
		{ false, 1, true, true, true },
		{ false, 1, true, false, false },
		{ false, 0, false, false, false },
		{ false, 1, false, false, true },
		{ true, 0, false, true, true, true },
		{ false, 0, false, true, true, true },
	};
	for (const auto& condition : cases) {
		globals::cache.compiling = condition.compiling;
		globals::cache.failed = condition.failed;
		globals::cache.hide = condition.hideErrors;
		auto* renderDoc = RenderDoc::GetSingleton();
		renderDoc->available = condition.renderDoc;
		renderDoc->upscalingCompatible = condition.upscalingCompatible;
		const auto warning = renderDoc->GetOverlayWarningMessage();
		Require((warning.find("VR upscaling capture is enabled") != std::string::npos) == condition.upscalingCompatible &&
					(warning.find("compatible VR upscaling path is unavailable") != std::string::npos) != condition.upscalingCompatible,
			"RenderDoc warning does not reflect upscaling compatibility");
		float narrowHeight = 0, wideHeight = 0;
		for (float width : { 1024.0f, 640.0f, 96.0f, 1920.0f, 1024.0f }) {
			io.DisplaySize = { width, 1024 };
			for (unsigned frame = 0; frame < 8; ++frame) {
				ImGui::NewFrame();
				globals::cache.progressQueries = 0;
				const auto depth = ImGui::GetCurrentContext()->CurrentWindowStack.Size;
				OverlayRenderer::RenderShaderCompilationStatus([](auto) { return "Esc"; });
				Require(ImGui::GetCurrentContext()->CurrentWindowStack.Size == depth,
					"A status path left its ImGui window open");
				auto* window = ImGui::FindWindowByName("ShaderCompilationInfo");
				Require(globals::cache.compiling || globals::cache.progressQueries == 0,
					"An idle status frame queried compilation progress");
				if (!condition.visible) {
					Require(!window || !window->Active, "Hidden shader errors opened the status window");
				} else if (frame >= 3) {
					Require(window && window->Active && window->Pos.x >= 0 && window->Pos.x + window->Size.x <= width,
						"The shader status window exceeded the desktop canvas");
					Require(window->ContentSize.x <= window->Size.x - ImGui::GetStyle().WindowPadding.x * 2 + 1,
						"Shader or RenderDoc status text was clipped instead of wrapped");
					Require(window->DC.TextWrapPosStack.empty(), "Status wrapping leaked into other windows");
					if (width == 640)
						narrowHeight = window->Size.y;
					if (width == 1920)
						wideHeight = window->Size.y;
				}
				ImGui::Render();
			}
		}
		if (condition.visible)
			Require(narrowHeight > wideHeight, "A narrow status window did not wrap its warning");
	}

	RenderDoc::GetSingleton()->available = true;
	globals::cache.failed = 0;
	globals::features::vr.compatible = true;
	globals::features::vr.selectedAttachMode = VR::Settings::OverlayAttachMode::None;
	for (ImVec2 workPosition : { ImVec2(0, 0), ImVec2(120, 80) }) {
		io.DisplaySize = { 1024, 768 };
		for (unsigned frame = 0; frame < 6; ++frame) {
			ImGui::NewFrame();
			auto* viewport = ImGui::GetMainViewport();
			viewport->WorkPos = workPosition;
			viewport->WorkSize = { 640, 600 };
			OverlayRenderer::RenderShaderCompilationStatus([](auto) { return "Esc"; });
			const auto* window = ImGui::FindWindowByName("ShaderCompilationInfo");
			const float padding = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
			Require(window && std::abs(window->Pos.x - workPosition.x - padding) < 1 &&
						std::abs(window->Pos.y - workPosition.y - padding) < 1,
				"Desktop-only VR status used a headset anchor or ignored the work area origin");
			if (frame >= 3)
				Require(window->Pos.x + window->Size.x <= workPosition.x + viewport->WorkSize.x,
					"A shifted work area let the status window extend off-canvas");
			ImGui::Render();
		}
	}

	for (const float width : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
		ImGui::NewFrame();
		ImGui::GetMainViewport()->WorkSize.x = width;
		const auto depth = ImGui::GetCurrentContext()->CurrentWindowStack.Size;
		OverlayRenderer::RenderShaderCompilationStatus([](auto) { return "Esc"; });
		const auto* window = ImGui::FindWindowByName("ShaderCompilationInfo");
		Require((!window || !window->Active) && ImGui::GetCurrentContext()->CurrentWindowStack.Size == depth,
			"Invalid work-area dimensions opened an unbounded status window");
		ImGui::Render();
	}
}

void CheckDesktopFrameFallback()
{
	auto* menu = Menu::GetSingleton();
	auto& vr = globals::features::vr;
	vr.compatible = false;
	SwapChain swapChain;
	globals::d3d::swapChain = &swapChain;
	for (const bool available : { false, true }) {
		for (const uint32_t height : { 0u, 1024u }) {
			swapChain.available = available;
			swapChain.height = height;
			Util::inputUpdates = 0;
			OverlayRenderer::InitializeImGuiFrame(*menu, false);
			const bool valid = available && height != 0;
			Require(ImGui::GetIO().DisplaySize.y == (valid ? 1024 : 768) && Util::inputUpdates == (valid ? 1u : 0u),
				"Failed or empty backbuffer metadata replaced the Win32 desktop canvas");
			ImGui::Render();
		}
	}
	globals::d3d::swapChain = nullptr;
}

void CheckGamepadInputFiltering()
{
	auto& io = ImGui::GetIO();
	auto& events = ImGui::GetCurrentContext()->InputEventsQueue;
	io.ClearInputKeys();
	events.clear();
	io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, true, 0.55f);
	io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, false, 0.08f);
	io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, true, 0.4f);
	io.AddMouseWheelEvent(0.0f, 0.25f);
	FilterWin32GamepadStickEvents(0);
	Require(events.Size == 4 && std::abs(events[0].Key.AnalogValue - 0.5f) < 1e-6f &&
				events[1].Key.AnalogValue == 0.0f && !events[1].Key.Down,
		"Resting sticks must be zero and active sticks must retain proportional navigation");
	Require(events[2].Key.AnalogValue == 0.4f && events[3].MouseWheel.WheelY == 0.25f,
		"Stick filtering changed triggers or fractional mouse wheel input");
	const int firstNewEvent = events.Size;
	io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickRight, true, 1.0f);
	FilterWin32GamepadStickEvents(firstNewEvent);
	Require(std::abs(events[0].Key.AnalogValue - 0.5f) < 1e-6f && events.back().Key.AnalogValue == 1.0f,
		"Retained ImGui input was filtered twice or full stick travel was reduced");

	auto& vr = globals::features::vr;
	SwapChain swapChain;
	globals::d3d::swapChain = &swapChain;
	vr.available = true;
	vr.selectedAttachMode = VR::Settings::OverlayAttachMode::HMDOnly;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	const bool previousTrickle = io.ConfigInputTrickleEventQueue;
	io.ConfigInputTrickleEventQueue = false;
	win32Input = [&] {
		io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, false, 0.08f);
		io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, true, 0.55f);
	};
	vr.controllerInput = [&] { io.AddMouseWheelEvent(0.0f, 0.25f); };
	for (const bool compatible : { false, true }) {
		vr.compatible = compatible;
		io.ClearInputKeys();
		events.clear();
		OverlayRenderer::InitializeImGuiFrame(*Menu::GetSingleton(), true);
		Require(ImGui::GetKeyData(ImGuiKey_GamepadLStickLeft)->AnalogValue == 0.0f &&
					std::abs(ImGui::GetKeyData(ImGuiKey_GamepadLStickRight)->AnalogValue - 0.5f) < 1e-6f,
			"Desktop or VR canvas bypassed Win32 stick filtering");
		Require(io.MouseWheel == (compatible ? 0.25f : 0.0f),
			"Win32 filtering changed VR-owned scrolling queued afterward");
		ImGui::Render();
	}
	win32Input = {};
	vr.controllerInput = {};
	io.ConfigInputTrickleEventQueue = previousTrickle;
	globals::d3d::swapChain = nullptr;
}

int main()
{
	try {
		ImGui::CreateContext();
		CheckCanvasAndFontFallback();
		CheckStatusBounds();
		CheckDesktopFrameFallback();
		CheckGamepadInputFiltering();
		ImGui::DestroyContext();
		std::puts("Desktop fallback, headset typography and wrapped status regressions passed.");
		return 0;
	} catch (const std::exception& error) {
		std::printf("FAIL: %s\n", error.what());
		return 1;
	}
}
