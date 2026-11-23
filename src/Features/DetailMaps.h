#pragma once

#include "DetailMapsConfig.h"
#include "DetailMapsPolicy.h"
#include <memory>
#include <optional>

/** @brief Applies path-selected surface detail to eligible lighting draws on SE, AE and VR. */
struct DetailMaps : Feature
{
	std::string GetName() override { return "Detail Maps"; }
	std::string GetShortName() override { return "DetailMaps"; }
	std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	std::string GetFeatureModLink() override { return MakeNexusModURL("197436"); }
	bool SupportsVR() override { return true; }
	std::string_view GetShaderCacheAbiVersion() override { return "1"; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { "Adds fine surface detail to nearby stone, wood, leather and fabric.",
			{ "Requires the Detail Maps texture and rule pack", "Supports legacy and PBR materials",
				"Rules select surfaces by diffuse texture path", "Fades smoothly with distance" } };
	}

	using Settings = DetailMapsConfig::Settings;
	Settings settings;

	void DrawSettings() override;
	bool HasPerformanceSettings() const override { return true; }
	void DrawPerformanceSettings(bool) override;
	SettingsHeaderStatus GetSettingsHeaderStatus() const override;
	void LoadSettings(json& value) override;
	void SaveSettings(json& value) override;
	void RestoreDefaultSettings() override;
	void SetupResources() override;
	void Reset() override;
	void GenerateShaderPermutations(RE::BSShader* shader) override;
	/** @brief Admits startup detail variants only when the feature and content directories are available. */
	bool ShouldPrecompileShaders() const;
	bool SupportsPerformanceCostMeasurement() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override { return settings.Enabled; }
	bool IsPerformanceCostMeasurementReady() const override { return !settings.Enabled || lastPendingShaders == 0; }
	const char* GetPerformanceCostMeasurementWaitText() const override { return "Waiting for Detail Maps shaders"; }
	void SetPerformanceCostMeasurementEnabled(bool value) override;
	json CapturePerformanceSettingsState() const override;
	void RestorePerformanceToggleState(const json& state) override;
	/** @brief The disabled draw path performs only this gate and the restoration gate. */
	bool IsEnabled() const { return loaded && settings.Enabled; }
	bool HasActivePass() const { return passState.has_value(); }

	/** @brief Binds a ready detail variant; missing content or shaders retain the existing draw. */
	void BeginPass(RE::BSShader* shader, RE::BSRenderPass* pass);
	/** @brief Restores the shader and resource bindings captured by BeginPass. */
	void EndPass();

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Inspects detail content or applies validated session settings/reload requests. */
	json Control(const json& args);
	/** @brief Returns the settings and status schemas used by the DevBench registration. */
	static json GetControlSchemas();
#endif

private:
	struct PerDraw
	{
		float Scale, Albedo, Normal, Roughness;
		float FadeEnd, FadeInvRange, Transpose, Pad;
	};
	static_assert(sizeof(PerDraw) == 32);
	struct Set
	{
		std::string name;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
	};
	struct Rule
	{
		std::vector<std::string> match, exclude;
		uint32_t set = 0;
		float scale = 4, albedo = 1, normal = 1, roughness = 1;
		bool transpose = false;
		int priority = 0;
		size_t file = 0;
	};
	struct Entry
	{
		RE::BSFixedString name;
		RE::BSFixedString fallback;
		int32_t rule = -1;
	};
	struct PassState
	{
		explicit PassState(ID3D11DeviceContext* context);
		~PassState();
		ID3D11DeviceContext* context;
		winrt::com_ptr<ID3D11PixelShader> shader;
		winrt::com_ptr<ID3D11Buffer> buffer;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		winrt::com_ptr<ID3D11SamplerState> sampler;
	};

	bool HasContent() const { return !sets.empty() && !rules.empty(); }
	void Reload();
	void LoadSets();
	void LoadRules();
	int32_t FindRule(RE::BSRenderPass* pass);
	int32_t MatchRule(const char* path) const;
	void ClampSettings();
	void DrawDistanceSettings();
	bool BindPass(RE::BSShader* shader, RE::BSRenderPass* pass);
	std::vector<Set> sets;
	std::vector<Rule> rules;
	std::vector<std::string> excludes, ruleErrors;
	ankerl::unordered_dense::map<RE::NiSourceTexture*, Entry> entries;
	std::unique_ptr<ConstantBuffer> perDrawCB;
	winrt::com_ptr<ID3D11SamplerState> detailSampler;
	std::optional<PassState> passState;
	PerDraw lastPerDraw{};
	bool hasPerDraw = false;
	std::string resourceError;
	uint32_t passCount = 0, lastPassCount = 0;
	uint32_t pendingShaders = 0, lastPendingShaders = 0;
};
