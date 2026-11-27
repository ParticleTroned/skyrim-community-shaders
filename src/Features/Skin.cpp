#include "Skin.h"

#include "Skin/TransmissionProfile.h"

#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Globals.h"
#include "Menu/SettingsPage.h"
#include "Utils/Finite.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
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
		float minimum;
		float maximum;
		float& (*value)(Settings&);
		const char* help;
		const char* format;
		const char* heading;
	};
	constexpr std::array Numbers{
		Number{ "/SkinMainRoughness", "Primary Roughness", "lighting", 0, 1, [](Settings& s) -> float& { return s.SkinMainRoughness; }, "Softness of the main skin highlight. Higher values spread the shine over a wider area.", "%.2f", "Highlights" },
		Number{ "/SkinSecondRoughness", "Secondary Roughness", "lighting", 0, 1, [](Settings& s) -> float& { return s.SkinSecondRoughness; }, "Softness of the secondary highlight. Lower values give a tighter shine.", "%.2f", nullptr },
		Number{ "/SkinSpecularTexMultiplier", "Specular Texture Multiplier", "lighting", 0, 10, [](Settings& s) -> float& { return s.SkinSpecularTexMultiplier; }, "Strength of the specular texture used by skins without an extra parameter map.", "%.2f", nullptr },
		Number{ "/SecondarySpecularStrength", "Secondary Specular Strength", "lighting", 0, 1, [](Settings& s) -> float& { return s.SecondarySpecularStrength; }, "Contribution of the second highlight to the overall shine.", "%.2f", nullptr },
		Number{ "/F0", "Fresnel Reflectance", "lighting", 0, .1f, [](Settings& s) -> float& { return s.F0; }, "Reflectivity when looking straight at the skin. Higher values make highlights brighter.", "%.3f", nullptr },
		Number{ "/BaseColorMultiplier", "Base Color Multiplier", "lighting", 0, 2, [](Settings& s) -> float& { return s.BaseColorMultiplier; }, "Brightness of the skin texture before lighting. Male and female brightness and saturation remain in Subsurface Scattering.", "%.2f", nullptr },
		Number{ "/PhysicalMainRoughnessMultiplier", "Physical Primary Roughness", "lighting", 0, 2, [](Settings& s) -> float& { return s.PhysicalMainRoughnessMultiplier; }, "Scales the main roughness supplied by an optional skin parameter map. Higher values soften highlights.", "%.2f", "Texture-driven highlights" },
		Number{ "/PhysicalSecondRoughnessMultiplier", "Physical Secondary Roughness", "lighting", 0, 2, [](Settings& s) -> float& { return s.PhysicalSecondRoughnessMultiplier; }, "Scales secondary roughness supplied by the skin parameter map.", "%.2f", nullptr },
		Number{ "/PhysicalSpecularStrength", "Physical Specular Strength", "lighting", 0, 2, [](Settings& s) -> float& { return s.PhysicalSpecularStrength; }, "Scales secondary highlight strength supplied by the skin parameter map.", "%.2f", nullptr },
		Number{ "/ExtraEdgeRoughness", "Extra Edge Roughness", "lighting", 0, 1, [](Settings& s) -> float& { return s.ExtraEdgeRoughness; }, "Softens skin highlights near the edge of the view-facing surface.", "%.2f", nullptr },
		Number{ "/FuzzStrength", "Fuzz Strength", "lighting", 0, 2, [](Settings& s) -> float& { return s.FuzzStrength; }, "Strength of the soft peach-fuzz sheen on skin. Zero removes it.", "%.2f", "Peach fuzz" },
		Number{ "/FuzzRoughness", "Fuzz Roughness", "lighting", .1f, 1, [](Settings& s) -> float& { return s.FuzzRoughness; }, "Softness of the peach-fuzz highlight.", "%.2f", nullptr },
		Number{ "/FuzzF0", "Fuzz Reflectance", "lighting", 0, .5f, [](Settings& s) -> float& { return s.FuzzF0; }, "Reflectivity of the peach-fuzz layer. Higher values brighten its sheen.", "%.3f", nullptr },
		Number{ "/Translucency", "Transmission Strength", "transmission", 0, 1, [](Settings& s) -> float& { return s.Translucency; }, "Strength of light passing through thin skin, such as backlit ears.", "%.2f", nullptr },
		Number{ "/sssWidth", "Transmission Depth", "transmission", .001f, 1, [](Settings& s) -> float& { return s.sssWidth; }, "How far transmitted light travels through skin. Higher values brighten thicker areas.", "%.3f", nullptr },
		Number{ "/SSSAmount", "Post-process SSS Strength", "transmission", 0, 1, [](Settings& s) -> float& { return s.SSSAmount; }, "Scales the skin diffusion mask. Requires Subsurface Scattering; its male and female controls still apply. Zero skips diffusion and its colour adjustments for this skin.", "%.2f", nullptr },
		Number{ "/SkinDetailStrength", "Skin Detail Strength", "detail", -2, 4, [](Settings& s) -> float& { return s.SkinDetailStrength; }, "Strength of fine surface normals. Higher values emphasize pores. Zero removes detail; negative values reverse bumps and grooves. Very fine detail can still fade at high tiling or distance.", "%.2f", nullptr },
		Number{ "/SkinDetailTiling", "Skin Detail Tiling", "detail", 1, 50, [](Settings& s) -> float& { return s.SkinDetailTiling; }, "How often the fine-detail texture repeats. Higher values make the pattern smaller.", "%.2f", nullptr },
		Number{ "/BodyTilingMultiplier", "Body Tiling Multiplier", "detail", .5f, 5, [](Settings& s) -> float& { return s.BodyTilingMultiplier; }, "Extra detail repetition on the body relative to the head.", "%.2f", nullptr },
		Number{ "/ExtraSkinWetness", "Extra Skin Wetness", "wetness", 0, 2, [](Settings& s) -> float& { return s.ExtraSkinWetness; }, "Adds persistent wetness to skin. Hair and clothing keep their own wetness response.", "%.2f", nullptr },
		Number{ "/WetResponse", "Wetness Response", "wetness", 0, 1, [](Settings& s) -> float& { return s.WetResponse; }, "Scales rain, sweat and retained water on skin. Zero removes this response.", "%.2f", nullptr },
		Number{ "/WetFadeTime", "Wetness Fade Time", "wetness", 0, 50, [](Settings& s) -> float& { return s.WetFadeTime; }, "Time for full sweat or retained water to dry after its source stops. Zero dries immediately.", "%.1f s", nullptr },
		Number{ "/StartSweat", "Sweat Starts Below Stamina", "wetness", 0, 1, [](Settings& s) -> float& { return s.StartSweat; }, "Stamina fraction where sweat begins. 1 means full stamina; 0 means empty. The two thresholds form an ordered range; equal values disable sweat.", "%.2f", "Sweat" },
		Number{ "/FullSweat", "Full Sweat Below Stamina", "wetness", 0, 1, [](Settings& s) -> float& { return s.FullSweat; }, "Stamina fraction where sweat reaches full strength. Normally lower than the start threshold; equal values disable sweat.", "%.2f", nullptr },
		Number{ "/WetParams/0", "Wetness Noise Scale", "wetness", 0, 1024, [](Settings& s) -> float& { return s.WetParams.x; }, "Size of the wetness noise pattern. Higher values make smaller droplets.", "%.0f", "Droplet pattern" },
		Number{ "/WetParams/1", "Wetness Noise Lacunarity", "wetness", 0, 2, [](Settings& s) -> float& { return s.WetParams.y; }, "Change in noise scale between detail layers, shaping the droplet pattern.", "%.2f", nullptr },
		Number{ "/WetParams/2", "Wetness Noise Persistence", "wetness", 0, 20, [](Settings& s) -> float& { return s.WetParams.z; }, "Contribution of finer noise layers to the droplet pattern.", "%.2f", nullptr },
		Number{ "/WetParams/3", "Wetness Normal Strength", "wetness", 0, 20, [](Settings& s) -> float& { return s.WetParams.w; }, "Strength of the surface distortion from wetness.", "%.2f", nullptr }
	};
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
		if (!a_json.is_object() || (a_strict && a_json.empty()))
			throw std::invalid_argument("Advanced Skin settings must be a nonempty object");
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
	DrawSettingsEnabledControl();
	if (a_advanced) {
		const auto disabled = Util::DisableGuard(!settings.EnableSkin);
		Util::Widgets::Checkbox("Enable Skin Detail", &settings.EnableSkinDetail);
		Util::AddTooltip("Adds small surface normals. Disabling skips the detail texture samples.");
		Util::Widgets::Checkbox("Enable Skin Transmission", &settings.UseSSS);
		Util::AddTooltip("Adds light transmitted through skin. The separate Subsurface Scattering feature controls screen-space diffusion.");
	}
}

