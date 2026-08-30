
#pragma once
#include "Feature.h"
#include "Menu/ThemeManager.h"
#include "Utils/Input.h"
#include "Utils/Serialize.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <dxgi1_4.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <winrt/base.h>

using json = nlohmann::json;

struct ImFont;

class Menu
{
public:
	/**
	 * @brief Semantic font roles for hierarchical UI typography
	 *
	 * FONT ROLE SYSTEM:
	 * =================
	 * Replaces legacy single-font approach with semantic typography system.
	 * Each role can use different font family, style, and size scaling.
	 *
	 * Roles:
	 * - Body (0):       Default UI text, setting labels, general content
	 * - Heading (1):    Feature section headers
	 * - Subheading (2): Subsection headers within features
	 * - Subtitle (3):   Secondary descriptive text, tooltips
	 *
	 * Theme JSON Configuration:
	 * "FontRoles": [
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
	 *   { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 }
	 * ]
	 *
	 * SizeScale multiplies the base FontSize for each role.
	 * Example: FontSize=27, Heading SizeScale=1.05 → 28.35px rendered size
	 *
	 * Migration from Legacy:
	 * Old "FontName" field auto-populates Body role on theme load.
	 * Themes without FontRoles get defaults (Jost family).
	 */
	enum class FontRole : std::uint8_t
	{
		Body = 0,    // Default UI text
		Title,       // Large title text (e.g., "CSX" header)
		Heading,     // Section headers (tabs, category labels)
		Subheading,  // Subsection headers (feature names, separators)
		Subtext,     // Smaller secondary text (descriptions, about content)
		Count        // Total number of roles
	};

	struct FontRoleDescriptor
	{
		std::string_view key;
		std::string_view displayName;
		float defaultScale;
	};

	static inline constexpr std::array<FontRoleDescriptor, static_cast<size_t>(FontRole::Count)> FontRoleDescriptors = {
		FontRoleDescriptor{ "Body", "Body Text", 1.0f },
		FontRoleDescriptor{ "Title", "Title", 1.0f },
		FontRoleDescriptor{ "Heading", "Headings", 1.0f },
		FontRoleDescriptor{ "Subheading", "Subheadings", 1.0f },
		FontRoleDescriptor{ "Subtext", "Subtext", 0.9f }
	};

	static constexpr std::string_view GetFontRoleKey(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].key;
	}

	static constexpr std::string_view GetFontRoleDisplayName(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].displayName;
	}

	static constexpr float GetFontRoleDefaultScale(FontRole role)
	{
		return FontRoleDescriptors[static_cast<size_t>(role)].defaultScale;
	}

	static std::optional<FontRole> ResolveFontRole(std::string_view key);

	~Menu();
	Menu(const Menu&) = delete;
	Menu& operator=(const Menu&) = delete;

	static Menu* GetSingleton()
	{
		static Menu menu;
		return &menu;
	}

	bool initialized = false;
	bool IsEnabled = false;
	double systemCommitLastRefreshTime = -1.0;
	std::uint64_t systemCommitCurrentBytes = 0;
	std::uint64_t systemCommitTotalBytes = 0;
	bool systemCommitSampleValid = false;

	void Load(json& o_json);
	void Save(json& o_json);

	void LoadTheme(json& o_json);
	void SaveTheme(json& o_json);

	// Multi-theme support
	std::vector<std::string> DiscoverThemes();
	bool LoadThemePreset(const std::string& themeName);
	void CreateDefaultThemes();

	void Init();
	void DrawSettings();
	bool HasUnsavedSettings() const { return settingsDirty; }
	void RequestSettingsDirtyCheck() { settingsDirtyCheckRequested = true; }
	void ResetSettingsDirtyState();
	void AcceptCurrentFeatureSettingsAsClean(const std::string& a_featureSettingsName);
	void ReportSettingsSaveResult(bool a_success, std::string a_message);
	void ClearSettingsSaveResult();
	const std::string& GetSettingsSaveMessage() const { return settingsSaveMessage; }
	bool IsSettingsSaveMessageError() const { return settingsSaveMessageIsError; }

	// Search bar state
	std::string featureSearch;  // For left pane feature search
	void DrawOverlay();
	/// True while either the main menu or its CS Editor surface is open.
	bool IsMenuSessionOpen() const;
	/// True when a status-only overlay must remain visible with the menu closed.
	bool HasClosedMenuOverlay() const;
	/// Opens a fresh main-menu session and prepares VR positioning/input state.
	void OpenMenu();
	/// Closes the main menu and any active CS Editor surface as one UI session.
	void CloseMenu();

	/** @brief Translates raw Skyrim input events into the internal key event queue. */
	void ProcessInputEvents(RE::InputEvent* const* a_events);
	/** @brief Records raw DirectInput wheel movement before Skyrim quantizes it. */
	void RecordDirectInputWheelDelta(std::int32_t a_delta);
	bool ShouldSwallowInput();
	bool ShouldBlockAllGameInput();
	bool IsPreviewFlying();
	std::string BuildFontSignature(float baseFontSize) const;

