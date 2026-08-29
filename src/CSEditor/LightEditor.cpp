#include "LightEditor.h"
#include "EditorWindow.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LightLimitFix.h"
#include "Globals.h"
#include "Menu.h"
#include "RE/B/BSLight.h"
#include "RE/B/BSShadowLight.h"
#include "RE/E/ExtraEmittanceSource.h"
#include "RE/T/TESRegion.h"
#include "Shadercache.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/PointLightFlags.h"
#include "Utils/UI.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <tuple>
#include <unordered_set>

std::vector<std::pair<std::string, RE::TESObjectLIGH*>> LightEditor::s_lighFormList;
std::vector<std::pair<std::string, RE::TESForm*>> LightEditor::s_emittanceFormList;

/** @brief Returns the named array member of a JSON object, or nullptr if missing or not an array. */
static const nlohmann::ordered_json* GetArrayMember(const nlohmann::ordered_json& obj, const char* key)
{
	const auto it = obj.find(key);
	return (it != obj.end() && it->is_array()) ? &*it : nullptr;
}

static bool HasWellFormedLPFilters(const nlohmann::ordered_json& lightEntry)
{
	for (const char* key : { "whiteList", "blackList" }) {
		const auto it = lightEntry.find(key);
		if (it != lightEntry.end() && !it->is_array())
			return false;
	}
	return true;
}

/** @brief Returns a light entry's "data"/"light" EDID string, or nullptr if absent or not a string. */
static const std::string* GetLightEntryEdid(const nlohmann::ordered_json& lightEntry)
{
	const auto dataIt = lightEntry.find("data");
	if (dataIt == lightEntry.end() || !dataIt->is_object())
		return nullptr;
	const auto lightIt = dataIt->find("light");
	if (lightIt == dataIt->end() || !lightIt->is_string())
		return nullptr;
	return &lightIt->get_ref<const std::string&>();
}

/** @brief True if the JSON array holds the given string value. */
static bool ArrayContainsString(const nlohmann::ordered_json& arr, std::string_view value)
{
	for (const auto& elem : arr)
		if (elem.is_string() && std::string_view(elem.get_ref<const std::string&>()) == value)
			return true;
	return false;
}

/** @brief True if a string carries a "0x"/"0X" hex prefix. */
static bool HasHexPrefix(std::string_view s)
{
	return s.starts_with("0x") || s.starts_with("0X");
}

static bool TryReadFiniteFloat(
	const nlohmann::ordered_json& data,
	const char* key,
	float fallback,
	float minimum,
	float maximum,
	float& value)
{
	const auto it = data.find(key);
	if (it != data.end() && !it->is_number())
		return false;

	double number = fallback;
	try {
		if (it != data.end())
			number = it->get<double>();
	} catch (const nlohmann::json::exception&) {
		return false;
	}
	if (!std::isfinite(number) ||
		number < static_cast<double>(minimum) ||
		number > static_cast<double>(maximum)) {
		return false;
	}

	value = static_cast<float>(number);
	return true;
}

/** @brief Downcasts a BSLight to BSShadowLight only when it actually is one, else nullptr. */
static RE::BSShadowLight* AsShadowLight(RE::BSLight* light)
{
	return (light && light->IsShadowLight()) ? static_cast<RE::BSShadowLight*>(light) : nullptr;
}

/** @brief Schedules a console command on the task thread, optionally against a selected reference. */
static void ScheduleConsoleCommand(std::string cmd, RE::TESObjectREFR* refr = nullptr)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		const RE::ObjectRefHandle targetHandle = refr ? RE::ObjectRefHandle(refr) : RE::ObjectRefHandle{};
		taskInterface->AddTask([cmd = std::move(cmd), targetHandle]() {
			auto target = targetHandle.get();
			auto* targetRef = target.get();
			if (targetHandle && !targetRef)
				return;

			const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			if (auto* script = factory ? static_cast<RE::Script*>(factory->Create()) : nullptr) {
				script->SetCommand(cmd);
				script->CompileAndRun(targetRef);
				delete script;
			}
		});
	}
}

/** @brief Atomically writes an ordered LP config without rewriting unrelated values or key order. */
static bool WriteLPConfig(const std::filesystem::path& filePath, const nlohmann::ordered_json& config)
{
	if (filePath.empty()) {
		logger::warn("[LightEditor] Refusing to write an invalid Light Placer config path");
		return false;
	}
	std::string output;
	try {
		output = config.dump(1, '\t');
	} catch (const std::exception& e) {
		logger::warn("[LightEditor] Failed to serialize Light Placer config {}: {}", filePath.string(), e.what());
		return false;
	}

	std::string writeError;
	if (!Util::FileHelpers::WriteTextFileAtomic(filePath, output, writeError)) {
		logger::warn("[LightEditor] Failed to write Light Placer config {}: {}", filePath.string(), writeError);
		return false;
	}
	return true;
}

static bool IsPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
	auto candidateIt = candidate.begin();
	for (auto rootIt = root.begin(); rootIt != root.end(); ++rootIt, ++candidateIt) {
		if (candidateIt == candidate.end() || _wcsicmp(rootIt->c_str(), candidateIt->c_str()) != 0)
			return false;
	}
	return true;
}

/** @brief Resolves an extension-less LP path and confines it below Data/LightPlacer. */
static std::filesystem::path LPConfigFilePath(const std::string& configPath)
{
	if (configPath.empty() || configPath.find(':') != std::string::npos)
		return {};

	std::filesystem::path relative(configPath);
	if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
		return {};
	for (const auto& component : relative) {
		const auto part = component.string();
		if (part.empty() || part == "." || part == ".." || part.ends_with(' ') || part.ends_with('.') ||
			part.find_first_of("<>:\"|?*") != std::string::npos)
			return {};
	}
	if (_wcsicmp(relative.extension().c_str(), L".json") != 0)
		relative += L".json";

	std::error_code ec;
	const auto root = std::filesystem::weakly_canonical(std::filesystem::path("Data") / "LightPlacer", ec);
	if (ec)
		return {};
	const auto candidate = std::filesystem::weakly_canonical(root / relative, ec);
	return !ec && IsPathWithin(root, candidate) ? candidate : std::filesystem::path{};
}

/**
 * @brief Loads a Light Placer config array into `out`.
 * @return False if the file is missing, fails to parse, or is not a JSON array.
 */
static bool LoadConfigArray(const std::string& configPath, nlohmann::ordered_json& out)
{
	const auto filePath = LPConfigFilePath(configPath);
	if (filePath.empty()) {
		logger::warn("[LightEditor] Rejected invalid Light Placer config path: {}", configPath);
		return false;
	}

	std::error_code existsError;
	const bool exists = std::filesystem::exists(filePath, existsError);
	if (existsError) {
		logger::warn("[LightEditor] Could not inspect Light Placer config {}: {}", filePath.string(), existsError.message());
		return false;
	}
	if (!exists) {
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}

	std::ifstream in(filePath, std::ios::binary);
	if (!in.is_open()) {
		logger::warn("[LightEditor] Could not open Light Placer config for reading: {}", filePath.string());
		return false;
	}

	nlohmann::ordered_json parsed;
	try {
		in >> parsed;
		if (in.bad()) {
			logger::warn("[LightEditor] I/O error while reading Light Placer config: {}", filePath.string());
			return false;
		}
	} catch (const std::exception& e) {
		logger::warn("[LightEditor] Failed to read or parse {}: {}", filePath.string(), e.what());
		return false;
	}
	if (!parsed.is_array()) {
		logger::warn("[LightEditor] Light Placer config root is not an array: {}", filePath.string());
		return false;
	}

	out = std::move(parsed);
	return true;
}

/** @brief Lower-cases and forward-slashes a model path so casing/separator variants compare equal. */
static std::string NormalizeModelPath(std::string path)
{
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}

/** @brief Builds the default LP light object for a freshly added bulb. */
static nlohmann::ordered_json MakeLightObject(const std::string& lighEdid)
{
	nlohmann::ordered_json data;
	data["light"] = lighEdid;
	data["fade"] = 1;
	data["radius"] = 1;
	data["flags"] = "";

	nlohmann::ordered_json light;
	light["data"] = std::move(data);
	light["points"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::array({ 0, 0, 1 }) });
	return light;
}

/** @brief Updates position only when the entry identifies one deterministic point/node. */
static bool SetUniquePointFromPos(nlohmann::ordered_json& lightEntry, const RE::NiPoint3& pos)
{
	const bool hasPoints = lightEntry.contains("points");
	const bool hasNodes = lightEntry.contains("nodes");
	if (!hasPoints && !hasNodes)
		return true;
	if (hasPoints && hasNodes)
		return false;

	auto& points = lightEntry[hasPoints ? "points" : "nodes"];
	if (!points.is_array() || points.size() != 1 || !points[0].is_array() || points[0].size() < 3)
		return false;
	points[0] = nlohmann::ordered_json::array({ static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z) });
	return true;
}

/** @brief True if the entry's "lights" array already contains a light with the given EDID. */
static bool EntryContainsLight(const nlohmann::ordered_json& entry, const std::string& lighEdid)
{
	if (auto* lights = GetArrayMember(entry, "lights"))
		for (const auto& le : *lights)
			if (const auto* edid = GetLightEntryEdid(le); edid && *edid == lighEdid)
				return true;
	return false;
}

void LightEditor::EnsureEmittanceFormListBuilt()
{
	if (!s_emittanceFormList.empty())
		return;
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh)
		return;
	auto containsCaseInsensitive = [](const std::string& str, std::string_view needle) {
		return std::ranges::search(str, needle, [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		}).begin() != str.end();
	};

	auto addForms = [&](auto& formArray, RE::FormType expectedType) {
		for (auto* form : formArray) {
			if (!form || form->formID == 0 || form->GetFormType() != expectedType)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (edid.empty())
				continue;
			if (!containsCaseInsensitive(edid, "fx") && !containsCaseInsensitive(edid, "weather"))
				continue;
			s_emittanceFormList.emplace_back(std::move(edid), static_cast<RE::TESForm*>(form));
		}
	};
	addForms(dh->GetFormArray<RE::TESRegion>(), RE::FormType::Region);
	std::ranges::sort(s_emittanceFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
}

void LightEditor::ApplyExternalEmittance(RE::TESForm* source)
{
	// Selected-bulb-only preview; deliberately does NOT write the ref's ExtraEmittanceSource (that makes
	// the engine drive every touched bulb even when unselected). Persistence is via Save to Light Placer.
	activeEmittanceSource = source;
	externalEmittanceChanged = true;

	// An explicit source is incompatible with NoExternalEmittance, so drop the flag (else the saved
	// entry and reloadlp would suppress the source).
	if (lpInfo.isLPLight && source)
		lpFlagSet.erase("NoExternalEmittance");
}

void LightEditor::ClearExternalEmittance()
{
	externalEmittanceEdid = {};
	useExternalEmittance = false;
	activeEmittanceSource = nullptr;
	emittanceColorActive = false;
	externalEmittanceChanged = true;
}

void LightEditor::QueueReselectCurrentLP()
{
	// reloadlp recreates LP bulbs, shifting per-iteration indices so the (id, index) match fails.
	// Re-acquire by stable identity (owner ref + config + light EDID) via the pendingAutoSelect path.
	if (!lpInfo.isLPLight)
		return;
	pendingSelectRefrId = selected.id;
	pendingSelectConfigPath = lpInfo.configPath;
	pendingSelectLighEdid = lpInfo.lightEDID;
	pendingAutoSelect = true;
	pendingAutoSelectTTL = 10;
}

void LightEditor::UpdateEmittanceColor()
{
	// Only regions carry a live time/weather-driven emittanceColor; anything else leaves the lerp
	// inactive so ApplyOverrides uses the base color.
	auto* region = activeEmittanceSource ? activeEmittanceSource->As<RE::TESRegion>() : nullptr;
	if (!region) {
		emittanceColorActive = false;
		return;
	}

	// The game already varies emittanceColor smoothly with time/weather, so track it directly;
	// smoothing would only add lag when scrubbing the time slider.
	emittanceColor = region->emittanceColor;
	emittanceColorActive = true;
}

// Picking a form sets the reference's runtime emittance source live; "(None)" removes it. LP bulbs
// additionally persist the choice via Save to Light Placer.
void LightEditor::DrawExternalEmittanceCombo()
{
	if (!activeRefr)
		return;

	EnsureEmittanceFormListBuilt();

	static constexpr const char* kEmittanceComboId = "EmittanceFormCombo";
	const char* kNoneLabel = "(None)";
	const char* preview = externalEmittanceEdid.empty() ? kNoneLabel : externalEmittanceEdid.c_str();
	const auto externalEmittanceLabel = fmt::format("{}##combo", "External Emittance");
	if (ImGui::BeginCombo(externalEmittanceLabel.c_str(), preview)) {
		auto searchText = Util::DrawComboSearchInput(kEmittanceComboId);
		if (searchText.empty() || Util::StringMatchesSearch(kNoneLabel, searchText)) {
			if (ImGui::Selectable(kNoneLabel, externalEmittanceEdid.empty())) {
				ClearExternalEmittance();
				Util::ClearComboSearch(kEmittanceComboId);
			}
			if (externalEmittanceEdid.empty())
				ImGui::SetItemDefaultFocus();
		}
		for (auto& [edid, form] : s_emittanceFormList) {
			if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
				continue;
			const bool isCurrent = edid == externalEmittanceEdid;
			if (ImGui::Selectable(edid.c_str(), isCurrent)) {
				externalEmittanceEdid = edid;
				useExternalEmittance = true;
				ApplyExternalEmittance(form);
				Util::ClearComboSearch(kEmittanceComboId);
			}
			if (isCurrent)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kEmittanceComboId);
	}
}

RE::FormID LightEditor::ResolveFormEntry(const std::string& entry)
{
	const auto tildePos = entry.find('~');
	const bool hasPrefix = HasHexPrefix(entry);
	if (tildePos == std::string::npos && !hasPrefix)
		return 0;

	const std::size_t hexStart = hasPrefix ? 2 : 0;
	const std::size_t hexEnd = tildePos == std::string::npos ? entry.size() : tildePos;
	if (hexEnd <= hexStart || (tildePos != std::string::npos && tildePos + 1 >= entry.size()))
		return 0;

	const std::string_view formIDText(entry.data() + hexStart, hexEnd - hexStart);
	RE::FormID relativeID = 0;
	const auto [end, error] = std::from_chars(
		formIDText.data(), formIDText.data() + formIDText.size(), relativeID, 16);
	if (error != std::errc{} || end != formIDText.data() + formIDText.size())
		return 0;
	if (tildePos == std::string::npos)
		return relativeID;

	auto* dh = RE::TESDataHandler::GetSingleton();
	auto* form = dh ? dh->LookupForm(relativeID, entry.substr(tildePos + 1)) : nullptr;
	return form ? form->GetFormID() : 0;
}

