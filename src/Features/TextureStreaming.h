#pragma once

#include "Feature.h"
#include <memory>
#include <nlohmann/json.hpp>

/** Pressure-driven residency for validated static opaque DDS material textures. */
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
	/** Thread-safe settings update; GPU work remains owned by the completed-frame service. */
	void Configure(bool enabled, std::uint32_t maximumDrop);
	/** Shared observations and logical allocations, never an estimate of physical bytes reclaimed. */
	nlohmann::json GetStatus() const;

private:
	struct State;
	std::unique_ptr<State> state;
};
