#include "ReferenceEffectWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

namespace
{
	namespace ReferenceEffectSetting
	{
		constexpr const char* kArtObject = "Art Object";
		constexpr const char* kEffectShader = "Effect Shader";
		constexpr const char* kFaceTarget = "Face Target";
		constexpr const char* kAttachToCamera = "Attach To Camera";
		constexpr const char* kInheritRotation = "Inherit Rotation";
	}

	template <class T>
	T* LoadFormReference(const json& source, const char* key, T* fallback)
	{
		if (!source.contains(key) || !source[key].is_string())
			return fallback;

		const auto identifier = source[key].get<std::string>();
		if (identifier.empty() || identifier == "00000000")
			return nullptr;

		if (auto* form = WeatherUtils::FindFormByEditorIDOrFileKey<T>(identifier))
			return form;

		// Backward compatibility for files that stored the load-order-dependent
		// eight-digit FormID used by older CS Editor versions.
		try {
			size_t parsedLength = 0;
			const auto formID = static_cast<RE::FormID>(std::stoul(identifier, &parsedLength, 16));
			if (parsedLength == identifier.size())
				return RE::TESForm::LookupByID<T>(formID);
		} catch (const std::exception&) {
		}

		logger::warn("ReferenceEffect: Could not resolve saved {} form '{}'", key, identifier);
		return fallback;
	}
}

void ReferenceEffectWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##ReferenceEffectSearch", true, true);
		DrawSearchDropdown();
		BeginScrollableContent("##REScroll");
		{
			auto editorWindow = EditorWindow::GetSingleton();
			auto drawFormPicker = [&](const char* label, auto& currentForm, const auto& widgets) {
				return DrawWithHighlight(label, [&]() {
					return WeatherUtils::DrawFormPickerCached(label, currentForm, widgets, false, true);
				});
			};

			DrawIfMatchesSearch(ReferenceEffectSetting::kArtObject, [&](const char* label) {
				ImGui::SeparatorText(label);
				if (editorWindow->artObjectWidgets.empty()) {
					ImGui::TextDisabled("No Art Objects available");
					return false;
				}
				return drawFormPicker(label, settings.artObject, editorWindow->artObjectWidgets);
			});
			DrawIfMatchesSearch(ReferenceEffectSetting::kEffectShader, [&](const char* label) {
				ImGui::SeparatorText(label);
				if (editorWindow->effectShaderWidgets.empty()) {
					ImGui::TextDisabled("No Effect Shaders available");
					return false;
				}
				return drawFormPicker(label, settings.effectShader, editorWindow->effectShaderWidgets);
			});
			if (MatchesAnySearch({ ReferenceEffectSetting::kFaceTarget, ReferenceEffectSetting::kAttachToCamera, ReferenceEffectSetting::kInheritRotation })) {
				ImGui::SeparatorText("Flags");
				WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kFaceTarget, settings.faceTarget);
				WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kAttachToCamera, settings.attachToCamera);
				WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kInheritRotation, settings.inheritRotation);
			}
		}
		EndScrollableContent();
	}
	ImGui::End();
}

void ReferenceEffectWidget::LoadSettings()
{
	if (!referenceEffect)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			settings.artObject = LoadFormReference(js, "artObject", vanillaSettings.artObject);
			settings.effectShader = LoadFormReference(js, "effectShader", vanillaSettings.effectShader);
			if (js.contains("faceTarget"))
				settings.faceTarget = js["faceTarget"];
			if (js.contains("attachToCamera"))
				settings.attachToCamera = js["attachToCamera"];
			if (js.contains("inheritRotation"))
				settings.inheritRotation = js["inheritRotation"];
		} catch (const std::exception& e) {
			logger::error("ReferenceEffect {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
		settings = vanillaSettings;
	}

	originalSettings = settings;
	ApplyChanges();
}

void ReferenceEffectWidget::LoadFromGameSettings()
{
	if (!referenceEffect)
		return;
	settings.artObject = referenceEffect->data.artObject;
	settings.effectShader = referenceEffect->data.effectShader;
	settings.faceTarget = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kFaceTarget);
	settings.attachToCamera = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kAttachToCamera);
	settings.inheritRotation = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kInheritRotation);
}

void ReferenceEffectWidget::SaveSettings()
{
	js["artObject"] = settings.artObject ? Util::GetFormFileKey(settings.artObject) : "";
	js["effectShader"] = settings.effectShader ? Util::GetFormFileKey(settings.effectShader) : "";
	js["faceTarget"] = settings.faceTarget;
	js["attachToCamera"] = settings.attachToCamera;
	js["inheritRotation"] = settings.inheritRotation;
	originalSettings = settings;
}

void ReferenceEffectWidget::ApplyChanges()
{
	if (!CanApplyPersistentChanges())
		return;

	if (!referenceEffect)
		return;

	referenceEffect->data.artObject = settings.artObject;
	referenceEffect->data.effectShader = settings.effectShader;

	referenceEffect->data.flags.reset();
	if (settings.faceTarget)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kFaceTarget);
	if (settings.attachToCamera)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kAttachToCamera);
	if (settings.inheritRotation)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kInheritRotation);

	Widget::ForceCurrentWeatherReinit();
}

void ReferenceEffectWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

Widget::UndoRestoreAction ReferenceEffectWidget::CaptureUndoState() const
{
	const auto snapshot = settings;
	return [snapshot](Widget& widget) {
		auto& self = static_cast<ReferenceEffectWidget&>(widget);
		self.settings = snapshot;
		self.ApplyChanges();
	};
}

Widget::UndoRestoreAction ReferenceEffectWidget::CaptureBaselineState() const
{
	const auto snapshot = vanillaSettings;
	return [snapshot](Widget& widget) {
		static_cast<ReferenceEffectWidget&>(widget).vanillaSettings = snapshot;
	};
}

bool ReferenceEffectWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

std::vector<Widget::SearchResult> ReferenceEffectWidget::CollectSearchableSettings() const
{
	return {
		{ ReferenceEffectSetting::kArtObject, "", ReferenceEffectSetting::kArtObject },
		{ ReferenceEffectSetting::kEffectShader, "", ReferenceEffectSetting::kEffectShader },
		{ ReferenceEffectSetting::kFaceTarget, "", ReferenceEffectSetting::kFaceTarget },
		{ ReferenceEffectSetting::kAttachToCamera, "", ReferenceEffectSetting::kAttachToCamera },
		{ ReferenceEffectSetting::kInheritRotation, "", ReferenceEffectSetting::kInheritRotation },
	};
}
