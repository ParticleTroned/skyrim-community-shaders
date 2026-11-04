#include "FurShells.h"

#include "Features/Wetterness.h"
#include "FurShells/AssetScan.h"
#include "Globals.h"
#include "GpuPass.h"
#include "Hooks.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/ActorUtils.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/Finite.h"
#include "Utils/Game.h"
#include "Utils/TexturePath.h"
#include "Utils/UI.h"
#include <thread>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FurShells::Settings,
	Enabled,
	ShellCount,
	Length,
	BodyLength,
	Droop,
	RootThreshold,
	TipThreshold,
	RootDarkening,
	ShellColor,
	FadeStart,
	FadeEnd,
	HideCoveredFur,
	OverlayFur,
	MinFurPixels,
	EnableWind,
	WindStrength,
	EnableWetFur,
	WetStrength,
	EnableTouch,
	TouchStrength,
	EnableDentRecovery,
	FitHandCapsules,
	PreciseContactSelection,
	EnableHeldObjects,
	ContactLimit,
	ContactRange,
	EnableInertia,
	InertiaStrength,
	InertiaPreset,
	InertiaFrequency,
	InertiaGain,
	InertiaLengthExponent,
	InertiaDroopExponent,
	InertiaMaxBend,
	ShowTouchEdges,
	DistanceShellBudget)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FurShells::ShellOverride,
	Enabled,
	ShellCount,
	Length,
	Droop,
	RootThreshold,
	TipThreshold,
	RootDarkening,
	ShellColor,
	FadeStart,
	FadeEnd)

namespace
{
	constexpr uint32_t FurFlag = static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::FurShells);
	constexpr uint32_t FurDepthFlag = FurFlag | static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::FurShellsDepth);
	constexpr uint32_t ModelSpaceNormalsFlag = static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals);
	constexpr uint32_t ReflectionsFlag = static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);

	constexpr uint32_t MaxShells = 32;
	constexpr uint32_t ShellTextureSlot = 124;
	constexpr uint32_t NormalTextureSlot = 126;
	constexpr uint32_t DepthTextureSlot = 125;
	constexpr uint32_t VertexPerPassSlot = 7;
	constexpr uint32_t PixelPerPassSlot = 10;
	constexpr uint32_t ResolvesPerFrame = 16;
	constexpr uint32_t TextureRetryFrames = 120;
	constexpr size_t MaxEntries = 8192;
	constexpr size_t MaxMissingShells = 65536;
	constexpr uint32_t MaxOverrideSize = 65536;
	constexpr float PixelsPerShell = 1.0f;
	constexpr float MaxMinFurPixels = 16.0f;
	constexpr uint32_t FirstPersonRenderMode = 0x16;
	constexpr size_t MaxDeferredPasses = 8192;
	constexpr size_t MaxDepthStates = 128;
	constexpr std::string_view EmptyOverlayPath = "textures\\actors\\character\\overlays\\default.dds";

	/** @brief Form ID of the reference that owns a mesh, found by walking up to the root that carries the reference, or 0. */
	RE::FormID GetWearer(const RE::NiAVObject* a_object, bool a_actorOnly = false)
	{
		constexpr int MaxDepth = 16;
		for (int depth = 0; a_object && depth < MaxDepth; ++depth, a_object = a_object->parent) {
			if (const auto* reference = a_object->GetUserData())
				return !a_actorOnly || reference->IsActor() ? reference->GetFormID() : 0;
		}
		return 0;
	}

	bool IsFirstPersonPass()
	{
		auto* accumulator = *globals::game::currentAccumulator.get();
		return accumulator && static_cast<uint32_t>(accumulator->GetRuntimeData().renderMode) == FirstPersonRenderMode;
	}

	bool CanDrawShells(RE::BSGeometry* a_geometry)
	{
		if (a_geometry->GetGeometryRuntimeData().skinInstance)
			return true;

		const auto type = a_geometry->GetType().get();
		return type == RE::BSGeometry::Type::kTriShape || type == RE::BSGeometry::Type::kMeshLODTriShape;
	}

	bool IsFurTechnique(uint32_t a_descriptor)
	{
		using enum SIE::ShaderCache::LightingShaderTechniques;

		switch (static_cast<SIE::ShaderCache::LightingShaderTechniques>((a_descriptor >> 24) & 0x3F)) {
		case None:
		case Envmap:
		case Glowmap:
		case Parallax:
		case Facegen:
		case FacegenRGBTint:
		case ParallaxOcc:
		case MultilayerParallax:
			return true;
		default:
			return false;
		}
	}

	bool IsSkinTechnique(uint32_t a_descriptor)
	{
		using enum SIE::ShaderCache::LightingShaderTechniques;

		const auto technique = static_cast<SIE::ShaderCache::LightingShaderTechniques>((a_descriptor >> 24) & 0x3F);
		return technique == Facegen || technique == FacegenRGBTint;
	}

	float GetFurPixels(float a_length, float a_distance)
	{
		const float projection = std::abs(Util::GetCameraData(0).projMat.m[1][1]);
		const float height = Util::ConvertToDynamic({ 0.0f, static_cast<float>(globals::game::graphicsState->screenHeight) }).y;
		const float pixels = a_length * 0.5f * height * projection / std::max(a_distance, 1.0f);
		return std::isfinite(pixels) ? std::max(pixels, 0.0f) : static_cast<float>(MaxShells) * PixelsPerShell;
	}

	DXGI_FORMAT GetDepthViewFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R24G8_TYPELESS:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_R16_UNORM;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	std::string GetOverridePath(const std::string& a_shellPath)
	{
		return a_shellPath.substr(0, a_shellPath.size() - 4) + ".json";
	}

	struct SettingRange
	{
		std::string_view name;
		double minimum;
		double maximum;
	};

	constexpr SettingRange SettingRanges[]{
		{ "ShellCount", 1, MaxShells },
		{ "Length", 0.05, 6 },
		{ "BodyLength", 0.05, 3 },
		{ "Droop", 0, 2 },
		{ "RootThreshold", 0, 1 },
		{ "TipThreshold", 0, 1 },
		{ "RootDarkening", 0, 1 },
		{ "ShellColor", 0, 1 },
		{ "FadeStart", 0, 8000 },
		{ "FadeEnd", 0, 8000 },
		{ "MinFurPixels", 0, MaxMinFurPixels },
		{ "WindStrength", 0, 2 },
		{ "WetStrength", 0, 1 },
		{ "TouchStrength", 0, 2 },
		{ "ContactLimit", 1, FurInteractors::MaxLiveCapsulesPerPass },
		{ "ContactRange", 100, 1600 },
		{ "InertiaStrength", 0, FurInertia::MaxStrength },
		{ "InertiaPreset", 0, 2 },
		{ "InertiaFrequency", FurInertia::MinFrequencyHz, FurInertia::MaxTuningFrequencyHz },
		{ "InertiaGain", 0.1, FurInertia::MaxGain },
		{ "InertiaLengthExponent", 0, FurInertia::MaxLengthExponent },
		{ "InertiaDroopExponent", 0, FurInertia::MaxDroopExponent },
		{ "InertiaMaxBend", 0.05, FurInertia::MaxBendLimit },
	};

	const SettingRange& GetSettingRange(std::string_view a_name)
	{
		const auto found = std::ranges::find(SettingRanges, a_name, &SettingRange::name);
		if (found == std::end(SettingRanges))
			throw std::logic_error("Missing fur range: " + std::string(a_name));
		return *found;
	}

	void ClampOverride(FurShells::ShellOverride& a_values)
	{
		const FurShells::ShellOverride defaults;
		const auto clamp = [](std::string_view name, float value, float fallback) {
			const auto& range = GetSettingRange(name);
			return Util::ClampFinite(value, static_cast<float>(range.minimum), static_cast<float>(range.maximum), fallback);
		};
		a_values.ShellCount = std::clamp(a_values.ShellCount, 1u, MaxShells);
		a_values.Length = clamp("Length", a_values.Length, defaults.Length);
		a_values.Droop = clamp("Droop", a_values.Droop, defaults.Droop);
		a_values.RootThreshold = clamp("RootThreshold", a_values.RootThreshold, defaults.RootThreshold);
		a_values.TipThreshold = clamp("TipThreshold", a_values.TipThreshold, defaults.TipThreshold);
		a_values.RootDarkening = clamp("RootDarkening", a_values.RootDarkening, defaults.RootDarkening);
		a_values.ShellColor = clamp("ShellColor", a_values.ShellColor, defaults.ShellColor);
		a_values.FadeEnd = clamp("FadeEnd", a_values.FadeEnd, defaults.FadeEnd);
		a_values.FadeStart = std::min(clamp("FadeStart", a_values.FadeStart, defaults.FadeStart), a_values.FadeEnd);
	}

	void ValidateSettingsObject(const json& a_values, bool a_global)
	{
		if (!a_values.is_object())
			throw std::invalid_argument("Fur settings must be an object");
		const json defaults = a_global ? json(FurShells::Settings{}) : json(FurShells::ShellOverride{});
		for (const auto& [name, value] : a_values.items()) {
			if (!defaults.contains(name))
				throw std::invalid_argument("Unknown fur setting: " + name);
			if (defaults.at(name).is_boolean()) {
				if (!value.is_boolean())
					throw std::invalid_argument(name + " must be boolean");
			} else {
				const auto& range = GetSettingRange(name);
				if (!value.is_number() || (defaults.at(name).is_number_integer() && !value.is_number_integer()) ||
					!std::isfinite(value.get<double>()) || value.get<double>() < range.minimum || value.get<double>() > range.maximum)
					throw std::invalid_argument(name + " is outside its finite range or has the wrong numeric type");
			}
		}
	}

	bool IsSafeTexturePath(std::string_view a_path)
	{
		return a_path.starts_with("textures\\") && a_path.ends_with(".dds") && a_path.size() <= 1024 &&
		       a_path.find("..") == std::string_view::npos && a_path.find(':') == std::string_view::npos &&
		       a_path.find('\0') == std::string_view::npos && a_path.find("\\\\") == std::string_view::npos;
	}

	RE::NiSourceTexturePtr LoadShell(const std::string& a_path, bool& a_exists, std::string& a_shellPath)
	{
		constexpr std::string_view pbrPrefix = "textures\\pbr\\";

		a_exists = false;
		if (!IsSafeTexturePath(a_path))
			return nullptr;

		const std::string stem = a_path.substr(0, a_path.size() - 4);
		std::array<std::string, 2> candidates{ stem + "_shell.dds", std::string() };
		if (stem.starts_with(pbrPrefix))
			candidates[1] = "textures\\" + stem.substr(pbrPrefix.size()) + "_shell.dds";

		for (const auto& candidate : candidates) {
			if (!IsSafeTexturePath(candidate) || !RE::BSResourceNiBinaryStream(candidate).good())
				continue;
			a_exists = true;

			RE::NiPointer<RE::NiTexture> texture;
			RE::BSShaderManager::GetTexture(candidate.c_str(), true, texture, false);
			if (texture && texture->GetRTTI() == globals::rtti::NiSourceTextureRTTI.get()) {
				logger::info("[Fur Shells] {} uses {}", a_path, candidate);
				a_shellPath = candidate;
				return RE::NiSourceTexturePtr(static_cast<RE::NiSourceTexture*>(texture.get()));
			}
		}

		return nullptr;
	}
}