public:
	// Input handling flags (made public for InputEventHandler access)
	bool settingToggleKey = false;
	bool settingSkipCompilationKey = false;
	bool settingsEffectsToggle = false;
	bool settingOverlayToggleKey = false;
	bool settingShaderBlockPrevKey = false;  // Debug: capture shader block prev key
	bool settingShaderBlockNextKey = false;  // Debug: capture shader block next key
	bool settingCSEditorToggleKey = false;   // CS Editor toggle key
	bool settingScreenshotKey = false;       // Screenshot capture key

	// Font caching (made public for ThemeManager and OverlayRenderer access)
	// Marked mutable because they're cache fields that may be updated from const methods
	float cachedFontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;  // Tracks whether font has been modified and may require reloading
	mutable std::string cachedFontName = "Jost/Jost-Regular.ttf";       // Tracks whether font file has changed and may require reloading
	std::array<std::string, static_cast<size_t>(FontRole::Count)> cachedFontFilesByRole = []() {
		std::array<std::string, static_cast<size_t>(FontRole::Count)> files{};
		auto setFile = [&files](FontRole role, std::string value) {
			files[static_cast<size_t>(role)] = std::move(value);
		};
		setFile(FontRole::Body, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Title, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Heading, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Subheading, "Jost/Jost-Regular.ttf");
		setFile(FontRole::Subtext, "Jost/Jost-Regular.ttf");
		return files;
	}();
	mutable std::array<float, static_cast<size_t>(FontRole::Count)> cachedFontPixelSizesByRole = {};
	std::string cachedFontSignature;
	mutable std::array<ImFont*, static_cast<size_t>(FontRole::Count)> loadedFontRoles = {};

	// Deferred reload systems (public for SettingsTabRenderer access)
	bool pendingFontReload = false;
	bool pendingIconReload = false;

	// Display size tracking for cross-session resolution change detection
	float2 lastDisplaySize{};
	bool resetLayout = false;

	// Used for resetting input keys to solve alt-tab stuck issue
	std::atomic<bool> focusChanged = false;
	void OnFocusChanged();

	struct Constants
	{
		static constexpr std::uint16_t KEY_PRESSED_MASK = 0x8000;
	};

	// UI icon textures
	struct UIIcon
	{
		ID3D11ShaderResourceView* texture = nullptr;
		ImVec2 size = ImVec2(32.0f, 32.0f);

		void Release()
		{
			if (texture) {
				texture->Release();
				texture = nullptr;
			}
		}
	};
	struct UIIcons
	{
		UIIcon saveSettings;
		UIIcon loadSettings;
		UIIcon deleteSettings;
		UIIcon clearCache;
		UIIcon logo;                  // New logo icon
		UIIcon search;                // Search icon for search bars
		UIIcon featureSettingRevert;  // Feature revert settings icon
		UIIcon applyToGame;           // Apply changes to game icon (CS editor)
		UIIcon pauseTime;             // Pause time icon (CS editor)
		UIIcon undo;                  // Undo icon (CS editor)
		UIIcon freeCamera;            // Free camera preview icon (CS editor)
		UIIcon playMode;              // Play mode preview icon (CS editor)

		// Social media/external link icons
		UIIcon faultier;
		UIIcon discord;

		// Category icons
		UIIcon characters;
		UIIcon display;
		UIIcon foliage;
		UIIcon lighting;
		UIIcon sky;
		UIIcon landscape;
		UIIcon water;
		UIIcon debug;
		UIIcon materials;
		UIIcon postProcessing;
	} uiIcons;

	struct ThemeSettings
	{
		struct FontRoleSettings
		{
			std::string Family;
			std::string Style;
			std::string File;
			float SizeScale = 1.0f;
		};

		float FontSize = ThemeManager::Constants::DEFAULT_FONT_SIZE;
		std::string FontName = "Jost/Jost-Regular.ttf";         // Default font file name (legacy)
		float GlobalScale = REL::Module::IsVR() ? -0.5f : 0.f;  // exponential
		std::array<FontRoleSettings, static_cast<size_t>(FontRole::Count)> FontRoles = []() {
			std::array<FontRoleSettings, static_cast<size_t>(FontRole::Count)> roles{};
			auto setRole = [&roles](FontRole role, std::string family, std::string style, std::string file, float sizeScale) {
				auto index = static_cast<size_t>(role);
				roles[index].Family = std::move(family);
				roles[index].Style = std::move(style);
				roles[index].File = std::move(file);
				roles[index].SizeScale = sizeScale;
			};

			setRole(FontRole::Body, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Title, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Heading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Subheading, "Jost", "Regular", "Jost/Jost-Regular.ttf", 1.0f);
			setRole(FontRole::Subtext, "Jost", "Regular", "Jost/Jost-Regular.ttf", 0.9f);

			return roles;
		}();

		bool UseSimplePalette = false;       // DEPRECATED: No longer affects behavior. UI now shows both Simple and Advanced controls.
		bool ShowActionIcons = true;         // whether to show action buttons as icons
		bool UseMonochromeIcons = false;     // whether to use monochrome (white) action icons with text color tinting
		bool UseMonochromeLogo = false;      // whether to use monochrome CSX logo
		bool ShowFooter = true;              // whether to show the footer with game version/GPU info
		bool CenterHeader = false;           // whether to center the header title and logo
		float TooltipHoverDelay = 0.5f;      // tooltip hover delay in seconds
		bool BackgroundBlurEnabled = false;  // enable background blur effect
		// Scrollbar opacity settings
		struct ScrollbarOpacitySettings
		{
			float Background = 0.0f;     // Background of the scrollbar area
			float Thumb = 0.5f;          // The draggable thumb/grip
			float ThumbHovered = 0.75f;  // Thumb when hovered
			float ThumbActive = 0.9f;    // Thumb when being dragged
		} ScrollbarOpacity;
		struct PaletteColors
		{
			ImVec4 Background{ 0.035f, 0.038f, 0.040f, 0.40f };
			ImVec4 Text{ 0.92f, 0.91f, 0.86f, 1.0f };
			// Separated border controls for better theming granularity
			ImVec4 WindowBorder{ 0.42f, 0.40f, 0.35f, 0.80f };    // Outer window borders
			ImVec4 FrameBorder{ 0.082f, 0.086f, 0.089f, 0.62f };  // Button, slider, input field backgrounds
			ImVec4 Separator{ 0.34f, 0.33f, 0.30f, 0.62f };       // Internal separators and dividers
			ImVec4 ResizeGrip{ 0.78f, 0.74f, 0.64f, 0.70f };      // Window resize grips
		} Palette;
		struct StatusPaletteColors
		{
			ImVec4 Disable{ 0.52f, 0.51f, 0.48f, 0.90f };
			ImVec4 Error{ 0.95f, 0.34f, 0.32f, 1.0f };
			ImVec4 Warning{ 1.00f, 0.64f, 0.18f, 1.0f };
			ImVec4 RestartNeeded{ 0.70f, 0.76f, 0.80f, 1.0f };
			ImVec4 CurrentHotkey{ 1.00f, 0.78f, 0.28f, 1.0f };
			ImVec4 SuccessColor{ 0.76f, 0.78f, 0.72f, 1.0f };
			ImVec4 InfoColor{ 0.98f, 0.73f, 0.22f, 1.0f };
		} StatusPalette;
		struct FeatureHeadingColors
		{
			ImVec4 ColorDefault{ 0.92f, 0.91f, 0.86f, 1.0f };
			ImVec4 ColorHovered{ 0.92f, 0.91f, 0.86f, 1.0f };
			float MinimizedFactor = 0.7f;    // 70% of original alpha for when the header is minimized
			float FeatureTitleScale = 1.5f;  // Scale multiplier for feature title text in settings tab
		} FeatureHeading;

		ImGuiStyle Style = []() {
			ImGuiStyle style = {};
			style.Alpha = 1.0f;
			style.DisabledAlpha = 0.55f;
			style.WindowBorderSize = 2.0f;
			style.ChildBorderSize = 0.0f;
			style.FrameBorderSize = 1.0f;
			style.WindowPadding = { 10.0f, 8.0f };
			style.WindowRounding = 8.0f;
			style.IndentSpacing = 10.0f;
			style.FramePadding = { 7.0f, 5.0f };
			style.CellPadding = { 8.0f, 3.0f };
			style.ItemSpacing = { 6.0f, 7.0f };
			style.ItemInnerSpacing = { 6.0f, 4.0f };
			style.FrameRounding = 5.0f;
			style.TabRounding = 5.0f;
			style.ScrollbarRounding = 8.0f;
			style.ScrollbarSize = 11.0f;
			style.GrabRounding = 5.0f;
			style.GrabMinSize = 12.0f;
			return std::move(style);
		}();
		// Entries ordered to match imgui 1.92+ ImGuiCol_ enum (62 entries).
		std::array<ImVec4, ImGuiCol_COUNT> FullPalette = {
			ImVec4(0.92f, 0.91f, 0.86f, 1.00f),     // [0]  Text
			ImVec4(0.52f, 0.51f, 0.48f, 0.58f),     // [1]  TextDisabled
			ImVec4(0.035f, 0.038f, 0.040f, 0.94f),  // [2]  WindowBg
			ImVec4(0.045f, 0.047f, 0.048f, 0.24f),  // [3]  ChildBg
			ImVec4(0.050f, 0.052f, 0.052f, 0.96f),  // [4]  PopupBg
			ImVec4(0.42f, 0.40f, 0.35f, 0.80f),     // [5]  Border
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),         // [6]  BorderShadow
			ImVec4(0.095f, 0.100f, 0.102f, 0.88f),  // [7]  FrameBg
			ImVec4(0.230f, 0.190f, 0.110f, 0.92f),  // [8]  FrameBgHovered
			ImVec4(0.340f, 0.250f, 0.110f, 0.96f),  // [9]  FrameBgActive
			ImVec4(0.030f, 0.033f, 0.035f, 0.92f),  // [10] TitleBg
			ImVec4(0.075f, 0.070f, 0.055f, 0.96f),  // [11] TitleBgActive
			ImVec4(0.030f, 0.033f, 0.035f, 0.76f),  // [12] TitleBgCollapsed
			ImVec4(0.052f, 0.052f, 0.050f, 0.92f),  // [13] MenuBarBg
			ImVec4(0.045f, 0.047f, 0.048f, 0.00f),  // [14] ScrollbarBg
			ImVec4(0.240f, 0.210f, 0.145f, 0.30f),  // [15] ScrollbarGrab
			ImVec4(0.410f, 0.320f, 0.140f, 0.50f),  // [16] ScrollbarGrabHovered
			ImVec4(0.640f, 0.460f, 0.150f, 0.80f),  // [17] ScrollbarGrabActive
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [18] CheckMark
			ImVec4(0.98f, 0.73f, 0.22f, 0.82f),     // [19] SliderGrab
			ImVec4(1.00f, 0.82f, 0.34f, 1.00f),     // [20] SliderGrabActive
			ImVec4(0.210f, 0.160f, 0.075f, 0.72f),  // [21] Button
			ImVec4(0.340f, 0.250f, 0.100f, 0.88f),  // [22] ButtonHovered
			ImVec4(0.470f, 0.340f, 0.120f, 0.96f),  // [23] ButtonActive
			ImVec4(0.190f, 0.150f, 0.075f, 0.62f),  // [24] Header
			ImVec4(0.340f, 0.250f, 0.100f, 0.82f),  // [25] HeaderHovered
			ImVec4(0.470f, 0.340f, 0.120f, 0.94f),  // [26] HeaderActive
			ImVec4(0.34f, 0.33f, 0.30f, 0.62f),     // [27] Separator
			ImVec4(0.98f, 0.73f, 0.22f, 0.74f),     // [28] SeparatorHovered
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [29] SeparatorActive
			ImVec4(0.78f, 0.74f, 0.64f, 0.70f),     // [30] ResizeGrip
			ImVec4(0.90f, 0.78f, 0.48f, 0.85f),     // [31] ResizeGripHovered
			ImVec4(1.00f, 0.85f, 0.34f, 0.95f),     // [32] ResizeGripActive
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [33] InputTextCursor
			ImVec4(0.420f, 0.300f, 0.110f, 0.92f),  // [34] TabHovered
			ImVec4(0.060f, 0.058f, 0.052f, 0.92f),  // [35] Tab
			ImVec4(0.250f, 0.185f, 0.085f, 0.96f),  // [36] TabSelected
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [37] TabSelectedOverline
			ImVec4(0.030f, 0.033f, 0.035f, 0.86f),  // [38] TabDimmed
			ImVec4(0.120f, 0.095f, 0.055f, 0.92f),  // [39] TabDimmedSelected
			ImVec4(0.98f, 0.73f, 0.22f, 0.45f),     // [40] TabDimmedSelectedOverline
			ImVec4(0.98f, 0.73f, 0.22f, 0.42f),     // [41] DockingPreview
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),         // [42] DockingEmptyBg
			ImVec4(0.82f, 0.80f, 0.72f, 1.00f),     // [43] PlotLines
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [44] PlotLinesHovered
			ImVec4(1.00f, 0.64f, 0.18f, 0.90f),     // [45] PlotHistogram
			ImVec4(1.00f, 0.80f, 0.34f, 1.00f),     // [46] PlotHistogramHovered
			ImVec4(0.135f, 0.105f, 0.060f, 0.88f),  // [47] TableHeaderBg
			ImVec4(0.34f, 0.33f, 0.30f, 1.00f),     // [48] TableBorderStrong
			ImVec4(0.34f, 0.33f, 0.30f, 0.70f),     // [49] TableBorderLight
			ImVec4(0.0f, 0.0f, 0.0f, 0.0f),         // [50] TableRowBg
			ImVec4(0.98f, 0.73f, 0.22f, 0.055f),    // [51] TableRowBgAlt
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [52] TextLink
			ImVec4(0.98f, 0.73f, 0.22f, 0.35f),     // [53] TextSelectedBg
			ImVec4(0.34f, 0.33f, 0.30f, 0.70f),     // [54] TreeLines
			ImVec4(1.00f, 0.64f, 0.18f, 1.00f),     // [55] DragDropTarget
			ImVec4(1.00f, 0.64f, 0.18f, 0.18f),     // [56] DragDropTargetBg
			ImVec4(1.00f, 0.64f, 0.18f, 1.00f),     // [57] UnsavedMarker
			ImVec4(0.98f, 0.73f, 0.22f, 1.00f),     // [58] NavCursor
			ImVec4(0.92f, 0.91f, 0.86f, 0.70f),     // [59] NavWindowingHighlight
			ImVec4(0.0f, 0.0f, 0.0f, 0.40f),        // [60] NavWindowingDimBg
			ImVec4(0.0f, 0.0f, 0.0f, 0.45f),        // [61] ModalWindowDimBg
		};
	};

	static const ThemeSettings::FontRoleSettings& GetDefaultFontRole(FontRole role);

	// Named-map palette serialization (resilient to ImGui enum changes)
	static void PaletteToJson(json& themeJson, const std::array<ImVec4, ImGuiCol_COUNT>& palette);
	static void PaletteFromJson(const json& themeJson, std::array<ImVec4, ImGuiCol_COUNT>& palette);

	struct Settings
	{
		std::vector<InputCombo> ToggleKey = { InputCombo::Keyboard(VK_END) };
		std::vector<InputCombo> SkipCompilationKey = { InputCombo::Keyboard(VK_ESCAPE) };
		std::vector<InputCombo> EffectToggleKey = { InputCombo::Keyboard(VK_MULTIPLY) };                               // toggle all effects
		std::vector<InputCombo> OverlayToggleKey = { InputCombo::Keyboard(VK_F10) };                                   // Global overlay toggle key for all overlays
		std::vector<InputCombo> ShaderBlockPrevKey = { InputCombo::Keyboard(VK_PRIOR) };                               // Debug: cycle backward through shaders (PageUp)
		std::vector<InputCombo> ShaderBlockNextKey = { InputCombo::Keyboard(VK_NEXT) };                                // Debug: cycle forward through shaders (PageDown)
		std::vector<InputCombo> CSEditorToggleKey = { InputCombo::Keyboard(VK_SHIFT), InputCombo::Keyboard(VK_END) };  // CS Editor toggle key
		std::vector<InputCombo> ScreenshotKey = { InputCombo::Keyboard(VK_SNAPSHOT) };                                 // Screenshot capture key
		bool EnableShaderBlocking = false;                                                                             // Enable shader blocking hotkeys for debugging
		bool FirstTimeSetupCompleted = false;                                                                          // Track if first-time setup has been completed
		bool SkipClearCacheConfirmation = false;                                                                       // Skip confirmation dialog when clearing shader cache
		bool SmartClearShaderCacheDefault = false;                                                                     // Plain-click clears active shaders; Shift-click selects the other clear scope
		bool BackgroundShaderCompilationOnBoot = false;                                                                // Load the menu immediately and compile shaders in the background on boot
		bool ShowCompilationHUDInVR = false;                                                                           // Opt in to shader compilation status in the HMD; desktop status is unaffected
		bool AutoHideFeatureList = false;                                                                              // Auto-hide left feature list panel, show on hover
		bool SkipConstraintWarning = false;                                                                            // Skip popup when a setting change creates new constraints
		int UiMode = 0;                                                                                                // Persisted as "UI Mode"; 0 = Essentials, 1 = Advanced
		bool RequireShiftToDock = true;                                                                                // Require holding Shift to dock windows
		bool UseResolutionFont = true;                                                                                 // When true, runtime font size scales with screen resolution; when persisted to theme files, FontSize is zeroed for backward compatibility
		ThemeSettings Theme;
		std::string SelectedThemePreset = "";  // Currently selected theme preset (empty = custom/user theme)
	};
	const ThemeSettings& GetTheme() const { return settings.Theme; }  // Provide read-only access to the Theme.
	Settings& GetSettings() { return settings; }                      // Provide access to settings for other components
	const Settings& GetSettings() const { return settings; }
	bool IsEssentialsUiMode() const { return settings.UiMode == 0; }
	bool IsAdvancedUiMode() const { return settings.UiMode != 0; }
	winrt::com_ptr<IDXGIAdapter3> GetDXGIAdapter3() const { return dxgiAdapter3; }  // Provide access to dxgiAdapter3
	ThemeSettings::FontRoleSettings& GetFontRoleSettings(FontRole role) { return settings.Theme.FontRoles[static_cast<size_t>(role)]; }
	const ThemeSettings::FontRoleSettings& GetFontRoleSettings(FontRole role) const { return settings.Theme.FontRoles[static_cast<size_t>(role)]; }
	ImFont* GetFont(FontRole role) const { return loadedFontRoles[static_cast<size_t>(role)]; }

	void SelectFeatureMenu(const std::string& featureName);
	bool overlayVisible = false;