void LightEditor::ApplyLighFormData(const RE::TESObjectLIGH* ligh)
{
	current.data.lighFormId = ligh->formID;

	current.data.flags.reset(LightLimitFix::LightFlags::InverseSquare);
	current.data.flags.reset(LightLimitFix::LightFlags::Linear);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		current.data.flags.set(LightLimitFix::LightFlags::InverseSquare);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kLinear)))
		current.data.flags.set(LightLimitFix::LightFlags::Linear);

	const float size = ligh->data.fov >= 50.f ? std::numbers::sqrt2_v<float> : ligh->data.fov;
	current.data.size = std::clamp(size, 0.01f, 50.f);
	current.data.cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	current.data.radius = static_cast<float>(ligh->data.radius);
	current.data.fade = ligh->fade;
	current.data.diffuse.red = ligh->data.color.red / 255.f;
	current.data.diffuse.green = ligh->data.color.green / 255.f;
	current.data.diffuse.blue = ligh->data.color.blue / 255.f;
}

void LightEditor::EnsureLighFormListBuilt()
{
	if (!s_lighFormList.empty())
		return;
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto* form : dh->GetFormArray<RE::TESObjectLIGH>()) {
			if (!form || form->formID == 0)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (!edid.empty())
				s_lighFormList.emplace_back(std::move(edid), form);
		}
		std::ranges::sort(s_lighFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
	}
}

const std::string* LightEditor::LighEdidPtrForFormId(RE::FormID formId)
{
	for (auto& [edid, ligh] : s_lighFormList)
		if (ligh->GetFormID() == formId)
			return &edid;
	return nullptr;
}

std::string LightEditor::LighEdidForFormId(RE::FormID formId)
{
	const auto* edid = LighEdidPtrForFormId(formId);
	return edid ? *edid : std::string{};
}

