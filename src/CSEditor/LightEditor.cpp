#include "LightEditor.h"

#include "Features/InverseSquareLighting.h"
#include "Features/LightLimitFix.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Shadercache.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.light_editor."

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

void LightEditor::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enable_light_editor"), "Enable Light Editor"), &enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_light_editor_tooltip"),
			"Allows for modifying lights in real-time to preview changes. "
			"Light Placer lights can be saved back to their JSON configs. "
			"Not intended for gameplay use."));
	}

	if (!enabled)
		return;

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox(T(TKEY("disable_regular_falloff_lights"), "Disable Regular Falloff Lights"), &disableRegularLights);
	ImGui::Checkbox(T(TKEY("disable_inverse_square_falloff_lights"), "Disable Inverse Square Falloff Lights"), &disableInvSqLights);

	ImGui::Spacing();
	ImGui::Text(T(TKEY("total_lights"), "Total Lights: %u"), totalLightCount);
	ImGui::Text(T(TKEY("active_shadow_lights"), "Active Shadow Lights: %u"), activeShadowLightCount);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox(T(TKEY("shadows_only"), "Shadows Only"), &shadowsOnly);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("shadows_only_tooltip"), "Only show lights with HemiShadow or OmniShadow flags."));
	}

	int selectedFilter = static_cast<int>(filterOption);
	if (ImGui::Combo(T(TKEY("filter_by"), "Filter By"), &selectedFilter, FilterOptionLabels, static_cast<int>(FilterOption::Count))) {
		filterOption = static_cast<FilterOption>(selectedFilter);
	}

	int selectedSort = static_cast<int>(sortOption);
	if (ImGui::Combo(T(TKEY("sort_by"), "Sort By"), &selectedSort, SortOptionLabels, static_cast<int>(SortOption::Count))) {
		sortOption = static_cast<SortOption>(selectedSort);
	}

	if (ImGui::BeginCombo(T(TKEY("lights"), "Lights"), selected.isSelected ? GetLightName(selected).c_str() : T(TKEY("select_a_light"), "Select a light"))) {
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			const bool isSelected = light == selected;

			if (ImGui::Selectable(displayName.c_str(), isSelected))
				selected = light;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (!selected.isSelected)
		return;

	if (selected.isRef || selected.isAttached) {
		ImGui::Text(T(TKEY("owner"), "Owner: 0x%08X | %s"), selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text(T(TKEY("owner_last_edited_by"), "Owner last edited by: %s"), displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text(T(TKEY("base_object"), "Base Object: 0x%08X | %s"), displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text(T(TKEY("ligh"), "LIGH: 0x%08X | %s"), displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text(T(TKEY("cell"), "Cell: %s"), displayInfo.cellEditorId.c_str());
	} else {
		ImGui::Text(T(TKEY("memory_address"), "Memory Address: %p"), selected.ptr);
		ImGui::Text(T(TKEY("ni_light_name"), "NiLight Name: %s"), selected.name.c_str());
	}

	ImGui::Spacing();
	DrawWaterRoutingDiagnostics();
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button(T(TKEY("revert_changes"), "Revert Changes"))) {
		current = original;
		current.pos = { 0, 0, 0 };
		waitFrames = 1;
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("save_to_light_placer"), "Save to Light Placer"))) {
			SaveToLightPlacer();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("save_to_light_placer_tooltip"), "Save current settings to the Light Placer JSON."));
		}
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (selected.isSpotlight)
		ImGui::TextDisabled("%s", T(TKEY("spotlight_not_applicable"), "Spotlight: ISL light type flags not applicable"));
	ImGui::BeginDisabled(selected.isSpotlight);
	ImGui::CheckboxFlags(T(TKEY("inverse_square_light"), "Inverse Square Light"), reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
	ImGui::EndDisabled();
	ImGui::CheckboxFlags(T(TKEY("linear_light"), "Linear Light"), reinterpret_cast<uint32_t*>(&current.data.flags), static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));

	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::ColorEdit3(T(TKEY("color"), "Color"), &current.data.diffuse.red);
	ImGui::SliderFloat(T(TKEY("intensity"), "Intensity"), &current.data.fade, 0.01f, 16.f, "%.3f");

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	if (isInvSq)
		ImGui::BeginDisabled();
	ImGui::SliderFloat(T(TKEY("radius"), "Radius"), &current.data.radius, 2.f, 8096.f, "%.0f");
	if (isInvSq)
		ImGui::EndDisabled();

	if (isInvSq) {
		ImGui::SliderFloat(T(TKEY("size"), "Size"), &current.data.size, 0.01f, 10.0f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cutoff"), "Cutoff"), &current.data.cutoffOverride, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text(T(TKEY("position_format"), "X: %.2f, Y: %.2f, Z: %.2f"), displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		ImGui::Spacing();
		ImGui::SliderFloat3(T(TKEY("position_offset"), "Position Offset"), &current.pos.x, -500.f, 500.f, "%.0f");

		ImGui::Spacing();
		ImGui::Spacing();

		auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
		ImGui::Spacing();
		ImGui::Text("%s", T(TKEY("light_flags"), "Light Flags"));
		ImGui::CheckboxFlags(T(TKEY("dynamic"), "Dynamic"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
		ImGui::CheckboxFlags(T(TKEY("negative"), "Negative"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
		ImGui::CheckboxFlags(T(TKEY("flicker"), "Flicker"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
		ImGui::CheckboxFlags(T(TKEY("flicker_slow"), "Flicker Slow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
		ImGui::CheckboxFlags(T(TKEY("pulse"), "Pulse"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
		ImGui::CheckboxFlags(T(TKEY("pulse_slow"), "Pulse Slow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
		ImGui::CheckboxFlags(T(TKEY("hemi_shadow"), "Hemi Shadow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
		ImGui::CheckboxFlags(T(TKEY("omni_shadow"), "Omni Shadow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
		ImGui::CheckboxFlags(T(TKEY("portal_strict"), "Portal Strict"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));
	}
}

void LightEditor::DrawWaterRoutingDiagnostics() const
{
	const auto& diagnostics = waterRoutingDiagnostics;
	ImGui::SeparatorText(T(TKEY("water_routing_diagnostics"), "Water Routing Diagnostics"));

	if (!diagnostics.available) {
		ImGui::TextDisabled("%s", T(TKEY("water_routing_unavailable"), "Runtime light data unavailable."));
		return;
	}

	const char* yes = T(TKEY("yes"), "Yes");
	const char* no = T(TKEY("no"), "No");

	if (ImGui::BeginTable("##WaterRoutingDiagnostics", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
		auto drawRow = [](const char* label, const char* value, const char* tooltip = nullptr) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextWrapped("%s", label);
			if (tooltip) {
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(tooltip);
			}
			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", value);
		};

		drawRow(T(TKEY("runtime_portal_strict"), "Runtime Portal Strict"), diagnostics.portalStrict ? yes : no);
		drawRow(T(TKEY("portal_graph_present"), "Portal Graph Present"), diagnostics.portalGraphPresent ? yes : no);
		drawRow(T(TKEY("affects_water"), "Affects Water"), diagnostics.affectWater ? yes : no);
		drawRow(T(TKEY("shadow_light"), "Shadow Light"), diagnostics.shadow ? yes : no);
		drawRow(
			T(TKEY("llf_water_candidate"), "LLF Water Candidate"),
			diagnostics.llfWaterCandidate ? yes : no,
			T(TKEY("llf_water_candidate_tooltip"),
				"Current clustered-water path before cluster, range, intensity, and room culling."));
		drawRow(
			T(TKEY("skyrim_water_pass_eligible"), "Skyrim Water-Pass Eligible"),
			diagnostics.skyrimWaterPassEligible ? yes : no,
			T(TKEY("skyrim_water_pass_eligible_tooltip"),
				"Computed from Affects Water and the LLF ownership gate. Pass observations provide the runtime proof."));
		drawRow(
			T(TKEY("potential_duplicate_water_route"), "Potential Duplicate Water Route"),
			diagnostics.llfWaterCandidate && diagnostics.skyrimWaterPassEligible ? yes : no);

		if (!diagnostics.observerAvailable) {
			drawRow(
				T(TKEY("water_pass_observation"), "Water-Pass Observation"),
				T(TKEY("unavailable"), "Unavailable"),
				T(TKEY("water_pass_observation_unavailable_tooltip"),
					"Actual water-pass observation requires the Light Limit Fix hook."));
		} else {
			const auto waterPassCount = diagnostics.waterPassCount.load(std::memory_order_acquire);
			const auto matchedWaterPassCount = diagnostics.matchedWaterPassCount.load(std::memory_order_acquire);
			const auto waterPassCountText = fmt::format("{}", waterPassCount);
			const auto matchedWaterPassCountText = fmt::format("{}", matchedWaterPassCount);
			drawRow(T(TKEY("water_pass_calls"), "Water-Pass Calls"), waterPassCountText.c_str());
			drawRow(T(TKEY("selected_light_pass_matches"), "Selected-Light Pass Matches"), matchedWaterPassCountText.c_str());

			if (matchedWaterPassCount > 0) {
				const auto lastMatchText = fmt::format(
					"pass=0x{:X}, sceneSlot={}, sceneLights={}",
					diagnostics.lastPassEnum.load(std::memory_order_relaxed),
					diagnostics.lastSceneLightIndex.load(std::memory_order_relaxed),
					diagnostics.lastSceneLightCount.load(std::memory_order_relaxed));
				drawRow(
					T(TKEY("last_water_pass_match"), "Last Water-Pass Match"),
					lastMatchText.c_str());
			}
		}

		ImGui::EndTable();
	}
}

#undef I18N_KEY_PREFIX

std::string LightEditor::GetLightName(LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::GatherLights()
{
	if (!enabled || !Menu::GetSingleton()->ShouldSwallowInput()) {
		ResetOverrides();
		return;
	}

	if (waitFrames > 0) {
		waitFrames--;
		return;
	}

	bool foundSelected = false;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& light) {
		const auto bsLight = light.get();
		if (!bsLight)
			return;

		const auto niLight = bsLight->light.get();
		if (!niLight)
			return;

		LightInfo current{};
		current.ptr = reinterpret_cast<void*>(niLight);
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				current.id = refr->GetFormID();
				current.name = clib_util::editorID::get_editorID(objRef);
				current.index = lightsAttached[refr]++;
			}
		}

		current.isRef = ligh != nullptr;

		if (!current.isRef && runtimeData->lighFormId != 0)
			ligh = RE::TESForm::LookupByID(runtimeData->lighFormId)->As<RE::TESObjectLIGH>();

		current.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow, RE::TES_LIGHT_FLAGS::kSpotShadow);

		totalLightCount++;
		if (isShadow)
			activeShadowLightCount++;

		if ((shadowsOnly) && (!ligh || !isShadow)) {
			return;
		}

		current.isAttached = !current.isRef && refr != nullptr;
		current.isOther = (!current.isRef && !current.isAttached) || (current.isSpotlight);

		const bool isRefMatch = (current.isRef && !current.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = current.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = current.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (current.isRef) {
			current.position = refr->GetPosition();
			current.hasPosition = true;
		} else if (niLight->parent) {
			current.position = niLight->parent->world.translate;
			current.hasPosition = true;
		}
		if (current.isOther) {
			if (current.name.empty())
				current.name = niLight->name.c_str();
			current.index = 0;
		}

		current.isSelected = selected == current;

		lights.push_back(current);

		if (!current.isSelected)
			return;
		selected = current;
		foundSelected = true;
		UpdateSelectedLight(refr, ligh, bsLight, niLight);
	};

	lights.clear();
	lightsAttached.clear();
	totalLightCount = 0;
	activeShadowLightCount = 0;
	const auto smState = globals::game::smState;
	const auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto& activeLights = shadowSceneNode->GetRuntimeData().activeLights;

	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = shadowSceneNode->GetRuntimeData().activeShadowLights;

	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	if (!foundSelected) {
		RestoreOriginal();
		previous = {};
		selected = {};
	}

	SortLights();
}

void LightEditor::ResetOverrides()
{
	RestoreOriginal();
	selected = {};
	previous = {};
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::BSLight* bsLight, RE::NiLight* niLight)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;

	if (previous != selected || activeBSLight.get() != bsLight || activeNiLight.get() != niLight) {
		RestoreOriginal();

		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef ? refr->GetPosition() : (niLight->parent ? niLight->parent->local.translate : RE::NiPoint3{});

		current = original;
		current.pos = { 0, 0, 0 };

		lpInfo = selected.isAttached ? ParseLPLightName(niLight->name.c_str()) : LPLightInfo{};
		if (lpInfo.isLPLight && refr) {
			if (auto* baseObj = refr->GetObjectReference()) {
				lpInfo.ownerEditorId = clib_util::editorID::get_editorID(baseObj);
				if (auto* model = baseObj->As<RE::TESModel>()) {
					if (const char* path = model->GetModel())
						lpInfo.ownerModelPath = path;
				}
			}
		}

		activeIsRef = selected.isRef;
		activeRefr = refr;
		activeLigh = ligh;

		previous = selected;
	}

	activeBSLight.reset(bsLight);
	activeNiLight.reset(niLight);
	UpdateWaterRoutingDiagnostics(bsLight, niLight);

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow);
		current.data.radius = InverseSquareLighting::CalculateRadius(
			current.data.fade * 4.f, isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			std::clamp(current.data.size, 0.1f, 50.0f));
	}

	if (selected.isRef) {
		const auto currentPos = refr->GetPosition();
		const auto newPos = original.pos + current.pos;
		if (currentPos != newPos) {
			refr->SetPosition(newPos);
			waitFrames = 1;
		}
		displayInfo.pos = newPos;
	} else if (selected.isAttached) {
		if (niLight->parent) {
			const auto currentPos = niLight->parent->local.translate;
			const auto newPos = original.pos + current.pos;
			if (currentPos != newPos) {
				niLight->parent->local.translate = newPos;
				RE::NiUpdateData updateData;
				niLight->parent->Update(updateData);
				waitFrames = 1;
			}
			displayInfo.pos = newPos;
		} else {
			displayInfo.pos = {};
		}
	}

	if (!selected.isOther && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
		waitFrames = 1;
	}

	displayInfo.ownerFormId = refr ? refr->GetFormID() : 0;
	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

void LightEditor::UpdateWaterRoutingDiagnostics(RE::BSLight* bsLight, RE::NiLight* niLight)
{
	if (!bsLight || !niLight) {
		ClearWaterRoutingDiagnostics();
		return;
	}

	auto& diagnostics = waterRoutingDiagnostics;
	const bool observerAvailable = globals::features::lightLimitFix.loaded;
	const bool portalStrict = bsLight->portalStrict;
	const bool portalGraphPresent = bsLight->portalGraph != nullptr;
	const bool affectWater = bsLight->affectWater;
	const bool shadow = bsLight->IsShadowLight();
	// The clustered shader follows the master CS toggle, while LLF's engine-ownership hook stays installed.
	const bool llfShaderActive =
		globals::features::lightLimitFix.loaded &&
		globals::shaderCache &&
		globals::shaderCache->IsEnabled();
	const bool llfWaterCandidate =
		llfShaderActive &&
		!shadow &&
		!niLight->GetFlags().any(RE::NiAVObject::Flag::kHidden);
	const bool skyrimWaterPassEligible =
		affectWater &&
		(!globals::features::lightLimitFix.loaded || LightLimitFix::RequiresEngineLightPath(bsLight));

	const bool classificationChanged =
		!diagnostics.available ||
		diagnostics.observerAvailable != observerAvailable ||
		diagnostics.portalStrict != portalStrict ||
		diagnostics.portalGraphPresent != portalGraphPresent ||
		diagnostics.affectWater != affectWater ||
		diagnostics.shadow != shadow ||
		diagnostics.llfWaterCandidate != llfWaterCandidate ||
		diagnostics.skyrimWaterPassEligible != skyrimWaterPassEligible;
	const bool targetChanged = diagnostics.target.load(std::memory_order_acquire) != bsLight;

	if (targetChanged || classificationChanged) {
		diagnostics.target.store(nullptr, std::memory_order_release);
		diagnostics.waterPassCount.store(0, std::memory_order_relaxed);
		diagnostics.matchedWaterPassCount.store(0, std::memory_order_relaxed);
		diagnostics.lastPassEnum.store(0, std::memory_order_relaxed);
		diagnostics.lastSceneLightIndex.store(0, std::memory_order_relaxed);
		diagnostics.lastSceneLightCount.store(0, std::memory_order_relaxed);
	}

	diagnostics.available = true;
	diagnostics.observerAvailable = observerAvailable;
	diagnostics.portalStrict = portalStrict;
	diagnostics.portalGraphPresent = portalGraphPresent;
	diagnostics.affectWater = affectWater;
	diagnostics.shadow = shadow;
	diagnostics.llfWaterCandidate = llfWaterCandidate;
	diagnostics.skyrimWaterPassEligible = skyrimWaterPassEligible;
	diagnostics.target.store(bsLight, std::memory_order_release);

	if (targetChanged || classificationChanged) {
		logger::debug(
			"[LightEditor] Water routing selection: BSLight=0x{:X}, NiLight=0x{:X}, "
			"portalStrict={}, portalGraphPresent={}, portalGraph=0x{:X}, affectWater={}, shadow={}, "
			"llfWaterCandidate={}, skyrimWaterPassEligible={}",
			reinterpret_cast<std::uintptr_t>(bsLight),
			reinterpret_cast<std::uintptr_t>(niLight),
			portalStrict,
			portalGraphPresent,
			reinterpret_cast<std::uintptr_t>(bsLight->portalGraph),
			affectWater,
			shadow,
			llfWaterCandidate,
			skyrimWaterPassEligible);
	}
}

void LightEditor::ClearWaterRoutingDiagnostics()
{
	auto& diagnostics = waterRoutingDiagnostics;
	auto* target = diagnostics.target.exchange(nullptr, std::memory_order_acq_rel);
	if (target && diagnostics.available) {
		logger::debug(
			"[LightEditor] Water routing summary: BSLight=0x{:X}, waterPassCalls={}, "
			"selectedLightPassMatches={}, lastPass=0x{:X}, lastSceneSlot={}, lastSceneLights={}",
			reinterpret_cast<std::uintptr_t>(target),
			diagnostics.waterPassCount.load(std::memory_order_relaxed),
			diagnostics.matchedWaterPassCount.load(std::memory_order_relaxed),
			diagnostics.lastPassEnum.load(std::memory_order_relaxed),
			diagnostics.lastSceneLightIndex.load(std::memory_order_relaxed),
			diagnostics.lastSceneLightCount.load(std::memory_order_relaxed));
	}
	diagnostics.available = false;
	diagnostics.observerAvailable = false;
	diagnostics.portalStrict = false;
	diagnostics.portalGraphPresent = false;
	diagnostics.affectWater = false;
	diagnostics.shadow = false;
	diagnostics.llfWaterCandidate = false;
	diagnostics.skyrimWaterPassEligible = false;
	diagnostics.waterPassCount.store(0, std::memory_order_relaxed);
	diagnostics.matchedWaterPassCount.store(0, std::memory_order_relaxed);
	diagnostics.lastPassEnum.store(0, std::memory_order_relaxed);
	diagnostics.lastSceneLightIndex.store(0, std::memory_order_relaxed);
	diagnostics.lastSceneLightCount.store(0, std::memory_order_relaxed);
}

void LightEditor::ObserveWaterPass(RE::BSRenderPass* pass)
{
	auto& diagnostics = waterRoutingDiagnostics;
	auto* target = diagnostics.target.load(std::memory_order_acquire);
	if (!target)
		return;

	diagnostics.waterPassCount.fetch_add(1, std::memory_order_relaxed);
	if (!pass || !pass->sceneLights)
		return;

	const uint32_t sceneLightCount = pass->numLights;
	for (uint32_t sceneLightIndex = 0; sceneLightIndex < sceneLightCount; ++sceneLightIndex) {
		if (pass->sceneLights[sceneLightIndex] != target)
			continue;
		if (diagnostics.target.load(std::memory_order_acquire) != target)
			return;

		diagnostics.lastPassEnum.store(pass->passEnum, std::memory_order_relaxed);
		diagnostics.lastSceneLightIndex.store(sceneLightIndex, std::memory_order_relaxed);
		diagnostics.lastSceneLightCount.store(sceneLightCount, std::memory_order_relaxed);
		const auto previousMatchCount = diagnostics.matchedWaterPassCount.fetch_add(1, std::memory_order_release);
		if (previousMatchCount == 0) {
			logger::debug(
				"[LightEditor] Selected BSLight 0x{:X} observed in Skyrim water pass "
				"(pass=0x{:X}, sceneSlot={}, sceneLights={})",
				reinterpret_cast<std::uintptr_t>(target),
				pass->passEnum,
				sceneLightIndex,
				sceneLightCount);
		}
		return;
	}
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (!enabled || niLight != activeNiLight.get())
		return false;

	runtimeData->diffuse = current.data.diffuse;
	runtimeData->fade = current.data.fade;
	runtimeData->cutoffOverride = current.data.cutoffOverride;
	runtimeData->size = current.data.size;

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);

	if (current.data.flags.any(LightLimitFix::LightFlags::Linear))
		runtimeData->flags.set(LightLimitFix::LightFlags::Linear);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::Linear);

	return true;
}

void LightEditor::RestoreOriginal()
{
	ClearWaterRoutingDiagnostics();
	activeBSLight.reset();

	if (!activeNiLight)
		return;

	auto* runtimeData = ISLCommon::RuntimeLightDataExt::Get(activeNiLight.get());
	*runtimeData = original.data;

	if (activeIsRef && activeRefr) {
		activeRefr->SetPosition(original.pos);
	} else if (activeNiLight->parent) {
		activeNiLight->parent->local.translate = original.pos;
		RE::NiUpdateData updateData;
		activeNiLight->parent->Update(updateData);
	}

	if (activeLigh && activeRefr && current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		activeRefr->Disable();
		activeRefr->Enable(false);
	}

	activeNiLight.reset();
	activeRefr = nullptr;
	activeLigh = nullptr;
	activeIsRef = false;
}

LightEditor::LPLightInfo LightEditor::ParseLPLightName(const std::string& name)
{
	LPLightInfo info;

	constexpr std::string_view prefix = "LP_Light[";
	if (!name.starts_with(prefix))
		return info;

	auto bracketEnd = name.find(']');
	if (bracketEnd == std::string::npos)
		return info;

	auto inner = name.substr(prefix.size(), bracketEnd - prefix.size());
	auto pipePos = inner.find('|');
	if (pipePos == std::string::npos)
		return info;

	info.configPath = inner.substr(0, pipePos);
	info.lightEDID = inner.substr(pipePos + 1);

	if (info.configPath.find("..") != std::string::npos) {
		logger::warn("[LightEditor] Rejected LP light name with path traversal: {}", name);
		return info;
	}

	info.isLPLight = true;
	return info;
}

std::string LightEditor::UpdateLPFlags(const std::string& existingFlags, bool inverseSquare, bool linear)
{
	std::vector<std::string> flags;
	if (!existingFlags.empty()) {
		std::istringstream ss(existingFlags);
		std::string flag;
		while (std::getline(ss, flag, '|')) {
			if (flag != "InverseSquare" && flag != "Linear")
				flags.push_back(flag);
		}
	}
	if (inverseSquare)
		flags.push_back("InverseSquare");
	if (linear)
		flags.push_back("Linear");

	std::string result;
	for (size_t i = 0; i < flags.size(); ++i) {
		if (i > 0)
			result += "|";
		result += flags[i];
	}
	return result;
}

bool LightEditor::MatchesLPFilters(const json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	auto resolveFilterEntry = [](const std::string& entry) -> RE::FormID {
		auto tildePos = entry.find('~');
		if (tildePos == std::string::npos || !entry.starts_with("0x"))
			return 0;
		RE::FormID relativeID;
		try {
			relativeID = static_cast<RE::FormID>(std::stoul(entry.substr(2, tildePos - 2), nullptr, 16));
		} catch (...) {
			return 0;
		}
		std::string plugin = entry.substr(tildePos + 1);
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler)
			return 0;
		auto* form = dataHandler->LookupForm(relativeID, plugin);
		return form ? form->GetFormID() : 0;
	};

	auto matchesEntry = [&](const std::string& entry) -> bool {
		if (entry.find('~') != std::string::npos) {
			RE::FormID resolvedId = resolveFilterEntry(entry);
			return resolvedId != 0 && resolvedId == refr->GetFormID();
		}
		if (auto* cell = refr->GetParentCell())
			if (entry == cell->GetFormEditorID())
				return true;
		if (auto* worldspace = refr->GetWorldspace()) {
			auto wsEdid = clib_util::editorID::get_editorID(worldspace);
			if (entry == wsEdid)
				return true;
		}
		return false;
	};

	auto getArray = [&](const char* key) -> const json* {
		auto it = lightEntry.find(key);
		return (it != lightEntry.end() && it->is_array()) ? &*it : nullptr;
	};

	auto anyMatches = [&](const json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	if (auto* wl = getArray("whiteList"); wl && !anyMatches(*wl))
		return false;
	if (auto* bl = getArray("blackList"); bl && anyMatches(*bl))
		return false;

	return true;
}

std::array<float, 3> LightEditor::GetJsonVec3(const json& data, const char* key)
{
	auto it = data.find(key);
	if (it != data.end() && it->is_array() && it->size() >= 3 && (*it)[0].is_number() && (*it)[1].is_number() && (*it)[2].is_number())
		return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
	return { 0.f, 0.f, 0.f };
}

bool LightEditor::SaveToLightPlacer()
{
	if (!lpInfo.isLPLight)
		return false;

	std::filesystem::path filePath = std::filesystem::path("Data\\LightPlacer") / (lpInfo.configPath + ".json");
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}

	json configArray;
	{
		std::ifstream inFile(filePath);
		if (!inFile.is_open()) {
			logger::warn("[LightEditor] Failed to open Light Placer config: {}", filePath.string());
			return false;
		}
		try {
			inFile >> configArray;
		} catch (const json::parse_error& e) {
			logger::warn("[LightEditor] Failed to parse Light Placer config: {} - {}", filePath.string(), e.what());
			return false;
		}
	}

	if (!configArray.is_array())
		return false;

	bool found = false;

	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};

	auto arrayContainsString = [](const json& arr, const std::function<bool(const std::string&)>& pred) -> bool {
		for (const auto& elem : arr)
			if (elem.is_string() && pred(elem.get<std::string>()))
				return true;
		return false;
	};

	std::string normalizedOwner = normalizePath(lpInfo.ownerModelPath);

	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;

		auto getArray = [&](const char* key) -> const json* {
			auto it = entry.find(key);
			return (it != entry.end() && it->is_array()) ? &*it : nullptr;
		};

		bool entryMatches = false;
		if (auto* models = getArray("models"); !normalizedOwner.empty() && models)
			entryMatches = arrayContainsString(*models, [&](const std::string& s) { return normalizePath(s) == normalizedOwner; });
		if (!entryMatches)
			if (auto* formIDs = getArray("formIDs"); !lpInfo.ownerEditorId.empty() && formIDs)
				entryMatches = arrayContainsString(*formIDs, [&](const std::string& s) { return s == lpInfo.ownerEditorId; });

		if (!entryMatches)
			continue;

		for (auto& lightEntry : entry["lights"]) {
			if (!lightEntry.contains("data"))
				continue;
			auto& data = lightEntry["data"];
			if (!data.contains("light") || !data["light"].is_string())
				continue;

			std::string edid = data["light"].get<std::string>();
			if (edid != lpInfo.lightEDID)
				continue;

			if (!MatchesLPFilters(lightEntry, activeRefr))
				continue;

			data["color"] = { current.data.diffuse.red, current.data.diffuse.green, current.data.diffuse.blue };
			data["fade"] = current.data.fade;
			data["radius"] = current.data.radius;
			data["cutoff"] = current.data.cutoffOverride;
			data["size"] = current.data.size;

			auto offset = GetJsonVec3(data, "offset");
			data["offset"] = {
				offset[0] + current.pos.x,
				offset[1] + current.pos.y,
				offset[2] + current.pos.z
			};

			std::string existingFlags = data.value("flags", std::string{});
			bool isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
			bool isLinear = current.data.flags.any(LightLimitFix::LightFlags::Linear);
			std::string newFlags = UpdateLPFlags(existingFlags, isInvSq, isLinear);
			if (!newFlags.empty())
				data["flags"] = newFlags;
			else
				data.erase("flags");

			found = true;
			break;
		}
		if (found)
			break;
	}

	if (!found) {
		logger::warn("[LightEditor] No matching entry found for model '{}' with light EDID '{}' in {}", lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
		return false;
	}

	{
		std::ofstream outFile(filePath);
		if (!outFile.is_open()) {
			logger::warn("[LightEditor] Failed to write Light Placer config: {}", filePath.string());
			return false;
		}
		outFile << configArray.dump(1, '\t');
		outFile.flush();
		if (outFile.fail()) {
			logger::warn("[LightEditor] Failed to write Light Placer config to {}: stream error", filePath.string());
			return false;
		}
	}

	original.pos = original.pos + current.pos;
	current.pos = { 0, 0, 0 };

	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

void LightEditor::SortLights()
{
	if (filterOption == FilterOption::OtherLights && (sortOption == SortOption::FormID || sortOption == SortOption::EditorID))
		sortOption = SortOption::None;

	switch (sortOption) {
	case SortOption::Distance:
		{
			const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
			std::ranges::sort(lights, [&](const LightInfo& a, const LightInfo& b) {
				if (a.hasPosition != b.hasPosition)
					return a.hasPosition;
				return a.position.GetSquaredDistance(playerPos) < b.position.GetSquaredDistance(playerPos);
			});
			break;
		}
	case SortOption::FormID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return (a.id * 10 + a.index) < (b.id * 10 + b.index);
		});
		break;
	case SortOption::EditorID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return a.name < b.name;
		});
		break;
	case SortOption::None:
	default:
		break;
	}
}
