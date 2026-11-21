#include "Skin.h"

#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Globals.h"
#include "Menu/SettingsPage.h"
#include "Utils/Finite.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
	using Settings = Skin::Settings;
	struct Number
	{
		const char* path;
		const char* label;
		const char* section;
		float minimum, maximum;
		float& (*value)(Settings&);
	};
#define SKIN_NUMBER(member, path, label, section, minimum, maximum) \
	{ path, label, section, minimum, maximum, [](Settings& s) -> float& { return s.member; } }
	constexpr Number Numbers[] = {
		SKIN_NUMBER(SkinMainRoughness, "/SkinMainRoughness", "Primary Roughness", "lighting", 0, 1),
		SKIN_NUMBER(SkinSecondRoughness, "/SkinSecondRoughness", "Secondary Roughness", "lighting", 0, 1),
		SKIN_NUMBER(SkinSpecularTexMultiplier, "/SkinSpecularTexMultiplier", "Specular Texture Multiplier", "lighting", 0, 10),
		SKIN_NUMBER(SecondarySpecularStrength, "/SecondarySpecularStrength", "Secondary Specular Strength", "lighting", 0, 1),
		SKIN_NUMBER(F0, "/F0", "Fresnel Reflectance", "lighting", 0, .1f),
		SKIN_NUMBER(BaseColorMultiplier, "/BaseColorMultiplier", "Base Color Multiplier", "lighting", 0, 2),
		SKIN_NUMBER(PhysicalMainRoughnessMultiplier, "/PhysicalMainRoughnessMultiplier", "Physical Primary Roughness", "lighting", 0, 2),
		SKIN_NUMBER(PhysicalSecondRoughnessMultiplier, "/PhysicalSecondRoughnessMultiplier", "Physical Secondary Roughness", "lighting", 0, 2),
		SKIN_NUMBER(PhysicalSpecularStrength, "/PhysicalSpecularStrength", "Physical Specular Strength", "lighting", 0, 2),
		SKIN_NUMBER(ExtraEdgeRoughness, "/ExtraEdgeRoughness", "Extra Edge Roughness", "lighting", 0, 1),
		SKIN_NUMBER(FuzzStrength, "/FuzzStrength", "Fuzz Strength", "lighting", 0, 2),
		SKIN_NUMBER(FuzzRoughness, "/FuzzRoughness", "Fuzz Roughness", "lighting", .1f, 1),
		SKIN_NUMBER(FuzzF0, "/FuzzF0", "Fuzz Reflectance", "lighting", 0, .5f),
		SKIN_NUMBER(Translucency, "/Translucency", "Transmission Strength", "transmission", 0, 1),
		SKIN_NUMBER(sssWidth, "/sssWidth", "Transmission Depth", "transmission", .001f, 1),
		SKIN_NUMBER(SkinDetailStrength, "/SkinDetailStrength", "Skin Detail Strength", "detail", -2, 2),
		SKIN_NUMBER(SkinDetailTiling, "/SkinDetailTiling", "Skin Detail Tiling", "detail", 1, 50),
		SKIN_NUMBER(BodyTilingMultiplier, "/BodyTilingMultiplier", "Body Tiling Multiplier", "detail", .5f, 5),
		SKIN_NUMBER(ExtraSkinWetness, "/ExtraSkinWetness", "Extra Skin Wetness", "wetness", 0, 2),
		SKIN_NUMBER(WetFadeTime, "/WetFadeTime", "Wetness Fade Time", "wetness", 0, 50),
		SKIN_NUMBER(StartSweat, "/StartSweat", "Sweat Starts Below Stamina", "wetness", 0, 1),
		SKIN_NUMBER(FullSweat, "/FullSweat", "Full Sweat Below Stamina", "wetness", 0, 1),
		SKIN_NUMBER(WetParams.x, "/WetParams/0", "Wetness Noise Scale", "wetness", 0, 1024),
		SKIN_NUMBER(WetParams.y, "/WetParams/1", "Wetness Noise Lacunarity", "wetness", 0, 2),
		SKIN_NUMBER(WetParams.z, "/WetParams/2", "Wetness Noise Persistence", "wetness", 0, 20),
		SKIN_NUMBER(WetParams.w, "/WetParams/3", "Wetness Normal Strength", "wetness", 0, 20)
	};
