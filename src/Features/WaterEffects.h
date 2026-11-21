#pragma once

#include <winrt/base.h>

struct WaterEffects : Feature
{
private:
	static constexpr std::string_view MOD_ID = "112762";

public:
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
	virtual inline std::string GetName() override { return "Water Effects"; }
	virtual inline std::string GetShortName() override { return "WaterEffects"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Adds moving underwater light patterns and depth to water waves.",
			{ "Animated caustics on submerged surfaces",
				"Parallax depth on compatible water waves",
				"Appearance controls in Visuals > Global > Water" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;
	void DrawSettings() override;

	virtual void SetupResources() override;

	virtual void Prepass() override;

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