void FurShells::RestoreDefaultSettings()
{
	settings = {};
	ClearDynamics();
}

void FurShells::LoadSettings(json& o_json)
{
	ValidateSettingsObject(o_json, true);
	auto next = o_json.get<Settings>();
	if (next.FadeStart > next.FadeEnd)
		throw std::invalid_argument("FadeStart must not exceed FadeEnd");
	settings = next;
	ClearDynamics();
}

void FurShells::SaveSettings(json& o_json)
{
	o_json = settings;
}

void FurShells::DrawSettings()
{
	int shellCount = static_cast<int>(settings.ShellCount);
	if (ImGui::SliderInt("Fur layers", &shellCount, 1, static_cast<int>(MaxShells), "%d", ImGuiSliderFlags_AlwaysClamp))
		settings.ShellCount = static_cast<uint32_t>(shellCount);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "More layers make fur look fuller and smoother up close, but cost more performance.");

	ImGui::SliderFloat("Length", &settings.Length, 0.05f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "How far fur grows from clothing or creatures. Higher values make it longer and fluffier.");

	ImGui::SliderFloat("Body Length", &settings.BodyLength, 0.05f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Length of fur on skin, such as Khajiit bodies and heads. Higher values make it longer.");

	ImGui::SliderFloat("Droop", &settings.Droop, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "How much fur hangs downward. Higher values make it droop more; 0 removes this sag.");

	ImGui::SliderFloat("Root Cutoff", &settings.RootThreshold, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Controls thickness near the skin. Lower values fill in more fur; higher values leave more gaps.");

	ImGui::SliderFloat("Tip Cutoff", &settings.TipThreshold, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Controls how thin the fur becomes toward its tips. Higher values give finer, sparser tips.");

	ImGui::SliderFloat("Root Brightness", &settings.RootDarkening, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Darkens the base of the fur to give it depth. Lower values darken it more; 1 leaves it as bright as the tips.");

	ImGui::SliderFloat("Fur texture color", &settings.ShellColor, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Choose the fur's color source: 0 uses the underlying skin or clothing; 1 uses the fur detail texture.");

	ImGui::SliderFloat("Fade Start", &settings.FadeStart, 0.0f, 8000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	Util::AddTooltip("Distance where fur starts losing detail. Lower values save work sooner; higher values keep nearby fur fuller.");
	ImGui::SliderFloat("Fade End", &settings.FadeEnd, 0.0f, 8000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	settings.FadeStart = std::min(settings.FadeStart, settings.FadeEnd);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Distance where added fur disappears completely. Increase it to keep fur visible farther away, at a higher cost.");

	ImGui::Checkbox("Hide Fur Under Clothing", &settings.HideCoveredFur);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "On: hides covered fur to reduce poking through clothing. Off: draws it even under clothing. Adds rendering work; applies outside first-person and menu views.");

	ImGui::Checkbox("Show Overlays On Fur", &settings.OverlayFur);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "On: RaceMenu tattoos, paint and patterns also color the fur. Off: they stay on the skin beneath it. More visible overlays can reduce performance.");

	ImGui::SliderFloat("Minimum Fur Size", &settings.MinFurPixels, 0.0f, MaxMinFurPixels, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Thins fur that looks very small on screen. Higher values make it sparser; 0 removes this extra thinning.");

	ImGui::Text("%s: %u", "Fur passes last frame", lastPassCount);

	if (ImGui::TreeNode("Optional Fur Effects")) {
		ImGui::TextWrapped("Optional movement and touch effects can reduce performance. Enable the effects you want.");
		bool changed = ImGui::Checkbox("Weather Wind", &settings.EnableWind);
		Util::AddTooltip("On: weather wind sways the fur. Off: wind does not move it.");
		if (settings.EnableWind) {
			changed |= ImGui::SliderFloat("Wind Strength", &settings.WindStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("How much weather wind moves the fur. Higher values give stronger swaying; 0 removes wind movement.");
		}
		changed |= ImGui::Checkbox("Wet Fur (Wetterness)", &settings.EnableWetFur);
		Util::AddTooltip("On: wet weather flattens and darkens fur on characters and creatures. Off: keeps its dry appearance. Requires Wetterness.");
		if (settings.EnableWetFur) {
			changed |= ImGui::SliderFloat("Wet Fur Strength", &settings.WetStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("How strongly wet weather flattens and darkens fur on characters and creatures. 0 keeps its dry appearance.");
		}
		changed |= ImGui::Checkbox("Contact Compression", &settings.EnableTouch);
		Util::AddTooltip("On: hands and nearby bodies press fur down. Off: fur does not react to touch.");
		if (settings.EnableTouch) {
			changed |= ImGui::SliderFloat("Touch Strength", &settings.TouchStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("How strongly contact flattens fur. Higher values deepen the dents; 0 removes visible touch deformation.");
			changed |= ImGui::Checkbox("Recovering Dents", &settings.EnableDentRecovery);
			Util::AddTooltip("On: touch leaves dents that gradually recover. Off: dents disappear when contact ends.");
			if (globals::game::isVR) {
				changed |= ImGui::Checkbox("Match VR hand shape", &settings.FitHandCapsules);
				Util::AddTooltip("Matches contact more closely to the visible hand shape. Off uses a simpler estimate. More precise fitting adds processing work.");
			}
			changed |= ImGui::Checkbox("Precise touch placement", &settings.PreciseContactSelection);
			Util::AddTooltip("Chooses touches using the visible fur surface. Off uses a simpler estimate. Can improve contact placement at a higher processing cost.");
			if (globals::game::isVR) {
				changed |= ImGui::Checkbox("HIGGS Held Objects (VR)", &settings.EnableHeldObjects);
				Util::AddTooltip("On: objects held with HIGGS can press fur down. Off: contact comes from bodies and hands only. Requires HIGGS in VR.");
			}
			int contacts = static_cast<int>(settings.ContactLimit);
			if (ImGui::SliderInt("Touches per object", &contacts, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp)) {
				settings.ContactLimit = static_cast<uint32_t>(contacts);
				changed = true;
			}
			Util::AddTooltip("Maximum simultaneous touches on one fur-covered object. More touches improve busy interactions but add processing work.");
			changed |= ImGui::SliderFloat("Touch distance", &settings.ContactRange, 100.0f, 1600.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			Util::AddTooltip("How far away bodies are checked for fur contact. Increase it to keep interactions farther away; lower values reduce processing work.");
			ImGui::TextWrapped("Hand contact works without HIGGS. More precise contact and lingering dents can reduce performance.");
			changed |= ImGui::Checkbox("Highlight touch areas", &settings.ShowTouchEdges);
			Util::AddTooltip("Highlights where contact presses the fur so you can check placement. Off shows the normal fur appearance.");
		}
		changed |= ImGui::Checkbox("Movement sway", &settings.EnableInertia);
		Util::AddTooltip("On: fur trails behind movement and settles after the wearer stops. Off: it follows the wearer without this extra sway.");
		if (settings.EnableInertia)
			DrawInertiaSettings();
		changed |= ImGui::Checkbox("Reduce fur detail with distance", &settings.DistanceShellBudget);
		Util::AddTooltip("On: uses fewer fur layers in the distance to save work. Off: fades density instead. Both use the selected fade distances.");
		if (changed)
			ClearDynamics();
		ImGui::TreePop();
	}
	DrawOverrideSettings();
}

void FurShells::DrawOverrideSettings()
{
	if (!ImGui::TreeNodeEx("Settings for individual textures", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	if (ImGui::BeginCombo("Fur texture", selectedShellName.c_str())) {
		for (auto& [name, data] : shellTextures) {
			if (ImGui::Selectable(name.c_str(), name == selectedShellName)) {
				selectedShellName = name;
				selectedShell = &data;
				overrideStatus.clear();
			}
		}
		ImGui::EndCombo();
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Choose which clothing or creature fur to customize. If it is missing, bring that item or creature into view first.");

	if (selectedShell) {
		auto& values = selectedShell->values;
		if (!selectedShell->overridden)
			values = GetGlobalValues(selectedShell->skin);

		ImGui::TextDisabled("%s", selectedShell->overridden ? "This texture uses its own values." : "No override yet. This texture follows the settings above.");

		bool edited = ImGui::Checkbox("Fur on selected texture", &values.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "On: adds fur to items using this texture. Off: removes their added fur without disabling fur on other items.");

		int shellCount = static_cast<int>(values.ShellCount);
		if (ImGui::SliderInt("Fur layers", &shellCount, 1, static_cast<int>(MaxShells), "%d", ImGuiSliderFlags_AlwaysClamp)) {
			values.ShellCount = static_cast<uint32_t>(shellCount);
			edited = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "More layers make fur look fuller and smoother up close, but cost more performance.");

		edited |= ImGui::SliderFloat("Length", &values.Length, 0.05f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "How far fur grows from clothing or creatures. Higher values make it longer and fluffier.");

		edited |= ImGui::SliderFloat("Droop", &values.Droop, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "How much fur hangs downward. Higher values make it droop more; 0 removes this sag.");

		edited |= ImGui::SliderFloat("Root Cutoff", &values.RootThreshold, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Controls thickness near the skin. Lower values fill in more fur; higher values leave more gaps.");

		edited |= ImGui::SliderFloat("Tip Cutoff", &values.TipThreshold, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Controls how thin the fur becomes toward its tips. Higher values give finer, sparser tips.");

		edited |= ImGui::SliderFloat("Root Brightness", &values.RootDarkening, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Darkens the base of the fur to give it depth. Lower values darken it more; 1 leaves it as bright as the tips.");

		edited |= ImGui::SliderFloat("Fur texture color", &values.ShellColor, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Choose the fur's color source: 0 uses the underlying skin or clothing; 1 uses the fur detail texture.");

		edited |= ImGui::SliderFloat("Fade Start", &values.FadeStart, 0.0f, 8000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Distance where this fur starts losing detail. Lower values save work sooner; higher values keep nearby fur fuller.");
		edited |= ImGui::SliderFloat("Fade End", &values.FadeEnd, 0.0f, 8000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Distance where added fur disappears completely. Increase it to keep fur visible farther away, at a higher cost.");

		if (edited) {
			ClampOverride(values);
			selectedShell->overridden = true;
			UpdateOverrideFadeEnd();
		}

		if (ImGui::Button("Save texture settings")) {
			selectedShell->overridden = true;
			UpdateOverrideFadeEnd();
			overrideStatusFailed = !SaveOverride(selectedShellName, values, overrideStatus);
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Saves these fur settings for every item using the selected texture, including future sessions.");

		ImGui::SameLine();
		if (ImGui::Button("Discard Changes")) {
			LoadOverride(selectedShellName, *selectedShell);
			overrideStatus.clear();
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", "Discards unsaved edits for this texture. Restores its saved settings, or uses the global settings if none were saved.");

		if (!overrideStatus.empty()) {
			if (overrideStatusFailed)
				ImGui::TextColored(Util::Colors::GetError(), "%s: %s", "Could not write", overrideStatus.c_str());
			else
				ImGui::TextColored(Util::Colors::GetSuccess(), "%s: %s", "Saved", overrideStatus.c_str());
		}
	}

	ImGui::TreePop();
}

FurShells::ShellOverride FurShells::GetGlobalValues(bool a_skin) const
{
	return {
		.Enabled = true,
		.ShellCount = settings.ShellCount,
		.Length = a_skin ? settings.BodyLength : settings.Length,
		.Droop = settings.Droop,
		.RootThreshold = settings.RootThreshold,
		.TipThreshold = settings.TipThreshold,
		.RootDarkening = settings.RootDarkening,
		.ShellColor = settings.ShellColor,
		.FadeStart = settings.FadeStart,
		.FadeEnd = settings.FadeEnd
	};
}

void FurShells::LoadOverride(const std::string& a_shellPath, ShellData& a_data)
{
	a_data.overridden = false;

	const std::string path = GetOverridePath(a_shellPath);
	RE::BSResourceNiBinaryStream stream(path);
	if (stream.good()) {
		const uint32_t size = stream.stream->totalSize;
		std::string text(std::min(size, MaxOverrideSize), '\0');
		if (size != 0 && size <= MaxOverrideSize && stream.read(text.data(), size)) {
			try {
				const auto values = json::parse(text, nullptr, true, true);
				ValidateSettingsObject(values, false);
				a_data.values = values.get<ShellOverride>();
				ClampOverride(a_data.values);
				a_data.overridden = true;
				logger::info("[Fur Shells] {} uses override {}", a_shellPath, path);
			} catch (const std::exception& e) {
				logger::warn("[Fur Shells] Failed to parse {}: {}", path, e.what());
			}
		} else {
			logger::warn("[Fur Shells] Failed to read {}", path);
		}
	}

	UpdateOverrideFadeEnd();
}

bool FurShells::SaveOverride(const std::string& a_shellPath, const ShellOverride& a_values, std::string& a_outputPath)
{
	a_outputPath = "Data\\" + GetOverridePath(a_shellPath);
	if (!IsSafeTexturePath(a_shellPath) || !a_shellPath.ends_with("_shell.dds")) {
		logger::error("[Fur Shells] Refused to write {}", a_outputPath);
		return false;
	}

	try {
		const auto destination = std::filesystem::u8path(a_outputPath);
		std::string error;
		if (!Util::FileHelpers::WriteTextFileAtomic(destination, json(a_values).dump(4), error)) {
			logger::error("[Fur Shells] Failed to write {}: {}", a_outputPath, error);
			return false;
		}
	} catch (const std::exception& error) {
		logger::error("[Fur Shells] Could not save override {}: {}", a_outputPath, error.what());
		return false;
	}

	logger::info("[Fur Shells] Wrote override {}", a_outputPath);
	return true;
}

void FurShells::UpdateOverrideFadeEnd()
{
	overrideFadeEnd = 0.0f;
	for (const auto& shellTexture : shellTextures)
		if (shellTexture.second.overridden)
			overrideFadeEnd = std::max(overrideFadeEnd, shellTexture.second.values.FadeEnd);
}

void FurShells::Reset()
{
	EndPass();
	++frameIndex;
	lastMaxInertia = maxInertia;
	lastWindSpeed = windSpeed;
	lastMaxTouchPerPass = maxTouchPerPass;
	lastTouchNearestGap = touchNearestGap;
	maxInertia = windSpeed = 0.0f;
	maxTouchPerPass = 0;
	touchNearestGap = FLT_MAX;
	if (!settings.Enabled || globals::state->IsMainOrLoadingMenuOpen()) {
		ClearDynamics();
	} else {
		std::erase_if(inertiaStates, [this](const auto& entry) { return !settings.EnableInertia || frameIndex - entry.second.frame > 120; });
		std::erase_if(dynamicsStates, [this](const auto& entry) { return frameIndex - entry.second.frame > 120; });
		if (!IsTouchActive()) {
			dentMemory.Clear();
			Util::BodyCapsules::GetTracker().Clear();
		} else {
			if (settings.EnableDentRecovery || settings.PreciseContactSelection)
				dentMemory.Advance();
			else
				dentMemory.Clear();
		}
	}
	lastTouchCapsules = static_cast<uint32_t>(Util::BodyCapsules::GetTracker().GetCount());
	resolveBudget = ResolvesPerFrame;
	lastPassCount = passCount;
	passCount = 0;
	lastStereoDraws = stereoDraws;
	lastMonoDraws = monoDraws;
	lastMaxShells = maxShells;
	lastMinDensity = minDensity;
	maxShells = 0;
	minDensity = 1.0f;
	stereoDraws = 0;
	monoDraws = 0;
	lastDiffuse = nullptr;
	lastLookup = {};
	if (!entries.empty() && globals::state->IsMainOrLoadingMenuOpen())
		entries.clear();
	deferredPasses.clear();
	deferredOverlayPasses.clear();
	frameBodies.clear();
	deferralClosed = false;
	rootTest = false;
}

void FurShells::GenerateShaderPermutations(RE::BSShader* a_shader)
{
	using Flags = SIE::ShaderCache::LightingShaderFlags;
	using Techniques = SIE::ShaderCache::LightingShaderTechniques;

	if (a_shader->shaderType != RE::BSShader::Type::Lighting)
		return;

	constexpr uint32_t vertexColor = static_cast<uint32_t>(Flags::VC);
	constexpr std::array techniques{ Techniques::None, Techniques::Envmap };
	constexpr std::array alphaTestFlags{ 0u, static_cast<uint32_t>(Flags::DoAlphaTest) };
	constexpr std::array deferredFlags{ 0u, static_cast<uint32_t>(Flags::Deferred) };
	constexpr std::array vertexColorFlags{ 0u, vertexColor };
	constexpr std::array skinnedFlags{ 0u, static_cast<uint32_t>(Flags::Skinned) };

	auto* shaderCache = globals::shaderCache;
	for (const auto technique : techniques) {
		const uint32_t type = static_cast<uint32_t>(technique) << 24;
		for (const uint32_t alphaTest : alphaTestFlags)
			for (const uint32_t deferred : deferredFlags) {
				std::ignore = shaderCache->GetPixelShader(*a_shader, type | vertexColor | alphaTest | deferred | FurFlag);
				std::ignore = shaderCache->GetPixelShader(*a_shader, type | vertexColor | alphaTest | deferred | FurDepthFlag);
			}
		for (const uint32_t color : vertexColorFlags)
			for (const uint32_t skinned : skinnedFlags)
				std::ignore = shaderCache->GetVertexShader(*a_shader, type | color | skinned | FurFlag);
	}

	if (globals::features::truePBR.loaded) {
		for (const uint32_t alphaTest : alphaTestFlags)
			for (const uint32_t deferred : deferredFlags) {
				std::ignore = shaderCache->GetPixelShader(*a_shader, static_cast<uint32_t>(Flags::TruePbr) | vertexColor | alphaTest | deferred | FurFlag);
				std::ignore = shaderCache->GetPixelShader(*a_shader, static_cast<uint32_t>(Flags::TruePbr) | vertexColor | alphaTest | deferred | FurDepthFlag);
			}
	}
}

FurShells::ShellLookup FurShells::FindShell(RE::BSRenderPass* a_pass)
{
	auto* property = a_pass->shaderProperty;
	auto* material = property ? static_cast<RE::BSLightingShaderMaterialBase*>(property->material) : nullptr;
	auto* diffuse = material ? material->diffuseTexture.get() : nullptr;
	if (!diffuse)
		return {};
	if (diffuse == lastDiffuse)
		return lastLookup;

	auto it = entries.find(diffuse);
	if (it == entries.end() || it->second.name != diffuse->name ||
		(it->second.retryLoad && frameIndex - it->second.resolvedFrame >= TextureRetryFrames)) {
		RE::NiSourceTexturePtr shell;
		ShellData* data = nullptr;
		bool unavailable = false;
		std::string previousPath;
		const char* setPath = material->textureSet ? material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
		for (const char* diffusePath : { diffuse->name.c_str(), setPath }) {
			if (shell || !diffusePath || !*diffusePath)
				continue;

			std::string path = Util::NormalizeTexturePath(diffusePath);
			if (path == previousPath || missingShells.contains(path))
				continue;
			if (resolveBudget == 0)
				return {};
			--resolveBudget;

			bool exists = false;
			std::string shellPath;
			shell = LoadShell(path, exists, shellPath);
			unavailable |= exists && !shell;
			if (shell) {
				if (!shellTextures.contains(shellPath) && shellTextures.size() >= MaxEntries)
					return {};
				const auto [dataIt, inserted] = shellTextures.try_emplace(shellPath);
				if (inserted)
					LoadOverride(shellPath, dataIt->second);
				data = &dataIt->second;
			}
			if (!exists) {
				if (missingShells.size() >= MaxMissingShells)
					missingShells.clear();
				missingShells.emplace(path);
			}
			previousPath = std::move(path);
		}

		const bool emptyOverlay = Util::NormalizeTexturePath(diffuse->name.c_str()) == EmptyOverlayPath || (setPath && Util::NormalizeTexturePath(setPath) == EmptyOverlayPath);
		const bool retryLoad = unavailable && !shell;
		if (entries.size() >= MaxEntries)
			entries.clear();
		it = entries.insert_or_assign(diffuse, Entry{ diffuse->name, std::move(shell), data, emptyOverlay, retryLoad, frameIndex }).first;
	}

	lastDiffuse = diffuse;
	lastLookup = { it->second.shell.get(), it->second.data, true, it->second.emptyOverlay };
	return lastLookup;
}

const FurShells::FrameBody* FurShells::FindFrameBody(RE::BSGeometry* a_geometry) const
{
	if (frameBodies.empty())
		return nullptr;

	auto* skinInstance = a_geometry->GetGeometryRuntimeData().skinInstance.get();
	auto* partition = skinInstance ? skinInstance->skinPartition.get() : nullptr;
	if (!partition)
		return nullptr;

	for (const auto& body : frameBodies)
		if (body.partition == partition && body.rootParent == skinInstance->rootParent && body.geometry != a_geometry)
			return &body;

	return nullptr;
}

void FurShells::RecordFrameBody(RE::NiSkinInstance* a_skinInstance, RE::BSGeometry* a_geometry, const PerPass& a_perPass, ID3D11ShaderResourceView* a_shellView, ID3D11ShaderResourceView* a_normalView, bool a_deferred)
{
	auto* partition = a_skinInstance->skinPartition.get();
	if (!partition)
		return;

	auto it = std::ranges::find_if(frameBodies, [&](const FrameBody& a_body) { return a_body.geometry == a_geometry; });
	if (it == frameBodies.end() && frameBodies.size() >= MaxDeferredPasses)
		return;
	if (it == frameBodies.end())
		it = frameBodies.emplace(frameBodies.end());

	it->partition = partition;
	it->rootParent = a_skinInstance->rootParent;
	it->geometry = a_geometry;
	it->shellView.copy_from(a_shellView);
	it->normalView.copy_from(a_normalView);
	it->perPass = a_perPass;
	it->deferred = a_deferred;
}

void FurShells::BeginPass(RE::BSShader* a_shader, RE::BSRenderPass* a_pass)
{
	using enum RE::BSGraphics::DepthStencilDepthMode;

	if (instanceCount != 0)
		EndPass();

	if (!settings.Enabled || !shellAssetsPresent.load(std::memory_order_relaxed) || !perPassCB || !noColorWrite || !a_pass || !globals::shaderCache->IsEnabled())
		return;

	auto* state = globals::state;
	StartAssetScan();
	if ((state->permutationData.ExtraShaderDescriptor & ReflectionsFlag) != 0 || !IsFurTechnique(state->currentPixelDescriptor))
		return;

	auto* geometry = a_pass->geometry;
	if (!geometry)
		return;

	if (const auto* body = FindFrameBody(geometry)) {
		BeginOverlayPass(a_shader, a_pass, *body);
		return;
	}

	auto* shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState)
	if (depthStencilDepthMode != kTestEqual && depthStencilDepthMode != kTestWrite)
		return;

	const float distance = IsFirstPersonPass() ? 0.0f : std::max(0.0f, geometry->worldBound.center.GetDistance(Util::GetEyePosition(0)) - geometry->worldBound.radius);
	if (!std::isfinite(distance) || distance >= std::max(settings.FadeEnd, overrideFadeEnd) || !CanDrawShells(geometry))
		return;

	auto* skinInstance = geometry->GetGeometryRuntimeData().skinInstance.get();
	const bool modelSpaceNormals = (state->modifiedVertexDescriptor & ModelSpaceNormalsFlag) != 0;
	if (modelSpaceNormals && !skinInstance)
		return;

	const auto lookup = FindShell(a_pass);
	auto* data = lookup.data;
	auto* shellView = lookup.shell && lookup.shell->rendererTexture ? lookup.shell->rendererTexture->resourceView : nullptr;
	if (!shellView || !data)
		return;

	data->skin = IsSkinTechnique(state->currentPixelDescriptor);
	const ShellOverride values = data->overridden ? data->values : GetGlobalValues(data->skin);
	if (!values.Enabled)
		return;

	const float fade = std::clamp((values.FadeEnd - distance) / std::max(values.FadeEnd - values.FadeStart, 1.0f), 0.0f, 1.0f);
	const auto distanceShells = static_cast<uint32_t>(std::lround(static_cast<float>(std::min(values.ShellCount, MaxShells)) * fade));
	if (fade <= 0.0f || (settings.DistanceShellBudget && distanceShells == 0))
		return;
	const float worldScale = skinInstance && skinInstance->rootParent ? skinInstance->rootParent->world.scale : geometry->world.scale;
	const float pixels = GetFurPixels(FurInertia::GetWorldLength(values.Length, worldScale), distance);
	const float density = (settings.DistanceShellBudget ? 1.0f : fade) * (settings.MinFurPixels > 0.0f ? std::clamp(pixels / settings.MinFurPixels, 0.0f, 1.0f) : 1.0f);
	if (density <= 0.0f)
		return;

	ID3D11ShaderResourceView* normalView = nullptr;
	if (modelSpaceNormals) {
		auto* normal = static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material)->normalTexture.get();
		normalView = normal && normal->rendererTexture ? normal->rendererTexture->resourceView : nullptr;
		if (!normalView)
			return;
	}

	// The selected layer budget bounds fractional projected layers.
	const float layers = std::clamp(pixels / PixelsPerShell, 1.0f, static_cast<float>(settings.DistanceShellBudget ? distanceShells : values.ShellCount));
	const auto shells = static_cast<uint32_t>(std::ceil(layers));
	const float tipWeight = layers - static_cast<float>(shells - 1);

	auto* shaderCache = globals::shaderCache;
	auto* vertexShader = shaderCache->GetVertexShader(*a_shader, state->modifiedVertexDescriptor | FurFlag);
	auto* pixelShader = shaderCache->GetPixelShader(*a_shader, state->modifiedPixelDescriptor | FurFlag);
	auto* depthShader = shaderCache->GetPixelShader(*a_shader, state->modifiedPixelDescriptor | FurDepthFlag);
	if (!vertexShader || !pixelShader || !depthShader)
		return;

	const bool defer = !replaying && !deferralClosed && settings.HideCoveredFur && state->inWorld && currentPass.pass == a_pass && deferredPasses.size() < MaxDeferredPasses;

	PerPass perPass{ values.Length, static_cast<float>(shells), values.Droop, values.RootThreshold, values.TipThreshold, values.RootDarkening, values.ShellColor, 0.0f, density, tipWeight };
	FillDynamics(perPass, geometry);
	if (skinInstance && settings.OverlayFur)
		RecordFrameBody(skinInstance, geometry, perPass, shellView, normalView, defer || replaying);

	if (defer) {
		deferredPasses.push_back(currentPass);
		return;
	}

	perPass.RootTest = replaying && rootTest ? 1.0f : 0.0f;
	furVertexShader = reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader);
	furPixelShader = reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader);
	depthPixelShader = reinterpret_cast<ID3D11PixelShader*>(depthShader->shader);
	BindPass(perPass, shellView, normalView);
}

void FurShells::FillTouch(Dynamics& a_perPass, const RE::BSGeometry* a_geometry, float a_length)
{
	std::array<FurInteractors::Capsule, FurInteractors::MaxCapsulesPerPass> touches;
	const auto wearer = GetWearer(a_geometry);
	const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
	const float scale = skin && skin->rootParent ? skin->rootParent->world.scale : a_geometry->world.scale;
	const float worldLength = FurInertia::GetWorldLength(a_length, scale);
	size_t count = 0;
	if (IsTouchActive()) {
		std::array<FurInteractors::Capsule, Util::BodyCapsules::MaxSelectedCapsules> candidates;
		const size_t found = Util::BodyCapsules::GetTracker().Select(a_geometry->worldBound.center, a_geometry->worldBound.radius, std::max(10.0f, worldLength * 3.0f), wearer, candidates, touchNearestGap);
		const size_t limit = std::min<size_t>(settings.ContactLimit, FurInteractors::MaxLiveCapsulesPerPass);
		if (settings.PreciseContactSelection)
			count = dentMemory.Nearest(a_geometry, wearer, std::span(candidates.data(), found), std::span(touches.data(), limit));
		else {
			count = std::min(found, limit);
			std::copy_n(candidates.begin(), count, touches.begin());
		}
	}
	if (IsTouchActive() && settings.EnableDentRecovery)
		count = dentMemory.Remember(a_geometry, wearer, worldLength, count, touches);
	maxTouchPerPass = std::max(maxTouchPerPass, static_cast<uint32_t>(count));
	a_perPass.TouchCount = static_cast<float>(count);
	a_perPass.TouchStrength = settings.TouchStrength;
	a_perPass.TouchDebug = settings.ShowTouchEdges ? 1.0f : 0.0f;
	for (int eye = 0; eye < 2; ++eye) {
		const auto position = Util::GetEyePosition(eye);
		a_perPass.TouchEye[eye][0] = position.x;
		a_perPass.TouchEye[eye][1] = position.y;
		a_perPass.TouchEye[eye][2] = position.z;
	}
	for (size_t index = 0; index < count; ++index) {
		const auto& touch = touches[index];
		a_perPass.TouchA[index][0] = touch.pointA.x;
		a_perPass.TouchA[index][1] = touch.pointA.y;
		a_perPass.TouchA[index][2] = touch.pointA.z;
		a_perPass.TouchA[index][3] = touch.radius;
		a_perPass.TouchB[index][0] = touch.pointB.x;
		a_perPass.TouchB[index][1] = touch.pointB.y;
		a_perPass.TouchB[index][2] = touch.pointB.z;
		a_perPass.TouchB[index][3] = touch.remembered ? 1.0f : 0.0f;
		a_perPass.TouchMove[index][0] = touch.move.x;
		a_perPass.TouchMove[index][1] = touch.move.y;
		a_perPass.TouchMove[index][2] = touch.move.z;
		a_perPass.TouchMove[index][3] = touch.strength;
	}
}

FurInertia::Tuning FurShells::GetInertiaTuning() const
{
	const FurInertia::Tuning custom{ settings.InertiaFrequency, settings.InertiaGain, settings.InertiaLengthExponent, settings.InertiaDroopExponent, settings.InertiaMaxBend };
	return FurInertia::GetTuning(static_cast<FurInertia::Preset>(std::min(settings.InertiaPreset, static_cast<uint32_t>(FurInertia::Preset::Custom))), custom);
}

void FurShells::DrawInertiaSettings()
{
	using FurInertia::Preset;

	ImGui::SliderFloat("Sway strength", &settings.InertiaStrength, 0.0f, FurInertia::MaxStrength, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "How much fur trails behind starts, stops and turns. Higher values give more sway; 0 removes it.");

	const std::array<std::string, 3> names{ "Stylized", "Realistic", "Custom" };
	const auto selected = std::min(settings.InertiaPreset, static_cast<uint32_t>(Preset::Custom));
	if (ImGui::BeginCombo("Movement style", names[selected].c_str())) {
		for (uint32_t i = 0; i < names.size(); ++i) {
			if (ImGui::Selectable(names[i].c_str(), i == selected) && i != selected) {
				if (i == static_cast<uint32_t>(Preset::Custom)) {
					const auto tuning = GetInertiaTuning();
					settings.InertiaFrequency = tuning.frequencyHz;
					settings.InertiaGain = tuning.gain;
					settings.InertiaLengthExponent = tuning.lengthExponent;
					settings.InertiaDroopExponent = tuning.droopExponent;
					settings.InertiaMaxBend = tuning.maxBend;
				}
				settings.InertiaPreset = i;
			}
		}
		ImGui::EndCombo();
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Stylized gives expressive movement. Realistic keeps short fur steadier and lets long fur sway. Custom lets you tune the response.");

	if (settings.InertiaPreset != static_cast<uint32_t>(Preset::Custom))
		return;

	ImGui::SliderFloat("Spring speed", &settings.InertiaFrequency, FurInertia::MinFrequencyHz, FurInertia::MaxTuningFrequencyHz, "%.1f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "How quickly fur springs back after movement. Higher values feel stiffer; lower values give slower, softer sway.");

	if (!ImGui::TreeNode("Movement tuning"))
		return;

	ImGui::SliderFloat("Movement response", &settings.InertiaGain, FurInertia::MinGain, FurInertia::MaxGain, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "How strongly starts, stops and turns move the fur. Higher values exaggerate its reaction.");

	ImGui::SliderFloat("Maximum Bend", &settings.InertiaMaxBend, FurInertia::MinBendLimit, FurInertia::MaxBendLimit, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Limits how far movement can bend the fur. Higher values allow larger swings.");

	ImGui::SliderFloat("Length Softening", &settings.InertiaLengthExponent, 0.0f, FurInertia::MaxLengthExponent, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Makes long fur sway more slowly and farther than short fur. Higher values strengthen the difference; 0 ignores length.");

	ImGui::SliderFloat("Droop Softening", &settings.InertiaDroopExponent, 0.0f, FurInertia::MaxDroopExponent, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Makes drooping fur sway more softly. Higher values strengthen the effect; 0 ignores droop.");
	ImGui::TreePop();
}

float3 FurShells::GetInertia(const RE::BSGeometry* a_geometry, float a_length, float a_droop)
{
	if (!settings.EnableInertia || settings.InertiaStrength <= 0.0f)
		return {};

	const auto& center = a_geometry->worldBound.center;
	const float3 position{ center.x, center.y, center.z };
	if (!std::isfinite(position.LengthSquared())) {
		inertiaStates.erase(a_geometry);
		return {};
	}
	const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
	const auto* root = skin ? skin->rootParent : a_geometry->parent;
	if (!inertiaStates.contains(a_geometry) && inertiaStates.size() >= 512)
		return {};
	auto [it, inserted] = inertiaStates.try_emplace(a_geometry, InertiaState{ FurInertia::Spring::At(position), frameIndex, skin, root });
	auto& state = it->second;
	if (state.skin != skin || state.root != root || frameIndex - state.frame > 1) {
		state = { FurInertia::Spring::At(position), frameIndex, skin, root };
	}
	if (state.frame != frameIndex) {
		state.frame = frameIndex;
		const auto* skinInstance = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		const float scale = skinInstance && skinInstance->rootParent ? skinInstance->rootParent->world.scale : a_geometry->world.scale;
		const float worldLength = FurInertia::GetWorldLength(a_length, scale);
		const auto tuning = GetInertiaTuning();
		state.spring.Step(position, globals::game::deltaTime ? *globals::game::deltaTime : 0.0f, tuning.maxBend * worldLength, FurInertia::GetResponse(tuning, worldLength, a_droop));
	}
	const float3 inertia = state.spring.offset * settings.InertiaStrength;
	maxInertia = std::max(maxInertia, inertia.Length());
	return inertia;
}

float3 FurShells::GetWind() const
{
	const auto* sky = globals::game::sky;
	if (!settings.EnableWind || settings.WindStrength <= 0.0f || !sky || Util::IsInterior())
		return {};
	const float angle = Util::ClampFinite(sky->windAngle, -100000.0f, 100000.0f, 0.0f);
	const float speed = Util::ClampFinite(sky->windSpeed, 0.0f, 1.0f, 0.0f) * settings.WindStrength;
	return float3{ std::sin(angle), std::cos(angle), 0.0f } * std::min(speed, 1.5f);
}

void FurShells::ClearDynamics()
{
	inertiaStates.clear();
	dynamicsStates.clear();
	dentMemory.Clear();
	Util::BodyCapsules::GetTracker().Clear();
}

void FurShells::FillDynamics(PerPass& a_perPass, const RE::BSGeometry* a_geometry)
{
	const std::array<float, 4> shape{ a_perPass.Length, a_perPass.ShellCount, a_perPass.Droop, 0.0f };
	a_perPass.PreviousShape = shape;
	if (globals::state->IsMainOrLoadingMenuOpen())
		return;
	const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
	const auto* partition = skin ? skin->skinPartition.get() : nullptr;
	const auto* root = skin ? skin->rootParent : a_geometry->parent;
	const auto& center = a_geometry->worldBound.center;
	if (!std::isfinite(center.SqrLength()))
		return;
	auto previous = dynamicsStates.find(a_geometry);
	const bool sameMesh = previous != dynamicsStates.end() && previous->second.skin == skin &&
	                      previous->second.partition == partition && previous->second.root == root &&
	                      previous->second.center.GetDistance(center) <= FurInertia::TeleportDistance;
	if (sameMesh && previous->second.frame == frameIndex && previous->second.shape == shape) {
		a_perPass.Current = previous->second.current;
		a_perPass.Previous = previous->second.previous;
		a_perPass.PreviousShape = previous->second.previousShape;
		return;
	}
	const auto wind = GetWind();
	a_perPass.Current.Wind = { wind.x, wind.y, wind.z, 0.0f };
	windSpeed = std::max(windSpeed, wind.Length());
	if (settings.EnableWetFur && settings.WetStrength > 0.0f && GetWearer(a_geometry, true) != 0 && globals::features::wetterness.IsRuntimeActive()) {
		const auto data = globals::features::wetterness.GetCommonBufferData();
		a_perPass.Current.WetStrength = Util::ClampFinite(data.Wetness, 0.0f, 1.0f, 0.0f) * settings.WetStrength;
	}
	a_perPass.Current.Inertia = GetInertia(a_geometry, a_perPass.Length, a_perPass.Droop);
	if (IsTouchActive()) {
		Util::BodyCapsules::GetTracker().Update(settings.ContactRange, settings.FitHandCapsules, settings.EnableHeldObjects);
		FillTouch(a_perPass.Current, a_geometry, a_perPass.Length);
	}
	const bool consecutive = sameMesh && previous->second.frame + 1 == frameIndex;
	a_perPass.Previous = consecutive ? previous->second.current : a_perPass.Current;
	a_perPass.PreviousShape = consecutive ? previous->second.shape : shape;
	if (previous != dynamicsStates.end() || dynamicsStates.size() < 512)
		dynamicsStates.insert_or_assign(a_geometry, DynamicsState{ a_perPass.Current, a_perPass.Previous, shape, a_perPass.PreviousShape, skin, partition, root, center, frameIndex });
}

void FurShells::BeginOverlayPass(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, const FrameBody& a_body)
{
	const auto lookup = FindShell(a_pass);
	if (!lookup.resolved || lookup.emptyOverlay || static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material)->materialAlpha <= 0.0f)
		return;

	auto* state = globals::state;
	const bool modelSpaceNormals = (state->modifiedVertexDescriptor & ModelSpaceNormalsFlag) != 0;
	if (modelSpaceNormals != static_cast<bool>(a_body.normalView))
		return;

	auto* shaderCache = globals::shaderCache;
	auto* vertexShader = shaderCache->GetVertexShader(*a_shader, state->modifiedVertexDescriptor | FurFlag);
	auto* pixelShader = shaderCache->GetPixelShader(*a_shader, state->modifiedPixelDescriptor | FurFlag);
	if (!vertexShader || !pixelShader)
		return;

	if (!replaying && !deferralClosed && a_body.deferred) {
		if (currentPass.pass == a_pass && deferredOverlayPasses.size() < MaxDeferredPasses)
			deferredOverlayPasses.push_back(currentPass);
		return;
	}

	PerPass perPass = a_body.perPass;
	perPass.ShellColor = 0.0f;
	perPass.RootTest = a_body.deferred && rootTest ? 1.0f : 0.0f;

	furVertexShader = reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader);
	furPixelShader = reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader);
	overlayPass = true;
	BindPass(perPass, a_body.shellView.get(), a_body.normalView.get());
}

void FurShells::BindPass(const PerPass& a_perPass, ID3D11ShaderResourceView* a_shellView, ID3D11ShaderResourceView* a_normalView)
{
	if (std::memcmp(&a_perPass, &lastPerPass, sizeof(PerPass)) != 0) {
		try {
			perPassCB->Update(a_perPass);
		} catch (const DX::com_exception& error) {
			logger::warn("[Fur Shells] Disabling fur resources after buffer update failed: {}", error.what());
			EndPass();
			perPassCB = nullptr;
			return;
		}
		lastPerPass = a_perPass;
	}

	auto* context = globals::d3d::context;
	passState.emplace(context);
	if (!overlayPass) {
		context->VSSetShader(furVertexShader, nullptr, 0);
		context->PSSetShader(furPixelShader, nullptr, 0);
	}

	ID3D11Buffer* buffer = perPassCB->CB();
	context->VSSetConstantBuffers(VertexPerPassSlot, 1, &buffer);
	context->PSSetConstantBuffers(PixelPerPassSlot, 1, &buffer);
	context->PSSetShaderResources(ShellTextureSlot, 1, &a_shellView);
	ID3D11ShaderResourceView* depthView = a_perPass.RootTest > 0.0f ? depthCopyView.get() : nullptr;
	context->PSSetShaderResources(DepthTextureSlot, 1, &depthView);
	context->VSSetShaderResources(NormalTextureSlot, 1, &a_normalView);

	const auto shellInstances = static_cast<uint32_t>(a_perPass.ShellCount);
	instanceCount = overlayPass || replaying ? shellInstances : shellInstances + 1;
	maxShells = std::max(maxShells, shellInstances);
	minDensity = std::min(minDensity, a_perPass.Density);
	++passCount;
}

void FurShells::EndPass()
{
	instanceCount = 0;
	overlayPass = false;

	passState.reset();
}

FurShells::PassState::PassState(ID3D11DeviceContext* a_context) : context(a_context)
{
	context->VSGetShader(vertexShader.put(), nullptr, nullptr);
	context->PSGetShader(pixelShader.put(), nullptr, nullptr);
	context->VSGetConstantBuffers(VertexPerPassSlot, 1, vertexBuffer.put());
	context->PSGetConstantBuffers(PixelPerPassSlot, 1, pixelBuffer.put());
	context->PSGetShaderResources(ShellTextureSlot, 1, shellView.put());
	context->PSGetShaderResources(DepthTextureSlot, 1, depthView.put());
	context->VSGetShaderResources(NormalTextureSlot, 1, normalView.put());
}

FurShells::PassState::~PassState()
{
	context->VSSetShader(vertexShader.get(), nullptr, 0);
	context->PSSetShader(pixelShader.get(), nullptr, 0);
	ID3D11Buffer* buffer = vertexBuffer.get();
	context->VSSetConstantBuffers(VertexPerPassSlot, 1, &buffer);
	buffer = pixelBuffer.get();
	context->PSSetConstantBuffers(PixelPerPassSlot, 1, &buffer);
	ID3D11ShaderResourceView* view = shellView.get();
	context->PSSetShaderResources(ShellTextureSlot, 1, &view);
	view = depthView.get();
	context->PSSetShaderResources(DepthTextureSlot, 1, &view);
	view = normalView.get();
	context->VSSetShaderResources(NormalTextureSlot, 1, &view);
}

const FurShells::DepthStates* FurShells::GetDepthStates(ID3D11DepthStencilState* a_source)
{
	if (!a_source)
		return nullptr;

	auto it = depthStates.find(a_source);
	if (it == depthStates.end()) {
		if (depthStates.size() >= MaxDepthStates)
			return nullptr;
		DepthStates states;
		states.source.copy_from(a_source);

		D3D11_DEPTH_STENCIL_DESC desc;
		a_source->GetDesc(&desc);

		const auto stencilWriteMask = desc.StencilWriteMask;
		D3D11_DEPTH_STENCIL_DESC overlayDesc = desc;
		overlayDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		overlayDesc.StencilWriteMask = 0;

		desc.DepthEnable = TRUE;
		desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.StencilWriteMask = 0;

		auto* device = globals::d3d::device;
		if (SUCCEEDED(device->CreateDepthStencilState(&desc, states.prepass.put()))) {
			Util::SetResourceName(states.prepass.get(), "FurShells::PrepassDepthState");
			desc.DepthFunc = D3D11_COMPARISON_EQUAL;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.StencilWriteMask = stencilWriteMask;
			if (SUCCEEDED(device->CreateDepthStencilState(&desc, states.shade.put())))
				Util::SetResourceName(states.shade.get(), "FurShells::ShadeDepthState");
			if (SUCCEEDED(device->CreateDepthStencilState(&overlayDesc, states.overlay.put())))
				Util::SetResourceName(states.overlay.get(), "FurShells::OverlayDepthState");
			if (!states.shade || !states.overlay)
				states.prepass = nullptr;
		}
		if (!states.prepass)
			logger::warn("[Fur Shells] Failed to create depth states");

		it = depthStates.emplace(a_source, std::move(states)).first;
	}

	return it->second.prepass ? &it->second : nullptr;
}

void FurShells::DrawInstanced(UINT a_indexCount, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation)
{
	issuingDraw = true;
	const SKSE::stl::scope_exit restore([&] { issuingDraw = false; });
	globals::d3d::context->DrawIndexedInstanced(a_indexCount, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, 0);
}

bool FurShells::DrawShells(UINT a_indexCount, UINT a_engineInstances, UINT a_startIndexLocation, INT a_baseVertexLocation)
{
	if (instanceCount == 0)
		return replaying;
	if (a_engineInstances != 1 && !(globals::game::isVR && a_engineInstances == 2)) {
		EndPass();
		return replaying;
	}

	CS_GPU_PASS("FurShells::Draw");

	const UINT totalInstances = instanceCount * a_engineInstances;
	++(a_engineInstances > 1 ? stereoDraws : monoDraws);
	auto* context = globals::d3d::context;

	winrt::com_ptr<ID3D11DepthStencilState> depthState;
	UINT stencilRef = 0;
	context->OMGetDepthStencilState(depthState.put(), &stencilRef);

	const auto* states = GetDepthStates(depthState.get());
	if (!states) {
		EndPass();
		return replaying;
	}
	const SKSE::stl::scope_exit restoreDepth([&] { context->OMSetDepthStencilState(depthState.get(), stencilRef); });

	if (overlayPass) {
		context->OMSetDepthStencilState(states->overlay.get(), stencilRef);
		context->VSSetShader(furVertexShader, nullptr, 0);
		context->PSSetShader(furPixelShader, nullptr, 0);
		DrawInstanced(a_indexCount, totalInstances, a_startIndexLocation, a_baseVertexLocation);
		context->VSSetShader(passState->vertexShader.get(), nullptr, 0);
		context->PSSetShader(passState->pixelShader.get(), nullptr, 0);
		return replaying;
	}

	winrt::com_ptr<ID3D11BlendState> blendState;
	FLOAT blendFactor[4]{};
	UINT sampleMask = 0;
	context->OMGetBlendState(blendState.put(), blendFactor, &sampleMask);
	const SKSE::stl::scope_exit restoreBlend([&] { context->OMSetBlendState(blendState.get(), blendFactor, sampleMask); });

	context->OMSetBlendState(noColorWrite.get(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(states->prepass.get(), stencilRef);
	context->PSSetShader(depthPixelShader, nullptr, 0);
	DrawInstanced(a_indexCount, totalInstances, a_startIndexLocation, a_baseVertexLocation);

	context->OMSetBlendState(blendState.get(), blendFactor, sampleMask);
	context->OMSetDepthStencilState(states->shade.get(), stencilRef);
	context->PSSetShader(furPixelShader, nullptr, 0);
	DrawInstanced(a_indexCount, totalInstances, a_startIndexLocation, a_baseVertexLocation);

	return true;
}

struct FurShells::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags)
		{
			func(a_shader, a_pass, a_renderFlags);
			globals::features::furShells.BeginPass(a_shader, a_pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags)
		{
			globals::features::furShells.EndPass();
			func(a_shader, a_pass, a_renderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation)
		{
			auto& furShells = globals::features::furShells;
			if (!furShells.IsDrawing() || a_context != globals::d3d::context || !furShells.DrawShells(a_indexCount, 1, a_startIndexLocation, a_baseVertexLocation))
				func(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation)
		{
			auto& furShells = globals::features::furShells;
			if (furShells.IsDrawing() && a_context == globals::d3d::context) {
				if (a_startInstanceLocation != 0) {
					furShells.EndPass();
					if (furShells.replaying)
						return;
				} else if (furShells.DrawShells(a_indexCountPerInstance, a_instanceCount, a_startIndexLocation, a_baseVertexLocation)) {
					return;
				}
			}
			func(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, a_startInstanceLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
};

bool FurShells::CopySceneDepth()
{
	auto* texture = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
	if (!texture || depthCopyFailed)
		return false;

	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	if (desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
		return false;

	D3D11_TEXTURE2D_DESC copyDesc{};
	if (depthCopy)
		depthCopy->GetDesc(&copyDesc);

	if (!depthCopyView || copyDesc.Width != desc.Width || copyDesc.Height != desc.Height || copyDesc.Format != desc.Format) {
		depthCopy = nullptr;
		depthCopyView = nullptr;

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format = GetDepthViewFormat(desc.Format);
		viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipLevels = 1;

		auto* device = globals::d3d::device;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		if (viewDesc.Format != DXGI_FORMAT_UNKNOWN && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, depthCopy.put()))) {
			Util::SetResourceName(depthCopy.get(), "FurShells::DepthCopy");
			if (SUCCEEDED(device->CreateShaderResourceView(depthCopy.get(), &viewDesc, depthCopyView.put())))
				Util::SetResourceName(depthCopyView.get(), "FurShells::DepthCopy SRV");
		}
		if (!depthCopyView) {
			depthCopy = nullptr;
			depthCopyView = nullptr;
			depthCopyFailed = true;
			logger::warn("[Fur Shells] Failed to create depth copy, fur under clothing stays visible");
			return false;
		}
	}

	globals::d3d::context->CopyResource(depthCopy.get(), texture);
	return true;
}

void FurShells::OnBeforeBlendedDecals()
{
	auto* state = globals::state;
	if (!state->inWorld || (state->permutationData.ExtraShaderDescriptor & ReflectionsFlag) != 0)
		return;

	deferralClosed = true;
	if (deferredPasses.empty() || !settings.Enabled || !perPassCB || !noColorWrite)
		return;

	CS_GPU_PASS("FurShells::DeferredShells");

	{
		CS_GPU_PASS("FurShells::DepthCopy");
		rootTest = CopySceneDepth();
	}

	auto* shadowState = globals::game::shadowState;
	auto* stateUpdateFlags = globals::game::stateUpdateFlags;
	GET_INSTANCE_MEMBER(alphaBlendMode, shadowState)
	GET_INSTANCE_MEMBER(alphaBlendWriteMode, shadowState)
	GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState)
	const auto savedAlphaBlendMode = alphaBlendMode;
	const auto savedAlphaBlendWriteMode = alphaBlendWriteMode;
	const auto savedDepthMode = depthStencilDepthMode;
	const SKSE::stl::scope_exit restore([&] {
		EndPass();
		replaying = false;
		deferredPasses.clear();
		deferredOverlayPasses.clear();
		alphaBlendMode = savedAlphaBlendMode;
		alphaBlendWriteMode = savedAlphaBlendWriteMode;
		depthStencilDepthMode = savedDepthMode;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
	});

	alphaBlendMode = 0;
	alphaBlendWriteMode = 1;
	depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

	{
		CS_GPU_PASS("FurShells::ReplayPasses");
		replaying = true;
		for (const auto& deferred : deferredPasses)
			::Hooks::DrawRenderPassImmediately(deferred.pass, deferred.technique, deferred.alphaTest, deferred.renderFlags);
		for (const auto& deferred : deferredOverlayPasses)
			::Hooks::DrawRenderPassImmediately(deferred.pass, deferred.technique, deferred.alphaTest, deferred.renderFlags);
	}
}

json FurShells::CapturePerformanceSettingsState() const
{
	return settings;
}

void FurShells::RestorePerformanceToggleState(const json& a_state)
{
	if (a_state.is_boolean()) {
		SetPerformanceCostMeasurementEnabled(a_state.get<bool>());
		return;
	}
	ValidateSettingsObject(a_state, true);
	auto next = a_state.get<Settings>();
	if (next.FadeStart > next.FadeEnd)
		throw std::invalid_argument("FadeStart must not exceed FadeEnd");
	settings = next;
	ClearDynamics();
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json FurShells::GetDiagnostics()
{
	size_t overridden = 0;
	json textures = json::array();
	for (const auto& [path, data] : shellTextures) {
		overridden += data.overridden ? 1 : 0;
		textures.push_back({ { "path", path }, { "overridden", data.overridden }, { "skin", data.skin }, { "values", data.overridden ? data.values : GetGlobalValues(data.skin) } });
	}

	D3D11_TEXTURE2D_DESC depthDesc{};
	if (depthCopy)
		depthCopy->GetDesc(&depthDesc);

	return {
		{ "shellAssetsPresentOrUnknown", shellAssetsPresent.load(std::memory_order_relaxed) },
		{ "touchCapsulesLastFrame", lastTouchCapsules },
		{ "maxTouchPerPassLastFrame", lastMaxTouchPerPass },
		{ "dentCount", dentMemory.GetDentCount() },
		{ "nearestContactGapLastFrame", lastTouchNearestGap == FLT_MAX ? json(nullptr) : json(lastTouchNearestGap) },
		{ "maxInertiaLastFrame", lastMaxInertia },
		{ "windSpeedLastFrame", lastWindSpeed },
		{ "inertiaTrackedMeshes", inertiaStates.size() },
		{ "loaded", loaded },
		{ "enabled", settings.Enabled },
		{ "resourcesReady", perPassCB && noColorWrite },
		{ "settings", settings },
		{ "depthCopyWidth", depthDesc.Width },
		{ "depthCopyHeight", depthDesc.Height },
		{ "depthCopyFormat", static_cast<uint32_t>(depthDesc.Format) },
		{ "vr", globals::game::isVR },
		{ "passesLastFrame", lastPassCount },
		{ "stereoDrawsLastFrame", lastStereoDraws },
		{ "monoDrawsLastFrame", lastMonoDraws },
		{ "maxShellsLastFrame", lastMaxShells },
		{ "minDensityLastFrame", lastMinDensity },
		{ "deferredPasses", deferredPasses.size() },
		{ "deferredOverlayPasses", deferredOverlayPasses.size() },
		{ "frameBodies", frameBodies.size() },
		{ "rootTest", rootTest },
		{ "depthCopyFailed", depthCopyFailed },
		{ "resolvedShellTextures", shellTextures.size() },
		{ "overriddenShellTextures", overridden },
		{ "shellTextures", std::move(textures) },
	};
}

json FurShells::Control(const json& a_args)
{
	for (const auto& [name, value] : a_args.items()) {
		(void)value;
		if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" && name != "expectedBuildId" &&
			name != "furShells" && name != "shellTexture" && name != "shellOverride" && name != "overrideAction")
			throw std::invalid_argument("Unknown fur_shells argument: " + name);
	}
	if (!loaded)
		throw std::invalid_argument("Fur Shells must be loaded");
	Settings next = settings;
	if (a_args.contains("furShells")) {
		ValidateSettingsObject(a_args.at("furShells"), true);
		json values = settings;
		values.update(a_args.at("furShells"));
		next = values.get<Settings>();
		if (next.FadeStart > next.FadeEnd)
			throw std::invalid_argument("FadeStart must not exceed FadeEnd");
	}
	const auto action = a_args.value("overrideAction", std::string{});
	if (a_args.contains("shellTexture") != !action.empty() ||
		(a_args.contains("shellOverride") && action != "apply") ||
		(!action.empty() && action != "apply" && action != "reload" && action != "save"))
		throw std::invalid_argument("Texture overrides require shellTexture and apply, reload or save overrideAction");
	auto it = shellTextures.end();
	ShellOverride nextOverride;
	if (!action.empty()) {
		const auto path = a_args.at("shellTexture").get<std::string>();
		it = shellTextures.find(path);
		if (it == shellTextures.end())
			throw std::invalid_argument("shellTexture must identify a resolved shell texture");
		nextOverride = it->second.overridden ? it->second.values : GetGlobalValues(it->second.skin);
		if (action == "apply") {
			if (!a_args.contains("shellOverride"))
				throw std::invalid_argument("apply requires shellOverride");
			ValidateSettingsObject(a_args.at("shellOverride"), false);
			json values = nextOverride;
			values.update(a_args.at("shellOverride"));
			nextOverride = values.get<ShellOverride>();
			if (nextOverride.FadeStart > nextOverride.FadeEnd)
				throw std::invalid_argument("Override FadeStart must not exceed FadeEnd");
		}
	}
	if (action == "save") {
		std::string path;
		if (!SaveOverride(it->first, nextOverride, path))
			throw std::runtime_error("Could not save fur override: " + path);
	} else if (action == "reload") {
		LoadOverride(it->first, it->second);
	} else if (action == "apply") {
		it->second.values = nextOverride;
		it->second.overridden = true;
		UpdateOverrideFadeEnd();
	}
	const bool settingsChanged = json(settings) != json(next);
	settings = next;
	if (settingsChanged || action == "apply" || action == "reload")
		ClearDynamics();
	return GetDiagnostics();
}
#endif

void FurShells::SetupRenderTargetResources()
{
	EndPass();
	ClearDynamics();
	deferredPasses.clear();
	deferredOverlayPasses.clear();
	frameBodies.clear();
	depthCopy = nullptr;
	depthCopyView = nullptr;
	depthCopyFailed = false;
	rootTest = false;
}

void FurShells::SetupResources()
{
	static bool drawHookInstalled = false;

	SetupRenderTargetResources();
	perPassCB = nullptr;
	noColorWrite = nullptr;
	depthStates.clear();
	lastPerPass = {};
	try {
		perPassCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PerPass>(), "FurShells::PerPass");
	} catch (const std::exception& error) {
		logger::warn("[Fur Shells] Fur resources unavailable: {}", error.what());
		return;
	}

	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;
	if (SUCCEEDED(globals::d3d::device->CreateBlendState(&blendDesc, noColorWrite.put())))
		Util::SetResourceName(noColorWrite.get(), "FurShells::NoColorWrite");
	else
		logger::warn("[Fur Shells] Failed to create blend state");

	if (!drawHookInstalled && globals::d3d::context) {
		drawHookInstalled = true;
		stl::detour_vfunc<12, Hooks::ID3D11DeviceContext_DrawIndexed>(globals::d3d::context);
		if (globals::game::isVR)
			stl::detour_vfunc<20, Hooks::ID3D11DeviceContext_DrawIndexedInstanced>(globals::d3d::context);
		logger::info("[Fur Shells] Installed draw hook");
	}
}

void FurShells::PostPostLoad()
{
	stl::write_vfunc<0x6, Hooks::BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x7, Hooks::BSLightingShader_RestoreGeometry>(RE::VTABLE_BSLightingShader[0]);
	logger::info("[Fur Shells] Installed hooks");
	if (settings.Enabled)
		StartAssetScan();
}

void FurShells::StartAssetScan()
{
	if (shellAssetScanStarted)
		return;
	shellAssetScanStarted = true;
	try {
		shellAssetScan = std::jthread([this](std::stop_token stop) {
			const bool present = FurShellAssets::AnyShellTextures(std::filesystem::path("Data"), stop);
			shellAssetsPresent.store(present, std::memory_order_relaxed);
			logger::info("[Fur Shells] Shell asset scan: {}", present ? "present or unconfirmed" : "none found");
		});
	} catch (const std::exception& error) {
		logger::warn("[Fur Shells] Asset scan could not start; retaining texture lookup: {}", error.what());
	}
}