#undef SKIN_NUMBER
	struct Toggle
	{
		const char* name;
		bool Settings::* member;
	};
	constexpr Toggle Toggles[] = {
		{ "EnableSkin", &Settings::EnableSkin },
		{ "EnableSkinDetail", &Settings::EnableSkinDetail },
		{ "UseSSS", &Settings::UseSSS }
	};

	json Serialize(Settings a_settings)
	{
		json value = json::object();
		for (const auto& field : Toggles)
			value[field.name] = a_settings.*field.member;
		for (const auto& field : Numbers)
			value[json::json_pointer(field.path)] = field.value(a_settings);
		return value;
	}

	Settings Parse(const json& a_json, Settings a_settings, bool a_strict)
	{
		if (!a_json.is_object())
			throw std::invalid_argument("Advanced Skin settings must be an object");
		const auto known = Serialize(a_settings);
		if (a_strict)
			for (const auto& [key, value] : a_json.items())
				if (!known.contains(key))
					throw std::invalid_argument("Unknown Advanced Skin setting: " + key);
		if (a_json.contains("WetParams") && (!a_json.at("WetParams").is_array() || a_json.at("WetParams").size() != 4))
			throw std::invalid_argument("WetParams requires four numeric components");
		for (const auto& field : Toggles) {
			if (const auto it = a_json.find(field.name); it != a_json.end()) {
				if (!it->is_boolean())
					throw std::invalid_argument(std::string(field.name) + " must be boolean");
				a_settings.*field.member = it->get<bool>();
			}
		}
		for (const auto& field : Numbers) {
			const json::json_pointer path(field.path);
			if (!a_json.contains(path))
				continue;
			const auto& input = a_json.at(path);
			if (!input.is_number())
				throw std::invalid_argument(std::string(field.label) + " must be numeric");
			const double value = input.get<double>();
			if (!Util::IsFinite(value) || (a_strict && (value < field.minimum || value > field.maximum)))
				throw std::invalid_argument(std::string(field.label) + " is outside its supported range");
			field.value(a_settings) = static_cast<float>(std::clamp(value, double(field.minimum), double(field.maximum)));
		}
		return a_settings;
	}
}

std::pair<std::string, std::vector<std::string>> Skin::GetFeatureSummary()
{
	return { "Adds physically based lighting, fine detail and dynamic wetness to character skin.",
		{ "Two specular lobes and peach-fuzz highlights", "Optional roughness and wetness texture maps",
			"Sweat and retained water wetness", "Works with male and female Burley SSS controls" } };
}

void Skin::LoadSettings(json& a_json)
{
	settings = Parse(a_json, Settings{}, false);
}

void Skin::SaveSettings(json& a_json)
{
	a_json = Serialize(settings);
}

void Skin::RestoreDefaultSettings()
{
	settings = {};
}

json Skin::CapturePerformanceSettingsState() const
{
	return Serialize(settings);
}

void Skin::RestorePerformanceToggleState(const json& a_state)
{
	if (a_state.is_boolean())
		settings.EnableSkin = a_state.get<bool>();
	else
		settings = Parse(a_state, settings, false);
}

void Skin::DrawPerformanceSettings(bool a_advanced)
{
	Util::Widgets::Checkbox("Enable Advanced Skin", &settings.EnableSkin);
	Util::AddTooltip("Enables skin lighting, detail normals and actor wetness. Changes apply without shader recompilation.");
	if (a_advanced) {
		const auto disabled = Util::DisableGuard(!settings.EnableSkin);
		Util::Widgets::Checkbox("Enable Skin Detail", &settings.EnableSkinDetail);
		Util::AddTooltip("Adds small surface normals. Disabling skips the detail texture samples.");
		Util::Widgets::Checkbox("Enable Skin Transmission", &settings.UseSSS);
		Util::AddTooltip("Adds light transmitted through skin. The separate Subsurface Scattering feature controls screen-space diffusion.");
	}
}

