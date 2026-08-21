#include "CSUtility.h"

#include "UnderwaterDepthOfField.h"
#include "Utils/UI.h"

namespace
{
	void SanitizeSettings(CSUtility::Settings& a_settings)
	{
		CSUtility::SanitizeDepthOfFieldOverride(a_settings.sceneDof);
		CSUtility::SanitizeDepthOfFieldOverride(a_settings.underwaterDof);
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldAutoFocusSettings,
	nearDistance,
	farDistance,
	nearRange,
	farRange,
	nearBlur,
	farBlur,
	blurMultiplier)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldSettings,
	strength,
	distance,
	range,
	mode,
	excludeSky,
	autoFocus,
	autoFocusSettings,
	blurRadius)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::DepthOfFieldOverride,
	locked,
	values,
	baseline)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSUtility::Settings,
	enabled,
	fixUnderwaterFogDofBlur,
	sceneDof,
	underwaterDof)

void CSUtility::DrawSettingsHeaderControls()
{
	ImGui::Checkbox("Enable DOF Utilities", &settings.enabled);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Controls the depth-of-field overrides and underwater fog blur correction on this page.");
}

void CSUtility::DrawSettings()
{
	DrawDepthOfFieldSettings();
}

void CSUtility::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void CSUtility::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void CSUtility::RestoreDefaultSettings()
{
	settings = {};
}

bool CSUtility::IsRuntimeEnabled() const
{
	return loaded && settings.enabled;
}

void CSUtility::PostPostLoad()
{
	InstallDepthOfFieldHooks();
}

void CSUtility::DataLoaded()
{
	UnderwaterDepthOfField::InstallHooks();
}
