#pragma once

#include "Buffer.h"
#include "LightEditor.h"
#include "Utils/VanityCamera.h"
#include "Weather/CellLightingWidget.h"
#include "Weather/ImageSpaceWidget.h"
#include "Weather/LensFlareWidget.h"
#include "Weather/LightingTemplateWidget.h"
#include "Weather/PrecipitationWidget.h"
#include "Weather/ReferenceEffectWidget.h"
#include "Weather/VolumetricLightingWidget.h"
#include "Weather/WeatherWidget.h"
#include "WeatherUtils.h"
#include "Widget.h"

#include <unordered_map>

class EditorWindow
{
public:
	static EditorWindow* GetSingleton()
	{
		static EditorWindow singleton;
		return &singleton;
	}

	// Preview modes for exploring the scene without the full editor UI
	enum class PreviewMode
	{
		None,              // Full editor UI visible
		FreeCamera,        // Flying free camera (tfc), input to game
		FreeCameraLocked,  // Camera locked in place, editor interactive
		PlayMode           // Normal gameplay, no scroll interception
	};

	bool open = false;
	PreviewMode previewMode = PreviewMode::None;
	const static int maxRecordMarkers = 10;

	// Owned by EditorWindow, created in Draw(), released in destructor
	Texture2D* tempTexture = nullptr;

	/** @brief Screen-space bounds and source dimensions of the current game preview image. */
	struct ViewportImageRect
	{
		ImVec2 min{};
		ImVec2 max{};
		ImVec2 renderSize{};
		bool valid = false;
	};

	/** @brief Returns the mapping from the most recent rendered Viewport window. */
	[[nodiscard]] const ViewportImageRect& GetViewportImageRect() const noexcept { return viewportImageRect; }

	// Widget collections owned by EditorWindow, created in SetupResources(), released in destructor
	using WidgetVec = std::vector<std::unique_ptr<Widget>>;
	WidgetVec weatherWidgets;
	WidgetVec lightingTemplateWidgets;
	WidgetVec imageSpaceWidgets;
	WidgetVec volumetricLightingWidgets;
	WidgetVec precipitationWidgets;
	WidgetVec lensFlareWidgets;
	WidgetVec referenceEffectWidgets;

	/// Returns references to all editable widget collections for centralized iteration.
	std::array<WidgetVec*, 7> GetWidgetCollections()
	{
		return { &weatherWidgets, &lightingTemplateWidgets, &imageSpaceWidgets,
			&volumetricLightingWidgets, &precipitationWidgets, &lensFlareWidgets,
			&referenceEffectWidgets };
	}
	std::vector<std::unique_ptr<Widget>> artObjectWidgets;
	std::vector<std::unique_ptr<Widget>> effectShaderWidgets;

	// Owned by EditorWindow, created on demand in ShowObjectsWindow(), released in destructor
	std::unique_ptr<CellLightingWidget> currentCellLightingWidget;
	std::unordered_map<std::string, std::unique_ptr<CellLightingWidget>> cachedCellLightingWidgets;

	LightEditor lightEditor;

	/// When true, resets all window positions/sizes on next frame (auto-cleared).
	bool resetLayout = false;

	/// Bottom Y of the viewport window, set during layout for palette positioning.
	float viewportBottomY = 0.0f;

	// Time control constants
	static constexpr float kVanillaTimeScale = 20.0f;
	static constexpr float kGameHourMax = 23.99f;
	static constexpr float kTimeScaleMin = 0.1f;
	static constexpr float kTimeScaleMax = 4000.0f;
	static constexpr float kMenuBarSliderWidth = 400.0f;

	// Preview mode constants
	static constexpr float kDefaultFlySpeed = 10.0f;
	static constexpr float kMinFlySpeed = 1.0f;
	static constexpr float kMaxFlySpeed = 100.0f;
	static constexpr float kFlySpeedScrollStep = 2.0f;
	static constexpr float kToggleActiveAlpha = 0.6f;
	static constexpr float kToggleHoverAlpha = 0.8f;
	static constexpr float kInactiveHoverAlpha = 0.25f;

