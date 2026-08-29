#pragma once

/** @brief Reduces terrain texture tiling artifacts by adding stochastic variation to texture sampling. */
struct TerrainVariation : Feature
{
private:
	static constexpr std::string_view MOD_ID = "148123";

public:
	virtual inline std::string GetName() override { return "Terrain Variation"; }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	/**
	 * @brief Returns true for Lighting shaders when the feature is loaded.
	 * @details LOD tiling remains gated by @c enableLODTerrainTilingFix.
	 */
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return loaded && shaderType == RE::BSShader::Type::Lighting;
	}
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Terrain Variation reduces the repeating pattern effect on terrain textures.\n"
			"This technique creates more natural-looking terrain by adding variation to texture sampling.",
			{ "Reduces terrain texture tiling",
				"Stochastic texture sampling",
				"Improved terrain visual quality",
				"Compatible with Extended Materials parallax" }
		};
	}

	struct alignas(16) Settings
	{
		uint32_t enableLODTerrainTilingFix = 1;
		uint32_t pad[3]{};
	};

	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	/** @brief Draws the ImGui settings panel for Terrain Variation configuration. */
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	/** @brief Suppresses the default failed-load message display. */
	virtual bool DrawFailLoadMessage() const override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Initializes the feature and applies shader settings after plugin load. */
	virtual void PostPostLoad() override;
};
