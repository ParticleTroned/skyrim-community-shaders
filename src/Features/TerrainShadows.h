#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"
#include <filesystem>

struct TerrainShadows : public Feature
{
private:
	static constexpr std::string_view MOD_ID = "135817";

public:
	virtual inline std::string GetName() override { return "Terrain Shadows"; }
	virtual inline std::string GetShortName() override { return "TerrainShadows"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Adds realistic shadow casting from terrain features using heightmap data to create accurate terrain shadows that enhance depth perception and visual realism.",
			{ "Heightmap-based terrain shadow calculation",
				"Dynamic shadow updates based on sun position",
				"Support for custom heightmap files",
				"Real-time shadow preprocessing and computation",
				"Integration with existing shadow systems" }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		bool EnableTerrainShadow = true;
	} settings;

	bool needPrecompute = false;
	uint shadowUpdateIdx = 0;
	bool hasPreviousLightDirection = false;
	RE::NiPoint3 previousLightDirection{};

	struct HeightMapMetadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
		float2 zRange;
	};
	std::unordered_map<std::string, HeightMapMetadata> heightmaps;
	HeightMapMetadata* cachedHeightmap;

	struct ShadowUpdateCB
	{
		float2 LightPxDir;   // direction on which light descends, from one pixel to next via dda
		float2 LightDeltaZ;  // per LightUVDir, upper penumbra and lower, should be negative
		uint StartPxCoord;
		float2 PxSize;
		float BlendWeight;
		float2 PosRange;
		float2 ZRange;
	} shadowUpdateCBData;
	static_assert(sizeof(ShadowUpdateCB) % 16 == 0);
	std::unique_ptr<ConstantBuffer> shadowUpdateCB = nullptr;

	struct alignas(16) PerFrame
	{
		uint EnableTerrainShadow;
		float3 Scale;
		float2 ZRange;
		float2 Offset;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	PerFrame GetCommonBufferData();

	Util::LazyShader<ID3D11ComputeShader> shadowUpdateProgram;

	std::unique_ptr<Texture2D> texHeightMap = nullptr;
	std::unique_ptr<Texture2D> texShadowHeight = nullptr;
	bool shadowHeightValid = false;

	bool IsHeightMapReady();

	virtual void SetupResources() override;
	void ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style);
	void CompileComputeShaders();
	ID3D11ComputeShader* GetShadowUpdateProgram();

	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override { return true; }
	virtual bool IsPerformanceCostMeasurementEnabled() const override { return settings.EnableTerrainShadow; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override { settings.EnableTerrainShadow = a_enabled; }

	virtual void EarlyPrepass() override;
	void LoadHeightmap();
	void Precompute();
	bool UpdateShadow(bool a_refreshImmediately);

	virtual void ReflectionsPrepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; }
	virtual void ClearShaderCache() override;
	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
