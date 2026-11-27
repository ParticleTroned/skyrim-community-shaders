#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "Globals.h"

using json = nlohmann::json;

struct Feature;

/// Manages scene-specific setting overrides (Interior Only, TimeOfDay, WeatherSpecific).
/// Zero coupling to individual features ? operates via JSON round-trip through Feature::SaveSettings/LoadSettings.
/// Event-driven: cell transitions detected via MenuOpenCloseEvent, mutations applied immediately.
class SceneSettingsManager
{
public:
	static SceneSettingsManager* GetSingleton()
	{
		static SceneSettingsManager singleton;
		return &singleton;
	}

	/** Suspends scene application while mutating base settings. */
	class BaseSettingsScope
	{
	public:
		BaseSettingsScope();
		~BaseSettingsScope();
		/** Resumes scene settings explicitly so callers can roll back on failure. */
		void Finish();
		BaseSettingsScope(const BaseSettingsScope&) = delete;
		BaseSettingsScope& operator=(const BaseSettingsScope&) = delete;

	private:
		bool active = true;
	};
	/** Replaces the selected Look's interior layer without writing scene/user files. */
	void SetLookLayer(const json& layer, const std::string& packId);
	/** Captures effective authored interior values, excluding paused entries. */
	json ExportLookLayer() const;
	/** Substitutes cached exterior values in a serialized snapshot, leaving active weather ownership to its serializer. */
	void OverlayBaseSettings(const std::string& feature, json& values, const std::set<std::string>& weatherVariables) const;
	/** Moves personal interior persistence into SettingsUser while a Look owns settings. */
	void UseLookPersistence(bool enabled);
	json SerializeLookEdits() const;
	void LoadLookEdits(const json& edits, const json& schemas);
	void ClearLookEditsUnder(const json& layer);

	// --- Scene Types ---

	enum class SceneType
	{
		InteriorOnly
		// Future: TimeOfDay, WeatherSpecific
	};

	// --- Event Handler ---

	/// Listens for LoadingMenu close to detect cell transitions.
	/// Same pattern as Skylighting::MenuOpenCloseEventHandler.
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		static bool Register()
		{
			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;
			if (!ui) {
				logger::error("[SceneSettings] UI event source not found");
				return false;
			}
			auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
			if (!eventSource) {
				logger::error("[SceneSettings] MenuOpenCloseEvent source not found");
				return false;
			}
			eventSource->AddEventSink(&singleton);
			logger::info("[SceneSettings] Registered MenuOpenCloseEventHandler");
			return true;
		}
	};

	// --- Setting Entry ---

	enum class EntrySource
	{
		User,       // User-added via UI
		Overwrite,  // Loaded from overwrite file
		LookPack
	};

	struct SettingEntry
	{
		std::string featureShortName;  // Feature's GetShortName()
		std::string settingKey;        // JSON key within the feature's settings
		json value;                    // Override value (bool, float, int, etc.)
		bool paused = false;           // Temporarily disabled
		EntrySource source = EntrySource::User;
		std::string sourceFilename;  // For overwrites: the filename it came from
	};
	struct LookState
	{
		std::vector<SettingEntry> interior;
		bool persistent;
	};
	/** Captures scene ownership for a profile transaction. */
	LookState CaptureLookState() const;
	/** Restores scene ownership while base settings are suspended. */
	void RestoreLookState(LookState state);

	// --- Generic Entry Management (scene-type agnostic) ---

	const std::vector<SettingEntry>& GetEntries(SceneType type) const;
	bool HasEntryFromSource(SceneType type, const std::string& featureShortName, const std::string& settingKey, EntrySource source) const;
	bool HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const;

	void AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value);
	void RemoveSetting(SceneType type, size_t index);
	void TogglePauseEntry(SceneType type, size_t index);
	void UpdateEntryValue(SceneType type, size_t index, const json& newValue, bool deferSave = false);

	void SetAllOverwritesPaused(SceneType type, bool paused);
	bool AreAllOverwritesPaused(SceneType type) const;
	bool HasOverwriteEntries(SceneType type) const;
	void DeleteAllOverwrites(SceneType type);

	void SetAllUserPaused(SceneType type, bool paused);
	bool AreAllUserPaused(SceneType type) const;
	void DeleteAllUserSettings(SceneType type);

	// --- Scene Application ---

	/// Called each frame from State::Draw() to process deferred cell transitions.
	/// Cell data is not yet available when the LoadingMenu close event fires,
	/// so we defer the actual transition check to the next rendered frame.
	void Update();

	/// Called by Update() when a deferred cell transition is pending.
	void OnCellTransition();

	/// Check if a specific feature+setting is currently being overridden by any active scene setting
	bool IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const;

	/// Check if any scene settings are active for a given feature
	bool HasActiveSettingsForFeature(const std::string& featureShortName) const;

	/// Per-feature pause: temporarily disable all scene-specific settings for a feature
	bool IsFeaturePaused(const std::string& featureShortName) const;
	void SetFeaturePaused(const std::string& featureShortName, bool paused);

	// --- Persistence ---

	void SaveUserSettings(SceneType type);
	void LoadUserSettings(SceneType type);
	/// Load up to 4096 JSON files in case-sensitive filename order; the first valid duplicate wins.
	/// Missing directories are optional; enumeration failures or excess files abort discovery before loading.
	void DiscoverOverwrites(SceneType type);

	/// Convenience: load all scene types
	void LoadAll();

	// --- Path Resolution ---

	static std::string GetSceneTypeName(SceneType type);
	static std::filesystem::path GetSettingsFilePath(SceneType type);
	static std::filesystem::path GetOverwritesPath(SceneType type);

	// --- Feature Metadata ---

	/// Get loaded feature short names filtered to only interior-relevant features
	static std::vector<std::string> GetInteriorRelevantFeatureNames();

	/// Get setting keys for a feature by JSON round-tripping its current settings
	static std::vector<std::string> GetFeatureSettingKeys(const std::string& featureShortName);

	/// Get current value of a specific setting from a feature
	static json GetFeatureSettingValue(const std::string& featureShortName, const std::string& settingKey);

	/// Detect the JSON type of a setting value for UI rendering
	enum class SettingType
	{
		Boolean,
		Integer,
		Float,
		String,
		Unknown
	};
	static SettingType DetectSettingType(const json& value);

private:
	SceneSettingsManager() = default;
	~SceneSettingsManager() = default;
	SceneSettingsManager(const SceneSettingsManager&) = delete;
	SceneSettingsManager& operator=(const SceneSettingsManager&) = delete;

	// --- Per scene-type storage ---
	std::map<SceneType, std::vector<SettingEntry>> entries;

	// --- Interior state tracking ---
	bool isCurrentlyApplied = false;
	bool queuedCellTransition = false;
	unsigned baseSettingsDepth = 0;
	bool resumeInterior = false;
	bool lookPersistence = false;

	// Stored exterior settings per-feature (only the overridden keys)
	std::map<std::string, json> savedExteriorSettings;

	// --- Pause states ---
	std::map<std::string, bool> featurePauseStates;

	static constexpr size_t MAX_OVERWRITE_FILE_SIZE = 1024 * 1024;
	static constexpr size_t MaxOverwriteFiles = 4096;

	// --- Helpers ---
	std::vector<SettingEntry>& GetEntriesMut(SceneType type);
	bool IsEntryActive(const SettingEntry& entry) const;

	void ReapplyIfActive();
	void ApplySettings(SceneType type);
	void RevertToExteriorSettings();
	void SaveExteriorSettings(SceneType type);
	std::array<EntrySource, 3> ApplicationOrder() const;
};