public:
	// Move KeyEvent struct here
	class CharEvent : public RE::InputEvent
	{
	public:
		uint32_t keyCode;  // 18 (ascii code)
	};
	struct KeyEvent
	{
		explicit KeyEvent(const RE::ButtonEvent* a_event) :
			keyCode(a_event->GetIDCode()),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(a_event->Value()),
			heldDownSecs(a_event->HeldDuration()),
			thumbstickX(0.0f),
			thumbstickY(0.0f) {}

		explicit KeyEvent(const CharEvent* a_event) :
			keyCode(a_event->keyCode),
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(0),
			heldDownSecs(0),
			thumbstickX(0.0f),
			thumbstickY(0.0f) {}

		explicit KeyEvent(const RE::ThumbstickEvent* a_event) :
			keyCode(0),  // For thumbstick events, keyCode/value are replaced by x/y floats
			device(a_event->GetDevice()),
			eventType(a_event->GetEventType()),
			value(0),
			heldDownSecs(0),
			thumbstickX(a_event->xValue),
			thumbstickY(a_event->yValue)
		{}
		// For thumbstick events, keyCode/value are replaced by x/y floats
		uint32_t keyCode;
		RE::INPUT_DEVICE device;
		RE::INPUT_EVENT_TYPE eventType;
		float value = 0;
		float heldDownSecs = 0;
		float thumbstickX = 0.0f;
		float thumbstickY = 0.0f;
		[[nodiscard]] constexpr bool IsPressed() const noexcept { return value > 0.0F; }
		[[nodiscard]] constexpr bool IsRepeating() const noexcept { return heldDownSecs > 0.0F; }
		[[nodiscard]] constexpr bool IsDown() const noexcept { return IsPressed() && (heldDownSecs == 0.0F); }
		[[nodiscard]] constexpr bool IsHeld() const noexcept { return IsPressed() && IsRepeating(); }
		[[nodiscard]] constexpr bool IsUp() const noexcept { return (value == 0.0F) && IsRepeating(); }
	};
	// VR overlay input and cursor helpers
	void ProcessVROverlayInput();

