#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

/** @brief Reduces terrain texture tiling artifacts by adding stochastic variation to texture sampling. */
struct TerrainVariation : Feature
{
private:
	static constexpr std::string_view MOD_ID = "148123";

public:
	virtual inline std::string GetName() override { return "Terrain Variation"; }
	std::string GetDisplayName() override { return T("feature.terrain_variation.name", "Terrain Variation"); }
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
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.terrain_variation.reduces_visible_repetition_in_terrain_textures", "Reduces visible repetition in terrain textures."),
			{ T("feature.terrain_variation.reduces_terrain_and_landscape_textured_mesh_tiling", "Reduces terrain and landscape-textured mesh tiling"),
				T("feature.terrain_variation.key_feature_2", "Stochastic texture sampling"),
				T("feature.terrain_variation.key_feature_3", "Improved terrain visual quality"),
				T("feature.terrain_variation.key_feature_4", "Compatible with Extended Materials parallax") }
		};
	}

	struct alignas(16) Settings
	{
		uint32_t enableLODTerrainTilingFix = 1;
		uint32_t enableMeshSupport = 1;
		uint32_t pad[2]{};
	};

	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 16);

	Settings settings;

	/** @brief Draws the ImGui settings panel for Terrain Variation configuration. */
	virtual void DrawSettings() override;
	/** @brief Suppresses the default failed-load message display. */
	virtual bool DrawFailLoadMessage() const override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Collects the diffuse paths in landscape texture records and their seasonal swaps. */
	virtual void DataLoaded() override;
	/** @brief Updates mesh eligibility for this draw, clearing stale eligibility on every call. */
	void UpdateMeshPermutation(RE::BSRenderPass* a_pass);
	/** @brief Sets the runtime mesh option; saving settings persists it. */
	void SetMeshSupportEnabled(bool a_enabled);
	/** @brief Reports whether installed mesh variation is enabled. */
	bool IsMeshSupportEnabled() const { return loaded && settings.enableMeshSupport != 0; }

	/** @brief Initializes the feature and applies shader settings after plugin load. */
	virtual void PostPostLoad() override;

private:
	struct CachedTexture
	{
		RE::BSFixedString name;  // Retains the interned key so its address cannot be recycled.
		bool landscape = false;
	};
	std::shared_mutex meshTextureMutex;
	std::unordered_set<std::string> landscapeDiffusePaths;
	bool landscapeDiffusePathsAvailable = false;
	std::unordered_map<const char*, CachedTexture> meshTextureCache;

	bool IsLandscapeDiffuseTexture(const RE::BSFixedString& a_name);
};
