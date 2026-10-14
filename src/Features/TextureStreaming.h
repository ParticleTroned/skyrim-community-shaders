#pragma once

#include "Feature.h"
#include "TextureStreaming/Settings.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

/** Configurable residency and load-time sizing for validated static DDS material textures. */
struct TextureStreaming : Feature
{
	static TextureStreaming& Instance();
	TextureStreaming();
	~TextureStreaming();
	std::string GetName() override { return "Texture Streaming"; }
	std::string GetShortName() override { return "TextureStreaming"; }
	bool SupportsVR() override { return true; }
	bool IsCore() const override { return true; }
	std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void PostPostLoad() override;
	void Reset() override;
	/** Capture world camera and material sampling before later UI passes overwrite renderer state. */
	void CaptureWorldDemand(float mipBias);
	void DrawSettings() override;
	void DrawSettingsEnabledControl() override;
	void LoadSettings(nlohmann::json&) override;
	void SaveSettings(nlohmann::json&) override;
	void RestoreDefaultSettings() override;
	/** Thread-safe settings update; omitted categories and modes retain the current selection.
	 * GPU work remains owned by the completed-frame service. */
	void Configure(bool enabled, std::uint32_t maximumDrop, std::optional<StreamingTextures::Categories> categories = std::nullopt,
		std::optional<StreamingTextures::Modes> modes = std::nullopt, std::optional<StreamingTextures::Options> options = std::nullopt);
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Legacy developer API alias for the persistent continuous trigger; requires enabled streaming. */
	void SetDebugForceStreaming(bool enabled);
#endif
	/** Fresh owned status with frame/generation identity; JSON formatting occurs after the live lock.
	 * Logical capacities do not measure physical VRAM relief; subsystem snapshots are independent. */
	nlohmann::json GetStatus() const;

private:
	struct State;
	std::unique_ptr<State> state;
};
