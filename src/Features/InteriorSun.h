#pragma once
#include "ShaderCache.h"

struct InteriorSun : Feature
{
private:
	static constexpr std::string_view MOD_ID = "153541";

public:
	virtual inline std::string GetName() override { return "Interior Sun"; }
	virtual inline std::string GetShortName() override { return "InteriorSun"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Allows for the sun and moon to cast light and shadows into interior spaces.",
			{ "Functions only for explicitly enabled interiors",
				"Utilizes existing sun, moon, and weather systems",
				"Includes an option to force double-sided rendering for unprepared interiors",
				"Fixes geometry culling issues that cause light leakage" }
		};
	}
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual bool SupportsVR() override { return true; }
	virtual void PostPostLoad() override;
	virtual void EarlyPrepass() override;

	virtual void DataLoaded() override
	{
		MenuOpenCloseEventHandler::Register();
	}

	struct Settings
	{
		bool Enabled = true;
		bool ForceDoubleSidedRendering = true;
		bool ForceSingleShadowCascade = true;
		float InteriorShadowDistance = 5000;
	};

	Settings settings;

	std::atomic<bool> runtimeEnabled = true;
	std::atomic<bool> isInteriorWithSun = false;

	struct GetWorldSpace
	{
		static RE::TESWorldSpace* thunk(RE::TES* tes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DirShadowLightCulling
	{
		static void thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShadowDirectionalLight_UpdateCamera
	{
		static bool thunk(RE::BSShadowDirectionalLight* a_light, const RE::NiCamera* a_camera);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void UpdateRasterStateCullMode(const RE::BSRenderPass* pass, const uint32_t technique) const
	{
		if (!pass || !pass->shaderProperty || !rasterStateCullMode || !globals::game::stateUpdateFlags) {
			return;
		}

		if (IsActiveInteriorSun() && settings.ForceDoubleSidedRendering && technique & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmap)) {
			const auto flags = pass->shaderProperty->flags;
			const auto renderTwoSided = flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided) || flags.none(RE::BSShaderProperty::EShaderPropertyFlag::kAssumeShadowmask, RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);
			if (renderTwoSided && *rasterStateCullMode != 0) {
				*rasterStateCullMode = 0;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			} else if (!renderTwoSided && *rasterStateCullMode != RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK) {
				*rasterStateCullMode = RE::BSGraphics::RasterStateCullMode::RASTER_STATE_CULL_MODE_BACK;
				globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_RASTER_CULL_MODE);
			}
		}
	}

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

		static bool Register()
		{
			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;
			if (!ui) {
				logger::error("UI event source not found");
				return false;
			}
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());
			return true;
		}
	};

	static bool IsInteriorWithSun(const RE::TESObjectCELL* cell);
	bool IsEnabled() const { return loaded && runtimeEnabled.load(std::memory_order_acquire); }
	bool IsActiveInteriorSun() const { return IsEnabled() && isInteriorWithSun.load(std::memory_order_acquire); }
	virtual bool IsCore() const override { return true; };

private:
	enum class CellFlagExt : uint16_t
	{
		kSunlightShadows = 1 << 15,
	};

	float* gShadowDistance = nullptr;
	float* gInteriorShadowDistance = nullptr;
	float vanillaInteriorShadowDistance = 0.0f;
	RE::Setting* fFirstSliceDistance = nullptr;
	uint32_t* rasterStateCullMode = nullptr;

	const RE::TESObjectCELL* currentCell = nullptr;

	bool arraysCleared = true;
	RE::BSTArray<RE::NiPointer<RE::NiAVObject>> currentCellRoomsAndPortals = {};
	RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>> replacementJobArrays = {};
	eastl::hash_set<RE::NiAVObject*> addedSet = {};

	static RE::TESWorldSpace* enableInteriorSun;
	static RE::TESWorldSpace* disableInteriorSun;

	bool DrawEnabledCheckbox();
	void SetRuntimeEnabled(bool a_enabled);

	void ClearArrays();

	void InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph);

	bool IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const;

	void PopulateReplacementJobArrays(const RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays);

	bool ShouldForceSingleShadowCascade() const;
	float GetInteriorShadowDistance() const;
	void ApplySingleShadowCascade(RE::BSShadowDirectionalLight* dirLight) const;

	static void SetShadowDistance(bool inInterior);
};