private:
	Settings settings;
	json settingsDirtyBaseline = json::object();
	bool settingsDirtyBaselineInitialized = false;
	bool settingsDirty = false;
	bool settingsDirtyCheckRequested = false;
	std::string settingsSaveMessage;
	bool settingsSaveMessageIsError = false;

	bool CaptureCurrentSettingsSnapshot(json& a_snapshot);
	bool EnsureSettingsDirtyBaseline();
	void UpdateSettingsDirtyState();

	std::string cachedIniPath;  // io.IniFilename must point to a string that lives for the duration of the runtime

	// Menu navigation
	std::string pendingFeatureSelection;  // Feature to select on next frame

	// Input event handling
	std::vector<KeyEvent> _keyEventQueue;
	mutable std::shared_mutex _inputEventMutex;
	std::atomic<std::int64_t> _directInputWheelDelta = 0;

	// Keys whose key-down already fired a combo hotkey. Their matching key-up is
	// suppressed so a shared single-key binding does not also fire after modifier release.
	std::unordered_set<uint32_t> _comboFiredKeys;

	Menu() = default;

	void DrawGeneralSettings();
	void DrawAdvancedSettings();
	void DrawDisableAtBootSettings();
	void DrawFooter();

	void addToEventQueue(KeyEvent e);
	void ProcessInputEventQueue();
	bool IsCapturingHotkeyInput() const;
	winrt::com_ptr<IDXGIAdapter3> dxgiAdapter3;
};