Feature::SettingsHeaderStatus Skin::GetSettingsHeaderStatus() const
{
	auto status = Feature::GetSettingsHeaderStatus();
	if (settings.EnableSkin && !resourceError.empty())
		return { "Unavailable", true, "Advanced Skin resources could not load. Check CSX.log and restart the game." };
	return status;
}

void Skin::DrawSettings()
{
	MenuUI::SettingsPage page("Skin", { { "lighting", "Lighting", "Tune highlights and skin color.", "Dual specular lobes and fuzz", true, true, "Refine skin lighting" },
										  { "detail", "Detail", "Add fine surface normals.", "Skin texture detail", true, true, nullptr },
										  { "transmission", "Transmission & SSS", "Control backlighting and skin diffusion.", "Backlit skin and subsurface scattering", true, true, nullptr },
										  { "wetness", "Wetness", "Tune sweat and drying after leaving water.", "Requires Wetterness or Wetness Effects", true, true, nullptr } });
	const auto disabled = Util::DisableGuard(!settings.EnableSkin);
	if (page.Is("detail")) {
		MenuUI::SectionHeading("Surface detail");
		Util::Widgets::Checkbox("Enable Skin Detail", &settings.EnableSkinDetail);
		Util::AddTooltip("Adds small surface normals. Disabling skips the detail texture samples.");
	}
	if (page.Is("transmission")) {
		MenuUI::SectionHeading("Backlighting and diffusion");
		Util::Widgets::Checkbox("Enable Skin Transmission", &settings.UseSSS);
		Util::AddTooltip("Adds backlighting through thin skin. Screen-space diffusion is controlled separately by Subsurface Scattering.");
	}
	const bool wetnessAvailable = IsWetnessEnabled();
	if (page.Is("wetness")) {
		MenuUI::SectionHeading("Skin wetness", "Hair and clothing keep their own wetness response.");
		if (settings.EnableSkin && !wetnessAvailable)
			MenuUI::PageNotice("Enable Wetterness or Wetness Effects to use skin wetness.", MenuUI::NoticeKind::Unavailable);
	}
	for (const auto& field : Numbers) {
		if (page.Is(field.section)) {
			if (field.heading)
				MenuUI::SectionHeading(field.heading);
			const std::string_view path(field.path);
			const auto controlDisabled = Util::DisableGuard(
				(std::string_view(field.section) == "detail" && !settings.EnableSkinDetail) ||
				((path == "/Translucency" || path == "/sssWidth") && !settings.UseSSS) ||
				(std::string_view(field.section) == "wetness" && !wetnessAvailable));
			Util::Widgets::SliderFloat(field.label, &field.value(settings), field.minimum, field.maximum, field.format, ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip(field.help);
		}
	}
	if (page.Is("detail") && ImGui::Button("Reload Skin Textures"))
		ReloadTextures();
}

Skin::SkinData Skin::GetCommonBufferData() const
{
	SkinData data{};
	const auto effective = settings;
	const bool enabled = loaded && effective.EnableSkin && perGeometryCB && resourceError.empty();
	data.skinParams = float4(effective.SkinMainRoughness, effective.SkinSecondRoughness, effective.SkinSpecularTexMultiplier, float(enabled));
	data.skinParams2 = float4(effective.SecondarySpecularStrength, effective.ExtraSkinWetness, effective.F0, effective.BaseColorMultiplier);
	data.skinDetailParams = float4(effective.SkinDetailTiling, effective.BodyTilingMultiplier, effective.SkinDetailStrength, float(enabled && effective.EnableSkinDetail && effective.SkinDetailStrength != 0 && detailTexture));
	data.sssParams = float4(effective.Translucency, effective.sssWidth, effective.SSSAmount, float(effective.UseSSS));
	data.fuzzParams = float4(effective.FuzzStrength, effective.FuzzRoughness, effective.FuzzF0, effective.ExtraEdgeRoughness);
	data.physicalParams = float4(effective.PhysicalMainRoughnessMultiplier, effective.PhysicalSecondRoughnessMultiplier, effective.PhysicalSpecularStrength, float(IsWetnessEnabled()));
	data.wetParams = effective.WetParams;
	data.wetResponse = effective.WetResponse;
	if (enabled && effective.UseSSS && effective.Translucency > 0) {
		// Thread-local packing state avoids shared cache writes across callers.
		thread_local SkinTransmission::ProfileCache cache;
		const auto& profile = cache.Get(effective.sssWidth);
		data.noMapTransmissionProfile = float3(profile[0], profile[1], profile[2]);
	}
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
		json descriptor{ { "type", "number" }, { "minimum", field.minimum }, { "maximum", field.maximum }, { "description", field.help } };
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
