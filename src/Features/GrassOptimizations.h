#pragma once

#include "GrassOptimizations/GrassPolicy.h"

#include <memory>
#include <mutex>

class GrassBucketRenderer;

/** @brief Batch native grass across compatible cells with optional instance culling and mesh LOD. */
struct GrassOptimizations : Feature
{
	GrassOptimizations();
	~GrassOptimizations();
	std::string GetName() override { return "Grass Optimizations"; }
	std::string GetShortName() override { return "GrassOptimizations"; }
	std::string_view GetCategory() const override { return FeatureCategories::kFoliage; }
	std::string_view GetShaderDefineName() override { return "GRASS_OPTIMIZATIONS"; }
	bool HasShaderDefine(RE::BSShader::Type type) override { return type == RE::BSShader::Type::Grass; }
	bool SupportsVR() override { return true; }
	std::string_view GetShaderCacheAbiVersion() override { return "native-cell-buckets-v6"; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	void DrawSettings() override;
	bool HasEssentialSettings() const override { return true; }
	void DrawEssentialSettings() override;
	bool HasPerformanceSettings() const override { return true; }
	void DrawPerformanceSettings(bool advanced) override;
	json CapturePerformanceSettingsState() const override;
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override;
	void SetPerformanceCostMeasurementEnabled(bool enabled) override { SetEnabled(enabled); }
	bool IsPerformanceCostMeasurementReady() const override;
	const char* GetPerformanceCostMeasurementWaitText() const override { return "Waiting for grass rendering"; }
	json CapturePerformanceCostMeasurementState() const override { return CapturePerformanceSettingsState(); }
	/** @brief Restore all grass controls after a cost comparison, respecting scene Hi-Z compatibility. */
	void RestorePerformanceCostMeasurementState(const json& state) override;
	void LoadSettings(json& settings) override;
	void SaveSettings(json& settings) override;
	void RestoreDefaultSettings() override;
	void SetupResources() override;
	void ClearShaderCache() override;
	void PostPostLoad() override;
	/** @brief Stage a toggle; changes take effect at the next grass pass without recompilation. */
	void SetEnabled(bool enabled);
	bool IsEnabled() const;
	bool IsHookInstalled() const;
	bool IsGrassHiZAvailable() const;
	GrassPolicy::Settings GetSettings() const;
	/** @brief Validate a complete settings transaction and reject conflicting grass Hi-Z activation. */
	bool SetSettings(const GrassPolicy::Settings& settings, std::string& error);
	/** @brief Apply a validated partial settings transaction; unknown fields reject the whole update. */
	bool UpdateSettings(const json& update, std::string& error);
	void PrepareGeometry(RE::BSRenderPass* pass);
	GrassBucketRenderer& GetRenderer() { return *renderer; }
#ifdef DEVBENCH_BRIDGE_ENABLED
	void SetDiagnosticsEnabled(bool enabled);
	json GetDiagnostics() const;
#endif
private:
	void DrawControls(bool advanced);
	mutable std::mutex settingsMutex;
	GrassPolicy::Settings settings;
	std::unique_ptr<GrassBucketRenderer> renderer;
};