void Skin::DrawSettings()
{
	MenuUI::SettingsPage page("Skin", { { "lighting", "Lighting", "Tune highlights and skin color.", "Dual specular lobes and fuzz", true, true, "Refine skin lighting" },
										  { "detail", "Detail", "Add fine surface normals.", "Skin texture detail", true, true, nullptr },
										  { "transmission", "Transmission", "Control light passing through thin skin.", "Backlit skin", true, true, nullptr },
										  { "wetness", "Wetness", "Tune sweat and drying after leaving water.", "Requires Wetterness or Wetness Effects", true, true, nullptr } });
	DrawPerformanceSettings(false);
	if (!resourceError.empty())
		ImGui::TextWrapped("%s", resourceError.c_str());
	const auto disabled = Util::DisableGuard(!settings.EnableSkin);
	if (page.Is("lighting"))
		ImGui::TextWrapped("Base Color Multiplier changes the material before lighting. Male/female skin brightness and saturation remain in Subsurface Scattering's Burley controls.");
	if (page.Is("detail"))
		Util::Widgets::Checkbox("Enable Skin Detail", &settings.EnableSkinDetail);
	if (page.Is("transmission")) {
		Util::Widgets::Checkbox("Enable Skin Transmission", &settings.UseSSS);
		ImGui::TextWrapped("These controls affect transmitted light. Subsurface Scattering still controls diffusion, intensity, brightness and saturation separately for male and female skin.");
	}
	for (const auto& field : Numbers)
		if (page.Is(field.section))
			Util::Widgets::SliderFloat(field.label, &field.value(settings), field.minimum, field.maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (page.Is("detail") && ImGui::Button("Reload Skin Textures"))
		ReloadTextures();
}

Skin::SkinData Skin::GetCommonBufferData() const
{
	SkinData data{};
	const bool enabled = loaded && settings.EnableSkin && perGeometryCB && resourceError.empty();
	data.skinParams = float4(settings.SkinMainRoughness, settings.SkinSecondRoughness, settings.SkinSpecularTexMultiplier, float(enabled));
	data.skinParams2 = float4(settings.SecondarySpecularStrength, settings.ExtraSkinWetness, settings.F0, settings.BaseColorMultiplier);
	data.skinDetailParams = float4(settings.SkinDetailTiling, settings.BodyTilingMultiplier, settings.SkinDetailStrength, float(enabled && settings.EnableSkinDetail && settings.SkinDetailStrength != 0 && detailTexture));
	data.sssParams = float4(settings.Translucency, settings.sssWidth, 0.0f, float(settings.UseSSS));
	data.fuzzParams = float4(settings.FuzzStrength, settings.FuzzRoughness, settings.FuzzF0, settings.ExtraEdgeRoughness);
	data.physicalParams = float4(settings.PhysicalMainRoughnessMultiplier, settings.PhysicalSecondRoughnessMultiplier, settings.PhysicalSpecularStrength, float(IsWetnessEnabled()));
	data.wetParams = settings.WetParams;
	return data;
}

bool Skin::IsWetnessEnabled() const
{
	return globals::features::wetterness.loaded ? globals::features::wetterness.IsRuntimeActive() : globals::features::wetnessEffects.IsRuntimeActive();
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json Skin::SettingsSchema()
{
	json properties = json::object();
	for (const auto& field : Toggles)
		properties[field.name] = { { "type", "boolean" } };
	json wetComponents = json::array();
	for (const auto& field : Numbers) {
		json descriptor{ { "type", "number" }, { "minimum", field.minimum }, { "maximum", field.maximum }, { "description", field.label } };
		if (std::string_view(field.path).starts_with("/WetParams/"))
			wetComponents.push_back(descriptor);
		else
			properties[field.path + 1] = descriptor;
	}
	properties["WetParams"] = { { "type", "array" }, { "minItems", 4 }, { "maxItems", 4 }, { "prefixItems", wetComponents }, { "items", false } };
	return { { "type", "object" }, { "additionalProperties", false }, { "minProperties", 1 }, { "properties", properties } };
}

json Skin::Control(const json& a_args)
{
	const auto operation = a_args.value("skinOperation", std::string("inspect"));
	if (operation != "inspect" && operation != "reload")
		throw std::invalid_argument("Unknown skin operation");
	if (operation == "reload") {
		if (a_args.contains("skin") || !loaded || !perGeometryCB)
			throw std::invalid_argument("Reload requires available skin resources and no settings edits");
		ReloadTextures();
	}
	if (a_args.contains("skin")) {
		if (!loaded || !resourceError.empty())
			throw std::runtime_error("Advanced Skin is unavailable");
		settings = Parse(a_args.at("skin"), settings, true);
	}
	return { { "available", loaded }, { "ready", bool(perGeometryCB) && resourceError.empty() },
		{ "settings", Serialize(settings) }, { "detailLoaded", bool(detailTexture) },
		{ "resourceError", resourceError }, { "sessionOnly", true } };
}
#endif