void LightEditor::DrawSettings()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked()) {
		ImGui::TextDisabled("Light Editor is unavailable while persistent mutations are blocked.");
		return;
	}

	bool requestedEnabled = enabled;
	if (ImGui::Checkbox("Enable Light Editor", &requestedEnabled))
		SetEnabled(requestedEnabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s",
			"Allows for modifying lights in real-time to preview changes. "
			"Light Placer lights can be saved back to their JSON configs. "
			"Not intended for gameplay use.");
	}

	if (!enabled)
		return;

	ImGui::Spacing();
	ImGui::Text("%s", "Light Editor");
	ImGui::Separator();

	const bool isAttaching = (attachPhase != AttachPhase::Idle);
	if (isAttaching) {
		ImGui::TextColored(Util::Colors::GetInfo(), "%s", "Attaching light, please wait...");
		ImGui::Separator();
		ImGui::BeginDisabled();
	}

	ImGui::Checkbox("Disable Regular Falloff Lights", &disableRegularLights);
	ImGui::Checkbox("Disable Inverse Square Falloff Lights", &disableInvSqLights);

	if (ImGui::Button("Toggle All LP Lights")) {
		ScheduleConsoleCommand("tlp 0");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", "Toggle all Light Placer lights on/off (tlp 0).");
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle LP Markers")) {
		ScheduleConsoleCommand("tlp 1");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", "Toggle Light Placer debug markers (tlp 1).");
	}

	ImGui::SameLine();
	if (ImGui::Button("Reload LP")) {
		QueueReselectCurrentLP();  // capture before RestoreOriginal clears lpInfo/activeRefr
		RestoreOriginal();
		previous = {};
		waitFrames = 3;
		ScheduleConsoleCommand("reloadlp");
		EditorWindow::GetSingleton()->ShowNotification("Reloading Light Placer configs...", Util::Colors::GetInfo());
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", "Reload all Light Placer JSON configs in-game (reloadlp).");
	}

	ImGui::SameLine();
	DrawAddLightButton();

	if (picker.IsPicking()) {
		ImGui::TextColored(Util::Colors::GetInfo(), "%s", "Click a mesh to attach a light... (right-click / ESC to cancel)");
	}

	DrawAddLightPopup();

	ImGui::Separator();

	ImGui::Text("Total Lights: %u", totalLightCount);
	ImGui::Text("Active Shadow Lights: %u", activeShadowLightCount);
	ImGui::Separator();

	{
		const auto& style = ImGui::GetStyle();
		const float arrowWidth = ImGui::GetFrameHeight();

		const char* filterLabels[] = {
			"Ref Lights",
			"Attached Lights",
			"Other Lights"
		};
		const char* sortLabels[] = {
			"None",
			"Distance",
			"FormID",
			"EditorID"
		};

		const float filterComboWidth = ImGui::CalcTextSize(filterLabels[static_cast<int>(FilterOption::AttachedLights)]).x + style.FramePadding.x * 2 + arrowWidth;
		const float sortComboWidth = ImGui::CalcTextSize(sortLabels[static_cast<int>(SortOption::EditorID)]).x + style.FramePadding.x * 2 + arrowWidth;

		ImGui::SetNextItemWidth(filterComboWidth);
		int selectedFilter = static_cast<int>(filterOption);
		if (ImGui::Combo("##Type", &selectedFilter, filterLabels, static_cast<int>(FilterOption::Count))) {
			filterOption = static_cast<FilterOption>(selectedFilter);
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(sortComboWidth);
		int selectedSort = static_cast<int>(sortOption);
		if (ImGui::Combo("##Sorting", &selectedSort, sortLabels, static_cast<int>(SortOption::Count))) {
			sortOption = static_cast<SortOption>(selectedSort);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Shadows Only", &shadowsOnly);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", "Only show lights with HemiShadow or OmniShadow flags.");
		}
	}

	static constexpr const char* kLightsComboId = "LightsCombo";
	LightInfo thisFrameHovered = {};
	bool anyItemHovered = false;  // mouse over any combo entry this frame (flashable or not)
	const bool lightsComboOpen = ImGui::BeginCombo("Lights", selected.isSelected ? GetLightName(selected).c_str() : "Select a light");
	if (lightsComboOpen) {
		auto searchText = Util::DrawComboSearchInput(kLightsComboId);
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			if (!searchText.empty() && !Util::StringMatchesSearch(displayName, searchText))
				continue;
			const bool isSelected = light == selected;
			if (ImGui::Selectable(displayName.c_str(), isSelected)) {
				selected = light;
				Util::ClearComboSearch(kLightsComboId);
			}
			// Flash only a flashable target: a ref/attached light (id != 0) other than the selected one
			// (whose fade ApplyOverrides drives). Selected/Other hover leaves thisFrameHovered empty, clearing it.
			if (ImGui::IsItemHovered()) {
				anyItemHovered = true;
				if (!isSelected && light.id != 0)
					thisFrameHovered = light;
			}
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kLightsComboId);
	}

	// Re-evaluate the hover flash whenever the mouse is over an entry or the combo closed. Moving onto
	// a non-flashable target (empty thisFrameHovered) stops the blink; dead-space hover keeps it going.
	if (anyItemHovered || !lightsComboOpen) {
		if (!(thisFrameHovered == comboHoveredLight)) {
			if (hoverFlashNiLight) {
				if (auto* rd = ISLCommon::RuntimeLightDataExt::Get(hoverFlashNiLight.get()))
					rd->fade = hoverFlashOriginalFade;
				hoverFlashNiLight.reset();
			}
			comboHoveredLight = thisFrameHovered;
			hoverFlashVisible = true;
			hoverFlashLastToggle = ImGui::GetTime();
		}
	}
	if (comboHoveredLight.id != 0) {
		const double now = ImGui::GetTime();
		if (now - hoverFlashLastToggle >= 0.25) {
			hoverFlashVisible = !hoverFlashVisible;
			hoverFlashLastToggle = now;
		}
	}

	ImGui::Separator();

	if (!selected.isSelected) {
		if (isAttaching)
			ImGui::EndDisabled();
		return;
	}

	if (selected.isRef || selected.isAttached) {
		ImGui::Text("Owner: 0x%08X | %s", selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text("Owner last edited by: %s", displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text("Base Object: 0x%08X | %s", displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text("LIGH: 0x%08X | %s", displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text("Cell: 0x%08X | %s", displayInfo.cellFormId, displayInfo.cellEditorId.c_str());
		if (lpInfo.isLPLight)
			ImGui::Text("Config: Data\\LightPlacer\\%s.json", lpInfo.configPath.c_str());
	} else {
		ImGui::Text("Memory Address: %p", selected.ptr);
		ImGui::Text("NiLight Name: %s", selected.name.c_str());
	}

	ImGui::Spacing();

	ImGui::Spacing();
	ImGui::Separator();

	if (ImGui::Button("Reset")) {
		current = original;
		if (selected.isRef)
			current.pos = {};
		if (lpInfo.isLPLight) {
			lpFlagSet = originalLpFlagSet;
			SyncLPFlagsToRuntime();
		}
		activeEmittanceSource = originalEmittanceSource;
		externalEmittanceEdid = originalExternalEmittanceEdid;
		useExternalEmittance = originalUseExternalEmittance;
		externalEmittanceChanged = false;
		shadowDepthBias = originalShadowDepthBias;
		ApplyShadowDepthBias();
		waitFrames = 1;
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle Light")) {
		const auto activeRef = GetActiveRefr();
		if (lpInfo.isLPLight && activeRef)
			ScheduleConsoleCommand("tlp 0", activeRef.get());
		else if (current.data.fade == 0.0f)
			current.data.fade = cachedFadeBeforeToggle;
		else {
			cachedFadeBeforeToggle = current.data.fade;
			current.data.fade = 0.0f;
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (lpInfo.isLPLight)
			ImGui::Text("%s", "Toggle this reference's LP-placed lights on/off (tlp 0).");
		else
			ImGui::Text("%s", "Toggle this light on/off.");
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		{
			auto _style = Util::StatusButtonStyle(lpMatchFound ? Util::Colors::GetSuccess() : Util::Colors::GetError());
			if (!lpRawDataLoaded)
				ImGui::BeginDisabled();
			if (ImGui::Button("Save to Light Placer")) {
				const bool ok = SaveToLightPlacer(saveColorToLP);
				if (ok) {
					ReloadLPAndReselect();
					lpMatchFound = true;
				}
				const std::string okMsg = fmt::format("Saved to {}", lpInfo.configPath);
				NotifyResult(ok, okMsg.c_str(), "Save failed \xe2\x80\x94 see log");
			}
			if (!lpRawDataLoaded)
				ImGui::EndDisabled();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (!lpRawDataLoaded)
				ImGui::Text("%s", "Cannot save because the source LIGH record could not be resolved.");
			else if (lpMatchFound)
				ImGui::Text("Matching entry found in %s.\nSave current settings to the Light Placer JSON.", lpInfo.configPath.c_str());
			else
				ImGui::Text("No matching entry found in %s.\nSaving will fail.", lpInfo.configPath.c_str());
		}

		ImGui::SameLine();
		if (Util::ErrorButton("Delete"))
			deleteConfirmPopupRequested = true;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", "Delete this light entry from the Light Placer JSON.\nIf it is the only light in its entry, the whole models/formIDs entry is removed too.");
		}
		DrawDeleteConfirmation();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Log Mode", &extendedLogMode);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", "Extend slider ranges and use a logarithmic scale.");
	}

	if (lpInfo.isLPLight) {
		auto doFilterButton = [&](bool isWhiteList) {
			bool& inList = isWhiteList ? lpInWhitelist : lpInBlacklist;
			const char* addLabel = isWhiteList ? "Add to Whitelist" : "Add to Blacklist";
			const char* removeLabel = isWhiteList ? "Remove from Whitelist" : "Remove from Blacklist";
			const ImVec4 activeColor = isWhiteList ? Util::Colors::GetSuccess() : Util::Colors::GetError();

			bool clicked = false;
			if (inList) {
				auto _style = Util::StatusButtonStyle(activeColor);
				clicked = ImGui::Button(removeLabel);
			} else {
				clicked = ImGui::Button(addLabel);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				const auto activeRef = GetActiveRefr();
				ImGui::Text("%s\nFormat: %s\nReload LP to apply.", inList ? removeLabel : addLabel,
					FormatOwnerFormEntry(activeRef.get()).c_str());
			}
			if (clicked) {
				if (ModifyLPFilterList(isWhiteList, !inList)) {
					inList = !inList;
					const char* msg = inList ? (isWhiteList ? "Added to whitelist" : "Added to blacklist") : (isWhiteList ? "Removed from whitelist" : "Removed from blacklist");
					EditorWindow::GetSingleton()->ShowNotification(msg, Util::Colors::GetInfo());
				} else {
					EditorWindow::GetSingleton()->ShowNotification("Filter update failed \xe2\x80\x94 see log", Util::Colors::GetError());
				}
			}
		};

		doFilterButton(true);
		ImGui::SameLine();
		doFilterButton(false);

		ImGui::SameLine();
		if (!lpRawDataLoaded)
			ImGui::BeginDisabled();
		if (ImGui::Button("Save as Separate Entry")) {
			const bool ok = SaveAsSeparateEntry(saveColorToLP);
			if (ok) {
				ReloadLPAndReselect();
				lpInBlacklist = true;
			}
			NotifyResult(ok,
				"Saved as separate entry",
				"Save failed \xe2\x80\x94 see log");
		}
		if (!lpRawDataLoaded)
			ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (!lpRawDataLoaded)
				ImGui::Text("%s", "Cannot save because the source LIGH record could not be resolved.");
			else {
				const auto activeRef = GetActiveRefr();
				ImGui::Text("Fork this bulb into a new whitelist entry for %s with the current edits, and blacklist it from the shared entry so the edits apply only to this reference.\nReload LP to apply.",
					FormatOwnerFormEntry(activeRef.get()).c_str());
			}
		}
	}

	ImGui::Spacing();

	if (selected.isAttached) {
		EnsureLighFormListBuilt();
		const char* kOriginalLabel = "(Original)";
		const char* previewEdid = kOriginalLabel;
		if (const auto* edid = LighEdidPtrForFormId(current.data.lighFormId))
			previewEdid = edid->c_str();

		static constexpr const char* kLighOverrideId = "LighFormOverride";
		const auto bulbTypeLabel = fmt::format("{}##combo", "Bulb type");
		if (ImGui::BeginCombo(bulbTypeLabel.c_str(), previewEdid)) {
			auto searchText = Util::DrawComboSearchInput(kLighOverrideId);
			if (searchText.empty() || Util::StringMatchesSearch(kOriginalLabel, searchText)) {
				if (ImGui::Selectable(kOriginalLabel, current.data.lighFormId == original.data.lighFormId)) {
					current.data = original.data;
					if (lpInfo.isLPLight)
						SyncLPFlagsToRuntime();
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (current.data.lighFormId == original.data.lighFormId)
					ImGui::SetItemDefaultFocus();
			}
			for (auto& [edid, ligh] : s_lighFormList) {
				if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
					continue;
				const bool isCurrent = ligh->GetFormID() == current.data.lighFormId;
				if (ImGui::Selectable(edid.c_str(), isCurrent)) {
					// Swap the LIGH form but keep the user's edited fade/radius/size/cutoff.
					const float savedFade = current.data.fade;
					const float savedRadius = current.data.radius;
					const float savedSize = current.data.size;
					const float savedCutoff = current.data.cutoffOverride;
					ApplyLighFormData(ligh);
					current.data.fade = savedFade;
					current.data.radius = savedRadius;
					current.data.size = savedSize;
					current.data.cutoffOverride = savedCutoff;
					if (lpInfo.isLPLight)
						SyncLPFlagsToRuntime();
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (isCurrent)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		} else {
			Util::ClearComboSearch(kLighOverrideId);
		}
	}

	// External emittance applies to any reference-backed bulb, so it lives outside the attached-only block.
	DrawExternalEmittanceCombo();

	ImGui::Spacing();

	WeatherUtils::DrawColorEdit("Color", reinterpret_cast<float3&>(current.data.diffuse));
	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		const auto saveColorLabel = fmt::format("{}##color", "Save");
		ImGui::Checkbox(saveColorLabel.c_str(), &saveColorToLP);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", "Include color when saving to Light Placer.\nWhen unchecked, the existing JSON color is preserved.");
		}
	}

	// Logarithmic+extended or normal slider per extendedLogMode. Log scale requires min > 0, so
	// extended mins are nudged above zero where needed.
	auto drawSlider = [&](const char* label, float& value,
						  float normalMin, float normalMax,
						  float extMin, float extMax,
						  const char* format) -> bool {
		if (extendedLogMode)
			return ImGui::SliderFloat(label, &value, extMin, extMax, format, ImGuiSliderFlags_Logarithmic);
		return static_cast<bool>(WeatherUtils::DrawSliderFloat(label, value, normalMin, normalMax, nullptr, format));
	};

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	// "Intensity" is only meaningful for Inverse Square bulbs; otherwise this value is the light's fade.
	const char* fadeLabel = isInvSq ? "Intensity" : "Fade";
	drawSlider(fadeLabel, current.data.fade, 0.01f, 16.f, 0.01f, 1024.f, "%.3f");

	if (isInvSq)
		ImGui::Text("Radius: %.0f", computedInverseSquareRadius);
	else
		drawSlider("Radius", current.data.radius, 2.f, 8096.f, 2.f, 65536.f, "%.0f");

	if (isInvSq) {
		drawSlider("Size", current.data.size, 0.01f, 10.0f, 0.01f, 50.0f, "%.3f");
		WeatherUtils::DrawSliderFloat("Cutoff", current.data.cutoffOverride, 0.01f, 1.f, nullptr, "%.3f");
	}

	if (HasShadowFlags(current.tesFlags.underlying())) {
		if (drawSlider("Shadow Depth Bias", shadowDepthBias, 0.0f, 50.0f, 0.01f, 50.f, "%.2f"))
			ApplyShadowDepthBias();
	}

	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		if (selected.isRef) {
			ImGui::Spacing();
			ImGui::SliderFloat3("Position Offset", &current.pos.x, -500.f, 500.f, "%.0f");
		} else if (lpInfo.isLPLight) {
			ImGui::Spacing();
			ImGui::SliderFloat3("Position", &current.pos.x, -1000.f, 1000.f, "%.0f");
		}

		ImGui::Spacing();

		auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
		auto* runtimeFlags = reinterpret_cast<uint32_t*>(&current.data.flags);

		if (lpInfo.isLPLight) {
			ImGui::Text("%s", "LP Flags");
			static constexpr const char* kLPFlagNames[] = {
				"NoExternalEmittance", "PortalStrict", "IgnoreScale",
				"InverseSquare", "Flicker", "Linear", "Shadow",
				"RandomAnimStart", "SyncAddonNodes", "UpdateOnCellTransition", "UpdateOnWaiting"
			};
			for (const char* flagName : kLPFlagNames) {
				const bool isInvSqEntry = (std::string_view(flagName) == "InverseSquare");
				const bool disabled = isInvSqEntry && selected.isSpotlight;
				if (disabled)
					ImGui::BeginDisabled();
				bool inSet = lpFlagSet.contains(flagName);
				if (ImGui::Checkbox(flagName, &inSet)) {
					if (inSet)
						lpFlagSet.insert(flagName);
					else
						lpFlagSet.erase(flagName);
					// NoExternalEmittance and an emittance source are mutually exclusive: enabling the
					// flag clears the source (combo shows None; the source line is dropped on save).
					if (inSet && std::string_view(flagName) == "NoExternalEmittance")
						ClearExternalEmittance();
					SyncLPFlagsToRuntime();
				}
				if (disabled)
					ImGui::EndDisabled();
			}
		}

		ImGui::Text("%s", "Light Flags");
		ImGui::BeginDisabled(lpInfo.isLPLight);

		if (!lpInfo.isLPLight) {
			// Inverse Square is disabled for spotlights since they have their own falloff model.
			ImGui::BeginDisabled(selected.isSpotlight);
			ImGui::CheckboxFlags("Inverse Square", runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
			ImGui::EndDisabled();
			ImGui::CheckboxFlags("Linear", runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));
		}

		// Dynamic and Negative are always shown; Flicker/OmniShadow/PortalStrict are hidden for LP lights.
		ImGui::CheckboxFlags("Dynamic", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
		ImGui::CheckboxFlags("Negative", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags("Flicker", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
		ImGui::CheckboxFlags("Flicker Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
		ImGui::CheckboxFlags("Pulse", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
		ImGui::CheckboxFlags("Pulse Slow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
		ImGui::CheckboxFlags("Hemi Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags("Omni Shadow", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags("Portal Strict", flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));

		ImGui::EndDisabled();
	}

	if (isAttaching)
		ImGui::EndDisabled();
}

void LightEditor::SetEnabled(bool value)
{
	if (value && State::GetSingleton()->IsPersistentMutationBlocked())
		return;

	if (!value) {
		CancelPick();
		deleteConfirmPopupRequested = false;
		ResetOverrides();
		savedSelection = {};
		FinishDeferredCleanup();
		enabled = false;
		return;
	}

	enabled = true;
}

void LightEditor::BeginPick()
{
	if (!enabled || State::GetSingleton()->IsPersistentMutationBlocked() || HasDeferredWork())
		return;
	auto* editorWindow = EditorWindow::GetSingleton();
	if (!editorWindow->IsEditorViewportVisible()) {
		editorWindow->ShowNotification("Enable the Editor Viewport before selecting a mesh.", Util::Colors::GetWarning());
		return;
	}
	picker.BeginPick();
}

void LightEditor::CancelPick()
{
	picker.Cancel();
}

bool LightEditor::ShouldDisableLight(bool inverseSquare) const
{
	return enabled && !State::GetSingleton()->IsPersistentMutationBlocked() &&
	       (inverseSquare ? disableInvSqLights : disableRegularLights);
}

bool LightEditor::HasDeferredWork() const
{
	return attachPhase != AttachPhase::Idle || pendingRefreshNeedsDisable || pendingRefreshFrames > 0;
}

static constexpr std::string_view kPopupPrefsPath =
	R"(Data\SKSE\Plugins\CommunityShaders\LightEditorPrefs.json)";

void LightEditor::SavePopupPrefs() const
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return;

	nlohmann::ordered_json j;
	j["addConfigSearch"] = addConfigSearch;
	j["addAttachMode"] = addAttachMode;
	j["addLighSearch"] = addLighSearch;
	j["addPopupMode"] = addPopupMode;
	j["addLightSubMode"] = addLightSubMode;
	std::string writeError;
	if (!Util::FileHelpers::WriteTextFileAtomic(std::filesystem::path(kPopupPrefsPath.data()), j.dump(1, '\t'), writeError))
		logger::warn("[LightEditor] Failed to save popup preferences: {}", writeError);
}

void LightEditor::LoadPopupPrefs()
{
	std::ifstream in(kPopupPrefsPath.data());
	if (!in.is_open())
		return;
	nlohmann::ordered_json j;
	try {
		in >> j;
	} catch (...) {
		return;
	}
	if (auto it = j.find("addConfigSearch"); it != j.end() && it->is_string()) {
		auto s = it->get<std::string>();
		std::strncpy(addConfigSearch, s.c_str(), sizeof(addConfigSearch) - 1);
		addConfigSearch[sizeof(addConfigSearch) - 1] = '\0';  // strncpy won't terminate an oversized source
	}
	if (auto it = j.find("addAttachMode"); it != j.end() && it->is_number_integer())
		addAttachMode = it->get<int>();
	if (auto it = j.find("addLighSearch"); it != j.end() && it->is_string()) {
		auto s = it->get<std::string>();
		std::strncpy(addLighSearch, s.c_str(), sizeof(addLighSearch) - 1);
		addLighSearch[sizeof(addLighSearch) - 1] = '\0';
	}
	if (auto it = j.find("addPopupMode"); it != j.end() && it->is_number_integer())
		addPopupMode = it->get<int>();
	if (auto it = j.find("addLightSubMode"); it != j.end() && it->is_number_integer())
		addLightSubMode = it->get<int>();
}

void LightEditor::DrawAddLightButton()
{
	if (ImGui::Button("Select Mesh")) {
		BeginPick();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", "Click a mesh in the world to attach a new bulb, edit an existing bulb, or whitelist/blacklist this reference.");
	}

	if (picker.IsPicking()) {
		int pm = static_cast<int>(picker.pickMode);
		ImGui::RadioButton("Collision", &pm, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Effect mesh", &pm, 1);
		const auto newPickMode = static_cast<LightPicker::PickMode>(pm);
		if (newPickMode != picker.pickMode) {
			picker.pickMode = newPickMode;
			picker.InvalidateHover();  // recompute the hover hit under the new mode immediately
		}
	}
}

std::vector<std::string> LightEditor::ScanLPConfigPaths() const
{
	std::vector<std::string> paths;
	// Mirrors LightPlacer's USVFS-safe scanning: relative path (not GetDataPath), throwing iterator (no
	// error_code), is_directory()/extension() only (is_regular_file(ec) hits an unhookable API, hiding virtual files).
	const std::filesystem::path root(R"(Data\LightPlacer)");
	std::error_code existsEc;
	if (!std::filesystem::exists(root, existsEc)) {
		logger::warn("[LightEditor] Data\\LightPlacer not found ({})", existsEc.message());
		return paths;
	}
	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			if (entry.is_directory() || entry.path().extension() != L".json")
				continue;
			// Use path ops, not character-count stripping: lexically_relative removes the prefix by
			// component (robust to USVFS casing/format variation) and stem() strips the extension cleanly.
			const auto relPath = entry.path().lexically_relative(root);
			std::string rel = (relPath.parent_path() / relPath.stem()).generic_string();
			if (!rel.empty() && rel.find("..") == std::string::npos)
				paths.push_back(std::move(rel));
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("[LightEditor] ScanLPConfigPaths error: {}", e.what());
	}
	logger::info("[LightEditor] Found {} LP config(s)", paths.size());
	std::ranges::sort(paths);
	return paths;
}

bool LightEditor::MatchesComboFilter(std::string_view filter, const std::string& text)
{
	return filter.empty() || Util::StringMatchesSearch(text, std::string(filter));
}

bool LightEditor::BeginSearchableCombo(const char* label, const char* preview, const char* searchId,
	char* searchBuf, size_t searchBufSize, std::string_view& filterOut, bool openNow)
{
	filterOut = {};
	// SetNextItemOpen is ignored for combos (they open via the derived popup id), so open it directly to
	// show the list without an extra click. One-shot: openNow is true only the frame the mode is entered.
	if (openNow)
		ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID(label)));
	if (!ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge))
		return false;
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText(searchId, searchBuf, searchBufSize);
	ImGui::Separator();
	filterOut = searchBuf;
	return true;
}

void LightEditor::NotifyResult(bool ok, const char* okMsg, const char* failMsg)
{
	EditorWindow::GetSingleton()->ShowNotification(ok ? okMsg : failMsg,
		ok ? Util::Colors::GetSuccess() : Util::Colors::GetError());
}

void LightEditor::ReloadLPAndReselect()
{
	QueueReselectCurrentLP();
	ScheduleConsoleCommand("reloadlp");
	previous = {};
	waitFrames = 3;
}

int LightEditor::DrawAttachedBulbCombo(const char* searchId, bool openNow)
{
	int clicked = -1;
	const char* preview = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size()) ? attachedBulbs[addSelectedBulb].lightEDID.c_str() : "Select a bulb";
	std::string_view filter;
	if (BeginSearchableCombo("Attached bulb", preview, searchId, addBulbSearch, sizeof(addBulbSearch), filter, openNow)) {
		for (int i = 0; i < (int)attachedBulbs.size(); ++i) {
			const auto& bulb = attachedBulbs[i];
			const std::string label = fmt::format("{}[{}]  ({})", bulb.lightEDID, bulb.index, bulb.configPath);
			if (!MatchesComboFilter(filter, label))
				continue;
			const bool isSel = (i == addSelectedBulb);
			if (ImGui::Selectable(label.c_str(), isSel)) {
				addSelectedBulb = i;
				clicked = i;
			}
			if (isSel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return clicked;
}

void LightEditor::DrawLightRecordCombo(const char* searchId)
{
	EnsureLighFormListBuilt();
	const char* preview = "Select a light";
	if (const auto* edid = LighEdidPtrForFormId(addSelectedLighFormId))
		preview = edid->c_str();
	std::string_view filter;
	if (BeginSearchableCombo("Light record", preview, searchId, addLighSearch, sizeof(addLighSearch), filter, false)) {
		for (auto& [edid, ligh] : s_lighFormList) {
			if (!MatchesComboFilter(filter, edid))
				continue;
			const bool isSel = ligh->GetFormID() == addSelectedLighFormId;
			if (ImGui::Selectable(edid.c_str(), isSel))
				addSelectedLighFormId = ligh->GetFormID();
			if (isSel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

void LightEditor::BeginAttachSequence(const std::string& configPath)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return;
	const auto pickedRef = ValidatedPickedRefr();
	if (!pickedRef)
		return;

	attachConfigPath = configPath;
	pendingSelectConfigPath = configPath;
	RestoreOriginal();
	previous = {};
	ScheduleConsoleCommand("reloadlp");
	attachPendingRefr = RE::ObjectRefHandle(pickedRef.get());
	attachPhase = AttachPhase::WaitingForReload;
	attachPhaseStart = std::chrono::steady_clock::now();
	SavePopupPrefs();
	ImGui::CloseCurrentPopup();
}

void LightEditor::ResetAddLightPopupState(bool requestClose)
{
	addLightPopupOpen = false;
	addLightPopupCloseRequested |= requestClose;
	pickedMesh = {};
	lpConfigPaths.clear();
	attachedBulbs.clear();
	addSelectedBulb = -1;
	addBulbSearch[0] = '\0';
	editBulbComboPendingOpen = false;
	filterListEntries.clear();
	addSelectedFilterEntry = -1;
	addFilterSearch[0] = '\0';
	addFilterEntryType = 0;
	addSelectedConfig = -1;
	addAttachMode = -1;
	addSelectedLighFormId = 0;
	addPopupMode = -1;
	addLightSubMode = -1;
}

void LightEditor::DrawAddLightPopup()
{
	if (addLightPopupOpen) {
		if (!addPopupPrefsLoaded) {
			LoadPopupPrefs();
			addPopupPrefsLoaded = true;
		}
		ImGui::OpenPopup("Select Mesh");
		addLightPopupOpen = false;
	}

	const float scale = Util::GetUIScale();
	// Anchor toward the top of the screen so combo dropdowns have room to open below.
	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.1f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(520 * scale, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Select Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (addLightPopupCloseRequested) {
			ImGui::CloseCurrentPopup();
			addLightPopupCloseRequested = false;
			ImGui::EndPopup();
			return;
		}

		ImGui::Text("EditorID: %s", pickedMesh.editorId.empty() ? "(none)" : pickedMesh.editorId.c_str());
		ImGui::Text("Mesh: %s", pickedMesh.modelPath.empty() ? "(none)" : pickedMesh.modelPath.c_str());
		ImGui::Text("Base FormID: 0x%08X", pickedMesh.baseFormId);
		ImGui::Text("Plugin: %s", pickedMesh.sourcePlugin.empty() ? "(unknown)" : pickedMesh.sourcePlugin.c_str());
		ImGui::Separator();

		auto notifyAddFailed = [] {
			EditorWindow::GetSingleton()->ShowNotification(
				"Failed to add light \xE2\x80\x94 see log",
				Util::Colors::GetError());
		};

		// "Selectable button": disabled+tooltip when unavailable, info-styled when active, else clickable.
		// Unavailability is checked first so a stale remembered-active option renders disabled, not clickable.
		auto selectableButton = [&](const char* label, bool active, bool available, const char* unavailTip) -> bool {
			if (!available) {
				{
					auto _d = Util::DisableGuard(true);
					ImGui::Button(label);
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", unavailTip);
				return false;
			}
			if (active) {
				auto _s = Util::StatusButtonStyle(Util::Colors::GetInfo());
				ImGui::Button(label);
				return false;
			}
			return ImGui::Button(label);
		};

		const bool hasBulbs = !attachedBulbs.empty();
		auto drawModeBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
			if (selectableButton(label, addPopupMode == mode, available, unavailTip)) {
				addPopupMode = mode;
				// Auto-open the bulb list when entering multi-bulb Edit Bulb mode (saves a click).
				if (mode == ModeEditBulb)
					editBulbComboPendingOpen = true;
			}
		};

		const char* noBulbsTip = "This mesh has no attached Light Placer bulbs.";
		drawModeBtn("Add Light", ModeAddLight, true, "");
		ImGui::SameLine();
		// Single bulb: "Edit Bulb" fires immediately without entering the mode.
		if (hasBulbs && attachedBulbs.size() == 1) {
			if (ImGui::Button("Edit Bulb")) {
				const auto& bulb = attachedBulbs[0];
				pendingSelectRefrId = bulb.refrId;
				pendingSelectConfigPath = bulb.configPath;
				pendingSelectLighEdid = bulb.lightEDID;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		} else {
			drawModeBtn("Edit Bulb", ModeEditBulb, hasBulbs, noBulbsTip);
		}
		ImGui::SameLine();
		drawModeBtn("Add to Whitelist", ModeWhitelist, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn("Add to Blacklist", ModeBlacklist, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn("Remove from List", ModeRemoveFromList,
			!filterListEntries.empty(),
			"This mesh has no whitelist or blacklist entries.");
		ImGui::Separator();

		if (addPopupMode == ModeAddLight) {
			auto drawSubModeBtn = [&](const char* label, int subMode) {
				if (selectableButton(label, addLightSubMode == subMode, true, nullptr))
					addLightSubMode = subMode;
			};

			if (hasBulbs) {
				drawSubModeBtn("Add new point", SubModeNewPoint);
				ImGui::SameLine();
				drawSubModeBtn("Add to entry", SubModeToEntry);
				ImGui::SameLine();
				drawSubModeBtn("Add new entry", SubModeNewEntry);
				ImGui::Separator();
			}

			if (!hasBulbs || addLightSubMode == SubModeNewEntry) {
				const char* configPreview = (addSelectedConfig >= 0 && addSelectedConfig < (int)lpConfigPaths.size()) ? lpConfigPaths[addSelectedConfig].c_str() : "Select a config";
				std::string_view cfgFilter;
				if (BeginSearchableCombo("Target JSON", configPreview, "##cfg_search", addConfigSearch, sizeof(addConfigSearch), cfgFilter, false)) {
					if (lpConfigPaths.empty())
						ImGui::TextDisabled("%s", "No configs found in Data\\LightPlacer\\");
					for (int i = 0; i < (int)lpConfigPaths.size(); ++i) {
						if (!MatchesComboFilter(cfgFilter, lpConfigPaths[i]))
							continue;
						const bool isSel = (i == addSelectedConfig);
						if (ImGui::Selectable(lpConfigPaths[i].c_str(), isSel))
							addSelectedConfig = i;
						if (isSel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				if (addSelectedConfig >= 0) {
					ImGui::Text("%s", "Attach by:");
					ImGui::SameLine();

					auto drawAttachBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
						if (selectableButton(label, addAttachMode == mode, available, unavailTip))
							addAttachMode = mode;
					};

					drawAttachBtn("Model", 0, !pickedMesh.modelPath.empty(), "No model path on this object.");
					ImGui::SameLine();
					drawAttachBtn("FormID", 1, !pickedMesh.sourcePlugin.empty(), "No source plugin on this object.");
					ImGui::SameLine();
					drawAttachBtn("EditorID", 2, !pickedMesh.editorId.empty(), "No EditorID on this object.");
				}

				if (addSelectedConfig >= 0 && addAttachMode >= 0)
					DrawLightRecordCombo("##ligh_search");

				ImGui::Separator();
				if (addSelectedConfig >= 0 && addAttachMode >= 0 && addSelectedLighFormId != 0) {
					std::string reason;
					const bool canAdd = CanAddBulb(reason);
					ImGui::BeginDisabled(!canAdd);
					const bool clicked = ImGui::Button("Add Bulb");
					ImGui::EndDisabled();
					if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("%s", reason.c_str());

					if (clicked && canAdd) {
						if (AddBulbToConfig()) {
							pendingSelectLighEdid = LighEdidForFormId(addSelectedLighFormId);
							pendingSelectRefrId = 0;
							if (auto refr = pickedMesh.refrHandle.get())
								pendingSelectRefrId = refr->GetFormID();
							BeginAttachSequence(lpConfigPaths[addSelectedConfig]);
						} else {
							notifyAddFailed();
						}
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeNewPoint) {
				DrawAttachedBulbCombo("##bulb_search_pt", false);

				ImGui::Separator();
				const bool canAddPt = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				ImGui::BeginDisabled(!canAddPt);
				const bool clickedPt = ImGui::Button("Add Point");
				ImGui::EndDisabled();

				if (clickedPt && canAddPt) {
					const auto& bulb = attachedBulbs[addSelectedBulb];
					if (AddPointToConfig(bulb)) {
						pendingSelectLighEdid = bulb.lightEDID;
						pendingSelectRefrId = bulb.refrId;
						BeginAttachSequence(bulb.configPath);
					} else {
						notifyAddFailed();
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeToEntry) {
				// Bulb picker: identifies the parent top-level entry
				DrawAttachedBulbCombo("##bulb_search_te", false);
				DrawLightRecordCombo("##ligh_search_te");

				ImGui::Separator();
				const bool bulbOk = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				const std::string teEdid = LighEdidForFormId(addSelectedLighFormId);
				const bool lightOk = !teEdid.empty();
				const bool isDupe = bulbOk && lightOk &&
				                    LightAlreadyInEntry(attachedBulbs[addSelectedBulb], teEdid);
				const bool canAddTE = bulbOk && lightOk && !isDupe;

				ImGui::BeginDisabled(!canAddTE);
				const bool clickedTE = ImGui::Button("Add to Entry");
				ImGui::EndDisabled();
				if (!canAddTE && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					if (isDupe)
						ImGui::SetTooltip("%s", "This light already exists in this entry.");
					else if (!bulbOk)
						ImGui::SetTooltip("%s", "Select a bulb");
					else
						ImGui::SetTooltip("%s", "Choose a light record.");
				}

				if (clickedTE && canAddTE) {
					const auto& bulb = attachedBulbs[addSelectedBulb];
					if (AddLightToExistingEntry(bulb, teEdid)) {
						pendingSelectLighEdid = teEdid;
						pendingSelectRefrId = bulb.refrId;
						BeginAttachSequence(bulb.configPath);
					} else {
						notifyAddFailed();
					}
				}
			}
		}

		if (addPopupMode == ModeEditBulb) {
			// Multi-bulb: combo fires immediately on selection. It returns the clicked index so the modal
			// is closed after the combo, not inside it (closing inside would close the combo, not the modal).
			const bool openNow = editBulbComboPendingOpen;
			editBulbComboPendingOpen = false;
			const int clickedBulb = DrawAttachedBulbCombo("##bulb_search", openNow);
			if (clickedBulb >= 0) {
				const auto& bulb = attachedBulbs[clickedBulb];
				pendingSelectRefrId = bulb.refrId;
				pendingSelectConfigPath = bulb.configPath;
				pendingSelectLighEdid = bulb.lightEDID;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		} else if (addPopupMode == ModeWhitelist || addPopupMode == ModeBlacklist) {
			DrawAttachedBulbCombo("##bulb_search", false);

			// Entry type: what string to write into the filter list.
			{
				const auto refr = PickedRefr();
				std::string cellEdid;
				if (refr)
					if (auto* cell = refr->GetParentCell())
						cellEdid = cell->GetFormEditorID();

				ImGui::Text("%s", "Add as:");
				ImGui::SameLine();
				auto drawEntryTypeBtn = [&](const char* label, int type, bool available, const char* unavailTip) {
					if (selectableButton(label, addFilterEntryType == type, available, unavailTip))
						addFilterEntryType = type;
				};
				drawEntryTypeBtn("Reference", 0, true, "");
				ImGui::SameLine();
				drawEntryTypeBtn("Cell", 1,
					!cellEdid.empty(),
					"This mesh's cell has no EditorID.");
			}

			ImGui::Separator();
			const bool bulbChosen = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
			// Append ##confirm to the label so the button has a unique ImGui ID distinct
			// from the identically-labelled mode selector button rendered in the same popup.
			const std::string confirmLabel = std::string(addPopupMode == ModeWhitelist ? "Add to Whitelist" : "Add to Blacklist") + "##confirm";
			ImGui::BeginDisabled(!bulbChosen);
			const bool confirm = ImGui::Button(confirmLabel.c_str());
			ImGui::EndDisabled();

			if (confirm && bulbChosen) {
				const auto& bulb = attachedBulbs[addSelectedBulb];
				const auto refr = PickedRefr();

				std::string entryStr;  // built from the chosen entry type
				if (addFilterEntryType == 1) {
					if (refr)
						if (auto* cell = refr->GetParentCell())
							entryStr = cell->GetFormEditorID();
				}
				if (entryStr.empty())
					entryStr = FormatOwnerFormEntry(refr.get());

				const MatchContext ctx = MakePickedContext(bulb.lightEDID);
				const bool isWhite = (addPopupMode == ModeWhitelist);
				const bool ok = ModifyLPFilterListFor(bulb.configPath, ctx, entryStr, isWhite, true);
				if (ok)
					ScheduleConsoleCommand("reloadlp");
				NotifyResult(ok,
					isWhite ? "Added to whitelist" : "Added to blacklist",
					"Filter update failed \xe2\x80\x94 see log");
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		}

		if (addPopupMode == ModeRemoveFromList) {
			const char* filterPreview = (addSelectedFilterEntry >= 0 && addSelectedFilterEntry < (int)filterListEntries.size()) ? filterListEntries[addSelectedFilterEntry].lightEDID.c_str() : "Select a bulb";
			std::string_view filterSv;
			if (BeginSearchableCombo("Attached bulb", filterPreview, "##filter_search", addFilterSearch, sizeof(addFilterSearch), filterSv, false)) {
				for (int i = 0; i < (int)filterListEntries.size(); ++i) {
					const auto& fe = filterListEntries[i];
					const std::string lbl = fmt::format("[{}]  {}  ({})  \"{}\"",
						fe.isWhiteList ? "whitelist" : "blacklist",
						fe.lightEDID, fe.configPath,
						fe.matchedEntry);
					if (!MatchesComboFilter(filterSv, lbl))
						continue;
					const bool isSel = (i == addSelectedFilterEntry);
					if (ImGui::Selectable(lbl.c_str(), isSel))
						addSelectedFilterEntry = i;
					if (isSel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();
			const bool entryChosen = (addSelectedFilterEntry >= 0 && addSelectedFilterEntry < (int)filterListEntries.size());
			ImGui::BeginDisabled(!entryChosen);
			const bool clickedRemove = ImGui::Button("Remove");
			ImGui::EndDisabled();

			if (clickedRemove && entryChosen) {
				const auto& fe = filterListEntries[addSelectedFilterEntry];
				const MatchContext ctx = MakePickedContext(fe.lightEDID);
				const bool ok = ModifyLPFilterListFor(fe.configPath, ctx, fe.matchedEntry, fe.isWhiteList, false);
				if (ok)
					ScheduleConsoleCommand("reloadlp");
				NotifyResult(ok,
					"Removed from list",
					"Filter update failed \xe2\x80\x94 see log");
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		}

		const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (ImGui::Button("Close") || escapePressed) {
			// Arm the handoff only while key-up is still pending; a queued press+release was already consumed.
			if (escapePressed && ImGui::IsKeyDown(ImGuiKey_Escape))
				EditorWindow::GetSingleton()->suppressNextEditorEscape = true;
			SavePopupPrefs();
			ResetAddLightPopupState(false);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

std::string LightEditor::AddEntryTargetString() const
{
	switch (addAttachMode) {
	case 0:
		return pickedMesh.modelPath;
	case 1:
		if (!pickedMesh.sourcePlugin.empty())
			return LightPicker::FormatFormEntry(pickedMesh.baseLocalFormId, pickedMesh.sourcePlugin);
		return {};
	case 2:
		return pickedMesh.editorId;
	default:
		return {};
	}
}

bool LightEditor::CanAddBulb(std::string& reasonOut) const
{
	if (!ValidatedPickedRefr()) {
		reasonOut = "Picked object has no base record.";
		return false;
	}
	if (addSelectedConfig < 0 || addSelectedConfig >= (int)lpConfigPaths.size()) {
		reasonOut = "Choose a target JSON.";
		return false;
	}
	if (addAttachMode < 0) {
		reasonOut = "Choose an attach type (Model, FormID, or EditorID).";
		return false;
	}
	if (addSelectedLighFormId == 0) {
		reasonOut = "Choose a light record.";
		return false;
	}
	if (addAttachMode == 0 && pickedMesh.modelPath.empty()) {
		reasonOut = "This object has no model path.";
		return false;
	}
	if (addAttachMode == 1 && pickedMesh.sourcePlugin.empty()) {
		reasonOut = "This object has no source plugin for a FormID entry.";
		return false;
	}
	if (addAttachMode == 2 && pickedMesh.editorId.empty()) {
		reasonOut = "This object has no EditorID.";
		return false;
	}

	reasonOut.clear();
	return true;
}

bool LightEditor::AddBulbToConfig()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;
	std::string invalidReason;
	if (!CanAddBulb(invalidReason)) {
		logger::warn("[LightEditor] AddBulbToConfig refused: {}", invalidReason);
		return false;
	}

	if (addSelectedConfig < 0 || addSelectedConfig >= (int)lpConfigPaths.size())
		return false;

	const std::string lighEdid = LighEdidForFormId(addSelectedLighFormId);
	if (lighEdid.empty())
		return false;

	const std::string target = AddEntryTargetString();
	if (target.empty())
		return false;

	const auto configPath = lpConfigPaths[addSelectedConfig];
	const auto filePath = LPConfigFilePath(configPath);
	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(configPath, configArray))
		return false;
	const MatchContext duplicateContext = MakePickedContext(lighEdid);
	if (std::ranges::any_of(configArray, [&](const auto& entry) {
			return EntryMatchesContext(entry, duplicateContext) && EntryContainsLight(entry, lighEdid);
		})) {
		logger::warn("[LightEditor] Refusing duplicate bulb '{}' for the picked owner in {}", lighEdid, configPath);
		return false;
	}

	nlohmann::ordered_json newEntry;
	newEntry[addAttachMode == 0 ? "models" : "formIDs"] = nlohmann::ordered_json::array({ target });
	newEntry["lights"] = nlohmann::ordered_json::array({ MakeLightObject(lighEdid) });

	configArray.push_back(std::move(newEntry));

	if (!WriteLPConfig(filePath, configArray)) {
		logger::warn("[LightEditor] Failed to write new bulb to {}", filePath.string());
		return false;
	}
	logger::info("[LightEditor] Added bulb '{}' to {} (target '{}')", lighEdid, filePath.string(), target);
	return true;
}

bool LightEditor::AddPointToConfig(const AttachedBulb& bulb)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;
	if (!ValidatedPickedRefr())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray)) {
		logger::warn("[LightEditor] AddPointToConfig: cannot load {}", bulb.configPath);
		return false;
	}

	const MatchContext ctx = MakePickedContext(bulb.lightEDID);

	auto* lightEntry = FindMatchingLightEntry(configArray, ctx, true);
	if (!lightEntry) {
		logger::warn("[LightEditor] AddPointToConfig: no matching entry for '{}' in {}",
			bulb.lightEDID, bulb.configPath);
		return false;
	}

	if (lightEntry->contains("nodes")) {
		logger::warn("[LightEditor] AddPointToConfig: refusing to add a point to a node-based entry");
		return false;
	}
	auto pointsIt = lightEntry->find("points");
	if (pointsIt != lightEntry->end() && !pointsIt->is_array()) {
		logger::warn("[LightEditor] AddPointToConfig: malformed points array for '{}' in {}", bulb.lightEDID, bulb.configPath);
		return false;
	}
	if (pointsIt == lightEntry->end()) {
		(*lightEntry)["points"] = nlohmann::ordered_json::array();
		pointsIt = lightEntry->find("points");
	}
	auto& points = *pointsIt;
	if (!std::ranges::all_of(points, [](const auto& point) { return point.is_array() && point.size() >= 3; })) {
		logger::warn("[LightEditor] AddPointToConfig: malformed point value for '{}' in {}", bulb.lightEDID, bulb.configPath);
		return false;
	}
	points.push_back(nlohmann::ordered_json::array({ 0, 0, 1 }));

	if (!WriteLPConfig(LPConfigFilePath(bulb.configPath), configArray)) {
		logger::warn("[LightEditor] AddPointToConfig: write failed for {}", bulb.configPath);
		return false;
	}
	logger::info("[LightEditor] AddPointToConfig: added point to '{}' in {}", bulb.lightEDID, bulb.configPath);
	return true;
}

bool LightEditor::LightAlreadyInEntry(const AttachedBulb& bulb, const std::string& lighEdid) const
{
	if (!ValidatedPickedRefr())
		return false;
	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray))
		return false;

	LightEntryLocation sourceLocation;
	if (!LocateLightEntry(configArray, MakePickedContext(bulb.lightEDID), sourceLocation))
		return false;
	return EntryContainsLight(*sourceLocation.topEntry, lighEdid);
}

bool LightEditor::AddLightToExistingEntry(const AttachedBulb& bulb, const std::string& lighEdid)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;
	if (!ValidatedPickedRefr())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: cannot load {}", bulb.configPath);
		return false;
	}

	LightEntryLocation sourceLocation;
	if (!LocateLightEntry(configArray, MakePickedContext(bulb.lightEDID), sourceLocation)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: no unique governing entry in {}", bulb.configPath);
		return false;
	}
	auto* parentEntry = sourceLocation.topEntry;

	// Duplicate guard (should be checked by UI already, but be safe)
	if (EntryContainsLight(*parentEntry, lighEdid))
		return false;

	auto& lightsArr = (*parentEntry)["lights"];
	if (!lightsArr.is_array())
		lightsArr = nlohmann::ordered_json::array();
	lightsArr.push_back(MakeLightObject(lighEdid));

	if (!WriteLPConfig(LPConfigFilePath(bulb.configPath), configArray)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: write failed for {}", bulb.configPath);
		return false;
	}
	logger::info("[LightEditor] AddLightToExistingEntry: added '{}' to existing entry in {}", lighEdid, bulb.configPath);
	return true;
}

bool LightEditor::HasShadowFlags(uint32_t tesFlags)
{
	return (tesFlags & (static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow) |
						   static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow) |
						   static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow))) != 0;
}

std::string LightEditor::GetLightName(const LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::TickDeferredWork()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked()) {
		FinishDeferredCleanup();
		return;
	}

	// These state machines must keep advancing after the editor closes. In particular, both workflows
	// temporarily disable a reference and would otherwise be able to strand it in that state forever.
	if (attachPhase != AttachPhase::Idle) {
		const auto now = std::chrono::steady_clock::now();
		if (now - attachPhaseStart >= kAttachStepDelay) {
			attachPhaseStart = now;
			switch (attachPhase) {
			case AttachPhase::WaitingForReload:
				if (auto refr = attachPendingRefr.get())
					ScheduleConsoleCommand("disable", refr.get());
				attachPhase = AttachPhase::WaitingForEnable;
				break;
			case AttachPhase::WaitingForEnable:
				if (auto refr = attachPendingRefr.get())
					ScheduleConsoleCommand("enable", refr.get());
				attachPendingRefr = {};
				attachPhase = AttachPhase::WaitingForRespawn;
				break;
			case AttachPhase::WaitingForRespawn:
				attachPhase = AttachPhase::Idle;
				waitFrames = 3;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				EditorWindow::GetSingleton()->ShowNotification(
					fmt::format("Added light to {}", attachConfigPath).c_str(),
					Util::Colors::GetSuccess());
				break;
			default:
				break;
			}
		}
	}

	UpdateRefRefresh();
}

void LightEditor::UpdatePicker()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked()) {
		picker.Cancel();
		return;
	}
	if (!enabled || !Menu::GetSingleton()->ShouldSwallowInput()) {
		picker.Cancel();
		return;
	}

	picker.Update();
	if (auto hit = picker.TakeResult(); hit.valid) {
		addLightPopupCloseRequested = false;
		pickedMesh = hit;
		lpConfigPaths = ScanLPConfigPaths();
		const auto pickedRef = PickedRefr();
		GatherAttachedBulbs(pickedRef.get());
		ScanFilterListEntries(pickedRef.get(), lpConfigPaths);
		addSelectedConfig = -1;
		addAttachMode = -1;
		addSelectedLighFormId = 0;
		addPopupMode = -1;
		addLightSubMode = -1;
		editBulbComboPendingOpen = false;
		// Filter-flow selections are mesh-specific: reset them so a remembered "Cell" type or filter
		// index from the previous pick can't persist for a mesh that can't offer it.
		addFilterEntryType = 0;  // Reference, always available
		addSelectedFilterEntry = -1;
		addLightPopupOpen = true;
	}
}

void LightEditor::GatherLights()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked()) {
		ResetOverrides();
		FinishDeferredCleanup();
		savedSelection = {};
		lights.clear();
		lightsAttached.clear();
		totalLightCount = 0;
		activeShadowLightCount = 0;
		return;
	}

	if (!enabled || !Menu::GetSingleton()->ShouldSwallowInput()) {
		ResetOverrides();
		return;
	}

	if (!selected.isSelected && savedSelection.isSelected) {
		selected = savedSelection;
		savedSelection = {};
	}

	// Skip a few frames after disruptive operations (reloadlp, Disable/Enable, position change)
	// so the game has time to rebuild the light list before we resample it.
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

		LightInfo info{};
		info.ptr = reinterpret_cast<void*>(niLight);
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		if (!runtimeData)
			return;
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				info.id = refr->GetFormID();
				info.name = clib_util::editorID::get_editorID(objRef);
				info.index = lightsAttached[refr]++;
			}
		}

		info.isRef = ligh != nullptr;

		if (!info.isRef && runtimeData->lighFormId != 0) {
			if (auto* lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId))
				ligh = lighForm->As<RE::TESObjectLIGH>();
		}

		info.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && HasShadowFlags(ligh->data.flags.underlying());

		totalLightCount++;
		if (isShadow)
			activeShadowLightCount++;

		if ((shadowsOnly) && (!ligh || !isShadow)) {
			return;
		}

		info.isAttached = !info.isRef && refr != nullptr;
		// Spotlights are always grouped under "Other" (their falloff isn't editable here).
		info.isOther = (!info.isRef && !info.isAttached) || (info.isSpotlight);

		const bool isRefMatch = (info.isRef && !info.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = info.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = info.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (info.isRef) {
			info.position = refr->GetPosition();
			info.hasPosition = true;
		} else if (niLight->parent) {
			info.position = niLight->parent->world.translate;
			info.hasPosition = true;
		}
		if (info.isOther) {
			if (info.name.empty())
				info.name = niLight->name.c_str();
			info.index = 0;
		}

		// Match a queued re-selection by stable identity so it survives reloadlp's index changes.
		if (pendingAutoSelect && info.isAttached && info.id == pendingSelectRefrId) {
			const auto parsedName = ParseLPLightName(niLight->name.c_str());
			if (parsedName.isLPLight && parsedName.configPath == pendingSelectConfigPath && parsedName.lightEDID == pendingSelectLighEdid) {
				selected = info;
				pendingAutoSelect = false;
			}
		}

		info.isSelected = selected == info;

		lights.push_back(info);

		// Capture the NiLight for hover-flash on the first frame this light is hovered.
		if (comboHoveredLight.id != 0 && info == comboHoveredLight && !hoverFlashNiLight) {
			hoverFlashNiLight.reset(niLight);
			const auto* rd = ISLCommon::RuntimeLightDataExt::Get(niLight);
			hoverFlashOriginalFade = rd ? rd->fade : 0.0f;
			hoverFlashOnFade = hoverFlashOriginalFade > 0.0f ? hoverFlashOriginalFade : 1.0f;
		}

		if (!info.isSelected)
			return;
		selected = info;
		foundSelected = true;
		UpdateSelectedLight(refr, ligh, niLight, bsLight);
	};

	lights.clear();
	lightsAttached.clear();
	totalLightCount = 0;
	activeShadowLightCount = 0;
	const auto smState = globals::game::smState;
	if (!smState) {
		ResetOverrides();
		return;
	}
	const auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode) {
		ResetOverrides();
		return;
	}

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

	if (pendingAutoSelect && --pendingAutoSelectTTL <= 0)
		pendingAutoSelect = false;
}

void LightEditor::GatherAttachedBulbs(RE::TESObjectREFR* refr)
{
	attachedBulbs.clear();
	addSelectedBulb = -1;
	addBulbSearch[0] = '\0';
	if (!refr)
		return;

	const auto smState = globals::game::smState;
	if (!smState)
		return;
	const auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;
	std::unordered_map<RE::TESObjectREFR*, uint32_t> running;

	auto collect = [&](const RE::NiPointer<RE::BSLight>& light) {
		auto* bsLight = light.get();
		if (!bsLight)
			return;
		auto* niLight = bsLight->light.get();
		if (!niLight)
			return;
		auto* owner = niLight->GetUserData();
		if (owner != refr)
			return;
		// Count every light owned by this ref (not just LP bulbs) so the index matches the main Lights
		// combo, which increments per owner over all its lights.
		const uint32_t ownerIndex = running[owner]++;
		const auto parsed = ParseLPLightName(niLight->name.c_str());
		if (!parsed.isLPLight)
			return;
		AttachedBulb bulb;
		bulb.lightEDID = parsed.lightEDID;
		bulb.configPath = parsed.configPath;
		bulb.refrId = refr->GetFormID();
		bulb.index = ownerIndex;
		attachedBulbs.push_back(std::move(bulb));
	};

	for (auto& light : shadowSceneNode->GetRuntimeData().activeLights)
		collect(light);
	for (auto& light : shadowSceneNode->GetRuntimeData().activeShadowLights)
		collect(light);
}

void LightEditor::ScanFilterListEntries(RE::TESObjectREFR* refr, const std::vector<std::string>& configPaths)
{
	filterListEntries.clear();
	addSelectedFilterEntry = -1;
	addFilterSearch[0] = '\0';
	addFilterEntryType = 0;
	if (!refr)
		return;

	const std::string refEntry = FormatOwnerFormEntry(refr);

	// Collect all entry strings that could match this reference.
	std::vector<std::string> entriesToScan;
	if (!refEntry.empty())
		entriesToScan.push_back(refEntry);
	if (auto* cell = refr->GetParentCell()) {
		std::string cellEdid = cell->GetFormEditorID();
		if (!cellEdid.empty())
			entriesToScan.push_back(std::move(cellEdid));
	}

	if (entriesToScan.empty())
		return;

	for (const auto& configPath : configPaths) {
		nlohmann::ordered_json configArray;
		if (!LoadConfigArray(configPath, configArray))
			continue;

		for (const auto& entry : configArray) {
			auto lightsIt = entry.find("lights");
			if (lightsIt == entry.end() || !lightsIt->is_array())
				continue;
			for (const auto& le : *lightsIt) {
				const auto* edidPtr = GetLightEntryEdid(le);
				if (!edidPtr)
					continue;
				const std::string& lightEDID = *edidPtr;

				auto checkList = [&](bool isWhiteList) {
					const char* key = isWhiteList ? "whiteList" : "blackList";
					if (auto* list = GetArrayMember(le, key)) {
						for (const auto& item : *list) {
							if (!item.is_string())
								continue;
							const std::string& itemStr = item.get_ref<const std::string&>();
							for (const auto& candidate : entriesToScan) {
								if (itemStr == candidate) {
									filterListEntries.push_back({ lightEDID, configPath, candidate, isWhiteList });
									return;
								}
							}
						}
					}
				};
				checkList(true);
				checkList(false);
			}
		}
	}
	logger::info("[LightEditor] ScanFilterListEntries: found {} entries", filterListEntries.size());
}

void LightEditor::ResetOverrides()
{
	CancelPick();
	ResetAddLightPopupState(true);
	if (selected.isSelected)
		savedSelection = selected;
	RestoreOriginal();
	if (hoverFlashNiLight) {
		if (auto* rd = ISLCommon::RuntimeLightDataExt::Get(hoverFlashNiLight.get()))
			rd->fade = hoverFlashOriginalFade;
		hoverFlashNiLight.reset();
	}
	comboHoveredLight = {};
	selected = {};
	previous = {};
}

void LightEditor::RestoreDefaultSettings()
{
	CancelPick();
	ResetOverrides();
	FinishDeferredCleanup();
	*this = {};
	addLightPopupCloseRequested = true;
}

void LightEditor::ApplyShadowDepthBias()
{
	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = shadowDepthBias;
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight, RE::BSLight* bsLight)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked() || !niLight)
		return;

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	if (!runtimeData) {
		RestoreOriginal();
		previous = {};
		selected = {};
		return;
	}
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;
	const bool logicalSelectionChanged = previous != selected;
	const bool runtimeInstanceChanged = activeBsLight.get() != bsLight || activeNiLight.get() != niLight;
	const auto incomingLpInfo = selected.isAttached ? ParseLPLightName(niLight->name.c_str()) : LPLightInfo{};

	// A ref rebuild replaces NiLight/BSLight pointers without changing the logical selection. Preserve
	// unsaved edits in that case, but reject an index collision that now points at a different bulb.
	bool runtimeIdentityChanged = false;
	if (!logicalSelectionChanged && runtimeInstanceChanged) {
		if (lpInfo.isLPLight || incomingLpInfo.isLPLight) {
			runtimeIdentityChanged =
				lpInfo.isLPLight != incomingLpInfo.isLPLight ||
				lpInfo.configPath != incomingLpInfo.configPath ||
				lpInfo.lightEDID != incomingLpInfo.lightEDID;
		} else {
			const auto previousLighFormId = activeLigh ? activeLigh->GetFormID() : 0;
			const auto incomingLighFormId = ligh ? ligh->GetFormID() : 0;
			runtimeIdentityChanged = previousLighFormId != incomingLighFormId;
		}
	}
	const bool initializeSelection = logicalSelectionChanged || runtimeIdentityChanged;

	// Per-selection initialization: snapshots the light's original state, populates lpInfo,
	// and runs a dry-run save to determine whether a matching LP JSON entry exists.
	if (initializeSelection) {
		const auto previousRef = activeRefr.get();
		const bool recreatesPreviousLight =
			logicalSelectionChanged && activeLigh && previousRef && !lpInfo.isLPLight &&
			current.tesFlags.underlying() != original.tesFlags.underlying();
		RestoreOriginal();
		if (recreatesPreviousLight) {
			previous = {};
			selected.isSelected = false;
			return;
		}

		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		// The hover-flash may have blinked fade to 0; snapshotting mid-blink would freeze 0 as the base, so
		// recover the stashed pre-flash value. Covers non-LP refs; LP lights get this from RefreshLPJsonState.
		if (hoverFlashNiLight && niLight == hoverFlashNiLight.get())
			original.data.fade = hoverFlashOriginalFade;
		original.pos = selected.isRef ?
		                   refr->GetPosition() :
		                   (niLight->parent ? niLight->parent->local.translate : RE::NiPoint3{});

		current = original;
		if (selected.isRef)
			current.pos = {};

		auto* originalShadowLight = AsShadowLight(bsLight);
		originalShadowDepthBias = originalShadowLight ? originalShadowLight->GetRuntimeData().shadowBiasScale : 0.0f;
		shadowDepthBias = originalShadowDepthBias;
		cachedFadeBeforeToggle = 0.0f;

		lpInfo = incomingLpInfo;
		lpInfo.runtimeSnapshot = *runtimeData;
		if (hoverFlashNiLight && niLight == hoverFlashNiLight.get())
			lpInfo.runtimeSnapshot.fade = hoverFlashOriginalFade;
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
		activeRefr = refr ? RE::ObjectRefHandle(refr) : RE::ObjectRefHandle{};
		activeLigh = ligh;

		// Reset emittance state; populated below from the JSON (LP) or the runtime extra (non-LP).
		externalEmittanceEdid = {};
		useExternalEmittance = false;
		externalEmittanceChanged = false;
		activeEmittanceSource = nullptr;
		emittanceColorActive = false;  // recomputed by UpdateEmittanceColor for the new bulb
		// LP bulbs author emittance in the JSON (no reliable runtime extra), read below by
		// RefreshLPJsonState; non-LP bulbs carry it as extra data.
		if (!lpInfo.isLPLight && refr) {
			if (const auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>())
				if (extra->source) {
					externalEmittanceEdid = clib_util::editorID::get_editorID(extra->source);
					activeEmittanceSource = extra->source;
				}
			useExternalEmittance = !externalEmittanceEdid.empty();
		}

		lpRawDataLoaded = false;
		lpInWhitelist = false;
		lpInBlacklist = false;
		lpFlagSet.clear();
		originalLpFlagSet.clear();
		lpMatchFound = lpInfo.isLPLight && SaveToLightPlacer(false, true);
		if (lpInfo.isLPLight) {
			lpRawDataLoaded = RefreshLPJsonState();
			originalLpFlagSet = lpFlagSet;
		}

		originalEmittanceSource = activeEmittanceSource;
		originalExternalEmittanceEdid = externalEmittanceEdid;
		originalUseExternalEmittance = useExternalEmittance;

		previous = selected;
	} else if (runtimeInstanceChanged) {
		activeIsRef = selected.isRef;
		activeRefr = refr ? RE::ObjectRefHandle(refr) : RE::ObjectRefHandle{};
		activeLigh = ligh;
	}

	activeNiLight.reset(niLight);
	activeBsLight.reset(bsLight);
	if (runtimeInstanceChanged)
		ApplyShadowDepthBias();

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		const bool isShadow = bsLight && bsLight->IsShadowLight();
		// Match ProcessLight, which runs on the LP-scaled runtime data (fade/size), so the readout tracks the game.
		computedInverseSquareRadius = InverseSquareLighting::CalculateRadius(
			current.data.fade * GetLPFadeFactor() * 4.f, isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			std::clamp(current.data.size * GetLPSizeFactor(), 0.1f, 50.0f));
	} else {
		computedInverseSquareRadius = current.data.radius * GetLPRadiusFactor();
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
		// Attached lights may share their parent node with the source mesh; never move that node live.
		displayInfo.pos = current.pos;
	}

	// Only non-LP lights apply TES-flag edits (mutating the base LIGH form + rebuilding the ref); LP flags
	// go via JSON/reloadlp. Running this for LP lights would mutate the shared form and vanish the mesh.
	if (!selected.isOther && !lpInfo.isLPLight && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		RequestRefRefresh(refr);
	}

	UpdateEmittanceColor();

	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellFormId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormID() : 0;
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

float LightEditor::GetLPFactor(float inferredFactor) const
{
	if (!lpInfo.isLPLight || !lpRawDataLoaded || lpFlagSet.contains("IgnoreScale"))
		return 1.0f;
	if (!lpInfo.ignoreScale)
		return inferredFactor;

	const auto activeRef = GetActiveRefr();
	const float scale = activeRef ? activeRef->GetScale() : 1.0f;
	return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
}

float LightEditor::GetLPFadeFactor() const
{
	return GetLPFactor(lpInfo.fadeFactor);
}

float LightEditor::GetLPRadiusFactor() const
{
	return GetLPFactor(lpInfo.radiusFactor);
}

float LightEditor::GetLPSizeFactor() const
{
	return GetLPFactor(lpInfo.sizeFactor);
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (State::GetSingleton()->IsPersistentMutationBlocked() || !enabled || !niLight || !runtimeData)
		return false;

	// Hovered (not selected) light: blink its fade so it flashes in the combo list.
	if (hoverFlashNiLight && niLight == hoverFlashNiLight.get() && niLight != activeNiLight.get()) {
		runtimeData->fade = hoverFlashVisible ? hoverFlashOnFade : 0.f;
		return true;
	}

	if (niLight != activeNiLight.get())
		return false;

	runtimeData->lighFormId = current.data.lighFormId;
	// Use the emittance source's live color while one drives this bulb (see UpdateEmittanceColor), else the editor color.
	runtimeData->diffuse = emittanceColorActive ? emittanceColor : current.data.diffuse;
	runtimeData->fade = current.data.fade * GetLPFadeFactor();
	runtimeData->cutoffOverride = current.data.cutoffOverride;
	runtimeData->size = current.data.size * GetLPSizeFactor();

	const bool isInverseSquare = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);
	const bool radiusWasEdited = current.data.radius != original.data.radius;
	if ((lpInfo.isLPLight && lpRawDataLoaded) || !isInverseSquare ||
		original.data.flags.none(LightLimitFix::LightFlags::InverseSquare) || radiusWasEdited) {
		runtimeData->originalRadius = current.data.radius * GetLPRadiusFactor();
	}

	if (isInverseSquare) {
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	} else {
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);
		// Restore the authoritative radius: ProcessLight's IS branch writes a computed value into the shared
		// runtimeData->radius that the non-IS branch only reads, so a stale inflated value would stick forever.
		runtimeData->radius = runtimeData->originalRadius;
	}

	if (current.data.flags.any(LightLimitFix::LightFlags::Linear))
		runtimeData->flags.set(LightLimitFix::LightFlags::Linear);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::Linear);

	return true;
}

void LightEditor::RestoreOriginal()
{
	if (!activeNiLight) {
		activeBsLight.reset();
		lpRawDataLoaded = false;
		return;
	}

	if (auto* runtimeData = ISLCommon::RuntimeLightDataExt::Get(activeNiLight.get())) {
		if (lpInfo.isLPLight && lpRawDataLoaded)
			*runtimeData = lpInfo.runtimeSnapshot;
		else
			*runtimeData = original.data;
	}

	const auto activeRef = activeRefr.get();
	if (activeIsRef && activeRef) {
		activeRef->SetPosition(original.pos);
	}

	// Mirror the non-LP gate in UpdateSelectedLight: only non-LP lights ever mutated the base form's
	// flags, so only they need the revert + rebuild here.
	if (!lpInfo.isLPLight && activeLigh && activeRef && current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		if (!State::GetSingleton()->IsPersistentMutationBlocked())
			RequestRefRefresh(activeRef.get());
	}

	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = originalShadowDepthBias;

	activeNiLight.reset();
	activeBsLight.reset();
	activeRefr = {};
	activeLigh = nullptr;
	activeIsRef = false;
	activeEmittanceSource = nullptr;
	emittanceColorActive = false;
	lpRawDataLoaded = false;
}

void LightEditor::RequestRefRefresh(RE::TESObjectREFR* refr)
{
	if (!refr)
		return;
	// Flush an already-disabled previous reference before replacing its pending refresh.
	if (pendingRefreshFrames > 0) {
		if (auto prev = pendingRefreshRefr.get(); prev && prev.get() != refr && !pendingRefreshNeedsDisable)
			prev->Enable(false);
	}
	pendingRefreshRefr = RE::ObjectRefHandle(refr);
	pendingRefreshNeedsDisable = true;
	pendingRefreshFrames = kRefreshEnableDelay;
	// Disable runs on the next deferred-work tick, outside active-light iteration.
	waitFrames = std::max(waitFrames, kRefreshEnableDelay + 2);
}

void LightEditor::UpdateRefRefresh()
{
	if (pendingRefreshNeedsDisable) {
		if (auto refr = pendingRefreshRefr.get()) {
			refr->Disable();
			pendingRefreshNeedsDisable = false;
		} else {
			pendingRefreshNeedsDisable = false;
			pendingRefreshFrames = 0;
			pendingRefreshRefr = {};
		}
		return;
	}
	if (pendingRefreshFrames <= 0)
		return;
	if (--pendingRefreshFrames == 0) {
		if (auto refr = pendingRefreshRefr.get())
			refr->Enable(false);
		pendingRefreshRefr = {};
	}
}

void LightEditor::FinishDeferredCleanup()
{
	// The attach sequence uses queued console commands. If its Disable has already been queued,
	// queue Enable after it before releasing the retained reference handle.
	if (attachPhase == AttachPhase::WaitingForEnable) {
		if (auto refr = attachPendingRefr.get())
			ScheduleConsoleCommand("enable", refr.get());
	}
	attachPendingRefr = {};
	attachPhase = AttachPhase::Idle;
	pendingAutoSelect = false;
	pendingAutoSelectTTL = 0;

	if (pendingRefreshFrames > 0) {
		if (auto refr = pendingRefreshRefr.get()) {
			if (pendingRefreshNeedsDisable) {
				// Explicit editor shutdown occurs outside GatherLights, so complete the queued rebuild now.
				// A persistence guard cancels new runtime mutation but still restores an already-disabled ref.
				if (!State::GetSingleton()->IsPersistentMutationBlocked()) {
					refr->Disable();
					refr->Enable(false);
				}
			} else {
				refr->Enable(false);
			}
		}
	}
	pendingRefreshRefr = {};
	pendingRefreshNeedsDisable = false;
	pendingRefreshFrames = 0;
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

	if (info.lightEDID.empty() || LPConfigFilePath(info.configPath).empty()) {
		logger::warn("[LightEditor] Rejected malformed LP light name: {}", name);
		return info;
	}

	info.isLPLight = true;
	return info;
}

LightEditor::MatchContext LightEditor::MakeSelectedContext() const
{
	MatchContext ctx;
	ctx.ownerModelPath = lpInfo.ownerModelPath;
	ctx.ownerEditorId = lpInfo.ownerEditorId;
	const auto refr = activeRefr.get();
	ctx.baseFormId = (refr && refr->GetObjectReference()) ? refr->GetObjectReference()->formID : 0;
	ctx.lightEDID = lpInfo.lightEDID;
	ctx.refr = refr;
	return ctx;
}

RE::NiPointer<RE::TESObjectREFR> LightEditor::ValidatedPickedRefr() const
{
	if (!pickedMesh.valid || pickedMesh.baseFormId == 0)
		return {};
	const auto refr = PickedRefr();
	const auto* baseObject = refr ? refr->GetBaseObject() : nullptr;
	if (!baseObject || baseObject->GetFormID() != pickedMesh.baseFormId)
		return {};
	return refr;
}

LightEditor::MatchContext LightEditor::MakePickedContext(const std::string& lightEDID) const
{
	MatchContext ctx;
	ctx.ownerModelPath = pickedMesh.modelPath;
	ctx.ownerEditorId = pickedMesh.editorId;
	ctx.baseFormId = pickedMesh.baseFormId;
	ctx.lightEDID = lightEDID;
	ctx.refr = ValidatedPickedRefr();
	return ctx;
}

bool LightEditor::MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!HasWellFormedLPFilters(lightEntry))
		return false;
	if (!refr)
		return true;

	std::unordered_set<RE::FormID> formIDs;
	std::unordered_set<std::string> editorIDs;
	auto addForm = [&](RE::TESForm* form) {
		if (!form)
			return;
		formIDs.insert(form->GetFormID());
		auto editorID = clib_util::editorID::get_editorID(form);
		if (!editorID.empty())
			editorIDs.insert(std::move(editorID));
	};

	addForm(refr);
	addForm(refr->GetBaseObject());
	auto* cell = refr->GetParentCell();
	addForm(cell);
	addForm(refr->GetWorldspace());

	std::unordered_set<RE::BGSLocation*> visitedLocations;
	auto* location = refr->GetCurrentLocation();
	if (!location && cell)
		location = cell->GetLocation();
	for (; location && visitedLocations.insert(location).second; location = location->parentLoc)
		addForm(location);

	auto matchesEntry = [&](const std::string& entry) -> bool {
		if (entry.find('~') != std::string::npos || HasHexPrefix(entry)) {
			const RE::FormID resolvedId = ResolveFormEntry(entry);
			return resolvedId != 0 && formIDs.contains(resolvedId);
		}
		return editorIDs.contains(entry);
	};

	auto anyMatches = [&](const nlohmann::ordered_json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	for (const auto& [key, isWhitelist] : {
			 std::pair{ "whiteList", true },
			 std::pair{ "blackList", false } }) {
		const auto it = lightEntry.find(key);
		if (it == lightEntry.end())
			continue;
		if (!it->is_array())
			return false;
		const bool matches = anyMatches(*it);
		if ((isWhitelist && !matches) || (!isWhitelist && matches))
			return false;
	}

	return true;
}

