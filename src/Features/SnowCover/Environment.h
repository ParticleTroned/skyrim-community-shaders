#pragma once
#include <string_view>
#include <vector>
namespace SnowEnvironment
{
	/** Active worldspace or interior cell ID; empty when no named world is available. */
	std::string_view GetCurrentWorldspaceOrCellEditorID();
	bool IsObjectLODTree(const char* name, const RE::BSShaderProperty* property);
	bool IsHarvestableFlora(RE::TESObjectREFR* ref);
	RE::NiBound MergeBounds(const RE::NiBound& lhs, const RE::NiBound& rhs);
	float GetFractionalGameMonth(const RE::Calendar& calendar);
	float GetPrecipitationIntensity(const RE::TESWeather* weather);
	bool IsArchitecturalModel(const RE::TESModel* model);
	bool IsPlacedFireCandidate(RE::TESBoundObject* base);
	bool IsFireGeometry(RE::BSGeometry* geometry);
	/** Appends bounds of visible placed-fire geometry without retaining scene nodes. */
	void CollectFireGeometry(RE::NiAVObject* object, bool hidden, bool& hasFire, std::vector<RE::NiBound>& samples);
}
