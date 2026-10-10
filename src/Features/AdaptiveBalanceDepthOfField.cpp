#include "AdaptiveBalanceDepthOfField.h"
#include "AdaptiveBrightness.h"
#include "Menu/SettingsPage.h"

#include "Globals.h"
#include "UnderwaterDepthOfField.h"
#include "Utils/Finite.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace
{
	using AdaptiveBalanceDepthOfField::kDofAutoFocusBlurMax;
	using AdaptiveBalanceDepthOfField::kDofAutoFocusBlurMin;
	using AdaptiveBalanceDepthOfField::kDofAutoFocusBlurMultiplierMax;
	using AdaptiveBalanceDepthOfField::kDofAutoFocusBlurMultiplierMin;
	using AdaptiveBalanceDepthOfField::kDofAutoFocusDepthMax;
	using AdaptiveBalanceDepthOfField::kDofDistanceMax;
	using AdaptiveBalanceDepthOfField::kDofDistanceMin;
	using AdaptiveBalanceDepthOfField::kDofRangeMax;
	using AdaptiveBalanceDepthOfField::kDofRangeMin;
	using AdaptiveBalanceDepthOfField::kDofStrengthMax;
	using AdaptiveBalanceDepthOfField::kDofStrengthMin;
	constexpr float kDofLockButtonWidth = 120.0f;
	using AdaptiveBalanceDepthOfField::kDofModeMask;
	constexpr uint32_t kDofNoSkyFlag = 0x4;
	constexpr uint32_t kDofBlurRadiusShift = 3;
	using AdaptiveBalanceDepthOfField::kDofBlurRadiusMax;
	constexpr uint32_t kDofAutoFocusFlag = 0x80;
	constexpr uint32_t kDofModeBack = 2;
	constexpr float kDofFloatEpsilon = 0.0001f;

	using DofAutoFocusSettings = AdaptiveBalanceDepthOfField::DepthOfFieldAutoFocusSettings;
	using DofSettings = AdaptiveBalanceDepthOfField::DepthOfFieldSettings;
	using DofOverride = AdaptiveBalanceDepthOfField::DepthOfFieldOverride;
	using DepthOfField = RE::ImageSpaceBaseData::DepthOfField;
	using SkyBlurRadius = DepthOfField::SkyBlurRadius;
	using DofAutoFocusMember = float DofAutoFocusSettings::*;

	struct DofAutoFocusSettingDefinition
	{
		const char* settingName;
		DofAutoFocusMember member;
		float defaultValue;
		float minValue;
		float maxValue;
		REL::VariantOffset valueOffset;
	};

	const std::array<DofAutoFocusSettingDefinition, 7> kDofAutoFocusSettingDefinitions{ {
		{ "fDynamicDOFNearDist:Display", &DofAutoFocusSettings::nearDistance, 0.0f, kDofDistanceMin, kDofAutoFocusDepthMax, REL::VariantOffset(0x323369c, 0x338cacc, 0x3485cd8) },
		{ "fDynamicDOFFarDist:Display", &DofAutoFocusSettings::farDistance, 0.0f, kDofDistanceMin, kDofAutoFocusDepthMax, REL::VariantOffset(0x32336a0, 0x338cad0, 0x3485cdc) },
		{ "fDynamicDOFNearRange:Display", &DofAutoFocusSettings::nearRange, 0.0f, kDofRangeMin, kDofAutoFocusDepthMax, REL::VariantOffset(0x32336a4, 0x338cad4, 0x3485ce0) },
		{ "fDynamicDOFFarRange:Display", &DofAutoFocusSettings::farRange, 0.0f, kDofRangeMin, kDofAutoFocusDepthMax, REL::VariantOffset(0x32336a8, 0x338cad8, 0x3485ce4) },
		{ "fDynamicDOFNearBlur:Display", &DofAutoFocusSettings::nearBlur, 0.0f, kDofAutoFocusBlurMin, kDofAutoFocusBlurMax, REL::VariantOffset(0x32336ac, 0x338cadc, 0x3485ce8) },
		{ "fDynamicDOFFarBlur:Display", &DofAutoFocusSettings::farBlur, 0.0f, kDofAutoFocusBlurMin, kDofAutoFocusBlurMax, REL::VariantOffset(0x32336b0, 0x338cae0, 0x3485cec) },
		{ "fDynamicDOFBlurMultiplier:Display", &DofAutoFocusSettings::blurMultiplier, 1.0f, kDofAutoFocusBlurMultiplierMin, kDofAutoFocusBlurMultiplierMax, REL::VariantOffset(0x32336b4, 0x338cae4, 0x3485cf0) },
	} };

	constexpr std::array<uint16_t, 8> kSkyBlurRadiusValues{
		static_cast<uint16_t>(SkyBlurRadius::kRadius0),
		static_cast<uint16_t>(SkyBlurRadius::kRadius1),
		static_cast<uint16_t>(SkyBlurRadius::kRadius2),
		static_cast<uint16_t>(SkyBlurRadius::kRadius3),
		static_cast<uint16_t>(SkyBlurRadius::kRadius4),
		static_cast<uint16_t>(SkyBlurRadius::kRadius5),
		static_cast<uint16_t>(SkyBlurRadius::kRadius6),
		static_cast<uint16_t>(SkyBlurRadius::kRadius7),
	};

	constexpr std::array<uint16_t, 8> kNoSkyBlurRadiusValues{
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius0),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius1),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius2),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius3),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius4),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius5),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius6),
		static_cast<uint16_t>(SkyBlurRadius::kNoSky_Radius7),
	};

	uint32_t ClampDofMode(uint32_t a_mode)
	{
		return std::min(a_mode, kDofModeMask);
	}

	uint32_t ClampDofBlurRadius(uint32_t a_radius)
	{
		return std::min(a_radius, kDofBlurRadiusMax);
	}

	float* GetDofAutoFocusValue(const DofAutoFocusSettingDefinition& a_definition)
	{
		const auto address = a_definition.valueOffset.address();
		return address ? reinterpret_cast<float*>(address) : nullptr;
	}

	void SanitizeDepthOfFieldAutoFocusSettings(DofAutoFocusSettings& a_settings)
	{
		for (const auto& definition : kDofAutoFocusSettingDefinitions) {
			a_settings.*definition.member = Util::ClampFinite(
				a_settings.*definition.member,
				definition.minValue,
				definition.maxValue,
				definition.defaultValue);
		}
	}

	uint32_t EncodeDofPackedValue(const DofSettings& a_settings)
	{
		return ClampDofMode(a_settings.mode) |
		       (a_settings.excludeSky ? kDofNoSkyFlag : 0) |
		       (a_settings.autoFocus ? kDofAutoFocusFlag : 0) |
		       (ClampDofBlurRadius(a_settings.blurRadius) << kDofBlurRadiusShift);
	}

	DofSettings DecodeDofPackedValue(float a_strength, float a_distance, float a_range, float a_packedValue)
	{
		uint32_t packedValue = 0;
		if (std::isfinite(a_packedValue) && a_packedValue > 0.0f &&
			static_cast<double>(a_packedValue) <= std::numeric_limits<uint32_t>::max()) {
			packedValue = static_cast<uint32_t>(a_packedValue);
		}

		DofSettings result{};
		result.strength = a_strength;
		result.distance = a_distance;
		result.range = a_range;
		result.mode = ClampDofMode(packedValue & kDofModeMask);
		result.excludeSky = (packedValue & kDofNoSkyFlag) != 0;
		result.autoFocus = (packedValue & kDofAutoFocusFlag) != 0;
		result.blurRadius = ClampDofBlurRadius((packedValue >> kDofBlurRadiusShift) & 0xF);
		AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(result);
		return result;
	}

	DofSettings DecodeUnderwaterDepthOfField(const DepthOfField& a_depthOfField)
	{
		DofSettings result{};
		result.strength = a_depthOfField.strength;
		result.distance = a_depthOfField.distance;
		result.range = a_depthOfField.range;
		result.mode = kDofModeBack;

		const uint16_t rawRadius = a_depthOfField.skyBlurRadius.underlying();
		for (uint32_t index = 0; index < kSkyBlurRadiusValues.size(); ++index) {
			if (rawRadius == kSkyBlurRadiusValues[index]) {
				result.excludeSky = false;
				result.blurRadius = index;
				AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(result);
				return result;
			}
			if (rawRadius == kNoSkyBlurRadiusValues[index]) {
				result.excludeSky = true;
				result.blurRadius = index;
				AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(result);
				return result;
			}
		}

		AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(result);
		return result;
	}

	SkyBlurRadius GetUnderwaterSkyBlurRadius(const DofSettings& a_settings)
	{
		const uint32_t blurRadius = ClampDofBlurRadius(a_settings.blurRadius);
		const uint16_t rawRadius = a_settings.excludeSky ? kNoSkyBlurRadiusValues[blurRadius] : kSkyBlurRadiusValues[blurRadius];
		return static_cast<SkyBlurRadius>(rawRadius);
	}

	void WriteUnderwaterFlagsAndRadius(DepthOfField& a_depthOfField, const DofSettings& a_settings)
	{
		a_depthOfField.flags = 0;
		a_depthOfField.skyBlurRadius = GetUnderwaterSkyBlurRadius(a_settings);
	}

	RE::Setting* FindDofSetting(std::string_view a_name)
	{
		if (auto* collection = globals::game::iniSettingCollection) {
			if (auto* setting = collection->GetSetting(a_name))
				return setting;
		}

		if (auto* collection = globals::game::iniPrefSettingCollection) {
			if (auto* setting = collection->GetSetting(a_name))
				return setting;
		}

		if (auto* collection = globals::game::gameSettingCollection) {
			if (auto* setting = collection->GetSetting(a_name.data()))
				return setting;
		}

		return RE::GetINISetting(a_name.data());
	}

	DofAutoFocusSettings ReadDofAutoFocusSettings()
	{
		DofAutoFocusSettings result{};
		for (const auto& definition : kDofAutoFocusSettingDefinitions) {
			if (auto* value = GetDofAutoFocusValue(definition)) {
				result.*definition.member = *value;
			} else if (auto* setting = FindDofSetting(definition.settingName); setting && setting->GetType() == RE::Setting::Type::kFloat) {
				result.*definition.member = setting->data.f;
			} else {
				result.*definition.member = definition.defaultValue;
			}
		}
		SanitizeDepthOfFieldAutoFocusSettings(result);
		return result;
	}

	bool DofAutoFocusSettingsChanged(const DofAutoFocusSettings& a_lhs, const DofAutoFocusSettings& a_rhs)
	{
		for (const auto& definition : kDofAutoFocusSettingDefinitions) {
			if (std::abs(a_lhs.*definition.member - a_rhs.*definition.member) > kDofFloatEpsilon)
				return true;
		}
		return false;
	}

	bool DofSettingsChanged(const DofSettings& a_lhs, const DofSettings& a_rhs)
	{
		return std::abs(a_lhs.strength - a_rhs.strength) > kDofFloatEpsilon ||
		       std::abs(a_lhs.distance - a_rhs.distance) > kDofFloatEpsilon ||
		       std::abs(a_lhs.range - a_rhs.range) > kDofFloatEpsilon ||
		       ClampDofMode(a_lhs.mode) != ClampDofMode(a_rhs.mode) ||
		       a_lhs.excludeSky != a_rhs.excludeSky ||
		       a_lhs.autoFocus != a_rhs.autoFocus ||
		       DofAutoFocusSettingsChanged(a_lhs.autoFocusSettings, a_rhs.autoFocusSettings) ||
		       ClampDofBlurRadius(a_lhs.blurRadius) != ClampDofBlurRadius(a_rhs.blurRadius);
	}

	std::optional<DofSettings> ReadSceneDepthOfField()
	{
		auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		if (!imageSpaceManager)
			return std::nullopt;

		GET_INSTANCE_MEMBER(data, imageSpaceManager);
		DofSettings result = DecodeDofPackedValue(
			data.modData.data[RE::ImageSpaceModData::kDOFStrength],
			data.modData.data[RE::ImageSpaceModData::kDOFDistance],
			data.modData.data[RE::ImageSpaceModData::kDOFRange],
			data.modData.data[RE::ImageSpaceModData::kDOFMode]);
		result.autoFocusSettings = ReadDofAutoFocusSettings();
		return result;
	}

	std::optional<DofSettings> ReadUnderwaterDepthOfField()
	{
		auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		if (!imageSpaceManager)
			return std::nullopt;

		GET_INSTANCE_MEMBER(underwaterBaseData, imageSpaceManager);
		if (!underwaterBaseData)
			return std::nullopt;

		DofSettings result = DecodeUnderwaterDepthOfField(underwaterBaseData->depthOfField);
		result.autoFocusSettings = ReadDofAutoFocusSettings();
		return result;
	}

	void WriteSceneDepthOfField(RE::ImageSpaceModData& a_modData, const DofSettings& a_settings)
	{
		DofSettings sanitizedSettings = a_settings;
		AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(sanitizedSettings);

		a_modData.data[RE::ImageSpaceModData::kDOFStrength] = sanitizedSettings.strength;
		a_modData.data[RE::ImageSpaceModData::kDOFDistance] = sanitizedSettings.distance;
		a_modData.data[RE::ImageSpaceModData::kDOFRange] = sanitizedSettings.range;
		a_modData.data[RE::ImageSpaceModData::kDOFMode] = static_cast<float>(EncodeDofPackedValue(sanitizedSettings));
	}

	void WriteUnderwaterDepthOfField(DepthOfField& a_depthOfField, const DofSettings& a_settings)
	{
		DofSettings sanitizedSettings = a_settings;
		AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(sanitizedSettings);
		sanitizedSettings.mode = kDofModeBack;
		sanitizedSettings.autoFocus = false;

		a_depthOfField.strength = sanitizedSettings.strength;
		a_depthOfField.distance = sanitizedSettings.distance;
		a_depthOfField.range = sanitizedSettings.range;
		WriteUnderwaterFlagsAndRadius(a_depthOfField, sanitizedSettings);
	}

	const char* GetDofModeLabel(uint32_t a_mode)
	{
		switch (ClampDofMode(a_mode)) {
		case 0:
			return "Front and Back";
		case 1:
			return "Front";
		case 2:
			return "Back";
		case 3:
			return "None";
		default:
			return "Back";
		}
	}

	const char* GetDofBlurRadiusLabel(uint32_t a_blurRadius)
	{
		switch (ClampDofBlurRadius(a_blurRadius)) {
		case 0:
			return "Radius 0";
		case 1:
			return "Radius 1";
		case 2:
			return "Radius 2";
		case 3:
			return "Radius 3";
		case 4:
			return "Radius 4";
		case 5:
			return "Radius 5";
		case 6:
			return "Radius 6";
		case 7:
			return "Radius 7";
		default:
			return "Radius 2";
		}
	}

	void DrawDofTooltip(const char* a_text)
	{
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", a_text);
		}
	}

	bool DrawDofModeCombo(uint32_t& a_mode)
	{
		const char* modeLabels[] = {
			GetDofModeLabel(0),
			GetDofModeLabel(1),
			GetDofModeLabel(2),
			GetDofModeLabel(3),
		};

		int mode = static_cast<int>(ClampDofMode(a_mode));
		const bool changed = Util::Widgets::Combo("Mode", &mode, modeLabels, IM_ARRAYSIZE(modeLabels));

		if (!changed)
			return false;

		a_mode = static_cast<uint32_t>(mode);
		return true;
	}

	bool DrawDofBlurRadiusCombo(uint32_t& a_blurRadius)
	{
		const char* blurRadiusLabels[] = {
			GetDofBlurRadiusLabel(0),
			GetDofBlurRadiusLabel(1),
			GetDofBlurRadiusLabel(2),
			GetDofBlurRadiusLabel(3),
			GetDofBlurRadiusLabel(4),
			GetDofBlurRadiusLabel(5),
			GetDofBlurRadiusLabel(6),
			GetDofBlurRadiusLabel(7),
		};

		int blurRadius = static_cast<int>(ClampDofBlurRadius(a_blurRadius));
		const bool changed = Util::Widgets::Combo("Blur Radius", &blurRadius, blurRadiusLabels, IM_ARRAYSIZE(blurRadiusLabels));

		if (!changed)
			return false;

		a_blurRadius = static_cast<uint32_t>(blurRadius);
		return true;
	}

	ImVec2 GetDepthOfFieldLockButtonSize()
	{
		const float labelWidth = std::max(
			ImGui::CalcTextSize("Unlock").x,
			ImGui::CalcTextSize("Lock").x);
		const float buttonWidth = std::max(
			kDofLockButtonWidth * Util::GetUIScale(),
			labelWidth + ImGui::GetStyle().FramePadding.x * 4.0f);
		return ImVec2(buttonWidth, 0.0f);
	}

	bool DrawDofAutoFocusControls(DofAutoFocusSettings& a_values)
	{
		bool changed = false;
		changed |= Util::Widgets::SliderFloat("Near Blur", &a_values.nearBlur, kDofAutoFocusBlurMin, kDofAutoFocusBlurMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		DrawDofTooltip("Near-side blur amount before the game slider multiplier is applied.");
		changed |= Util::Widgets::SliderFloat("Far Blur", &a_values.farBlur, kDofAutoFocusBlurMin, kDofAutoFocusBlurMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		DrawDofTooltip("Far-side blur amount before the game slider multiplier is applied.");

		changed |= Util::Widgets::SliderFloat("Game Slider / Multiplier", &a_values.blurMultiplier, kDofAutoFocusBlurMultiplierMin, kDofAutoFocusBlurMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		DrawDofTooltip("Scale Near Blur and Far Blur. Matches Settings > Display > Depth of Field and fDynamicDOFBlurMultiplier.");

		if (auto fineTuning = Util::SectionWrapper("Fine tuning")) {
			DrawDofTooltip("Auto Focus uses Skyrim's dynamic DOF. Game Slider / Multiplier matches Settings > Display > Depth of Field.");
			changed |= Util::Widgets::SliderFloat("Near Distance", &a_values.nearDistance, kDofDistanceMin, kDofAutoFocusDepthMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Offset before pixels nearer than the sampled focus depth begin to blur.");
			changed |= Util::Widgets::SliderFloat("Far Distance", &a_values.farDistance, kDofDistanceMin, kDofAutoFocusDepthMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Offset before pixels farther than the sampled focus depth begin to blur.");
			changed |= Util::Widgets::SliderFloat("Near Range", &a_values.nearRange, kDofRangeMin, kDofAutoFocusDepthMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Near-blur fade width. Higher makes the transition more gradual.");
			changed |= Util::Widgets::SliderFloat("Far Range", &a_values.farRange, kDofRangeMin, kDofAutoFocusDepthMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Far-blur fade width. Higher makes the transition more gradual.");
		}

		if (changed) {
			SanitizeDepthOfFieldAutoFocusSettings(a_values);
		}
		return changed;
	}

	bool DrawDepthOfFieldControls(DofSettings& a_values, bool a_enabled, bool a_allowMode, bool a_allowAutoFocus)
	{
		bool changed = false;
		Util::DisableGuard disabled(!a_enabled);

		if (a_allowAutoFocus) {
			changed |= Util::Widgets::Checkbox("Auto Focus", &a_values.autoFocus);
			DrawDofTooltip("Focus follows sampled scene depth. bDoDepthOfField can disable DOF; fDDOFFocusCenterweightExt controls sample weighting.");
		}

		if (a_allowAutoFocus && a_values.autoFocus) {
			changed |= DrawDofAutoFocusControls(a_values.autoFocusSettings);
		} else {
			changed |= Util::Widgets::SliderFloat("Strength", &a_values.strength, kDofStrengthMin, kDofStrengthMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= Util::Widgets::SliderFloat("Distance", &a_values.distance, kDofDistanceMin, kDofDistanceMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Fixed focus distance. Mode chooses blur in front, behind, both or neither.");
			changed |= Util::Widgets::SliderFloat("Range", &a_values.range, kDofRangeMin, kDofRangeMax, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			DrawDofTooltip("Blur transition width around Distance. Higher fades blur in more gradually.");
		}

		if (a_allowMode) {
			changed |= DrawDofModeCombo(a_values.mode);
		} else {
			uint32_t fixedMode = kDofModeBack;
			Util::DisableGuard fixedModeDisabled(true);
			DrawDofModeCombo(fixedMode);
		}

		changed |= Util::Widgets::Checkbox("Exclude Sky", &a_values.excludeSky);
		DrawDofTooltip("Exclude the sky from DOF blur using the No Sky flag.");
		changed |= DrawDofBlurRadiusCombo(a_values.blurRadius);

		if (changed) {
			AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(a_values);
		}
		return changed;
	}

	void SetDepthOfFieldPopupText(Util::ConfirmationPopup& a_popup, const char* a_id)
	{
		a_popup.title = "Lock Depth of Field?";
		a_popup.title += "##";
		a_popup.title += a_id;
		a_popup.message = "Discard the manual depth of field values and lock controls to live image space values?";
		a_popup.confirmLabel = "Discard and Lock";
		a_popup.cancelLabel = "Cancel";
	}

	void DrawDepthOfFieldLockButton(DofOverride& a_override, const std::optional<DofSettings>& a_liveValues, Util::ConfirmationPopup& a_popup)
	{
		const bool hasLiveValues = a_liveValues.has_value();
		const ImVec2 buttonSize = GetDepthOfFieldLockButtonSize();
		if (a_override.locked) {
			if (Util::WarningButton("Lock", buttonSize)) {
				if (DofSettingsChanged(a_override.values, a_override.baseline)) {
					a_popup.Request();
				} else {
					a_override.locked = false;
				}
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", "Lock depth of field controls to live image space values.");
			}
			return;
		}

		{
			Util::DisableGuard missingDataDisabled(!hasLiveValues);
			if (Util::SuccessButton("Unlock", buttonSize) && hasLiveValues) {
				a_override.values = *a_liveValues;
				a_override.baseline = *a_liveValues;
				a_override.locked = true;
			}
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", hasLiveValues ?
								  "Unlock depth of field for manual editing." :
								  "No live image space values are available.");
		}
	}

	void DrawDepthOfFieldSection(
		const char* a_label,
		const char* a_id,
		DofOverride& a_override,
		const std::optional<DofSettings>& a_liveValues,
		Util::ConfirmationPopup& a_popup,
		bool a_allowMode,
		bool a_allowAutoFocus,
		const char* a_missingDataText)
	{
		MenuUI::SectionHeading(a_label);

		ImGui::PushID(a_id);
		const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
		SetDepthOfFieldPopupText(a_popup, a_id);
		if (a_popup.Draw()) {
			a_override.locked = false;
		}

		DrawDepthOfFieldLockButton(a_override, a_liveValues, a_popup);

		if (!a_liveValues) {
			Util::TextUnformattedDisabled(a_missingDataText);
		}

		DofSettings displayValues = a_override.locked ? a_override.values : a_liveValues.value_or(a_override.values);
		const bool controlsEnabled = a_override.locked && a_liveValues.has_value();
		if (controlsEnabled) {
			DrawDepthOfFieldControls(a_override.values, true, a_allowMode, a_allowAutoFocus);
		} else {
			DrawDepthOfFieldControls(displayValues, false, a_allowMode, a_allowAutoFocus);
		}
	}

	bool IsCurrentUnderwaterImageSpace(RE::ImageSpaceManager* a_imageSpaceManager)
	{
		GET_INSTANCE_MEMBER(currentBaseData, a_imageSpaceManager);
		GET_INSTANCE_MEMBER(underwaterBaseData, a_imageSpaceManager);
		return underwaterBaseData && currentBaseData == underwaterBaseData;
	}

	class DepthOfFieldOverrideScope
	{
	public:
		DepthOfFieldOverrideScope()
		{
			const auto& settings = globals::features::adaptiveBrightness.settings.depthOfField;
			if (!AdaptiveBalanceDepthOfField::IsRuntimeEnabled() ||
				(!settings.sceneDof.locked && !settings.underwaterDof.locked))
				return;

			auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
			if (!imageSpaceManager)
				return;

			const bool currentUnderwater = IsCurrentUnderwaterImageSpace(imageSpaceManager);
			const DofOverride* autoFocusOverride = nullptr;
			if (!currentUnderwater && settings.sceneDof.locked) {
				autoFocusOverride = &settings.sceneDof;
			}
			if (autoFocusOverride && autoFocusOverride->values.autoFocus) {
				ApplyAutoFocusOverride(autoFocusOverride->values.autoFocusSettings);
			}

			if (settings.sceneDof.locked) {
				GET_INSTANCE_MEMBER(data, imageSpaceManager);
				sceneModData = &data.modData;
				sceneBackup = {
					sceneModData->data[RE::ImageSpaceModData::kDOFStrength],
					sceneModData->data[RE::ImageSpaceModData::kDOFDistance],
					sceneModData->data[RE::ImageSpaceModData::kDOFRange],
					sceneModData->data[RE::ImageSpaceModData::kDOFMode],
				};
				WriteSceneDepthOfField(*sceneModData, settings.sceneDof.values);
			}

			if (settings.underwaterDof.locked) {
				GET_INSTANCE_MEMBER(underwaterBaseData, imageSpaceManager);
				if (underwaterBaseData) {
					underwaterDepthOfField = &underwaterBaseData->depthOfField;
					underwaterBackup = *underwaterDepthOfField;
					WriteUnderwaterDepthOfField(*underwaterDepthOfField, settings.underwaterDof.values);
				}
			}
		}

		~DepthOfFieldOverrideScope()
		{
			if (sceneModData) {
				sceneModData->data[RE::ImageSpaceModData::kDOFStrength] = sceneBackup[0];
				sceneModData->data[RE::ImageSpaceModData::kDOFDistance] = sceneBackup[1];
				sceneModData->data[RE::ImageSpaceModData::kDOFRange] = sceneBackup[2];
				sceneModData->data[RE::ImageSpaceModData::kDOFMode] = sceneBackup[3];
			}

			if (underwaterDepthOfField) {
				*underwaterDepthOfField = underwaterBackup;
			}

			for (std::size_t index = 0; index < autoFocusValueCount; ++index) {
				*autoFocusValues[index] = autoFocusBackup[index];
			}
		}

	private:
		void ApplyAutoFocusOverride(const DofAutoFocusSettings& a_settings)
		{
			DofAutoFocusSettings sanitizedSettings = a_settings;
			SanitizeDepthOfFieldAutoFocusSettings(sanitizedSettings);

			for (const auto& definition : kDofAutoFocusSettingDefinitions) {
				auto* value = GetDofAutoFocusValue(definition);
				if (!value)
					continue;

				BackupAutoFocusValue(value);
				*value = sanitizedSettings.*definition.member;
			}
		}

		void BackupAutoFocusValue(float* a_value)
		{
			for (std::size_t index = 0; index < autoFocusValueCount; ++index) {
				if (autoFocusValues[index] == a_value)
					return;
			}

			if (autoFocusValueCount >= autoFocusValues.size())
				return;

			autoFocusValues[autoFocusValueCount] = a_value;
			autoFocusBackup[autoFocusValueCount] = *a_value;
			++autoFocusValueCount;
		}

		RE::ImageSpaceModData* sceneModData = nullptr;
		std::array<float, 4> sceneBackup{};
		DepthOfField* underwaterDepthOfField = nullptr;
		DepthOfField underwaterBackup{};
		std::array<float*, kDofAutoFocusSettingDefinitions.size()> autoFocusValues{};
		std::array<float, kDofAutoFocusSettingDefinitions.size()> autoFocusBackup{};
		std::size_t autoFocusValueCount = 0;
	};

	struct ImageSpaceEffectDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffectDepthOfField* a_effect)
		{
			DepthOfFieldOverrideScope overrideScope;
			return func(a_effect);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectDepthOfField_Render
	{
		static void thunk(RE::ImageSpaceEffectDepthOfField* a_effect, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param)
		{
			UnderwaterDepthOfField::BeginRender();
			const SKSE::stl::scope_exit endRender([]() noexcept {
				UnderwaterDepthOfField::EndRender();
			});
			func(a_effect, a_shape, a_param);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectDepthOfField_UpdateParams
	{
		static bool thunk(RE::ImageSpaceEffectDepthOfField* a_effect, RE::ImageSpaceEffectParam* a_param)
		{
			DepthOfFieldOverrideScope overrideScope;
			const bool result = func(a_effect, a_param);
			UnderwaterDepthOfField::RecordShaderConstants(a_effect, a_param);
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings(DepthOfFieldSettings& a_settings)
{
	const DepthOfFieldSettings defaults{};
	a_settings.strength = Util::ClampFinite(a_settings.strength, kDofStrengthMin, kDofStrengthMax, defaults.strength);
	a_settings.distance = Util::ClampFinite(a_settings.distance, kDofDistanceMin, kDofDistanceMax, defaults.distance);
	a_settings.range = Util::ClampFinite(a_settings.range, kDofRangeMin, kDofRangeMax, defaults.range);
	a_settings.mode = ClampDofMode(a_settings.mode);
	SanitizeDepthOfFieldAutoFocusSettings(a_settings.autoFocusSettings);
	a_settings.blurRadius = ClampDofBlurRadius(a_settings.blurRadius);
}

void AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldOverride(DepthOfFieldOverride& a_override)
{
	SanitizeDepthOfFieldSettings(a_override.values);
	SanitizeDepthOfFieldSettings(a_override.baseline);
}

void AdaptiveBalanceDepthOfField::DrawSettings()
{
	auto& settings = globals::features::adaptiveBrightness.settings.depthOfField;
	static Util::ConfirmationPopup sceneLockPopup;
	static Util::ConfirmationPopup underwaterLockPopup;

	Util::Widgets::Checkbox("Enable depth-of-field overrides", &settings.enabled);
	DrawDofTooltip("Apply the unlocked scene and underwater controls globally. Adaptive Balance must also be enabled.");

	{
		Util::DisableGuard overridesDisabled(!settings.enabled);
		DrawDepthOfFieldSection(
			"Scene",
			"Scene",
			settings.sceneDof,
			ReadSceneDepthOfField(),
			sceneLockPopup,
			true,
			true,
			"No live scene image space data is available.");

		ImGui::Separator();

		DrawDepthOfFieldSection(
			"Underwater",
			"Underwater",
			settings.underwaterDof,
			ReadUnderwaterDepthOfField(),
			underwaterLockPopup,
			false,
			false,
			"No underwater image space record is currently applied.");
	}

	ImGui::Separator();
	Util::Widgets::Checkbox("Underwater fog blur correction", &settings.fixUnderwaterFogDofBlur);
	DrawDofTooltip("Blur underwater fog correctly with vanilla DOF. Applies immediately and controls only the correction. Independent of manual DOF overrides; Adaptive Balance must be enabled.");
}

void AdaptiveBalanceDepthOfField::InstallHooks()
{
	stl::write_vfunc<0x1, ImageSpaceEffectDepthOfField_Render>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
	stl::write_vfunc<0x6, ImageSpaceEffectDepthOfField_IsActive>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
	stl::write_vfunc<0x7, ImageSpaceEffectDepthOfField_UpdateParams>(RE::VTABLE_ImageSpaceEffectDepthOfField[0]);
	logger::info("[Adaptive Balance] Installed vanilla depth of field hooks");
}

bool AdaptiveBalanceDepthOfField::IsRuntimeEnabled()
{
	const auto& adaptiveBalance = globals::features::adaptiveBrightness;
	return adaptiveBalance.IsRuntimeEnabled() && adaptiveBalance.settings.depthOfField.enabled;
}

bool AdaptiveBalanceDepthOfField::IsCorrectionEnabled()
{
	const auto& adaptiveBalance = globals::features::adaptiveBrightness;
	return adaptiveBalance.IsRuntimeEnabled() && adaptiveBalance.settings.depthOfField.fixUnderwaterFogDofBlur;
}

void AdaptiveBalanceDepthOfField::SanitizeSettings(Settings& a_settings)
{
	SanitizeDepthOfFieldOverride(a_settings.sceneDof);
	SanitizeDepthOfFieldOverride(a_settings.underwaterDof);
}

std::optional<AdaptiveBalanceDepthOfField::DepthOfFieldSettings> AdaptiveBalanceDepthOfField::GetLiveSceneSettings()
{
	return ReadSceneDepthOfField();
}

std::optional<AdaptiveBalanceDepthOfField::DepthOfFieldSettings> AdaptiveBalanceDepthOfField::GetLiveUnderwaterSettings()
{
	return ReadUnderwaterDepthOfField();
}
