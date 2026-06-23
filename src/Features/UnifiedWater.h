#pragma once
#include "OverlayFeature.h"
#include "UnifiedWater/Flowmap.h"
#include "UnifiedWater/WaterCache.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

struct UnifiedWater : OverlayFeature
{
	virtual inline std::string GetName() override { return "Unified Water"; }
	virtual inline std::string GetShortName() override { return "UnifiedWater"; }
	virtual inline std::string_view GetShaderDefineName() override { return "UNIFIED_WATER"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Unified Water provides a comprehensive fix to water LOD mismatch by replacing distant water tiles with LOD0 (Close Water).",
			{ "Unifies distant and close water appearance, streamlining all lighting visuals.",
				"Completely and fundamentally resolves water LOD mismatch issues.",
				"Provides background systems for water geometry rendering, allowing more advanced water effects.",
				"Improves vanilla performance by using optimized water meshes for distant water." }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }
	// Temporary kill switch paired with State.cpp's forced boot-disable list.
	virtual bool IsHiddenFromUserView() const override { return true; }

	struct Settings
	{
		bool UseOptimisedMeshes = true;
	};

	Settings settings;

	struct WaterTilePlacement
	{
		int32_t x{};
		int32_t y{};
		uint32_t size{};
		RE::FormID waterForm{};
		std::uint8_t waterFlags{};
	};

	void RegisterGeneratedWaterTile(const RE::NiAVObject* object, const WaterTilePlacement& placement);
	void UnregisterGeneratedWaterTilesInTree(const RE::NiAVObject* object);
	bool TryGetGeneratedWaterTile(const RE::NiAVObject* object, WaterTilePlacement& placement) const;

	struct TESWaterSystem_InitializeWater_SetWaterShaderMaterialParams
	{
		static void thunk(RE::TESWaterForm* form, RE::BSWaterShaderMaterial* material);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSWaterShaderMaterial_ComputeCRC32
	{
		static int32_t thunk(RE::BSWaterShaderMaterial* material, uint32_t srcHash);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Attach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Detach
	{
		static void thunk(RE::BGSTerrainBlock* block);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainNode_UpdateWaterMeshSubVisibility
	{
		static void thunk(const RE::BGSTerrainNode* node, RE::BSMultiBoundNode* waterParent);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TES_SetWorldSpace
	{
		static void thunk(RE::TES* tes, RE::TESWorldSpace* worldSpace, bool isExterior);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TES_DestroySkyCell
	{
		static void thunk(RE::TES* tes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* waterShader, RE::BSRenderPass* pass, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESWaterSystem_UpdateDisplacementMeshPosition
	{
		static void thunk(RE::TESWaterSystem* waterSystem);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		static bool Register();
	};

	virtual void DrawSettings() override;

	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override;

	virtual void DataLoaded() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; }

	virtual void PostPostLoad() override;

private:
	RE::NiPointer<RE::BSTriShape> waterMesh;
	RE::NiPointer<RE::BSTriShape> optimisedWaterMesh;
	std::unique_ptr<Flowmap> flowmap;
	std::unique_ptr<WaterCache> waterCache;

	RE::NiNode** gWaterLOD = nullptr;
	RE::NiPointer<RE::NiSourceTexture>* gFlowMapSourceTex = nullptr;
	int32_t* gFlowMapSize = nullptr;
	float4* gDisplacementCellTexCoordOffset = nullptr;
	RE::NiPoint2* gDisplacementMeshPos = nullptr;
	RE::NiPoint2* gDisplacementMeshFlowCellOffset = nullptr;

	std::atomic<RE::TESWorldSpace*> currentPlayerWorldSpace{ nullptr };
	std::atomic<bool> pendingChildWsCull{ false };
	std::atomic<int64_t> nextChildWsCullRetryMs{ 0 };
	// Game-thread TES snapshot used by deferred child-worldspace cull fallbacks.
	std::atomic<RE::TES*> cachedTes{ nullptr };
	std::atomic_bool exteriorWorldspaceActive{ false };
	std::atomic_bool mapMenuOpen{ false };

	mutable std::shared_mutex generatedWaterTilesLock;
	std::unordered_map<const RE::NiAVObject*, WaterTilePlacement> generatedWaterTiles;

	void ClearGeneratedWaterTiles();
	void RemoveDuplicateGeneratedWaterTiles(RE::TESWaterSystem* waterSystem, RE::NiNode* lodRoot, const std::vector<WaterTilePlacement>& touchedTiles);
	void TryCompleteDeferredChildWorldspaceCull(RE::TES* tes = nullptr);

	void SetFlowmapTex() const;
	bool IsExteriorWorldspaceActive() const;
	void UpdateWaterLODCull() const;
	static bool LoadOrderChanged();
};