bool LightEditor::LoadLPConfig(nlohmann::ordered_json& out) const
{
	if (!LoadConfigArray(lpInfo.configPath, out)) {
		logger::warn("[LightEditor] Could not load Light Placer config: {}", lpInfo.configPath);
		return false;
	}
	return true;
}

bool LightEditor::EntryMatchesContext(const nlohmann::ordered_json& entry, const MatchContext& ctx)
{
	const std::string normalizedOwner = NormalizeModelPath(ctx.ownerModelPath);
	if (auto* models = GetArrayMember(entry, "models"); !normalizedOwner.empty() && models)
		for (const auto& v : *models)
			if (v.is_string() && NormalizeModelPath(v.get<std::string>()) == normalizedOwner)
				return true;
	auto matchesFormArray = [&](const char* key) {
		const auto* formIDs = GetArrayMember(entry, key);
		if (!formIDs)
			return false;
		for (const auto& v : *formIDs) {
			if (!v.is_string())
				continue;
			const std::string s = v.get<std::string>();
			if (s.find('~') == std::string::npos && !HasHexPrefix(s)) {
				if (!ctx.ownerEditorId.empty() && s == ctx.ownerEditorId)
					return true;
			} else if (ctx.baseFormId != 0) {
				const RE::FormID resolved = ResolveFormEntry(s);
				if (resolved != 0 && resolved == ctx.baseFormId)
					return true;
			}
		}
		return false;
	};
	return matchesFormArray("formIDs") || matchesFormArray("visualEffects");
}

