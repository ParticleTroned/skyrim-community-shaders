#pragma once

#include "Buffer.h"

struct GrassLighting : Feature
{
private:
	static constexpr std::string_view MOD_ID = "86502";

public:
	static constexpr float kGlossinessMin = 1.0f;
	static constexpr float kGlossinessMax = 100.0f;
	static constexpr float kSpecularStrengthMin = 0.0f;
	static constexpr float kSpecularStrengthMax = 1.0f;

	virtual inline std::string GetName() override { return "Grass Lighting"; }
	virtual inline std::string GetShortName() override { return "GrassLighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_LIGHTING"; }
	virtual bool IsCore() const override { return true; };
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Grass; };
	virtual std::string_view GetCategory() const override { return FeatureCategories::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Grass Lighting enhances grass rendering with improved lighting, specularity, and subsurface scattering.\n"
			"This makes grass appear more natural and responsive to lighting conditions.",
			{ "Enhanced grass lighting model",
				"Specular highlights on grass",
				"Subsurface scattering effects",
				"Improved grass visual quality",
				"Configurable material properties" }
		};
	}

	struct alignas(16) Settings
	{
		float Glossiness = 20.0f;
		float SpecularStrength = 0.5f;
		float SubsurfaceScatteringAmount = 1.0f;
		uint OverrideComplexGrassSettings = false;
		float BasicGrassBrightness = 1.0f;
		float ComplexGrassThreshold = 0.03f;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	static float ClampGlossiness(float glossiness, float fallback);
	static float ClampSpecularStrength(float specularStrength, float fallback);
	void SanitizeSettings();

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};