	// Preview mode state
	float flySpeed = kDefaultFlySpeed;
	ImVec2 savedMousePos = { -FLT_MAX, -FLT_MAX };

	void EnterPreviewMode(PreviewMode mode);
	void ExitPreviewMode();
	bool IsInPreviewMode() const { return previewMode != PreviewMode::None; }
	bool IsPreviewFlying() const { return previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::PlayMode; }
	PreviewMode GetPreviewMode() const { return previewMode; }
	void ToggleFreeCameraLock();
	void AdjustFlySpeed(float scrollDelta);

	// Vanity camera control
	Util::VanityCameraSuppressionLease vanityCameraSuppression;

	void ShowObjectsWindow();

	void ShowViewportWindow();

	void ShowWidgetWindow();

	void RenderUI();

	void SetupResources();
	void EnsureResources();

	void Draw();
	/** @brief Advances delayed light-reference work independently of editor visibility. */
	void AdvanceLightEditorDeferredWork();
	void UpdateOpenState();

	/** @brief Selects and enables the Light Editor after its parent editor has opened. */
	void ActivateLightEditor();
	[[nodiscard]] bool IsLightEditorSelected() const;
	[[nodiscard]] bool IsLightEditorEnabled() const { return lightEditor.IsEnabled(); }
	[[nodiscard]] bool IsLightPickerActive() const { return lightEditor.IsPicking(); }
	[[nodiscard]] bool HasLightEditorDeferredWork() const { return lightEditor.HasDeferredWork(); }
	/** @brief True only when the most recent editor UI pass published a valid preview. */
	[[nodiscard]] bool IsEditorViewportVisible() const { return open && settings.showViewport && viewportImageRect.valid; }
	void BeginLightPick() { lightEditor.BeginPick(); }
	void CancelLightPick() { lightEditor.CancelPick(); }

	void LockWeather(RE::TESWeather* weather);
	void UnlockWeather();
	bool IsWeatherLocked() const;
	RE::TESWeather* GetLockedWeather() const;
	static void InstallWeatherLockHooks();
	static bool AreWeatherLockHooksInstalled();
	static void MaintainWeatherLock();