nlohmann::ordered_json* LightEditor::FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters) const
{
	std::vector<nlohmann::ordered_json*> ownerCandidates;
	std::vector<nlohmann::ordered_json*> filteredCandidates;
	std::vector<nlohmann::ordered_json*> unfilteredCandidates;
	const auto activeRef = GetActiveRefr();
	const bool allowSelectedFallback =
		applyFilters && ctx.refr && activeRef && ctx.refr.get() == activeRef.get() &&
		lpInfo.isLPLight && ctx.lightEDID == lpInfo.lightEDID;

	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;
		const bool ownerMatches = EntryMatchesContext(entry, ctx);

		for (auto& lightEntry : *lightsIt) {
			const auto* edid = GetLightEntryEdid(lightEntry);
			if (!edid || *edid != ctx.lightEDID)
				continue;

			if (allowSelectedFallback && HasWellFormedLPFilters(lightEntry))
				unfilteredCandidates.push_back(&lightEntry);
			if (applyFilters && !MatchesLPFilters(lightEntry, ctx.refr.get()))
				continue;
			if (allowSelectedFallback)
				filteredCandidates.push_back(&lightEntry);
			if (ownerMatches)
				ownerCandidates.push_back(&lightEntry);
		}
	}

	auto selectUnique = [&](const std::vector<nlohmann::ordered_json*>& candidates, const char* tier) {
		if (candidates.size() == 1)
			return candidates.front();
		if (candidates.size() > 1)
			logger::warn(
				"[LightEditor] Refusing ambiguous Light Placer {} match: {} entries for model '{}' and light '{}' in {}",
				tier, candidates.size(), ctx.ownerModelPath, ctx.lightEDID, lpInfo.configPath);
		return static_cast<nlohmann::ordered_json*>(nullptr);
	};

	if (auto* match = selectUnique(ownerCandidates, "owner"))
		return match;
	if (ownerCandidates.size() > 1 || !allowSelectedFallback)
		return nullptr;
	if (auto* match = selectUnique(filteredCandidates, "filtered fallback"))
		return match;
	if (filteredCandidates.size() > 1)
		return nullptr;
	if (auto* match = selectUnique(unfilteredCandidates, "unfiltered fallback"))
		return match;
	return nullptr;
}

