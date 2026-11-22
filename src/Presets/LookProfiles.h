#pragma once

#include "LookPack.h"

struct Feature;

namespace Presets
{
	/** Owns selected Look baselines; feature serializers remain the source of settings values. */
	class LookProfiles
	{
	public:
		static LookProfiles& Get();
		/** Captures code defaults once, before settings or overrides have been loaded. */
		void CaptureFactorySettings();
		/** Hydrates saved selection and personal edits after feature availability is known. */
		void Load(const Json& installedDefaults, const Json& userSettings);
		/** Adds selection to full snapshots, including dirty-state snapshots. */
		void SaveSelection(Json& settings) const;
		/** Converts appearance to edits relative to the active pack without changing other fields. */
		bool PrepareSave(Json& settings, std::string& error, Feature* feature = nullptr) const;
		bool IsManaged() const { return managed; }
		/** Explicit default restoration clears pack ownership while retaining the new save contract. */
		void OnDefaultsRestored();
		/** Inspects installed packs without applying them. */
		Json Inspect();
		/** Selects one Look and restores its personal appearance edits. */
		bool Select(const std::string& id, std::string& error);
		/** Returns to installed appearance and its saved personal edits. */
		bool Clear(std::string& error);
		/** Exports authored appearance and scene rules as a new upstream-shaped pack. */
		bool Export(const std::string& id, std::string& error, const std::filesystem::path& folder = {});
		/** Imports a pack folder or a legacy Visuals file, preserving the source. */
		bool Import(const std::filesystem::path& source, const std::string& id, std::string& error);
		/** Restores authored values for the active profile without deleting another profile's edits. */
		bool Restore(std::string& error);
		/** Shared UI/API availability, including saves, comparisons and rejected full presets. */
		const char* MutationBlockReason() const;
		const std::string& GetLoadError() const { return loadError; }
		std::string ActiveName() const;
		std::string ActiveId() const { return selected.empty() ? std::string{} : selected.front().id; }
		static std::filesystem::path RootPath();

	private:
		Json Capture() const;
		Json Schemas() const;
		bool Apply(const Json& candidate, std::string& error);
		Json Appearance(const Json& values) const;
		Json BuildBase(const std::vector<LookPack>& packs) const;
		bool Change(const std::vector<LookPack>& packs, bool resetEdits, std::string& error);
		Json factory = Json::object();
		Json installed = Json::object();
		Json activeBase = Json::object();
		std::vector<LookPack> selected;
		Json savedSelection = Json::object();
		bool managed = false;
		bool ready = false;
		std::string loadError;
	};
}