	void PauseTime();
	void ResumeTime();
	inline void TogglePause() { timePaused ? ResumeTime() : PauseTime(); }
	void ResetTimeScale();
	bool IsTimePaused() const { return timePaused; }
	void SetTimeRunningForMenu(bool a_needsRunningTime);

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		static bool Register();
	};

	bool DrawGameHourSlider(const char* label = "Game Time", const char* format = "%.2f");
	void DrawTimeControls();

	/** @brief Returns true if ESC should close the editor rather than a child interaction. */
	bool ShouldHandleEscapeKey();

	/** @brief One-shot handoff set when a picker or popup consumes ESC on key-down. */
	bool suppressNextEditorEscape = false;

	void DisableVanityCamera();
	void RestoreVanityCamera();

	// Undo system
	struct UndoState
	{
		std::string widgetIdentity;
		std::string widgetLabel;
		bool restoreRuntime = false;
		Widget::UndoRestoreAction restore;
	};
	std::vector<UndoState> undoStack;
	static const size_t maxUndoStates = 50;

	void PushUndoState(Widget* widget);
	void PerformUndo();
	bool CanUndo() const { return !undoStack.empty(); }

	// Notification system
	struct Notification
	{
		std::string message;
		ImVec4 color;
		float startTime;
		float duration;
	};
	std::vector<Notification> notifications;

	void ShowNotification(const std::string& message, const ImVec4& color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f), float duration = 3.0f);
	void RenderNotifications();

	struct Settings
	{
		std::map<std::string, ImVec4> recordMarkers = {
			{ "To Do", { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ "In Progress", { 190.0f / 255.0f, 155.0f / 255.0f, 0.0f, 1.0f } },
			{ "Complete", { 0.0f, 130.0f / 255.0f, 0.0f, 1.0f } }
		};
		std::map<std::string, std::string> markedRecords;
		bool autoApplyChanges = true;
		bool useTextButtons = false;
		bool enableInheritFromParent = false;
		float editorUIScale = 1.0f;
		std::vector<std::string> favoriteWidgets;
		std::map<std::string, std::vector<std::string>> recentWidgets;
		int maxRecentWidgets = 10;

		bool showViewport = true;

		// Per-widget-type window sizes (serialized as JSON for persistence)
		json widgetTypeSizes;

		// Palette settings
		struct PaletteColorEntry
		{
			float r = 0.0f;
			float g = 0.0f;
			float b = 0.0f;
			int useCount = 0;
			float lastUsedTime = 0.0f;
			bool isFavorite = false;
		};
		struct PaletteValueEntry
		{
			std::string name;
			float value = 0.0f;
			int useCount = 0;
			float lastUsedTime = 0.0f;
			bool isFavorite = false;
		};
		struct PaletteFavoriteColor
		{
			bool hasValue = false;
			float r = 0.0f, g = 0.0f, b = 0.0f;
		};
		std::vector<PaletteColorEntry> paletteColors;
		std::vector<PaletteValueEntry> paletteValues;
		std::array<PaletteFavoriteColor, 10> paletteFavorites;
	};

	Settings settings;

	void Save();
	void AddToRecent(const std::string& widgetId, const std::string& category);
	void ToggleFavorite(const std::string& widgetId);
	bool IsFavorite(const std::string& widgetId) const;

	// Navigation helpers for weather-controlled settings
	void OpenWeatherFeatureSetting(RE::TESWeather* weather, const std::string& featureName, const std::string& settingName);

	~EditorWindow();

private:
	friend class Widget;

	void SaveAll();
	void SaveSettings();
	void LoadSettings();
	void SanitizeSettings();
	void ShowSettingsWindow();
	void Load();
	bool resourcesInitialized = false;
	json j;
	std::string settingsFilename = "EditorSettings";
	bool showSettingsWindow = false;
	std::string settingsSelectedCategory = "Flags";
	bool wasOpen = false;
	ViewportImageRect viewportImageRect;

	// Widget focus tracking for Ctrl+W
	Widget* lastFocusedWidget = nullptr;
	void OpenCellLightingWidget(RE::TESObjectCELL* cell, bool showNotification, bool requestFocus = false);
	Widget* FindWidgetByIdentity(std::string_view identity);

	// Time control state
	bool timePaused = false;
	float savedTimeScale = kVanillaTimeScale;
	float timeScaleSlider = kVanillaTimeScale;
	bool timeRestoredForMenu = false;
	bool wasPausedBeforeMenu = false;

	// Sorting state
	enum class SortColumn
	{
		None,
		EditorID,
		FormID,
		File,
		Status,
		JsonAttachment
	};
	SortColumn currentSortColumn = SortColumn::None;
	bool sortAscending = true;

	Widget* pendingDeleteWidget = nullptr;
	bool pendingDeletePopupRequested = false;

	void OnWidgetJsonAttachmentChanged(Widget* widget);
	std::unordered_map<Widget*, bool> jsonAttachmentCache;
	void RefreshJsonAttachmentCache(const std::vector<Widget*>& widgets);
	bool HasCachedJsonAttachment(Widget* widget) const;
	void InvalidateJsonAttachmentCache(Widget* widget = nullptr);
	// Objects window filter state
	enum class FilterColumn : int
	{
		All = 0,
		EditorID,
		FormID,
		File,
		Status,
		Count_  // Sentinel – must equal IM_ARRAYSIZE(kFilterColumnNames)
	};
	std::string m_selectedCategory = "Weather";
	std::string m_previousSelectedCategory = "Weather";
	char m_filterBuffer[256] = {};
	bool m_showOnlyFlagged = false;
	bool m_showOnlyFavorites = false;
	FilterColumn m_currentFilterColumn = FilterColumn::All;
	void ResetObjectsFilter();
	bool MatchesObjectFilter(Widget* w) const;
	static std::string ResolveEditorId(RE::TESForm* form, const WidgetVec& widgets);
};