bool LightEditor::LocateLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, LightEntryLocation& out) const
{
	out = {};
	auto* matchedEntry = FindMatchingLightEntry(configArray, ctx, true);
	if (!matchedEntry)
		return false;

	for (size_t t = 0; t < configArray.size(); ++t) {
		auto& topEntry = configArray[t];
		auto lightsIt = topEntry.find("lights");
		if (lightsIt == topEntry.end() || !lightsIt->is_array())
			continue;
		for (size_t i = 0; i < lightsIt->size(); ++i) {
			auto& le = (*lightsIt)[i];
			if (&le != matchedEntry)
				continue;
			out.topEntry = &topEntry;
			out.topIdx = t;
			out.lightsArr = &*lightsIt;
			out.lightIdx = i;
			return true;
		}
	}
	return false;
}

bool LightEditor::SaveToLightPlacer(bool includeColor, bool dryRun)
{
	if (!lpInfo.isLPLight)
		return false;
	const auto activeRef = GetActiveRefr();
	if (!activeRef)
		return false;
	if (!dryRun && State::GetSingleton()->IsPersistentMutationBlocked())
		return false;
	if (!dryRun && !lpRawDataLoaded) {
		logger::warn("[LightEditor] Refusing to save '{}' without a resolved raw Light Placer baseline", lpInfo.configPath);
		return false;
	}

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	auto* matchedEntry = FindMatchingLightEntry(configArray, MakeSelectedContext());
	if (!matchedEntry) {
		logger::warn("[LightEditor] No matching entry found for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	if (dryRun)
		return true;
	if (!ValidateCurrentLightValues())
		return false;

	auto& data = (*matchedEntry)["data"];
	data = BuildEditedData(data, includeColor);

	if (current.pos != original.pos && !SetUniquePointFromPos(*matchedEntry, current.pos)) {
		logger::warn("[LightEditor] Refusing to save an edited position without one unique point/node");
		return false;
	}

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	auto savedRuntime = lpInfo.runtimeSnapshot;
	if (activeNiLight)
		ApplyOverrides(activeNiLight.get(), &savedRuntime);
	lpInfo.runtimeSnapshot = savedRuntime;
	original = current;
	originalLpFlagSet = lpFlagSet;
	originalEmittanceSource = activeEmittanceSource;
	originalExternalEmittanceEdid = externalEmittanceEdid;
	originalUseExternalEmittance = useExternalEmittance;
	externalEmittanceChanged = false;
	originalShadowDepthBias = shadowDepthBias;
	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

nlohmann::ordered_json LightEditor::BuildEditedData(const nlohmann::ordered_json& existingData, bool includeColor) const
{
	const bool isInvSq = lpFlagSet.contains("InverseSquare");
	nlohmann::ordered_json newData = existingData;

	// Preserve the authored representation verbatim unless the user explicitly opts into a color write.
	if (includeColor) {
		auto authoredColor = current.data.diffuse;
		auto* colorLigh = activeLigh;
		if (current.data.lighFormId != 0 && (!colorLigh || colorLigh->GetFormID() != current.data.lighFormId)) {
			if (auto* form = RE::TESForm::LookupByID(current.data.lighFormId))
				colorLigh = form->As<RE::TESObjectLIGH>();
		}
		if (colorLigh && colorLigh->data.flags.any(RE::TES_LIGHT_FLAGS::kNegative)) {
			authoredColor.red = -authoredColor.red;
			authoredColor.green = -authoredColor.green;
			authoredColor.blue = -authoredColor.blue;
		}
		newData["color"] = {
			std::clamp(authoredColor.red, -1.0f, 1.0f),
			std::clamp(authoredColor.green, -1.0f, 1.0f),
			std::clamp(authoredColor.blue, -1.0f, 1.0f)
		};
	}

	const std::string editedLighEdid = LighEdidForFormId(current.data.lighFormId);
	if (!editedLighEdid.empty())
		newData["light"] = editedLighEdid;
	newData["fade"] = current.data.fade;
	if (isInvSq) {
		newData["size"] = std::clamp(current.data.size, 0.01f, 50.0f);
		newData["cutoff"] = std::clamp(current.data.cutoffOverride, 0.01f, 1.0f);
		if (current.data.radius != original.data.radius)
			newData["radius"] = current.data.radius;
	} else {
		newData["radius"] = current.data.radius;
		if (current.data.size != original.data.size)
			newData["size"] = std::clamp(current.data.size, 0.01f, 50.0f);
		if (current.data.cutoffOverride != original.data.cutoffOverride)
			newData["cutoff"] = std::clamp(current.data.cutoffOverride, 0.01f, 1.0f);
	}
	if (existingData.contains("shadowDepthBias") || shadowDepthBias != originalShadowDepthBias)
		newData["shadowDepthBias"] = shadowDepthBias;

	if (externalEmittanceChanged) {
		if (useExternalEmittance && !externalEmittanceEdid.empty())
			newData["externalEmittance"] = externalEmittanceEdid;
		else
			newData.erase("externalEmittance");
	}

	if (lpFlagSet != originalLpFlagSet) {
		std::string flags;
		for (const auto& flag : lpFlagSet) {
			if (!flags.empty())
				flags += '|';
			flags += flag;
		}
		if (flags.empty())
			newData.erase("flags");
		else
			newData["flags"] = std::move(flags);
	}
	return newData;
}

bool LightEditor::ValidateCurrentLightValues() const
{
	const auto finite = [](float value) { return std::isfinite(value); };
	const bool valuesAreFinite =
		finite(current.data.diffuse.red) && finite(current.data.diffuse.green) && finite(current.data.diffuse.blue) &&
		finite(current.data.fade) && finite(current.data.radius) && finite(current.data.size) &&
		finite(current.data.cutoffOverride) && finite(shadowDepthBias) &&
		finite(current.pos.x) && finite(current.pos.y) && finite(current.pos.z);
	const auto positionInRange = [](float value) {
		const double position = static_cast<double>(value);
		return position >= static_cast<double>(std::numeric_limits<int>::lowest()) &&
		       position <= static_cast<double>(std::numeric_limits<int>::max());
	};
	const bool valuesAreInRange =
		current.data.fade >= 0.0f && current.data.radius >= 0.0f &&
		current.data.size >= 0.01f && current.data.size <= 50.0f &&
		current.data.cutoffOverride >= 0.01f && current.data.cutoffOverride <= 1.0f &&
		shadowDepthBias >= 0.0f && shadowDepthBias <= 50.0f &&
		positionInRange(current.pos.x) && positionInRange(current.pos.y) && positionInRange(current.pos.z);
	if (!valuesAreFinite || !valuesAreInRange) {
		logger::warn("[LightEditor] Refusing to save non-finite or out-of-range Light Placer values");
		return false;
	}
	return true;
}

bool LightEditor::SaveAsSeparateEntry(bool includeColor)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;

	if (!lpInfo.isLPLight || !lpRawDataLoaded || !activeRefr)
		return false;
	if (!ValidateCurrentLightValues())
		return false;

	const auto activeRef = GetActiveRefr();
	const std::string ownerEntry = FormatOwnerFormEntry(activeRef.get());
	if (ownerEntry.empty())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	const MatchContext ctx = MakeSelectedContext();

	// Locate the governing light entry (+ parent array/index) so the fork can be inserted as the next sibling.
	LightEntryLocation loc;
	if (!LocateLightEntry(configArray, ctx, loc)) {
		logger::warn("[LightEditor] SaveAsSeparateEntry: no matching entry for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	nlohmann::ordered_json* lightsArr = loc.lightsArr;
	const size_t lightIdx = loc.lightIdx;
	auto& sourceEntry = (*lightsArr)[lightIdx];

	// Track whether this ref is whitelisted, and whether the whiteList is its alone (a prior fork);
	// a shared whiteList is not a fork and may still be split off.
	bool ownerWhitelisted = false;
	if (auto* wl = GetArrayMember(sourceEntry, "whiteList")) {
		ownerWhitelisted = ArrayContainsString(*wl, ownerEntry);
		if (ownerWhitelisted && wl->size() == 1) {
			logger::info("[LightEditor] SaveAsSeparateEntry: {} already has its own whitelisted entry", ownerEntry);
			return false;
		}
	}

	// Fork: deep copy, apply the current editor edits, whitelist only this reference.
	nlohmann::ordered_json forkedEntry = sourceEntry;
	forkedEntry["data"] = BuildEditedData(sourceEntry["data"], includeColor);

	if (current.pos != original.pos && !SetUniquePointFromPos(forkedEntry, current.pos)) {
		logger::warn("[LightEditor] Refusing to fork an edited position without one unique point/node");
		return false;
	}

	forkedEntry.erase("blackList");
	forkedEntry["whiteList"] = nlohmann::ordered_json::array({ ownerEntry });

	// Exclude this ref from the source so it resolves to the fork: drop from a shared whiteList, else blacklist.
	if (ownerWhitelisted)
		MutateFilterList(sourceEntry, "whiteList", ownerEntry, false);
	else
		MutateFilterList(sourceEntry, "blackList", ownerEntry, true);

	lightsArr->insert(lightsArr->begin() + lightIdx + 1, std::move(forkedEntry));

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	logger::info("[LightEditor] SaveAsSeparateEntry: forked {} into its own whitelisted entry in {}", ownerEntry, filePath.string());
	return true;
}

bool LightEditor::DeleteFromLightPlacer()
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;

	if (!lpInfo.isLPLight)
		return false;
	const auto activeRef = GetActiveRefr();
	if (!activeRef)
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	LightEntryLocation loc;
	if (!LocateLightEntry(configArray, MakeSelectedContext(), loc)) {
		logger::warn("[LightEditor] DeleteFromLightPlacer: no matching entry for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	// Removing the only light would leave a dangling models/formIDs block, so drop the whole top-level
	// entry; otherwise remove just this light.
	if (loc.lightsArr->size() <= 1)
		configArray.erase(configArray.begin() + loc.topIdx);
	else
		loc.lightsArr->erase(loc.lightsArr->begin() + loc.lightIdx);

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	logger::info("[LightEditor] Deleted light entry (model '{}', light '{}') from {}", lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
	return true;
}

void LightEditor::DrawDeleteConfirmation()
{
	if (deleteConfirmPopupRequested) {
		ImGui::OpenPopup("Delete");
		deleteConfirmPopupRequested = false;
	}

	if (auto popup = Util::CenteredPopupModal("Delete")) {
		ImGui::Text("%s", "Delete this light entry from the Light Placer JSON?");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (Util::ErrorButton("Yes, Delete")) {
			const bool ok = DeleteFromLightPlacer();
			if (ok) {
				RestoreOriginal();
				selected = {};
				previous = {};
				waitFrames = 3;
				ScheduleConsoleCommand("reloadlp");
			}
			NotifyResult(ok,
				"Deleted entry",
				"Delete failed \xe2\x80\x94 see log");
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
	}
}

void LightEditor::SortLights()
{
	// Other lights have no FormID/EditorID, so those sort modes fall back to None.
	if (filterOption == FilterOption::OtherLights && (sortOption == SortOption::FormID || sortOption == SortOption::EditorID))
		sortOption = SortOption::None;

	switch (sortOption) {
	case SortOption::Distance:
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				break;
			const auto playerPos = player->GetPosition();
			std::ranges::sort(lights, [&](const LightInfo& a, const LightInfo& b) {
				if (a.hasPosition != b.hasPosition)
					return a.hasPosition;
				return a.position.GetSquaredDistance(playerPos) < b.position.GetSquaredDistance(playerPos);
			});
			break;
		}
	case SortOption::FormID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return std::tie(a.id, a.index) < std::tie(b.id, b.index);
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

std::string LightEditor::FormatOwnerFormEntry(RE::TESObjectREFR* refr)
{
	// Single source of truth lives in the picker, which owns reference-identity resolution.
	return LightPicker::FormatRefFormEntry(refr);
}

bool LightEditor::RefreshLPJsonState()
{
	const auto activeRef = GetActiveRefr();
	if (!lpInfo.isLPLight || !activeRef)
		return false;

	const std::string ownerEntry = FormatOwnerFormEntry(activeRef.get());

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	// Apply WL/BL filters to resolve the entry that actually governs this reference; without them a
	// model/formID with two entries for the same light resolves to whichever appears first (wrong fade/flags).
	const auto* lightEntry = FindMatchingLightEntry(configArray, MakeSelectedContext(), true);
	if (!lightEntry)
		return false;

	const auto dataIt = lightEntry->find("data");
	if (dataIt == lightEntry->end() || !dataIt->is_object()) {
		logger::warn("[LightEditor] Invalid Light Placer data for '{}' in {}", lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	bool parsedInWhitelist = false;
	bool parsedInBlacklist = false;
	if (!ownerEntry.empty()) {
		auto containsEntry = [&](const char* listKey) {
			const auto* list = GetArrayMember(*lightEntry, listKey);
			return list && ArrayContainsString(*list, ownerEntry);
		};
		parsedInWhitelist = containsEntry("whiteList");
		parsedInBlacklist = containsEntry("blackList");
	}

	std::set<std::string> parsedFlagSet;
	const auto flagsIt = dataIt->find("flags");
	if (flagsIt != dataIt->end()) {
		if (!flagsIt->is_string()) {
			logger::warn("[LightEditor] Invalid Light Placer flags for '{}' in {}", lpInfo.lightEDID, lpInfo.configPath);
			return false;
		}
		std::istringstream ss(flagsIt->get<std::string>());
		std::string flag;
		while (std::getline(ss, flag, '|')) {
			if (!flag.empty())
				parsedFlagSet.insert(flag);
		}
	}
	const bool parsedIgnoreScale = parsedFlagSet.contains("IgnoreScale");

	RE::TESObjectLIGH* resolvedLigh = nullptr;
	const auto lightIt = dataIt->find("light");
	if (lightIt != dataIt->end() && lightIt->is_string()) {
		EnsureLighFormListBuilt();
		const auto& lightEdid = lightIt->get_ref<const std::string&>();
		for (const auto& [edid, candidate] : s_lighFormList) {
			if (edid == lightEdid) {
				resolvedLigh = candidate;
				break;
			}
		}
	}
	if (!resolvedLigh) {
		logger::warn("[LightEditor] Could not resolve Light Placer LIGH '{}' in {}", lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	constexpr float maxFloat = std::numeric_limits<float>::max();
	const float baseFade = resolvedLigh->fade;
	const float baseRadius = static_cast<float>(resolvedLigh->data.radius);
	const float baseSize = resolvedLigh->data.fov;
	const float baseCutoff = resolvedLigh->data.fallofExponent;
	float fadeValue = baseFade;
	float radiusValue = baseRadius;
	float sizeValue = baseSize;
	float cutoffValue = baseCutoff;
	auto readData = [&](const char* key, float fallback, float& value) {
		if (TryReadFiniteFloat(*dataIt, key, fallback, -maxFloat, maxFloat, value))
			return true;
		logger::warn("[LightEditor] Invalid Light Placer '{}' value for '{}' in {}", key, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	};
	if (!readData("fade", baseFade, fadeValue) ||
		!readData("radius", baseRadius, radiusValue) ||
		!readData("size", baseSize, sizeValue) ||
		!readData("cutoff", baseCutoff, cutoffValue)) {
		return false;
	}

	const float parsedFade = fadeValue > 0.0f ? fadeValue : baseFade;
	const float parsedRadius = radiusValue > 0.0f ? radiusValue : baseRadius;
	float parsedSize = sizeValue > 0.0f ? sizeValue : baseSize;
	float parsedCutoff = cutoffValue > 0.0f ? cutoffValue : baseCutoff;
	if (parsedSize >= 50.0f)
		parsedSize = 1.414f;
	parsedSize = std::clamp(parsedSize, 0.01f, 50.0f);
	parsedCutoff = std::clamp(parsedCutoff, 0.01f, 1.0f);

	float fallbackFactor = parsedIgnoreScale ? 1.0f : activeRef->GetScale();
	if (!std::isfinite(fallbackFactor) || fallbackFactor <= 0.0f)
		fallbackFactor = 1.0f;
	const auto inferFactor = [](float runtimeValue, float rawValue, float fallback) {
		if (std::isfinite(runtimeValue) &&
			std::isfinite(rawValue) &&
			rawValue != 0.0f) {
			const float factor = runtimeValue / rawValue;
			if (std::isfinite(factor) && factor >= 0.0f)
				return factor;
		}
		return fallback;
	};
	const float runtimeRadius = lpInfo.runtimeSnapshot.flags.any(LightLimitFix::LightFlags::Initialised) ?
	                                lpInfo.runtimeSnapshot.originalRadius :
	                                lpInfo.runtimeSnapshot.radius;
	const float parsedFadeFactor = inferFactor(lpInfo.runtimeSnapshot.fade, parsedFade, fallbackFactor);
	const float parsedRadiusFactor = inferFactor(runtimeRadius, parsedRadius, fallbackFactor);
	const float parsedSizeFactor = inferFactor(lpInfo.runtimeSnapshot.size, parsedSize, fallbackFactor);

	const bool hasAuthoredShadowBias = dataIt->contains("shadowDepthBias");
	float parsedShadowBias = shadowDepthBias;
	if (hasAuthoredShadowBias &&
		!TryReadFiniteFloat(*dataIt, "shadowDepthBias", shadowDepthBias, 0.0f, 50.0f, parsedShadowBias)) {
		logger::warn("[LightEditor] Invalid Light Placer shadowDepthBias for '{}' in {}", lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	RE::TESForm* parsedEmittanceSource = nullptr;
	std::string parsedExternalEmittanceEdid;
	bool parsedUseExternalEmittance = false;
	const auto emitIt = dataIt->find("externalEmittance");
	if (emitIt != dataIt->end() && !emitIt->is_string()) {
		logger::warn("[LightEditor] Invalid Light Placer externalEmittance for '{}' in {}", lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}
	if (!parsedFlagSet.contains("NoExternalEmittance") && emitIt != dataIt->end()) {
		if (const std::string entry = emitIt->get<std::string>(); !entry.empty()) {
			EnsureEmittanceFormListBuilt();
			for (auto& [edid, form] : s_emittanceFormList) {
				if (edid == entry) {
					parsedEmittanceSource = form;
					break;
				}
			}
			if (!parsedEmittanceSource) {
				if (const auto formId = ResolveFormEntry(entry); formId != 0)
					parsedEmittanceSource = RE::TESForm::LookupByID(formId);
			}
			parsedExternalEmittanceEdid = parsedEmittanceSource ?
			                                  clib_util::editorID::get_editorID(parsedEmittanceSource) :
			                                  entry;
			parsedUseExternalEmittance = true;
		}
	}

	// Commit only after the entire entry has validated, so a malformed later field cannot leak raw
	// authored values into the live effective runtime state or poison the restoration snapshot.
	activeLigh = resolvedLigh;
	lpInWhitelist = parsedInWhitelist;
	lpInBlacklist = parsedInBlacklist;
	lpFlagSet = std::move(parsedFlagSet);
	lpInfo.ignoreScale = parsedIgnoreScale;
	lpInfo.fadeFactor = parsedFadeFactor;
	lpInfo.radiusFactor = parsedRadiusFactor;
	lpInfo.sizeFactor = parsedSizeFactor;
	original.data.fade = current.data.fade = parsedFade;
	original.data.radius = current.data.radius = parsedRadius;
	original.data.originalRadius = current.data.originalRadius = parsedRadius;
	original.data.size = current.data.size = parsedSize;
	original.data.cutoffOverride = current.data.cutoffOverride = parsedCutoff;
	if (hasAuthoredShadowBias) {
		shadowDepthBias = originalShadowDepthBias = parsedShadowBias;
		ApplyShadowDepthBias();
	}
	activeEmittanceSource = parsedEmittanceSource;
	externalEmittanceEdid = std::move(parsedExternalEmittanceEdid);
	useExternalEmittance = parsedUseExternalEmittance;

	SyncLPFlagsToRuntime();

	// SyncLPFlagsToRuntime only updates `current`; apply the JSON InverseSquare/Linear state to `original`
	// too, so RestoreOriginal can't re-assert a stale IS bit (which makes ProcessLight re-inflate the radius).
	ApplyLPFalloffFlags(original.data, lpFlagSet);
	return true;
}

void LightEditor::ApplyLPFalloffFlags(ISLCommon::RuntimeLightDataExt& data, const std::set<std::string>& lpFlagSet)
{
	auto apply = [&](LightLimitFix::LightFlags bit, const char* name) {
		if (lpFlagSet.contains(name))
			data.flags.set(bit);
		else
			data.flags.reset(bit);
	};
	apply(LightLimitFix::LightFlags::InverseSquare, "InverseSquare");
	apply(LightLimitFix::LightFlags::Linear, "Linear");
}

void LightEditor::SyncLPFlagsToRuntime()
{
	if (!lpInfo.isLPLight)
		return;

	ApplyLPFalloffFlags(current.data, lpFlagSet);

	auto& tesUnderlying = reinterpret_cast<uint32_t&>(current.tesFlags);
	auto syncTesBit = [&](RE::TES_LIGHT_FLAGS bit, bool val) {
		const auto mask = static_cast<uint32_t>(bit);
		if (val)
			tesUnderlying |= mask;
		else
			tesUnderlying &= ~mask;
	};
	syncTesBit(RE::TES_LIGHT_FLAGS::kFlicker, lpFlagSet.contains("Flicker"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kOmniShadow, lpFlagSet.contains("Shadow"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kPortalStrict, lpFlagSet.contains("PortalStrict"));
}

void LightEditor::MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add)
{
	if (add) {
		auto& list = lightEntry[listKey];
		if (!list.is_array())
			list = nlohmann::ordered_json::array();
		if (ArrayContainsString(list, ownerEntry))
			return;
		list.push_back(ownerEntry);
	} else {
		// Use find, not operator[]: indexing a missing key would materialize a stray "listKey": null
		// on this entry. Removal only ever runs on an entry already known to hold the value.
		const auto it = lightEntry.find(listKey);
		if (it == lightEntry.end() || !it->is_array())
			return;
		auto& list = *it;
		list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& elem) {
			return elem.is_string() && elem.template get<std::string>() == ownerEntry;
		}),
			list.end());
		if (list.empty())
			lightEntry.erase(listKey);
	}
}

bool LightEditor::ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, const std::string& entryStr, bool isWhiteList, bool add)
{
	if (State::GetSingleton()->IsPersistentMutationBlocked())
		return false;

	if (!ctx.refr || entryStr.empty())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(configPath, configArray))
		return false;

	const char* listKey = isWhiteList ? "whiteList" : "blackList";
	std::vector<nlohmann::ordered_json*> allCandidates;
	std::vector<nlohmann::ordered_json*> ownerCandidates;
	std::vector<nlohmann::ordered_json*> filteredCandidates;
	std::vector<nlohmann::ordered_json*> ownerFilteredCandidates;
	std::vector<nlohmann::ordered_json*> allHolders;
	std::vector<nlohmann::ordered_json*> ownerHolders;
	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;
		const bool ownerMatches = EntryMatchesContext(entry, ctx);
		for (auto& candidate : *lightsIt) {
			const auto* edid = GetLightEntryEdid(candidate);
			if (!edid || *edid != ctx.lightEDID)
				continue;
			if (!HasWellFormedLPFilters(candidate)) {
				logger::warn("[LightEditor] Refusing to mutate malformed Light Placer filters for '{}' in {}", ctx.lightEDID, configPath);
				return false;
			}

			allCandidates.push_back(&candidate);
			if (ownerMatches)
				ownerCandidates.push_back(&candidate);
			if (MatchesLPFilters(candidate, ctx.refr.get())) {
				filteredCandidates.push_back(&candidate);
				if (ownerMatches)
					ownerFilteredCandidates.push_back(&candidate);
			}
			const auto* list = GetArrayMember(candidate, listKey);
			if (list && ArrayContainsString(*list, entryStr)) {
				allHolders.push_back(&candidate);
				if (ownerMatches)
					ownerHolders.push_back(&candidate);
			}
		}
	}

	auto chooseUnique = [&](const std::vector<nlohmann::ordered_json*>& candidates, const char* tier,
							bool& ambiguous) -> nlohmann::ordered_json* {
		if (candidates.size() == 1)
			return candidates.front();
		if (candidates.size() > 1) {
			ambiguous = true;
			logger::warn("[LightEditor] Refusing ambiguous filter {}: {} entries for '{}' in {}",
				tier, candidates.size(), ctx.lightEDID, configPath);
		}
		return nullptr;
	};

	bool ambiguous = false;
	nlohmann::ordered_json* lightEntry = nullptr;
	if (add) {
		lightEntry = chooseUnique(ownerFilteredCandidates, "owner match", ambiguous);
		if (!lightEntry && !ambiguous)
			lightEntry = chooseUnique(ownerCandidates, "owner fallback", ambiguous);
		if (!lightEntry && !ambiguous)
			lightEntry = chooseUnique(filteredCandidates, "filtered fallback", ambiguous);
		if (!lightEntry && !ambiguous)
			lightEntry = chooseUnique(allCandidates, "unfiltered fallback", ambiguous);
	} else {
		lightEntry = chooseUnique(ownerHolders, "removal owner match", ambiguous);
		if (!lightEntry && !ambiguous)
			lightEntry = chooseUnique(allHolders, "removal fallback", ambiguous);
	}

	if (!lightEntry)
		return false;
	if (add) {
		if (const auto* list = GetArrayMember(*lightEntry, listKey); list && ArrayContainsString(*list, entryStr))
			return true;
	}

	MutateFilterList(*lightEntry, listKey, entryStr, add);
	return WriteLPConfig(LPConfigFilePath(configPath), configArray);
}

bool LightEditor::ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add)
{
	if (!ctx.refr)
		return false;
	const std::string ownerEntry = FormatOwnerFormEntry(ctx.refr.get());
	return ModifyLPFilterListFor(configPath, ctx, ownerEntry, isWhiteList, add);
}

bool LightEditor::ModifyLPFilterList(bool isWhiteList, bool add)
{
	if (!lpInfo.isLPLight || !activeRefr)
		return false;
	return ModifyLPFilterListFor(lpInfo.configPath, MakeSelectedContext(), isWhiteList, add);
}
