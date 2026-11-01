#define I18N_KEY_PREFIX "cs_editor."
#include "EditorWindow.h"
#include "I18n/I18n.h"

#include "Features/CSEditor.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "InteriorOnlyPanel.h"
#include "Menu.h"
#include "Menu/BackgroundBlur.h"
#include "PaletteWindow.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"
#include "Utils/Subrect.h"
#include "Utils/UI.h"
#include "Weather/LightingTemplateWidget.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <imgui_stdlib.h>
#include <optional>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteColorEntry, r, g, b, useCount, lastUsedTime, isFavorite)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteValueEntry, name, value, useCount, lastUsedTime, isFavorite)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteFavoriteColor, hasValue, r, g, b)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings, recordMarkers, markedRecords, autoApplyChanges, useTextButtons, enableInheritFromParent, editorUIScale, favoriteWidgets, recentWidgets, maxRecentWidgets, showViewport, selectedCategory, widgetTypeSizes, paletteColors, paletteValues, paletteFavorites)

void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	constexpr int numPoints = 5;
	const float angleStep = IM_PI / numPoints;
	ImVec2 points[10];

	for (int i = 0; i < numPoints * 2; i++) {
		float angle = -IM_PI * 0.5f + i * angleStep;
		float r = (i % 2 == 0) ? radius : radius * 0.38f;
		points[i] = ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r);
	}

	if (filled) {
		// Disable AA fill temporarily — ImGui adds a 1px fringe around each filled shape
		// that creates visible seams at the pentagon/triangle boundaries.
		ImDrawListFlags oldFlags = drawList->Flags;
		drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

		ImVec2 innerPentagon[5];
		for (int i = 0; i < 5; i++) {
			innerPentagon[i] = points[i * 2 + 1];  // inner points
		}
		drawList->AddConvexPolyFilled(innerPentagon, 5, color);
		for (int i = 0; i < 5; i++) {
			drawList->AddTriangleFilled(points[i * 2], points[i * 2 + 1], points[(i * 2 + 9) % 10], color);
		}

		drawList->Flags = oldFlags;

		// Draw an AA polyline over the outer perimeter to restore smooth edges
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	} else {
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	}
}

void DrawIconCircle(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	if (filled)
		drawList->AddCircleFilled(center, radius, color, 16);
	else
		drawList->AddCircle(center, radius, color, 16, 1.5f * Util::GetUIScale());
}

void DrawIconWave(ImVec2 center, float width, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const float thickness = (filled ? 3.0f : 1.5f) * Util::GetUIScale();
	const int segments = 8;
	const float amplitude = width * 0.15f;
	const float waveWidth = width * 0.8f;
	const float segmentWidth = waveWidth / segments;
	ImVec2 start(center.x - waveWidth * 0.5f, center.y);

	for (int i = 0; i < segments; i++) {
		float x1 = start.x + i * segmentWidth;
		float x2 = start.x + (i + 1) * segmentWidth;
		float y1 = start.y + sinf(i * 3.14159f / 2.0f) * amplitude;
		float y2 = start.y + sinf((i + 1) * 3.14159f / 2.0f) * amplitude;
		drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, thickness);
	}
}

bool IconButton(const char* label, bool filled, const char* iconType)
{
	ImVec2 buttonSize(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
	ImVec2 cursorPos = ImGui::GetCursorScreenPos();

	bool result = ImGui::InvisibleButton(label, buttonSize);

	bool hovered = ImGui::IsItemHovered();
	bool active = ImGui::IsItemActive();

	ImU32 bgColor = active  ? ImGui::GetColorU32(ImGuiCol_ButtonActive) :
	                hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) :
	                          ImGui::GetColorU32(ImGuiCol_Button);
	ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);

	auto* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + buttonSize.x, cursorPos.y + buttonSize.y), bgColor, ImGui::GetStyle().FrameRounding);

	ImVec2 center(cursorPos.x + buttonSize.x * 0.5f, cursorPos.y + buttonSize.y * 0.5f);
	float iconSize = buttonSize.x * 0.35f;

	if (strcmp(iconType, "star") == 0) {
		DrawIconStar(center, iconSize, iconColor, filled);
	} else if (strcmp(iconType, "circle") == 0) {
		DrawIconCircle(center, iconSize, iconColor, filled);
	} else if (strcmp(iconType, "wave") == 0) {
		DrawIconWave(center, buttonSize.x * 0.7f, iconColor, filled);
	}

	return result;
}

namespace
{
	constexpr const char* kFilterColumnNames[] = { "All", "Editor ID", "Form ID", "File", "Status" };
}  // namespace

void EditorWindow::ResetObjectsFilter()
{
	m_currentFilterColumn = FilterColumn::All;
	m_filterBuffer[0] = '\0';
	m_showOnlyFlagged = false;
	m_showOnlyFavorites = false;
}

bool EditorWindow::MatchesObjectFilter(Widget* w) const
{
	static_assert(static_cast<int>(FilterColumn::Count_) == IM_ARRAYSIZE(kFilterColumnNames),
		"kFilterColumnNames must have one entry per FilterColumn value");
	if (!w)
		return false;
	if (m_filterBuffer[0] == '\0')
		return true;
	switch (m_currentFilterColumn) {
	case FilterColumn::EditorID:
		return ContainsStringIgnoreCase(w->GetEditorID(), m_filterBuffer);
	case FilterColumn::FormID:
		return ContainsStringIgnoreCase(w->GetFormID(), m_filterBuffer);
	case FilterColumn::File:
		return ContainsStringIgnoreCase(w->GetFilename(), m_filterBuffer);
	case FilterColumn::Status:
		{
			auto it = settings.markedRecords.find(w->GetEditorID());
			return it != settings.markedRecords.end() && ContainsStringIgnoreCase(it->second, m_filterBuffer);
		}
	case FilterColumn::All:
	default:
		{
			const auto editorId = w->GetEditorID();
			if (ContainsStringIgnoreCase(editorId, m_filterBuffer))
				return true;
			if (ContainsStringIgnoreCase(w->GetFormID(), m_filterBuffer))
				return true;
			if (ContainsStringIgnoreCase(w->GetFilename(), m_filterBuffer))
				return true;
			auto it = settings.markedRecords.find(editorId);
			if (it != settings.markedRecords.end() && ContainsStringIgnoreCase(it->second, m_filterBuffer))
				return true;
			return false;
		}
	}
}

std::string EditorWindow::ResolveEditorId(RE::TESForm* form, const WidgetVec& widgets)
{
	if (!form)
		return "";
	for (const auto& widget : widgets) {
		if (widget->form && widget->form->GetFormID() == form->GetFormID())
			return widget->GetEditorID();
	}
	const char* editorid = form->GetFormEditorID();
	return editorid ? editorid : std::format("0x{:08X}", form->GetFormID());
}

void EditorWindow::ShowObjectsWindow()
{
	Util::BeginWithRoundedClose(T("cs_editor.weather_lighting_browser", "CS Editor Browser"), nullptr);

	// Reset filter state when the user switches categories so stale column
	// selections (e.g. Status) don't hide all items in the new category.
	if (m_selectedCategory != m_previousSelectedCategory) {
		ResetObjectsFilter();
		m_previousSelectedCategory = m_selectedCategory;
	}

	// Create a table with two columns
	if (ImGui::BeginTable("ObjectTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner)) {
		// Fixed categories column, objects column fills remaining width
		const float categoriesWidth = 180.0f * Util::GetUIScale();
		ImGui::TableSetupColumn(T("cs_editor.categories", "Categories"), ImGuiTableColumnFlags_WidthFixed, categoriesWidth);
		ImGui::TableSetupColumn(T("cs_editor.objects", "Objects"), ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		if (resetLayout)
			ImGui::TableSetColumnWidth(0, categoriesWidth);

		// Left column: Categories
		ImGui::TableSetColumnIndex(0);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
		if (ImGui::BeginListBox("##CategoriesList", { -FLT_MIN, -FLT_MIN })) {
			ImGui::TextUnformatted(T("cs_editor.categories", "Categories"));
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// List of categories
			const std::pair<const char*, const char*> categories[] = {
				{ "Weather", T("cs_editor.category_weather", "Weather") },
				{ "ImageSpace", T("cs_editor.category_imagespace", "ImageSpace") },
				{ "Lighting Template", T("cs_editor.category_lighting_template", "Lighting Template") },
				{ "Cell Lighting", T("cs_editor.category_cell_lighting", "Cell Lighting") },
				{ "Volumetric Lighting", T("cs_editor.category_volumetric_lighting", "Volumetric Lighting") },
				{ "Shader Particle Geometry", T("cs_editor.category_shader_particle", "Shader Particle Geometry") },
				{ "Lens Flare", T("cs_editor.category_lens_flare", "Lens Flare") },
				{ "Visual Effect", T("cs_editor.category_visual_effect", "Visual Effect") },
				{ "Interior Only", T("cs_editor.category_interior_only", "Interior Only") },
				{ "Light Editor", T("cs_editor.category_lighting_editor", "Light Editor") }
			};
			for (const auto& [id, label] : categories) {
				if (ImGui::Selectable(label, m_selectedCategory == id))
					m_selectedCategory = id;
			}
			ImGui::EndListBox();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// Right column: Objects
		ImGui::TableSetColumnIndex(1);

		if (ImGui::BeginChild("##ObjectsContent", { 0, 0 }, ImGuiChildFlags_Borders, kStickyHeaderFlags)) {
			// Interior Only category has its own panel
			if (m_selectedCategory == "Interior Only") {
				InteriorOnlyPanel::Draw();
				ImGui::EndChild();
				ImGui::EndTable();
				ImGui::End();
				return;
			}

			if (m_selectedCategory == "Light Editor") {
				BeginScrollableContent("##LightEditorScroll");
				lightEditor.DrawSettings();
				EndScrollableContent();
				ImGui::EndChild();
				ImGui::EndTable();
				ImGui::End();
				return;
			}

			// Returns the widget collection for a given category; Cell Lighting and unknown
			// categories return an empty collection since they have no standalone widget list.
			auto getWidgetsForCategory = [&](const std::string& cat) -> const std::vector<std::unique_ptr<Widget>>& {
				static const std::vector<std::unique_ptr<Widget>> emptyWidgets;
				if (cat == "Weather")
					return weatherWidgets;
				if (cat == "Lighting Template")
					return lightingTemplateWidgets;
				if (cat == "ImageSpace")
					return imageSpaceWidgets;
				if (cat == "Volumetric Lighting")
					return volumetricLightingWidgets;
				if (cat == "Shader Particle Geometry")
					return precipitationWidgets;
				if (cat == "Lens Flare")
					return lensFlareWidgets;
				if (cat == "Visual Effect")
					return referenceEffectWidgets;
				return emptyWidgets;
			};

			// Build active records for the current category tab
			struct ActiveRecord
			{
				std::string label;
				std::string suffix;
				RE::FormID formId;
				std::function<void()> open;
			};
			std::vector<ActiveRecord> activeRecords;

			{
				auto* sky = globals::game::sky;
				auto* weather = sky ? sky->currentWeather : nullptr;

				auto openByFormId = [](RE::FormID id, const WidgetVec* widgets) -> std::function<void()> {
					return [id, widgets]() {
						for (const auto& widget : *widgets) {
							if (widget->form && widget->form->GetFormID() == id) {
								widget->SetOpen(true);
								widget->RequestFocus();
								break;
							}
						}
					};
				};

				auto addSingle = [&](RE::TESForm* form, const WidgetVec& widgets, std::string suffix = "") {
					if (!form)
						return;
					auto id = form->GetFormID();
					activeRecords.push_back({ ResolveEditorId(form, widgets), std::move(suffix), id, openByFormId(id, &widgets) });
				};

				auto addTOD = [&](auto*(&fields)[RE::TESWeather::ColorTimes::kTotal], const WidgetVec& widgets) {
					for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
						auto* form = fields[tod];
						if (!form)
							continue;
						auto id = form->GetFormID();
						bool already = std::any_of(activeRecords.begin(), activeRecords.end(),
							[&](const ActiveRecord& r) { return r.formId == id; });
						if (!already)
							activeRecords.push_back({ ResolveEditorId(form, widgets), TOD::GetPeriodLabel(tod), id, openByFormId(id, &widgets) });
					}
				};

				auto addWeather = [&](RE::TESWeather* weatherRecord, std::string suffix = "") {
					if (!weatherRecord)
						return;
					auto id = weatherRecord->GetFormID();
					activeRecords.push_back({ ResolveEditorId(weatherRecord, weatherWidgets), std::move(suffix), id, openByFormId(id, &weatherWidgets) });
				};

				if (m_selectedCategory == "Weather") {
					addWeather(weather);
					if (sky && sky->lastWeather != weather)
						addWeather(sky->lastWeather, T("cs_editor.transitioning", "transitioning"));
				} else if (m_selectedCategory == "ImageSpace") {
					if (weather)
						addTOD(weather->imageSpaces, imageSpaceWidgets);
				} else if (m_selectedCategory == "Lighting Template") {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell)
						addSingle(player->parentCell->GetRuntimeData().lightingTemplate, lightingTemplateWidgets);
				} else if (m_selectedCategory == "Cell Lighting") {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
						auto* cell = player->parentCell;
						const char* cellName = cell->GetName();
						std::string displayName = cellName && cellName[0] ? cellName : T("cs_editor.unnamed_cell", "[Unnamed Cell]");
						activeRecords.push_back({ std::move(displayName), "", cell->GetFormID(),
							[this, cell]() {
								if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
									currentCellLightingWidget->SetOpen(true);
									currentCellLightingWidget->RequestFocus();
								} else {
									OpenCellLightingWidget(cell, false, true);
								}
							} });
					}
				} else if (m_selectedCategory == "Volumetric Lighting") {
					if (weather)
						addTOD(weather->volumetricLighting, volumetricLightingWidgets);
				} else if (m_selectedCategory == "Shader Particle Geometry") {
					if (weather)
						addSingle(weather->precipitationData, precipitationWidgets);
				} else if (m_selectedCategory == "Lens Flare") {
					if (weather)
						addSingle(weather->sunGlareLensFlare, lensFlareWidgets);
				} else if (m_selectedCategory == "Visual Effect") {
					if (weather)
						addSingle(weather->referenceEffect, referenceEffectWidgets);
				}

				// Fall back to current weather when the active category has no active record
				if (activeRecords.empty())
					addWeather(weather);
			}

			if (activeRecords.size() > 4)
				activeRecords.resize(4);

			if (!activeRecords.empty()) {
				const auto& theme = Menu::GetSingleton()->GetTheme();

				ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.RestartNeeded);
				ImGui::TextUnformatted(T("cs_editor.active", "Active:"));
				ImGui::PopStyleColor();
				ImGui::SameLine();
				const float recordX = ImGui::GetCursorPosX();

				for (int i = 0; i < (int)activeRecords.size(); ++i) {
					if (i > 0) {
						ImGui::NewLine();
						ImGui::SameLine(recordX);
					}
					const auto& rec = activeRecords[i];
					ImGui::TextColored(theme.Palette.Text, "%s", rec.label.c_str());
					ImGui::SameLine();
					if (!rec.suffix.empty()) {
						ImGui::TextDisabled("(%s)", rec.suffix.c_str());
						ImGui::SameLine();
					}
					ImGui::TextDisabled("(0x%08X)", rec.formId);
					ImGui::SameLine();
					const auto btnId = std::format("{}##active_{}", T("cs_editor.open", "Open"), i);
					if (ImGui::SmallButton(btnId.c_str()))
						rec.open();
				}
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			// Handle Ctrl+F to focus search bar
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
				if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
					ImGui::SetKeyboardFocusHere();
				}
			}
			// Compute fixed widths once; reuse for both the search bar and the following combo.
			const auto& style = ImGui::GetStyle();
			// comboW = preview text + left/right padding + arrow button
			const float comboW = ImGui::CalcTextSize(T("cs_editor.editor_id", "Editor ID")).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
			const float helpW = ImGui::CalcTextSize("(?)").x;
			const float iconW = ImGui::GetFrameHeight();
			const float scale = Util::GetUIScale();
			const float spacerW = 10.0f * scale;
			// Fixed width is the sum of every item that follows the search bar on the same row.
			// Each SameLine() contributes style.ItemSpacing.x; widths are listed explicitly
			// so adding or removing a widget only requires updating its own expression.
			const float fixedW =
				style.ItemSpacing.x + comboW +                                                        // combo
				style.ItemSpacing.x + helpW +                                                         // help marker
				style.ItemSpacing.x + spacerW +                                                       // spacer before favorites
				style.ItemSpacing.x + iconW +                                                         // fav icon
				style.ItemSpacing.x + ImGui::CalcTextSize(T("cs_editor.favorites", "Favorites")).x +  // T("cs_editor.favorites", "Favorites") label
				style.ItemSpacing.x + spacerW +                                                       // spacer before flagged
				style.ItemSpacing.x + iconW +                                                         // flag icon
				style.ItemSpacing.x + ImGui::CalcTextSize(T("cs_editor.flagged", "Flagged")).x;       // T("cs_editor.flagged", "Flagged") label
			ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x - fixedW));
			ImGui::InputTextWithHint("##ObjectFilter", T("cs_editor.filter_hint", "Filter... (Ctrl+F)"), m_filterBuffer, sizeof(m_filterBuffer));

			ImGui::SameLine();
			ImGui::SetNextItemWidth(comboW);
			int col = static_cast<int>(m_currentFilterColumn);
			const char* filterColumnLabels[] = { T("cs_editor.filter_all", "All"), T("cs_editor.filter_editor_id", "Editor ID"), T("cs_editor.filter_form_id", "Form ID"), T("cs_editor.filter_file", "File"), T("cs_editor.filter_status", "Status") };
			static_assert(IM_ARRAYSIZE(filterColumnLabels) == IM_ARRAYSIZE(kFilterColumnNames));
			if (Util::Widgets::Combo("##FilterBy", &col, filterColumnLabels, IM_ARRAYSIZE(filterColumnLabels)))
				m_currentFilterColumn = static_cast<FilterColumn>(col);

			ImGui::SameLine();
			Util::HelpMarker(T("editor.editor_window.search_the_selected_column_choose_all_to_sear_1bccca5d", "Search the selected column. Choose All to search Editor ID, Form ID, File and Status. A non-empty Status search hides unmarked items.\nCtrl+F: focus search\nEnter: open selection"));

			// Quick filter buttons
			const ImVec2 filterSpacer(spacerW, 0.0f);
			ImGui::SameLine();
			ImGui::Dummy(filterSpacer);
			ImGui::SameLine();
			if (IconButton("##filterFavorites", m_showOnlyFavorites, "star")) {
				m_showOnlyFavorites = !m_showOnlyFavorites;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(T("cs_editor.favorites", "Favorites"));

			ImGui::SameLine();
			ImGui::Dummy(filterSpacer);
			ImGui::SameLine();
			if (IconButton("##filterFlagged", m_showOnlyFlagged, "circle")) {
				m_showOnlyFlagged = !m_showOnlyFlagged;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(T("cs_editor.flagged", "Flagged"));

			// Show recent widgets section for current category
			auto recentIt = settings.recentWidgets.find(m_selectedCategory);
			if (recentIt != settings.recentWidgets.end() && !recentIt->second.empty()) {
				ImGui::Spacing();
				ImGui::TextUnformatted(T("cs_editor.recent", "Recent:"));
				ImGui::SameLine();
				for (size_t i = 0; i < std::min(size_t(5), recentIt->second.size()); ++i) {
					if (i > 0)
						ImGui::SameLine();
					if (ImGui::SmallButton(recentIt->second[i].c_str())) {
						// Find and open widget in current category's collection
						const auto& widgets = getWidgetsForCategory(m_selectedCategory);
						for (auto& widget : widgets) {
							if (widget->GetEditorID() == recentIt->second[i]) {
								widget->SetOpen(true);
								break;
							}
						}
					}
				}
			}

			// Scrollable area for the object table
			BeginScrollableContent("##ObjectsScrollable");

			// Stable user IDs for sortable columns — used instead of ColumnIndex so reordering/insertion won't break sorting.
			enum ColumnID : ImGuiID
			{
				ColFav = 0,
				ColEditorID,
				ColFormID,
				ColFile,
				ColStatus,
				ColJson
			};

			// Create a table for the right column with "Name" and "ID" headers. Different weights to prevent truncation.
			if (ImGui::BeginTable("DetailsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable)) {
				ImGui::TableSetupColumn(T("cs_editor.fav", "Fav"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 38.0f * scale, ColFav);  // Favorite indicator
				ImGui::TableSetupColumn(T("cs_editor.editor_id", "Editor ID"), ImGuiTableColumnFlags_WidthStretch, 3.5f, ColEditorID);                       // Largest - weather/template names
				ImGui::TableSetupColumn(T("cs_editor.form_id", "Form ID"), ImGuiTableColumnFlags_WidthFixed, 90.0f * scale, ColFormID);                      // Fixed - 8 hex chars
				ImGui::TableSetupColumn(T("cs_editor.file", "File"), ImGuiTableColumnFlags_WidthStretch, 2.0f, ColFile);                                     // Medium - plugin names
				ImGui::TableSetupColumn(T("cs_editor.status", "Status"), ImGuiTableColumnFlags_WidthStretch, 1.5f, ColStatus);                               // Smaller - status text
				ImGui::TableSetupColumn(T("cs_editor.json", "json"), ImGuiTableColumnFlags_WidthFixed, 55.0f * scale, ColJson);                              // JSON file / delete

				ImGui::TableHeadersRow();

				// Handle column sorting
				if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
					if (sortSpecs->SpecsDirty) {
						if (sortSpecs->SpecsCount > 0) {
							const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
							switch (spec.ColumnUserID) {
							case ColEditorID:
								currentSortColumn = SortColumn::EditorID;
								break;
							case ColFormID:
								currentSortColumn = SortColumn::FormID;
								break;
							case ColFile:
								currentSortColumn = SortColumn::File;
								break;
							case ColStatus:
								currentSortColumn = SortColumn::Status;
								break;
							case ColJson:
								currentSortColumn = SortColumn::JsonAttachment;
								break;
							default:
								currentSortColumn = SortColumn::None;
								break;
							}
							sortAscending = (spec.SortDirection == ImGuiSortDirection_Ascending);
						} else {
							currentSortColumn = SortColumn::None;
						}
						sortSpecs->SpecsDirty = false;
					}
				}

				// Display objects based on the selected category
				const auto& widgets = getWidgetsForCategory(m_selectedCategory);
				// Sort widgets based on current sort column
				std::vector<Widget*> sortedWidgets;
				sortedWidgets.reserve(widgets.size());
				for (const auto& w : widgets) {
					sortedWidgets.push_back(w.get());
				}
				RefreshJsonAttachmentCache(sortedWidgets);
				bool weatherTooltipShownThisFrame = false;
				if (currentSortColumn != SortColumn::None) {
					std::sort(sortedWidgets.begin(), sortedWidgets.end(), [this](Widget* a, Widget* b) {
						int comparison = 0;
						switch (currentSortColumn) {
						case SortColumn::EditorID:
							comparison = _stricmp(a->GetEditorID().c_str(), b->GetEditorID().c_str());
							break;
						case SortColumn::FormID:
							comparison = _stricmp(a->GetFormID().c_str(), b->GetFormID().c_str());
							break;
						case SortColumn::File:
							comparison = _stricmp(a->GetFilename().c_str(), b->GetFilename().c_str());
							break;
						case SortColumn::Status:
							{
								auto markerA = settings.markedRecords.find(a->GetEditorID());
								auto markerB = settings.markedRecords.find(b->GetEditorID());
								std::string statusA = (markerA != settings.markedRecords.end()) ? markerA->second : "";
								std::string statusB = (markerB != settings.markedRecords.end()) ? markerB->second : "";
								comparison = _stricmp(statusA.c_str(), statusB.c_str());
								break;
							}
						case SortColumn::JsonAttachment:
							{
								bool aHasJson = HasCachedJsonAttachment(a);
								bool bHasJson = HasCachedJsonAttachment(b);
								comparison = static_cast<int>(aHasJson) - static_cast<int>(bHasJson);
								break;
							}
						default:
							break;
						}
						return sortAscending ? (comparison < 0) : (comparison > 0);
					});
				}

				// Helper lambda: renders the JSON delete button column for a widget
				auto drawJsonDeleteButton = [&](Widget* widget) {
					ImGui::TableNextColumn();
					if (HasCachedJsonAttachment(widget)) {
						auto* menu = globals::menu;
						if (menu && menu->uiIcons.deleteSettings.texture) {
							const float iconSize = ImGui::GetFrameHeight() * 0.85f;
							ImGui::SetNextItemAllowOverlap();
							char idBuf[32];
							snprintf(idBuf, sizeof(idBuf), "##jsondel_%s", widget->GetFormID().c_str());
							if (Util::ErrorImageButton(idBuf, menu->uiIcons.deleteSettings.texture, { iconSize, iconSize })) {
								pendingDeleteWidget = widget;
								pendingDeletePopupRequested = true;
							}
							Util::AddTooltip(T("cs_editor.delete_json_file", "Delete JSON file"));
						}
					}
				};

				// Special handling for Cell Lighting category
				if (m_selectedCategory == "Cell Lighting") {
					auto player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell) {
						auto cell = player->parentCell;
						bool isInterior = cell->IsInteriorCell();

						if (isInterior) {
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);

							// No favorite star for cell lighting (it's always the current cell)
							ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
							ImGui::TableNextColumn();

							// Display current cell name
							const char* cellName = cell->GetName();
							std::string displayName = cellName && cellName[0] ? cellName : T("cs_editor.unnamed_cell", "[Unnamed Cell]");
							std::string label = displayName;

							// Highlight current cell (before TableRowSelectable so hover/active can override)
							auto highlightColor = Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor;
							highlightColor.w = 0.3f;
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(highlightColor));
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(highlightColor));

							bool isOpen = currentCellLightingWidget && currentCellLightingWidget->IsOpen();
							if (Util::TableRowSelectable(label.c_str(), isOpen, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
								if (ImGui::IsMouseDoubleClicked(0)) {
									// Open or reuse the cell lighting widget
									if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
										currentCellLightingWidget->SetOpen(true);
									} else {
										OpenCellLightingWidget(cell, true);
									}
								}
							}

							// Enter key to open
							if (isOpen && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
								if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
									currentCellLightingWidget->SetOpen(true);
								}
							}

							// Form ID column
							ImGui::TableNextColumn();
							ImGui::Text("0x%08X", cell->GetFormID());

							// File column
							ImGui::TableNextColumn();
							auto file = cell->GetFile(0);
							if (file) {
								ImGui::Text("%s", file->fileName);
							}

							// Status column
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(T("cs_editor.interior_cell", "Interior Cell"));

							// json column (empty for cells - no standalone json)
							ImGui::TableNextColumn();
						} else {
							// Show message that cell lighting is only for interior cells
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(1);
							ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
							ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Warning, "%s", T("cs_editor.cell_lighting_interior_only", "Cell Lighting is only available for interior cells."));
							ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Disable, "%s", T("cs_editor.currently_exterior_cell", "You are currently in an exterior cell."));
							ImGui::PopTextWrapPos();
						}
					} else {
						// No player or cell
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(1);
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
						ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Error, "%s", T("cs_editor.player_cell_unavailable", "Player cell not available."));
						ImGui::PopTextWrapPos();
					}
				}

				// Centralized filter check used by the display loop below.
				auto shouldShowWidget = [&](Widget* w) {
					if (!MatchesObjectFilter(w))
						return false;
					if (m_showOnlyFavorites && !IsFavorite(w->GetEditorID()))
						return false;
					if (m_showOnlyFlagged && settings.markedRecords.find(w->GetEditorID()) == settings.markedRecords.end())
						return false;
					return true;
				};

				// Filtered display of widgets
				for (int i = 0; i < sortedWidgets.size(); ++i) {
					if (!shouldShowWidget(sortedWidgets[i]))
						continue;

					auto editorLabel = sortedWidgets[i]->GetEditorID();
					auto markedRecord = settings.markedRecords.find(editorLabel);
					ImGui::PushID(sortedWidgets[i]->GetFormID().c_str());
					ImGui::TableNextRow();

					// Set background colour
					if (markedRecord != settings.markedRecords.end()) {
						auto& color = settings.recordMarkers[markedRecord->second];
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(color));
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(color));
					}

					ImGui::TableSetColumnIndex(0);

					// Favorite star
					if (IconButton(std::format("##fav_{}", i).c_str(), IsFavorite(sortedWidgets[i]->GetEditorID()), "star")) {
						ToggleFavorite(sortedWidgets[i]->GetEditorID());
					}

					ImGui::TableNextColumn();

					// Editor ID column
					bool isSelected = sortedWidgets[i]->IsOpen();
					if (Util::TableRowSelectable(editorLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap)) {
						if (ImGui::IsMouseDoubleClicked(0)) {
							sortedWidgets[i]->SetOpen(true);
							AddToRecent(sortedWidgets[i]->GetEditorID(), m_selectedCategory);
						}
					}

					// Show ImageSpace and VolumetricLighting info for weather widgets
					if (!weatherTooltipShownThisFrame && m_selectedCategory == "Weather" && ImGui::IsItemHovered()) {
						auto* weatherWidget = dynamic_cast<WeatherWidget*>(sortedWidgets[i]);
						if (weatherWidget && weatherWidget->weather) {
							const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
							const ImVec2 pad = ImGui::GetStyle().WindowPadding;
							const float spacingHeight = ImGui::GetStyle().ItemSpacing.y;
							constexpr int kSectionHeaders = 2;  // "ImageSpace:" + "Volumetric Lighting:"
							constexpr int kTodValuesPerSection = 4;
							constexpr int kSpacingSeparators = 1;  // Spacing between sections
							const float estimatedTooltipHeight = (kSectionHeaders + kTodValuesPerSection * 2) * lineHeight + kSpacingSeparators * spacingHeight + pad.y * 2.0f;
							Util::SetTooltipPositionNearMouse(estimatedTooltipHeight);
							if (ImGui::BeginTooltip()) {
								// ImageSpace info - use widget cache for proper editor IDs
								ImGui::TextUnformatted(T("cs_editor.imagespace_label", "ImageSpace:"));
								for (int tod = 0; tod < 4; tod++) {
									auto name = ResolveEditorId(weatherWidget->weather->imageSpaces[tod], imageSpaceWidgets);
									ImGui::Text("  %s: %s", TOD::GetPeriodLabel(tod), name.empty() ? T("cs_editor.none_filter", "None") : name.c_str());
								}

								ImGui::Spacing();

								// VolumetricLighting info - show short local FormID only
								ImGui::TextUnformatted(T("cs_editor.volumetric_lighting_label", "Volumetric Lighting:"));
								for (int tod = 0; tod < 4; tod++) {
									auto* f = weatherWidget->weather->volumetricLighting[tod];
									ImGui::Text("  %s: %s", TOD::GetPeriodLabel(tod), f ? std::format("0x{:X}", f->GetLocalFormID()).c_str() : T("cs_editor.none_filter", "None"));
								}
								ImGui::EndTooltip();
							}
							weatherTooltipShownThisFrame = true;
						}
					}

					// Enter key to open
					if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
						sortedWidgets[i]->SetOpen(true);
						AddToRecent(sortedWidgets[i]->GetEditorID(), m_selectedCategory);
					}

					// Opens a context menu on right click to mark records by color
					if (ImGui::BeginPopupContextItem(std::format("widget_context_menu##{}", sortedWidgets[i]->GetFormID()).c_str(), ImGuiPopupFlags_MouseButtonRight)) {
						auto& markedRecords = settings.markedRecords;

						for (auto& recordMarker : settings.recordMarkers) {
							if (ImGui::MenuItem(recordMarker.first.c_str())) {
								settings.markedRecords[editorLabel] = recordMarker.first;
								Save();
							}
						}

						if (ImGui::MenuItem(T("cs_editor.remove", "Remove"))) {
							markedRecords.erase(editorLabel);
							Save();
						}

						ImGui::EndPopup();
					}

					// Form ID column
					ImGui::TableNextColumn();
					ImGui::Text(sortedWidgets[i]->GetFormID().c_str());

					// File column
					ImGui::TableNextColumn();
					ImGui::Text(sortedWidgets[i]->GetFilename().c_str());

					// Status column
					ImGui::TableNextColumn();

					// Re-check if the record exists after potential removal
					markedRecord = settings.markedRecords.find(editorLabel);
					if (markedRecord != settings.markedRecords.end()) {
						ImGui::Text("%s", markedRecord->second.c_str());
					}

					// json / delete column
					drawJsonDeleteButton(sortedWidgets[i]);

					ImGui::PopID();
				}

				ImGui::EndTable();  // End DetailsTable
			}  // End if BeginTable("DetailsTable")

			EndScrollableContent();  // End ObjectsScrollable

		}  // End if BeginChild("##ObjectsContent")
		ImGui::EndChild();  // End ObjectsContent child

		ImGui::EndTable();  // End ObjectTable
	}  // End if BeginTable("ObjectTable")

	// Confirmation modal for json deletion - must be outside BeginChild so the modal can block the root window
	if (pendingDeleteWidget) {
		if (pendingDeletePopupRequested) {
			ImGui::OpenPopup("ListDeleteConfirmation");
			pendingDeletePopupRequested = false;
		}
		pendingDeleteWidget->DrawDeleteConfirmationModal("ListDeleteConfirmation");
		if (!ImGui::IsPopupOpen("ListDeleteConfirmation")) {
			pendingDeleteWidget = nullptr;
		}
	}

	// End the window
	ImGui::End();
}

void EditorWindow::ShowViewportWindow()
{
	viewportImageRect = {};
	const bool visible = Util::BeginWithRoundedClose(std::format("{}###CSEditorViewport", T("cs_editor.viewport", "Viewport")).c_str(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
	if (!visible) {
		ImGui::End();
		return;
	}

	// The size of the image in ImGui																														   // Get the available space in the current window
	ImVec2 availableSpace = ImGui::GetContentRegionAvail();
	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	if (!std::isfinite(availableSpace.x) || !std::isfinite(availableSpace.y) ||
		!std::isfinite(displaySize.x) || !std::isfinite(displaySize.y) ||
		availableSpace.x <= 0.0f || availableSpace.y <= 0.0f ||
		displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
		ImGui::TextDisabled("%s", T("cs_editor.viewport_unavailable", "Viewport unavailable"));
		ImGui::End();
		return;
	}

	// Calculate aspect ratio of the image
	float aspectRatio = displaySize.x / displaySize.y;

	// Determine the size to fit while preserving the aspect ratio
	ImVec2 imageSize;
	if (availableSpace.x / availableSpace.y < aspectRatio) {
		// Fit width
		imageSize.x = availableSpace.x;
		imageSize.y = availableSpace.x / aspectRatio;
	} else {
		// Fit height
		imageSize.y = availableSpace.y;
		imageSize.x = availableSpace.y * aspectRatio;
	}

	if (tempTexture && tempTexture->srv) {
		// The scene preview must remain opaque regardless of the render target alpha.
		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		Util::Subrect::ImageOpaque(tempTexture->srv.get(), imageSize);
		if (std::isfinite(imageMin.x) && std::isfinite(imageMin.y) &&
			std::isfinite(imageSize.x) && std::isfinite(imageSize.y) &&
			imageSize.x > 0.0f && imageSize.y > 0.0f &&
			tempTexture->desc.Width > 0 && tempTexture->desc.Height > 0) {
			viewportImageRect = {
				.min = imageMin,
				.max = ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y),
				.renderSize = ImVec2(
					static_cast<float>(tempTexture->desc.Width),
					static_cast<float>(tempTexture->desc.Height)),
				.valid = true,
			};
		}
	} else {
		ImGui::TextDisabled("%s", T("cs_editor.viewport_unavailable", "Viewport unavailable"));
	}

	ImGui::End();
}

void EditorWindow::ShowWidgetWindow()
{
	// Global shortcut for closing focused widget (Ctrl+W)
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
		if (lastFocusedWidget && lastFocusedWidget->IsOpen()) {
			lastFocusedWidget->SetOpen(false);
			lastFocusedWidget = nullptr;
		}
	}

	// Draw all open widgets using WidgetFactory template
	for (auto* collection : GetWidgetCollections())
		WidgetFactory::DrawOpenWidgets(*collection, lastFocusedWidget);

	// Draw current cell lighting widget if open
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
		currentCellLightingWidget->DrawWidget();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			lastFocusedWidget = currentCellLightingWidget.get();
	}
}

void EditorWindow::RenderUI()
{
	// Apply editor UI scale
	ImGuiIO& io = ImGui::GetIO();
	float previousScale = ImGui::GetStyle().FontScaleMain;
	ImGui::GetStyle().FontScaleMain = settings.editorUIScale;

	if (IsViewportActive()) {
		// Dim the game scene using the theme's modal dim background color
		ImGui::GetBackgroundDrawList()->AddRectFilled({ 0, 0 }, io.DisplaySize, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
	}

	// Check for Ctrl+Z to undo
	if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (CanUndo()) {
			PerformUndo();
		}
	}

	if (ImGui::BeginMainMenuBar()) {
		// Tighten bottom clip rect to prevent content bleeding over the bottom border
		{
			auto* window = ImGui::GetCurrentWindowRead();
			float borderInset = std::ceil(window->WindowBorderSize * 0.5f);
			ImGui::PushClipRect(window->ClipRect.Min,
				ImVec2(window->ClipRect.Max.x, window->ClipRect.Max.y - borderInset), true);
		}

		if (ImGui::BeginMenu(T("cs_editor.file", "File"))) {
			if (ImGui::MenuItem(T("cs_editor.save_all_open_widgets", "Save All Open Widgets"), "Ctrl+S")) {
				SaveAll();
			}

			// Save individual widgets submenu
			if (ImGui::BeginMenu(T("cs_editor.save", "Save"))) {
				bool hasOpen = false;
				for (auto* collection : GetWidgetCollections())
					hasOpen = WidgetFactory::DrawSaveWidgetMenuItems(*collection, hasOpen);

				if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
					hasOpen = true;
					if (ImGui::MenuItem(currentCellLightingWidget->GetEditorID().c_str()))
						currentCellLightingWidget->Save();
				}

				if (!hasOpen)
					ImGui::TextDisabled("%s", T("cs_editor.no_open_widgets", "No open widgets"));

				ImGui::EndMenu();
			}

			ImGui::Separator();
			for (auto* collection : GetWidgetCollections())
				WidgetFactory::DrawCloseAllMenuItem(*collection);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T("cs_editor.settings", "Settings"))) {
			if (ImGui::MenuItem(T("cs_editor.general_settings", "General Settings"))) {
				showSettingsWindow = true;
				settingsSelectedCategory = "General";
			}
			if (ImGui::MenuItem(T("cs_editor.editor_flags", "Editor Flags"))) {
				showSettingsWindow = true;
				settingsSelectedCategory = "Flags";
			}
			ImGui::Separator();

			// Current cell lighting
			auto player = RE::PlayerCharacter::GetSingleton();
			if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
				if (ImGui::MenuItem(T("cs_editor.edit_current_cell_lighting", "Edit Current Cell Lighting"))) {
					// Check if widget already exists
					bool found = false;
					if (currentCellLightingWidget && currentCellLightingWidget->cell == player->parentCell) {
						currentCellLightingWidget->SetOpen(true);
						found = true;
					}

					if (!found) {
						// Create new widget for current cell
						OpenCellLightingWidget(player->parentCell, true);
					}
				}
			} else {
				ImGui::BeginDisabled();
				ImGui::MenuItem(T("cs_editor.edit_current_cell_lighting", "Edit Current Cell Lighting"));
				ImGui::EndDisabled();
				Util::AddTooltip(T("cs_editor.interior_only_available", "Only available in interior cells"), ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
			}

			ImGui::Separator();

			if (Util::Widgets::Checkbox(T("cs_editor.auto_apply_changes", "Auto-Apply Changes"), &settings.autoApplyChanges)) {
				Save();
			}
			Util::AddTooltip(T("cs_editor.auto_apply_changes_tooltip", "Automatically apply weather changes to the game as you edit"));

			if (Util::Widgets::Checkbox(T("cs_editor.enable_inherit_from_parent", "Enable Inherit From Parent"), &settings.enableInheritFromParent)) {
				Save();
			}
			Util::AddTooltip(T("cs_editor.enable_inherit_tooltip", "Show inherit from parent options in weather widgets"));
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T("cs_editor.window", "Window"))) {
			if (Util::Widgets::Checkbox(T("cs_editor.viewport", "Viewport"), &settings.showViewport)) {
				Save();
			}
			if (Util::Widgets::Checkbox(T("cs_editor.palette", "Palette"), &PaletteWindow::GetSingleton()->open)) {
			}

			if (ImGui::MenuItem(T("cs_editor.reset_window_layout", "Reset Window Layout"))) {
				resetLayout = true;
			}

			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.open_widgets", "Open Widgets:"));

			int openCount = 0;
			for (auto* collection : GetWidgetCollections())
				openCount = WidgetFactory::DrawOpenWidgetMenuItems(*collection, openCount);

			if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
				++openCount;
				if (ImGui::MenuItem(std::format("{}: {}", currentCellLightingWidget->GetWidgetTypeName(), currentCellLightingWidget->GetEditorID()).c_str()))
					ImGui::SetWindowFocus(currentCellLightingWidget->GetWindowTitle().c_str());
			}

			if (openCount == 0)
				ImGui::TextDisabled("%s", T("cs_editor.no_widgets_open", "No widgets open"));

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T("cs_editor.help", "Help"))) {
			ImGui::TextUnformatted(T("cs_editor.cs_editor", "CS Editor"));
			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.keyboard_shortcuts", "Keyboard Shortcuts:"));
			ImGui::BulletText(T("cs_editor.shortcut_ctrl_f", "Ctrl+F: Focus search"));
			ImGui::BulletText(T("cs_editor.shortcut_ctrl_s", "Ctrl+S: Save all open widgets"));
			ImGui::BulletText(T("cs_editor.shortcut_ctrl_w", "Ctrl+W: Close focused widget"));
			ImGui::BulletText(T("cs_editor.shortcut_enter", "Enter: Open selected widget"));
			ImGui::BulletText(T("cs_editor.shortcut_esc", "Esc: Close editor"));
			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.quick_tips", "Quick Tips:"));
			ImGui::BulletText(T("cs_editor.tip_double_click", "Double-click to edit"));
			ImGui::BulletText(T("cs_editor.tip_right_click", "Right-click to mark status"));
			ImGui::BulletText(T("cs_editor.tip_star_favorite", "Click star icon to favorite"));
			ImGui::BulletText(T("cs_editor.tip_quick_filters", "Use quick filters for fast sorting"));
			ImGui::BulletText(T("cs_editor.tip_auto_apply", "Auto-Apply updates game live"));
			ImGui::BulletText(T("cs_editor.tip_lock_weather", "Lock weather to prevent changes"));
			ImGui::BulletText(T("cs_editor.tip_undo", "Undo button reverts recent changes (Ctrl+Z)"));
			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.total_objects", "Total Objects:"));
			ImGui::BulletText(T("cs_editor.weathers_count", "Weathers: %d"), (int)weatherWidgets.size());
			ImGui::BulletText(T("cs_editor.lighting_count", "Lighting: %d"), (int)lightingTemplateWidgets.size());
			ImGui::BulletText(T("cs_editor.imagespaces_count", "ImageSpaces: %d"), (int)imageSpaceWidgets.size());
			ImGui::Separator();
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.CurrentHotkey, T("cs_editor.favorites_count", "Favorites: %d"), (int)settings.favoriteWidgets.size());

			// Count total recent widgets across all categories
			int totalRecent = 0;
			for (const auto& [category, widgets] : settings.recentWidgets) {
				totalRecent += static_cast<int>(widgets.size());
			}
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor, T("cs_editor.recent_count", "Recent: %d"), totalRecent);
			ImGui::EndMenu();
		}

		auto menu = globals::menu;
		constexpr float kIconButtonPadding = 1.0f;  // minimal padding so icons render larger and smoother
		const float iconButtonDim = ImGui::GetFrameHeight() - kIconButtonPadding * 2;
		const ImVec2 iconButtonSize(iconButtonDim, iconButtonDim);
		const auto iconTint = Util::GetIconTint();

		// Undo button (stays on left side)
		if (menu && menu->uiIcons.undo.texture) {
			bool canUndo = CanUndo();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kIconButtonPadding, kIconButtonPadding));
			{
				auto _style = Util::TransparentIconButtonStyle();
				auto textColor = canUndo ? menu->GetTheme().Palette.Text : menu->GetTheme().StatusPalette.Disable;
				if (!canUndo)
					textColor.w = 0.5f;
				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				if (ImGui::ImageButton("##GlobalUndo", menu->uiIcons.undo.texture, iconButtonSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), iconTint) && canUndo)
					PerformUndo();
				ImGui::PopStyleColor();
			}
			ImGui::PopStyleVar(2);
			Util::AddTooltip(canUndo ? std::format("Undo (Ctrl+Z) - {} states", (int)undoStack.size()).c_str() : T("cs_editor.undo_no_changes", "Undo (Ctrl+Z) - No changes to undo"));
		}

		// Right-aligned items — use SetCursorScreenPos to bypass menu bar GroupOffset
		const float scale = Util::GetUIScale();
		const float clipRight = ImGui::GetWindowDrawList()->GetClipRectMax().x;
		const float cursorY = ImGui::GetCursorScreenPos().y;
		const float closeButtonSize = ImGui::GetFrameHeight();
		const float& itemSpacing = ImGui::GetStyle().ItemSpacing.x;
		const float sliderWidth = kMenuBarSliderWidth * scale;

		// Measure right-side elements to compute positions right-to-left
		constexpr float kCloseButtonInset = 3.0f;
		float rightCursor = clipRight - kCloseButtonInset * scale;

		// X button
		rightCursor -= closeButtonSize;
		const float xButtonX = rightCursor;

		// Time slider
		rightCursor -= itemSpacing + sliderWidth;
		const float sliderX = rightCursor;

		// Period text
		char periodBuf[64];
		std::snprintf(periodBuf, sizeof(periodBuf), "(%s)", TOD::GetPeriodLabel(TOD::GetActivePeriod()));
		float periodWidth = ImGui::CalcTextSize(periodBuf).x;
		rightCursor -= itemSpacing + periodWidth;
		const float periodX = rightCursor;

		// Pause Time button
		float pauseButtonX = 0;
		bool hasPauseButton = menu && menu->uiIcons.pauseTime.texture;
		if (hasPauseButton) {
			rightCursor -= itemSpacing + iconButtonDim + kIconButtonPadding * 2;
			pauseButtonX = rightCursor;
		}

		// Preview mode buttons (free camera / play mode)
		const float previewButtonWidth = iconButtonDim + kIconButtonPadding * 2;
		float freeCameraX = 0, playModeX = 0;
		bool hasFreeCam = menu && menu->uiIcons.freeCamera.texture;
		bool hasPlayMode = menu && menu->uiIcons.playMode.texture;
		if (hasPlayMode) {
			rightCursor -= itemSpacing + previewButtonWidth;
			playModeX = rightCursor;
		}
		if (hasFreeCam) {
			rightCursor -= itemSpacing + previewButtonWidth;
			freeCameraX = rightCursor;
		}

		// Preview mode status text (mirrors TIME PAUSED pattern, with hotkey + pulsating color)
		float previewStatusX = 0;
		char previewStatusBuf[128] = {};
		bool showPreviewStatus = previewMode != PreviewMode::None;
		if (showPreviewStatus) {
			std::string hotkey = Util::Input::KeyIdToString(menu->GetSettings().CSEditorToggleKey);
			if (previewMode == PreviewMode::FreeCamera)
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T("cs_editor.preview_free_camera", " [ %s ] FREE CAMERA (Speed: %.0f)"), hotkey.c_str(), flySpeed);
			else if (previewMode == PreviewMode::FreeCameraLocked)
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T("cs_editor.preview_free_camera_locked", " [ %s ] FREE CAMERA LOCKED"), hotkey.c_str());
			else
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T("cs_editor.preview_play_mode", " [ %s ] PLAY MODE"), hotkey.c_str());
			rightCursor -= itemSpacing + ImGui::CalcTextSize(previewStatusBuf).x;
			previewStatusX = rightCursor;
		}

		// Time paused text
		float timePausedX = 0;
		bool showTimePaused = IsTimePaused();
		const char* timePausedText = T("cs_editor.time_paused_status", " [TIME PAUSED]");
		if (showTimePaused) {
			rightCursor -= itemSpacing + ImGui::CalcTextSize(timePausedText).x;
			timePausedX = rightCursor;
		}

		// Weather lock text
		float weatherLockX = 0;
		char weatherLockBuf[128] = {};
		auto* lockedWeather = GetLockedWeather();
		bool showWeatherLock = IsWeatherLocked() && lockedWeather;
		if (showWeatherLock) {
			const char* weatherName = lockedWeather->GetFormEditorID();
			std::snprintf(weatherLockBuf, sizeof(weatherLockBuf), T("cs_editor.locked_weather_status", " [LOCKED: %s]"), weatherName ? weatherName : T("cs_editor.unknown", "Unknown"));
			rightCursor -= itemSpacing + ImGui::CalcTextSize(weatherLockBuf).x;
			weatherLockX = rightCursor;
		}

		// Render right-aligned items left to right
		const auto& statusPalette = menu->GetTheme().StatusPalette;

		if (showWeatherLock) {
			ImGui::SetCursorScreenPos(ImVec2(weatherLockX, cursorY));
			ImGui::PushStyleColor(ImGuiCol_Text, statusPalette.SuccessColor);
			ImGui::TextUnformatted(weatherLockBuf);
			ImGui::PopStyleColor();
		}

		if (showTimePaused) {
			ImGui::SetCursorScreenPos(ImVec2(timePausedX, cursorY));
			ImGui::PushStyleColor(ImGuiCol_Text, statusPalette.CurrentHotkey);
			ImGui::TextUnformatted(timePausedText);
			ImGui::PopStyleColor();
		}

		if (showPreviewStatus) {
			ImGui::SetCursorScreenPos(ImVec2(previewStatusX, cursorY));
			ImGui::TextColored(Util::GetPulsingColor(statusPalette.CurrentHotkey), "%s", previewStatusBuf);
		}

		// Toggle-style icon button helper (active: SuccessColor bg, inactive: transparent)
		auto DrawToggleIconButton = [&](const char* id, ImTextureRef texture, bool isActive, float posX) -> bool {
			ImGui::SetCursorScreenPos(ImVec2(posX, cursorY));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kIconButtonPadding, kIconButtonPadding));
			if (isActive) {
				auto color = statusPalette.SuccessColor;
				color.w = kToggleActiveAlpha;
				auto hover = color;
				hover.w = kToggleHoverAlpha;
				ImGui::PushStyleColor(ImGuiCol_Button, color);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
			} else {
				auto hover = menu->GetTheme().Palette.Text;
				hover.w = kInactiveHoverAlpha;
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
			}
			bool clicked = ImGui::ImageButton(id, texture, iconButtonSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), iconTint);
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(2);
			return clicked;
		};

		// Preview mode buttons
		if (hasFreeCam) {
			bool isActive = previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::FreeCameraLocked;
			if (DrawToggleIconButton("##FreeCamera", menu->uiIcons.freeCamera.texture, isActive, freeCameraX)) {
				if (isActive)
					ExitPreviewMode();
				else
					EnterPreviewMode(PreviewMode::FreeCamera);
			}
			Util::AddTooltip(isActive ? T("cs_editor.exit_free_camera", "Exit Free Camera") : T("cs_editor.free_camera_scroll", "Free Camera (scroll to adjust speed)"));
		}
		if (hasPlayMode) {
			bool isActive = previewMode == PreviewMode::PlayMode;
			if (DrawToggleIconButton("##PlayMode", menu->uiIcons.playMode.texture, isActive, playModeX)) {
				if (isActive)
					ExitPreviewMode();
				else
					EnterPreviewMode(PreviewMode::PlayMode);
			}
			Util::AddTooltip(isActive ? T("cs_editor.exit_play_mode", "Exit Play Mode") : T("cs_editor.play_mode_walk", "Play Mode - Walk around normally"));
		}

		if (hasPauseButton) {
			bool isPaused = IsTimePaused();
			if (DrawToggleIconButton("##GlobalPauseTime", menu->uiIcons.pauseTime.texture, isPaused, pauseButtonX))
				TogglePause();
			Util::AddTooltip(isPaused ? T("cs_editor.resume_time", "Resume Time") : T("cs_editor.pause_time", "Pause Time"));
		}

		// Period text and time slider
		auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
		if (calendar && calendar->gameHour) {
			ImGui::SetCursorScreenPos(ImVec2(periodX, cursorY));
			ImGui::TextUnformatted(periodBuf);
			ImGui::SetCursorScreenPos(ImVec2(sliderX, cursorY));
			ImGui::SetNextItemWidth(sliderWidth);
			DrawGameHourSlider("##MenuBarSlider", "Time: %.2f");
		}

		// Close button
		ImGui::SetCursorScreenPos(ImVec2(xButtonX, cursorY));
		if (Util::ErrorButton("X", ImVec2(closeButtonSize, closeButtonSize)))
			open = false;
		Util::AddTooltip(T("cs_editor.close_cs_editor", "Close CS Editor (Esc)"));

		ImGui::PopClipRect();  // End bottom-border clip rect

		// Redraw the menu bar border on top of all content so elements appear behind it
		{
			auto* window = ImGui::GetCurrentWindowRead();
			const float border = ImGui::GetStyle().WindowBorderSize;
			if (window && border > 0.0f) {
				ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
				window->DrawList->AddRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), borderCol, 0.0f, 0, border);
			}
		}

		ImGui::EndMainMenuBar();
	}

	// Establish a viewport-wide DockSpace so all editor windows are snappable and dockable
	ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

	auto width = ImGui::GetIO().DisplaySize.x;
	auto height = ImGui::GetIO().DisplaySize.y;
	const float scale = Util::GetUIScale();
	const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const float menuBarHeight = ImGui::GetFrameHeight();
	const float availableWidth = width - pad * 3.0f;  // left pad + gap + right pad
	const float availableHeight = (height - menuBarHeight - pad * 2.0f) * 0.85f;
	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	// Browser gets up to half the available width, capped so ultrawide gives extra to viewport
	const float maxBrowserWidth = 960.0f * scale;
	const float browserWidth = std::min(availableWidth * 0.5f, maxBrowserWidth);
	const float viewportWidth = availableWidth - browserWidth;
	ImGui::SetNextWindowSize(ImVec2(browserWidth, availableHeight), layoutCond);
	ImGui::SetNextWindowPos(ImVec2(pad, menuBarHeight + pad), layoutCond);
	lightEditor.GatherLights();
	ShowObjectsWindow();

	viewportImageRect = {};
	if (IsViewportActive()) {
		// Size viewport height to match game aspect ratio so the preview fits snugly
		const float aspectRatio = width / height;
		const float imageHeight = viewportWidth / aspectRatio;
		const float chromeHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
		const float viewportHeight = imageHeight + chromeHeight;
		ImGui::SetNextWindowSize(ImVec2(viewportWidth, viewportHeight), layoutCond);
		ImGui::SetNextWindowPos(ImVec2(pad + browserWidth + pad, menuBarHeight + pad), layoutCond);
		viewportBottomY = menuBarHeight + pad + viewportHeight;
		ShowViewportWindow();
	} else {
		viewportBottomY = menuBarHeight + pad;
	}

	// Picker input needs the exact preview-image mapping published this frame.
	lightEditor.UpdatePicker();

	auto settingsWindowHeight = height * 0.25f;
	auto settingsWindowWidth = width * 0.25f;
	if (showSettingsWindow) {
		ImGui::SetNextWindowSize(ImVec2(settingsWindowWidth, settingsWindowHeight), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos({ (width / 2.0f) - (settingsWindowWidth / 2.0f), (height / 2.0f) - (settingsWindowHeight / 2.0f) }, ImGuiCond_Appearing);
		ShowSettingsWindow();
	}

	ShowWidgetWindow();

	// Show palette window
	PaletteWindow::GetSingleton()->Draw();

	if (resetLayout)
		ResetWidgetTypeSizes();
	resetLayout = false;

	// Render notifications on top of everything
	RenderNotifications();

	// Restore previous font scale
	ImGui::GetStyle().FontScaleMain = previousScale;
}

void EditorWindow::OpenWeatherFeatureSetting(RE::TESWeather* weather, const std::string& featureName, const std::string& settingName)
{
	if (!weather) {
		return;
	}

	EnsureResources();

	// Open the editor if it's not already open
	if (!open) {
		open = true;
	}

	// Find the weather widget
	for (auto& widget : weatherWidgets) {
		auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget.get());
		if (weatherWidget && weatherWidget->weather == weather) {
			// Open the widget if it's not already open
			if (!weatherWidget->open) {
				weatherWidget->open = true;
			}

			// Set up navigation to the specific feature/setting
			weatherWidget->NavigateToFeatureSetting(featureName, settingName);

			// Focus the widget window
			weatherWidget->RequestFocus();
			break;
		}
	}
}

EditorWindow::~EditorWindow()
{
	ShowGameMenus();
	delete tempTexture;
	for (auto* collection : GetWidgetCollections())
		collection->clear();
	artObjectWidgets.clear();
	effectShaderWidgets.clear();
	currentCellLightingWidget.reset();
	cachedCellLightingWidgets.clear();
}

void EditorWindow::SetupResources()
{
	if (resourcesInitialized)
		return;

	if (!RE::TESDataHandler::GetSingleton())
		return;

	Load();
	PaletteWindow::GetSingleton()->Load();
	InvalidateJsonAttachmentCache();

	Util::FormEditorIDIndex editorIDs;

	// Populate all widget collections using WidgetFactory templates
	WidgetFactory::PopulateWidgets<WeatherWidget, RE::TESWeather>(weatherWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<LightingTemplateWidget, RE::BGSLightingTemplate>(lightingTemplateWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<ImageSpaceWidget, RE::TESImageSpace>(imageSpaceWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<VolumetricLightingWidget, RE::BGSVolumetricLighting>(volumetricLightingWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<PrecipitationWidget, RE::BGSShaderParticleGeometryData>(precipitationWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<LensFlareWidget, RE::BGSLensFlare>(lensFlareWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<ReferenceEffectWidget, RE::BGSReferenceEffect>(referenceEffectWidgets, editorIDs);

	// Cache simple form widgets for form picker performance
	WidgetFactory::PopulateSimpleWidgets<RE::BGSArtObject>(artObjectWidgets, editorIDs);
	WidgetFactory::PopulateSimpleWidgets<RE::TESEffectShader>(effectShaderWidgets, editorIDs);

	resourcesInitialized = true;
}

void EditorWindow::EnsureResources()
{
	SetupResources();
}

void EditorWindow::OpenCellLightingWidget(RE::TESObjectCELL* cell, bool showNotification, bool requestFocus)
{
	if (!cell)
		return;

	if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
		currentCellLightingWidget->SetOpen(true);
		if (requestFocus)
			currentCellLightingWidget->RequestFocus();
		return;
	}

	if (lastFocusedWidget == currentCellLightingWidget.get())
		lastFocusedWidget = nullptr;

	const auto hasUndoState = [&](std::string_view identity) {
		return std::ranges::any_of(undoStack, [&](const UndoState& state) {
			return state.widgetIdentity == identity;
		});
	};
	for (auto it = cachedCellLightingWidgets.begin(); it != cachedCellLightingWidgets.end();) {
		if (!it->second ||
			(!it->second->HasUnsavedChanges() && !hasUndoState(it->first))) {
			it = cachedCellLightingWidgets.erase(it);
		} else {
			++it;
		}
	}

	if (currentCellLightingWidget) {
		const auto oldIdentity = currentCellLightingWidget->GetStableIdentity();
		if (currentCellLightingWidget->HasUnsavedChanges() || hasUndoState(oldIdentity))
			cachedCellLightingWidgets[oldIdentity] = std::move(currentCellLightingWidget);
		else
			currentCellLightingWidget.reset();
	}

	const auto targetIdentity = std::format(
		"{}/{}",
		Widget::kCellLightingFolderName,
		Util::GetFormFileKey(cell));
	if (auto cached = cachedCellLightingWidgets.find(targetIdentity);
		cached != cachedCellLightingWidgets.end()) {
		currentCellLightingWidget = std::move(cached->second);
		cachedCellLightingWidgets.erase(cached);
	} else {
		currentCellLightingWidget = std::make_unique<CellLightingWidget>(cell);
		currentCellLightingWidget->CacheFormData();
		currentCellLightingWidget->RememberBaseline();
		currentCellLightingWidget->RestoreRememberedBaseline();
		currentCellLightingWidget->Load(showNotification);
	}
	currentCellLightingWidget->SetOpen(true);
	if (requestFocus)
		currentCellLightingWidget->RequestFocus();
}

void EditorWindow::UpdateOpenState()
{
	AdvanceLightEditorDeferredWork();
	if (open == wasOpen)
		return;
	if (open) {
		DisableVanityCamera();
		HideGameMenus();
		BackgroundBlur::SetCSEditorActive(IsViewportActive());
	} else {
		if (previewMode != PreviewMode::None)
			ExitPreviewMode();
		lightEditor.ResetOverrides();
		suppressNextEditorEscape = false;
		viewportImageRect = {};
		RestoreVanityCamera();
		ShowGameMenus();
		BackgroundBlur::SetCSEditorActive(false);
		lastFocusedWidget = nullptr;
		previewResourceReleasePending.store(true, std::memory_order_release);
	}
	wasOpen = open;
}

void EditorWindow::ReleasePendingPreviewResources()
{
	if (!previewResourceReleasePending.load(std::memory_order_relaxed) ||
		!previewResourceReleasePending.exchange(false, std::memory_order_acq_rel))
		return;

	// Close requests may originate outside rendering or while draw lists still use these SRVs.
	delete tempTexture;
	tempTexture = nullptr;
	for (auto& widget : weatherWidgets)
		if (auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget.get()))
			weatherWidget->cloudTextureCache.clear();
}

void EditorWindow::AdvanceLightEditorDeferredWork()
{
	lightEditor.TickDeferredWork();
}

void EditorWindow::ActivateLightEditor()
{
	m_selectedCategory = "Light Editor";
	lightEditor.SetEnabled(true);
}

bool EditorWindow::IsLightEditorSelected() const
{
	return m_selectedCategory == "Light Editor";
}

void EditorWindow::Draw()
{
	ReleasePendingPreviewResources();

	// Keep background blur in sync when HDR toggles while the editor stays open
	{
		static bool prevViewportActive = false;
		const bool viewportActive = IsViewportActive();
		if (viewportActive != prevViewportActive) {
			BackgroundBlur::SetCSEditorActive(viewportActive);
			prevViewportActive = viewportActive;
		}
	}

	if (!IsViewportActive()) {
		delete tempTexture;
		tempTexture = nullptr;
	} else {
		auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (renderer) {
			auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
			if (framebuffer.SRV) {
				ID3D11Resource* resource = nullptr;
				framebuffer.SRV->GetResource(&resource);

				if (resource) {
					auto texture = static_cast<ID3D11Texture2D*>(resource);
					D3D11_TEXTURE2D_DESC texDesc{};
					texture->GetDesc(&texDesc);

					const bool needsRecreate = !tempTexture || !tempTexture->resource || !tempTexture->srv ||
					                           tempTexture->desc.Width != texDesc.Width || tempTexture->desc.Height != texDesc.Height ||
					                           tempTexture->desc.MipLevels != texDesc.MipLevels || tempTexture->desc.ArraySize != texDesc.ArraySize ||
					                           tempTexture->desc.Format != texDesc.Format ||
					                           tempTexture->desc.SampleDesc.Count != texDesc.SampleDesc.Count ||
					                           tempTexture->desc.SampleDesc.Quality != texDesc.SampleDesc.Quality;

					if (needsRecreate) {
						delete tempTexture;
						tempTexture = nullptr;

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
						framebuffer.SRV->GetDesc(&srvDesc);

						tempTexture = new Texture2D(texDesc, "CSEditor::ViewportCopy");
						tempTexture->CreateSRV(srvDesc);
					}

					if (tempTexture && tempTexture->resource) {
						globals::d3d::context->CopyResource(tempTexture->resource.get(), resource);
					}

					resource->Release();
				}
			}
		}
	}

	RenderUI();
}

void EditorWindow::SaveAll()
{
	auto saveOpen = [](auto& widgets) {
		for (auto& w : widgets)
			if (w->IsOpen())
				w->Save();
	};
	for (auto* collection : GetWidgetCollections())
		saveOpen(*collection);
	for (auto& [_, widget] : cachedCellLightingWidgets)
		if (widget && widget->IsOpen())
			widget->Save();
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen())
		currentCellLightingWidget->Save();

	Save();
}

void EditorWindow::SaveSettings()
{
	settings.selectedCategory = m_selectedCategory;
	settings.widgetTypeSizes = GetWidgetTypeSizesJson();
	j = settings;
}

void EditorWindow::LoadSettings()
{
	if (!j.empty()) {
		try {
			auto loadedSettings = j.get<Settings>();
			settings = std::move(loadedSettings);
		} catch (const nlohmann::json::exception& e) {
			logger::warn("Failed to deserialize editor settings; keeping defaults: {}", e.what());
		}
	}
	SanitizeSettings();
	m_selectedCategory = settings.selectedCategory;
	SetWidgetTypeSizesFromJson(settings.widgetTypeSizes);
}

void EditorWindow::SanitizeSettings()
{
	if (!std::isfinite(settings.editorUIScale))
		settings.editorUIScale = 1.0f;
	settings.editorUIScale = std::clamp(settings.editorUIScale, 0.5f, 2.0f);
	settings.maxRecentWidgets = std::clamp(settings.maxRecentWidgets, 5, 20);

	auto sanitizeUnit = [](float value, float fallback) {
		return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
	};

	while (settings.recordMarkers.size() > maxRecordMarkers)
		settings.recordMarkers.erase(std::prev(settings.recordMarkers.end()));
	for (auto& [_, color] : settings.recordMarkers) {
		color.x = sanitizeUnit(color.x, 0.5f);
		color.y = sanitizeUnit(color.y, 0.5f);
		color.z = sanitizeUnit(color.z, 0.5f);
		color.w = sanitizeUnit(color.w, 1.0f);
	}
	std::erase_if(settings.markedRecords, [&](const auto& entry) {
		return !settings.recordMarkers.contains(entry.second);
	});

	for (auto& [_, recent] : settings.recentWidgets) {
		std::vector<std::string> sanitized;
		sanitized.reserve(std::min(recent.size(), static_cast<size_t>(settings.maxRecentWidgets)));
		for (const auto& id : recent) {
			if (std::ranges::find(sanitized, id) == sanitized.end())
				sanitized.push_back(id);
			if (sanitized.size() == static_cast<size_t>(settings.maxRecentWidgets))
				break;
		}
		recent = std::move(sanitized);
	}

	constexpr size_t kMaxPaletteEntries = 4096;
	if (settings.paletteColors.size() > kMaxPaletteEntries)
		settings.paletteColors.resize(kMaxPaletteEntries);
	for (auto& entry : settings.paletteColors) {
		entry.r = sanitizeUnit(entry.r, 0.0f);
		entry.g = sanitizeUnit(entry.g, 0.0f);
		entry.b = sanitizeUnit(entry.b, 0.0f);
		entry.useCount = std::max(entry.useCount, 0);
		if (!std::isfinite(entry.lastUsedTime) || entry.lastUsedTime < 0.0f)
			entry.lastUsedTime = 0.0f;
	}

	if (settings.paletteValues.size() > kMaxPaletteEntries)
		settings.paletteValues.resize(kMaxPaletteEntries);
	for (auto& entry : settings.paletteValues) {
		if (!std::isfinite(entry.value))
			entry.value = 0.0f;
		entry.useCount = std::max(entry.useCount, 0);
		if (!std::isfinite(entry.lastUsedTime) || entry.lastUsedTime < 0.0f)
			entry.lastUsedTime = 0.0f;
	}

	for (auto& favorite : settings.paletteFavorites) {
		favorite.r = sanitizeUnit(favorite.r, 0.0f);
		favorite.g = sanitizeUnit(favorite.g, 0.0f);
		favorite.b = sanitizeUnit(favorite.b, 0.0f);
	}
}

void EditorWindow::ShowSettingsWindow()
{
	Util::BeginWithRoundedClose(T("cs_editor.settings", "Settings"), &showSettingsWindow);

	if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		ImGui::TableSetupColumn(T("cs_editor.options", "Options"), ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("##Settings", ImGuiTableColumnFlags_WidthStretch, 0.7f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		const std::pair<const char*, const char*> options[] = { { "General", T("cs_editor.general", "General") }, { "Flags", T("cs_editor.flags", "Flags") } };
		for (const auto& [id, label] : options) {
			if (ImGui::Selectable(label, settingsSelectedCategory == id))
				settingsSelectedCategory = id;
		}

		ImGui::TableSetColumnIndex(1);

		if (settingsSelectedCategory == "General") {
			if (Util::Widgets::Checkbox(T("editor.editor_window.auto_apply_changes", "Auto-apply changes"), &settings.autoApplyChanges))
				Save();
			Util::AddTooltip(T("editor.editor_window.automatically_apply_changes_to_weather_lighti_eeede076", "Automatically apply changes to weather/lighting when editing"));

			if (Util::Widgets::Checkbox(T("cs_editor.use_text_buttons", "Use text buttons instead of icons"), &settings.useTextButtons))
				Save();
			Util::AddTooltip(T("cs_editor.text_buttons_tooltip", "Display action buttons as text labels instead of icons"));

			if (Util::Widgets::Checkbox(T("cs_editor.enable_inherit_feature", "Enable 'Inherit From Parent' feature"), &settings.enableInheritFromParent))
				Save();
			Util::AddTooltip(T("cs_editor.enable_inherit_feature_tooltip", "Show checkboxes to copy settings from parent weather (editor-only feature)"));

			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.ui_scale", "UI Scale"));
			ImGui::Spacing();

			Util::Widgets::SliderFloat(T("cs_editor.editor_ui_scale", "Editor UI Scale"), &settings.editorUIScale, 0.5f, 2.0f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				Save();
			}
			Util::AddTooltip(T("cs_editor.editor_ui_scale_tooltip", "Scale the size of all editor UI elements (0.5 = 50%, 2.0 = 200%)"));

			if (Util::ButtonWithFlash(T("cs_editor.reset_to_default", "Reset to 1.0"))) {
				settings.editorUIScale = 1.0f;
				Save();
			}
			ImGui::SameLine();
			Util::AddTooltip(T("cs_editor.reset_ui_scale_tooltip", "Reset UI scale to default (100%)"));

			ImGui::Separator();
			ImGui::TextUnformatted(T("cs_editor.session_history", "Session & History"));
			ImGui::Spacing();

			Util::Widgets::SliderInt(T("cs_editor.max_recent_widgets", "Max recent widgets"), &settings.maxRecentWidgets, 5, 20);
			if (ImGui::IsItemDeactivatedAfterEdit())
				Save();
			Util::AddTooltip(T("cs_editor.max_recent_widgets_tooltip", "Maximum number of recent widgets to remember"));

			if (Util::ButtonWithFlash(T("cs_editor.clear_recent_history", "Clear Recent History"))) {
				settings.recentWidgets.clear();
				Save();
			}
			ImGui::SameLine();
			if (Util::ButtonWithFlash(T("cs_editor.clear_favorites", "Clear Favorites"))) {
				settings.favoriteWidgets.clear();
				Save();
			}

		} else if (settingsSelectedCategory == "Flags") {
			if (ImGui::BeginTable("FlagsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn(T("cs_editor.label", "Label"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T("cs_editor.colour", "Colour"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T("cs_editor.actions", "Actions"), ImGuiTableColumnFlags_WidthFixed, 60.0f * Util::GetUIScale());

				auto& recordMarkers = settings.recordMarkers;

				std::string markerToDelete;
				std::optional<std::pair<std::string, std::string>> renameInfo;
				std::erase_if(markerLabelBuffers, [&](const auto& entry) { return !recordMarkers.contains(entry.first); });

				for (auto& recordMarker : recordMarkers) {
					ImGui::PushID(&recordMarker);
					const SKSE::stl::scope_exit restoreMarkerId([] { ImGui::PopID(); });
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					auto& labelBuffer = markerLabelBuffers.try_emplace(recordMarker.first, recordMarker.first).first->second;
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##Label", &labelBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
						renameInfo = { recordMarker.first, labelBuffer };

					ImGui::TableSetColumnIndex(1);
					ImGui::ColorEdit3(T("editor.editor_window.color", "Color"), (float*)&recordMarker.second);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						Save();
					}

					ImGui::TableSetColumnIndex(2);
					auto deleteColor = Menu::GetSingleton()->GetTheme().StatusPalette.Warning;
					deleteColor.y = deleteColor.y * 0.5f;
					auto deleteHovered = deleteColor;
					deleteHovered.w = 0.8f;
					auto deleteActive = deleteColor;
					deleteActive.w = 1.0f;
					{
						auto styledButton = Util::StyledButtonWrapper(deleteColor, deleteHovered, deleteActive);
						if (ImGui::Button(T("cs_editor.delete", "Delete"), ImVec2(-1, 0))) {
							markerToDelete = recordMarker.first;
						}
					}
				}

				if (renameInfo) {
					const auto error = RenameRecordMarker(renameInfo->first, renameInfo->second);
					if (!error.empty())
						ShowNotification(error, Util::Colors::GetWarning());
				}
				if (!markerToDelete.empty())
					DeleteRecordMarker(markerToDelete);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (recordMarkers.size() < maxRecordMarkers && ImGui::Selectable(T("cs_editor.add_new_marker", "Add new marker")) && !AddRecordMarker())
					ShowNotification("No marker could be added.", Util::Colors::GetWarning());

				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

bool EditorWindow::AddRecordMarker()
{
	auto& markers = settings.recordMarkers;
	if (markers.size() >= maxRecordMarkers)
		return false;
	std::string name;
	for (size_t ordinal = 0;; ++ordinal) {
		name = std::format("New marker {}", ordinal);
		if (!markers.contains(name))
			break;
	}
	if (!markers.emplace(name, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)).second)
		return false;
	markerLabelBuffers.erase(name);
	Save();
	return true;
}

std::string EditorWindow::RenameRecordMarker(const std::string& oldName, const std::string& newName)
{
	auto& markers = settings.recordMarkers;
	const auto source = markers.find(oldName);
	if (source == markers.end())
		return "The marker no longer exists.";
	if (newName.empty())
		return "Marker names cannot be empty.";
	if (oldName == newName)
		return {};
	if (!markers.emplace(newName, source->second).second)
		return "A marker with that name already exists.";
	markers.erase(source);
	for (auto& [recordId, markerName] : settings.markedRecords) {
		if (markerName == oldName)
			markerName = newName;
	}
	markerLabelBuffers.erase(oldName);
	markerLabelBuffers.erase(newName);
	Save();
	return {};
}

void EditorWindow::DeleteRecordMarker(const std::string& name)
{
	if (settings.recordMarkers.erase(name) == 0)
		return;
	markerLabelBuffers.erase(name);
	std::erase_if(settings.markedRecords, [&](const auto& record) { return record.second == name; });
	Save();
}

void EditorWindow::Save()
{
	const auto file = Util::PathHelpers::GetCommunityShaderPath() / std::format("{}.json", settingsFilename);

	logger::info("Saving settings file: {}", file.string());

	try {
		SaveSettings();
		std::string errorMessage;
		if (!Util::FileHelpers::WriteTextFileAtomic(file, j.dump(1), errorMessage)) {
			logger::warn("Failed to save settings file ({}): {}", file.string(), errorMessage);
			ShowNotification("Editor settings not saved: " + errorMessage, Util::Colors::GetError());
		}
	} catch (const std::exception& e) {
		logger::warn("Error serializing settings file ({}): {}", file.string(), e.what());
		ShowNotification("Editor settings not saved: " + std::string(e.what()), Util::Colors::GetError());
	}
}

void EditorWindow::Load()
{
	const auto file = Util::PathHelpers::GetCommunityShaderPath() / std::format("{}.json", settingsFilename);
	json loaded;
	std::string errorMessage;
	const auto result = Util::FileHelpers::ReadJsonFile(file, loaded, errorMessage);
	if (result == Util::FileHelpers::JsonFileReadResult::NotFound)
		return;
	if (result == Util::FileHelpers::JsonFileReadResult::Error) {
		logger::warn("Failed to load settings file ({}): {}", file.string(), errorMessage);
		return;
	}

	try {
		if (!loaded.is_object())
			throw std::runtime_error("root value is not a JSON object");
		j = std::move(loaded);
		LoadSettings();
	} catch (const std::exception& e) {
		logger::warn("Invalid settings file ({}): {}", file.string(), e.what());
		j = json::object();
		settings = Settings{};
		SetWidgetTypeSizesFromJson(settings.widgetTypeSizes);
	}
}

// A plain ForceWeather does not hold: scripts, climate timers, cell transitions, and
// other plugins can immediately select another weather. Guard both engine entry points
// and retain a per-frame repair path if hook installation is unavailable.
namespace
{
	std::atomic<RE::TESWeather*> g_lockedWeather{ nullptr };
	std::atomic_bool g_weatherLockHooksInstalled{ false };
	std::atomic_bool g_weatherLockHookInstallAttempted{ false };

	RE::TESWeather* GetActiveWeatherLock()
	{
		return g_lockedWeather.load(std::memory_order_acquire);
	}

	struct SetWeatherHook
	{
		static void thunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride, bool accelerate);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ForceWeatherHook
	{
		static void thunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void CancelPendingWeatherRelease(RE::Sky* sky)
	{
		if (sky)
			sky->flags.reset(RE::Sky::Flags::kReleaseWeatherOverride);
	}

	void ReapplyWeatherLock(RE::Sky* sky, RE::TESWeather* weather)
	{
		if (!sky || !weather)
			return;

		EditorWindow::ForceWeather(sky, weather, true);
	}

	void SetWeatherHook::thunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride, bool accelerate)
	{
		auto* locked = GetActiveWeatherLock();
		if (!locked) {
			func(sky, weather, isOverride, accelerate);
			return;
		}
		if (!sky)
			return;

		CancelPendingWeatherRelease(sky);
		if (weather == locked) {
			func(sky, locked, true, accelerate);
			return;
		}

		// Suppress competing selections unless the sky has already drifted from the lock.
		if (sky->currentWeather != locked || sky->overrideWeather != locked)
			ReapplyWeatherLock(sky, locked);
	}

	void ForceWeatherHook::thunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride)
	{
		EditorWindow::ForceWeather(sky, weather, isOverride);
	}
}  // namespace

void EditorWindow::ForceWeather(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override)
{
	if (!a_sky)
		return;
	if (auto* locked = GetActiveWeatherLock()) {
		a_weather = locked;
		a_override = true;
	}

	// The saved native entry/trampoline avoids re-entering the installed detour.
	if (ForceWeatherHook::func.address() != 0)
		ForceWeatherHook::func(a_sky, a_weather, a_override);
	else
		a_sky->ForceWeather(a_weather, a_override);
	Util::RefreshForcedWeatherSky(a_sky);
}

void EditorWindow::InstallWeatherLockHooks()
{
	if (g_weatherLockHookInstallAttempted.exchange(true, std::memory_order_acq_rel)) {
		logger::debug("[CSEditor] Weather-lock hook installation already attempted");
		return;
	}

	SetWeatherHook::func = REL::RelocationID(25694, 26241).address();
	ForceWeatherHook::func = REL::RelocationID(25696, 26243).address();

	LONG result = DetourTransactionBegin();
	if (result != NO_ERROR) {
		logger::error("[CSEditor] Could not begin atomic weather-lock hook transaction (error {})", result);
		return;
	}

	result = DetourUpdateThread(GetCurrentThread());
	if (result == NO_ERROR) {
		result = DetourAttach(
			reinterpret_cast<PVOID*>(&SetWeatherHook::func),
			reinterpret_cast<PVOID>(SetWeatherHook::thunk));
	}
	if (result == NO_ERROR) {
		result = DetourAttach(
			reinterpret_cast<PVOID*>(&ForceWeatherHook::func),
			reinterpret_cast<PVOID>(ForceWeatherHook::thunk));
	}

	if (result != NO_ERROR) {
		const LONG abortResult = DetourTransactionAbort();
		logger::error(
			"[CSEditor] Could not queue both weather-lock hooks (error {}, abort result {}); using per-frame fallback",
			result,
			abortResult);
		return;
	}

	result = DetourTransactionCommit();
	if (result != NO_ERROR) {
		logger::error(
			"[CSEditor] Could not commit atomic weather-lock hooks (error {}); using per-frame fallback",
			result);
		return;
	}

	g_weatherLockHooksInstalled.store(true, std::memory_order_release);
	logger::info("[CSEditor] Installed atomic SetWeather and ForceWeather lock hooks");
}

bool EditorWindow::AreWeatherLockHooksInstalled()
{
	return g_weatherLockHooksInstalled.load(std::memory_order_acquire);
}

void EditorWindow::MaintainWeatherLock()
{
	auto* locked = GetActiveWeatherLock();
	auto* sky = globals::game::sky ? globals::game::sky : RE::Sky::GetSingleton();
	if (!locked || !sky)
		return;

	const bool releasePending = sky->flags.any(RE::Sky::Flags::kReleaseWeatherOverride);
	if (!releasePending && sky->currentWeather == locked && sky->overrideWeather == locked)
		return;

	// ReleaseWeatherOverride is deferred until the next sky update. Cancel it before
	// restoring a true override so fallback maintenance cannot immediately undo itself.
	ReapplyWeatherLock(sky, locked);
}

bool EditorWindow::IsWeatherLocked() const
{
	return GetActiveWeatherLock() != nullptr;
}

RE::TESWeather* EditorWindow::GetLockedWeather() const
{
	return GetActiveWeatherLock();
}

void EditorWindow::LockWeather(RE::TESWeather* weather)
{
	if (!weather)
		return;

	g_lockedWeather.store(weather, std::memory_order_release);
	MaintainWeatherLock();

	logger::info("Weather locked: {}", weather->GetFormEditorID() ? weather->GetFormEditorID() : T("cs_editor.unknown", "Unknown"));
}

void EditorWindow::UnlockWeather()
{
	if (!g_lockedWeather.exchange(nullptr, std::memory_order_acq_rel))
		return;

	if (auto* sky = globals::game::sky ? globals::game::sky : RE::Sky::GetSingleton())
		sky->ReleaseWeatherOverride();

	logger::info("Weather unlocked");
}

void EditorWindow::ResetWeatherLock(bool a_releaseOverride)
{
	if (!g_lockedWeather.exchange(nullptr, std::memory_order_acq_rel))
		return;

	if (a_releaseOverride) {
		// Resolve the live singleton directly: cached globals may be changing during loads.
		if (auto* sky = RE::Sky::GetSingleton())
			sky->ReleaseWeatherOverride();
	}

	logger::info("[CSEditor] Cleared weather lock for world transition");
}

namespace
{
	constexpr std::array kTimeSensitiveMenus{
		RE::LoadingMenu::MENU_NAME,
		RE::SleepWaitMenu::MENU_NAME,
		RE::MapMenu::MENU_NAME
	};

	float SanitizeRunningTimeScale(float a_timeScale)
	{
		return std::isfinite(a_timeScale) && a_timeScale > 0.0f ?
		           a_timeScale :
		           EditorWindow::kVanillaTimeScale;
	}

	float GetMenuRunningTimeScale(float a_timeScale)
	{
		return std::max(SanitizeRunningTimeScale(a_timeScale), EditorWindow::kVanillaTimeScale);
	}

	std::uint8_t GetTimeSensitiveMenuBit(const RE::BSFixedString& a_menuName)
	{
		for (std::size_t index = 0; index < kTimeSensitiveMenus.size(); ++index) {
			if (a_menuName == kTimeSensitiveMenus[index])
				return static_cast<std::uint8_t>(1u << index);
		}
		return 0;
	}

	std::uint8_t GetOpenTimeSensitiveMenuMask(RE::UI* a_ui)
	{
		std::uint8_t mask = 0;
		if (!a_ui)
			return mask;

		for (std::size_t index = 0; index < kTimeSensitiveMenus.size(); ++index) {
			if (a_ui->IsMenuOpen(kTimeSensitiveMenus[index]))
				mask |= static_cast<std::uint8_t>(1u << index);
		}
		return mask;
	}
}

void EditorWindow::PauseTime()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;

	const float currentTimeScale = calendar->timeScale->value;
	if (timePaused) {
		if (timeMenuGuardActive) {
			if (!std::isfinite(currentTimeScale) || currentTimeScale <= 0.0f)
				calendar->timeScale->value = GetMenuRunningTimeScale(savedTimeScale);
			return;
		}
		if (currentTimeScale == 0.0f)
			return;
	}

	// A resume requested during the guard already preserved the pre-menu scale. Do not
	// replace it with the temporary safety scale if the user pauses again before close.
	if (restoreTimeScaleAfterMenu)
		savedTimeScale = SanitizeRunningTimeScale(savedTimeScale);
	else if (std::isfinite(currentTimeScale) && currentTimeScale > 0.0f)
		savedTimeScale = currentTimeScale;
	else if (timePaused)
		savedTimeScale = SanitizeRunningTimeScale(savedTimeScale);
	else
		savedTimeScale = kVanillaTimeScale;

	timePaused = true;
	restoreTimeScaleAfterMenu = false;
	timeStatePendingAfterMenu = false;
	if (timeMenuGuardActive) {
		if (!std::isfinite(currentTimeScale) || currentTimeScale <= 0.0f)
			calendar->timeScale->value = GetMenuRunningTimeScale(savedTimeScale);
		logger::info("Time pause deferred until menu transition completes (saved timescale: {})", savedTimeScale);
	} else {
		calendar->timeScale->value = 0.0f;
		logger::info("Time paused (saved timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResumeTime()
{
	if (!timePaused)
		return;
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;

	savedTimeScale = SanitizeRunningTimeScale(savedTimeScale);
	timePaused = false;
	if (timeMenuGuardActive) {
		restoreTimeScaleAfterMenu = true;
		timeStatePendingAfterMenu = false;
		if (!std::isfinite(calendar->timeScale->value) || calendar->timeScale->value <= 0.0f)
			calendar->timeScale->value = GetMenuRunningTimeScale(savedTimeScale);
		logger::info("Time pause cancelled during menu transition (timescale: {})", calendar->timeScale->value);
	} else {
		restoreTimeScaleAfterMenu = false;
		timeStatePendingAfterMenu = false;
		calendar->timeScale->value = savedTimeScale;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResetTimeScale()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;
	savedTimeScale = kVanillaTimeScale;
	restoreTimeScaleAfterMenu = false;
	timeStatePendingAfterMenu = false;
	if (!timePaused)
		calendar->timeScale->value = kVanillaTimeScale;
	timeScaleSlider = kVanillaTimeScale;
}

void EditorWindow::SetTimeRunningForMenu(bool a_needsRunningTime)
{
	const bool wasGuardActive = timeMenuGuardActive;
	timeMenuGuardActive = a_needsRunningTime;
	if (a_needsRunningTime)
		timeStatePendingAfterMenu = false;

	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (a_needsRunningTime) {
		if (calendar && calendar->timeScale &&
			(!std::isfinite(calendar->timeScale->value) || calendar->timeScale->value <= 0.0f)) {
			const float safetyScale = timePaused || restoreTimeScaleAfterMenu ? savedTimeScale : kVanillaTimeScale;
			calendar->timeScale->value = GetMenuRunningTimeScale(safetyScale);
		}
		if (!wasGuardActive && calendar && calendar->timeScale)
			logger::debug("Time restored for menu transition (timescale: {})", calendar->timeScale->value);
	} else if (wasGuardActive) {
		timeStatePendingAfterMenu = timePaused || restoreTimeScaleAfterMenu;
		if (!calendar || !calendar->timeScale)
			return;

		if (timePaused) {
			calendar->timeScale->value = 0.0f;
			logger::debug("Editor-owned time pause restored after menu transition");
		} else if (restoreTimeScaleAfterMenu) {
			savedTimeScale = SanitizeRunningTimeScale(savedTimeScale);
			calendar->timeScale->value = savedTimeScale;
			logger::debug("Pre-menu timescale restored after menu transition ({})", savedTimeScale);
		}
		restoreTimeScaleAfterMenu = false;
		timeStatePendingAfterMenu = false;
	}
}

void EditorWindow::SyncExternalTimeScale()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;

	if (timeMenuGuardActive) {
		if (!std::isfinite(calendar->timeScale->value) || calendar->timeScale->value <= 0.0f) {
			const float safetyScale = timePaused || restoreTimeScaleAfterMenu ? savedTimeScale : kVanillaTimeScale;
			calendar->timeScale->value = GetMenuRunningTimeScale(safetyScale);
		}
		return;
	}

	if (timeStatePendingAfterMenu) {
		if (timePaused) {
			calendar->timeScale->value = 0.0f;
		} else if (restoreTimeScaleAfterMenu) {
			savedTimeScale = SanitizeRunningTimeScale(savedTimeScale);
			calendar->timeScale->value = savedTimeScale;
		}
		restoreTimeScaleAfterMenu = false;
		timeStatePendingAfterMenu = false;
		return;
	}

	if (!timePaused)
		return;

	const float currentTimeScale = calendar->timeScale->value;
	if (std::isfinite(currentTimeScale) && currentTimeScale > 0.0f) {
		timePaused = false;
		savedTimeScale = currentTimeScale;
		timeScaleSlider = currentTimeScale;
		logger::info("External timescale change released editor pause ownership (timescale: {})", currentTimeScale);
	}
}

RE::BSEventNotifyControl EditorWindow::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	const std::uint8_t menuBit = GetTimeSensitiveMenuBit(a_event->menuName);
	if (menuBit == 0)
		return RE::BSEventNotifyControl::kContinue;

	if (a_event->opening)
		openMenuMask |= menuBit;
	else
		openMenuMask &= static_cast<std::uint8_t>(~menuBit);

	GetSingleton()->SetTimeRunningForMenu(openMenuMask != 0);
	return RE::BSEventNotifyControl::kContinue;
}

bool EditorWindow::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	static bool registered = false;
	if (registered)
		return true;

	auto ui = globals::game::ui ? globals::game::ui : RE::UI::GetSingleton();
	if (!ui) {
		logger::error("[CSEditor] UI event source not found");
		return false;
	}

	auto eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[CSEditor] MenuOpenCloseEvent source not found");
		return false;
	}

	singleton.openMenuMask = GetOpenTimeSensitiveMenuMask(ui);
	eventSource->AddEventSink(&singleton);
	registered = true;
	GetSingleton()->SetTimeRunningForMenu(singleton.openMenuMask != 0);
	logger::info("[CSEditor] Registered MenuOpenCloseEventHandler");
	return true;
}

bool EditorWindow::DrawGameHourSlider(const char* label, const char* format)
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->gameHour)
		return false;
	const bool changed = ImGui::SliderFloat(label, &calendar->gameHour->value, 0.0f, kGameHourMax, format);
	const bool activated = ImGui::IsItemActivated();
	const bool active = ImGui::IsItemActive();
	const bool deactivated = ImGui::IsItemDeactivated();
	const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
	if (activated)
		gameHourScrubRefreshIssued = false;
	if (changed && active) {
		const double currentTime = ImGui::GetTime();
		if (!gameHourScrubRefreshIssued || currentTime - lastGameHourScrubRefreshTime >= kGameHourScrubRefreshIntervalSeconds) {
			Util::RequestTimeJumpTransition();
			lastGameHourScrubRefreshTime = currentTime;
			gameHourScrubRefreshIssued = true;
		}
	}
	if (deactivatedAfterEdit)
		Util::RequestTimeJumpTransition();
	if (deactivated)
		gameHourScrubRefreshIssued = false;
	return true;
}

void EditorWindow::DrawTimeControls()
{
	SyncExternalTimeScale();

	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->gameHour || !calendar->timeScale)
		return;

	const float framePadX = ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* resumeTimeText = T(TKEY("resume_time"), "Resume Time");
	const char* pauseTimeText = T(TKEY("pause_time"), "Pause Time");
	const char* resetSpeedText = T(TKEY("reset_speed"), "Reset Speed");
	const float buttonWidth = std::max({ ImGui::CalcTextSize(resumeTimeText).x,
								  ImGui::CalcTextSize(pauseTimeText).x,
								  ImGui::CalcTextSize(resetSpeedText).x }) +
	                          framePadX;
	if (ImGui::Button(timePaused ? resumeTimeText : pauseTimeText, ImVec2(buttonWidth, 0)))
		TogglePause();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T("cs_editor.pause_time_tooltip", "Pause or resume game time progression"));
	ImGui::SameLine();
	DrawGameHourSlider(T(TKEY("game_time"), "Game Time"));
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T("cs_editor.game_time_tooltip", "Adjust the current game time"));

	// Sync slider with actual value
	if (timePaused)
		timeScaleSlider = std::max(savedTimeScale, kTimeScaleMin);
	else if (std::abs(calendar->timeScale->value - timeScaleSlider) > 0.01f)
		timeScaleSlider = calendar->timeScale->value;

	// Row 2: Reset Speed + TimeScale slider + speed label
	if (ImGui::Button(resetSpeedText, ImVec2(buttonWidth, 0)))
		ResetTimeScale();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("cs_editor.reset_speed_tooltip", "Reset time speed to vanilla (%.1fx)"), kVanillaTimeScale);

	ImGui::SameLine();
	ImGui::BeginDisabled(timePaused);
	if (ImGui::SliderFloat("##TimeScale", &timeScaleSlider, kTimeScaleMin, kTimeScaleMax,
			timeScaleSlider == kVanillaTimeScale ? T(TKEY("vanilla_speed"), "Vanilla Speed") : "", ImGuiSliderFlags_Logarithmic))
		calendar->timeScale->value = timeScaleSlider;
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("%.1fx", calendar->timeScale->value);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("cs_editor.time_scale_tooltip", "Adjust how fast time passes (vanilla: %.1fx)"), kVanillaTimeScale);
}

void EditorWindow::DisableVanityCamera()
{
	vanityCameraSuppression.Acquire();
}

void EditorWindow::RestoreVanityCamera()
{
	vanityCameraSuppression.Release();
}

void EditorWindow::EnterPreviewMode(PreviewMode mode)
{
	if (mode == PreviewMode::None)
		return;

	// Already in free camera flying — ignore duplicate click
	if (mode == previewMode)
		return;

	// Re-enter flying from locked state via button click
	if (mode == PreviewMode::FreeCamera && previewMode == PreviewMode::FreeCameraLocked) {
		previewMode = PreviewMode::FreeCamera;
		logger::info("Free camera unlocked (re-entered flying)");
		return;
	}

	// Switch from a different active mode first
	if (previewMode != PreviewMode::None)
		ExitPreviewMode();

	previewMode = mode;
	savedMousePos = ImGui::GetIO().MousePos;

	if (mode == PreviewMode::FreeCamera) {
		flySpeed = kDefaultFlySpeed;
		RE::Console::ExecuteCommand("tfc");
		RE::Console::ExecuteCommand(std::format("sucsm {:.0f}", flySpeed).c_str());
	}

	logger::info("Entered preview mode: {}", mode == PreviewMode::FreeCamera ? "FreeCamera" : "PlayMode");
}

void EditorWindow::ExitPreviewMode()
{
	bool wasFlying = IsPreviewFlying();

	if (previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::FreeCameraLocked)
		RE::Console::ExecuteCommand("tfc");

	logger::info("Exited preview mode");
	previewMode = PreviewMode::None;

	// Only restore cursor if exiting from a flying state; FreeCameraLocked already has the cursor active
	if (wasFlying) {
		ImGui::GetIO().MousePos = savedMousePos;
		ImGui::GetIO().WantSetMousePos = true;
	}
}

void EditorWindow::ToggleFreeCameraLock()
{
	if (previewMode == PreviewMode::FreeCamera) {
		previewMode = PreviewMode::FreeCameraLocked;
		ImGui::GetIO().MousePos = savedMousePos;
		ImGui::GetIO().WantSetMousePos = true;
		logger::info("Free camera locked");
	} else if (previewMode == PreviewMode::FreeCameraLocked) {
		savedMousePos = ImGui::GetIO().MousePos;
		previewMode = PreviewMode::FreeCamera;
		logger::info("Free camera unlocked");
	}
}

void EditorWindow::AdjustFlySpeed(float scrollDelta)
{
	if (previewMode != PreviewMode::FreeCamera)
		return;

	flySpeed = std::clamp(flySpeed + scrollDelta * kFlySpeedScrollStep, kMinFlySpeed, kMaxFlySpeed);
	RE::Console::ExecuteCommand(std::format("sucsm {:.0f}", flySpeed).c_str());
}

bool EditorWindow::ShouldHandleEscapeKey()
{
	// Input backlogs can deliver press and release before the next ImGui frame.
	// Cancel an active picker directly so the same release cannot close its parent.
	if (lightEditor.IsPicking()) {
		lightEditor.CancelPick();
		suppressNextEditorEscape = false;
		return false;
	}
	if (suppressNextEditorEscape) {
		suppressNextEditorEscape = false;
		return false;
	}
	return !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void EditorWindow::PushUndoState(Widget* widget)
{
	if (!widget)
		return;

	auto restore = widget->CaptureUndoState();
	if (!restore)
		return;

	UndoState state{
		.widgetIdentity = widget->GetStableIdentity(),
		.widgetLabel = widget->GetEditorID(),
		.restoreRuntime =
			settings.autoApplyChanges && !widget->RequiresManualApply(),
		.restore = std::move(restore)
	};

	undoStack.push_back(std::move(state));

	if (undoStack.size() > maxUndoStates) {
		undoStack.erase(undoStack.begin());
	}
}

void EditorWindow::PerformUndo()
{
	if (undoStack.empty())
		return;

	UndoState state = std::move(undoStack.back());
	undoStack.pop_back();

	if (auto* widget = FindWidgetByIdentity(state.widgetIdentity)) {
		state.restore(*widget);
		if (state.restoreRuntime)
			widget->ApplyChanges();
		ShowNotification(
			std::vformat(T("cs_editor.undone_changes_to", "Undone changes to {}"), std::make_format_args(state.widgetLabel)),
			Menu::GetSingleton()->GetSettings().Theme.StatusPalette.InfoColor,
			2.0f);
	} else {
		ShowNotification(
			I18n::GetSingleton()->Format("cs_editor.undo_editor_unavailable", { { "editor", state.widgetLabel } }, "Cannot undo {editor} because its editor is no longer active"),
			Menu::GetSingleton()->GetSettings().Theme.StatusPalette.Warning,
			3.0f);
	}
}

Widget* EditorWindow::FindWidgetByIdentity(std::string_view identity)
{
	for (auto* collection : GetWidgetCollections()) {
		for (auto& widget : *collection) {
			if (widget && widget->GetStableIdentity() == identity)
				return widget.get();
		}
	}

	if (currentCellLightingWidget && currentCellLightingWidget->GetStableIdentity() == identity)
		return currentCellLightingWidget.get();

	if (auto cached = cachedCellLightingWidgets.find(std::string(identity));
		cached != cachedCellLightingWidgets.end())
		return cached->second.get();

	return nullptr;
}

void EditorWindow::ShowNotification(const std::string& message, const ImVec4& color, float duration)
{
	// Guard against calls before ImGui is initialized
	if (!ImGui::GetCurrentContext()) {
		logger::warn("ShowNotification called before ImGui initialization: {}", message);
		return;
	}

	Notification notif;
	notif.message = message;
	notif.color = color;
	notif.startTime = static_cast<float>(ImGui::GetTime());
	notif.duration = duration;
	notifications.push_back(notif);
}

void EditorWindow::RenderNotifications()
{
	// Guard against calls before ImGui is initialized
	if (!ImGui::GetCurrentContext()) {
		return;
	}

	float currentTime = static_cast<float>(ImGui::GetTime());
	const float scale = Util::GetUIScale();
	float yOffset = 10.0f * scale;

	// Remove expired notifications
	notifications.erase(
		std::remove_if(notifications.begin(), notifications.end(),
			[currentTime](const Notification& n) { return currentTime - n.startTime > n.duration; }),
		notifications.end());

	// Render active notifications
	for (auto& notif : notifications) {
		float elapsed = currentTime - notif.startTime;
		float fadeStart = notif.duration - 0.5f;  // Start fading 0.5s before end
		float alpha = 1.0f;

		// Fade out in the last 0.5 seconds
		if (elapsed > fadeStart) {
			alpha = 1.0f - ((elapsed - fadeStart) / 0.5f);
		}

		// Position in top-left corner
		ImGui::SetNextWindowPos(ImVec2(10.0f * scale, yOffset), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.8f * alpha);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f * scale, 10.0f * scale));

		if (ImGui::Begin(std::format("##Notification{}", (void*)&notif).c_str(),
				nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
			ImVec4 colorWithAlpha = notif.color;
			colorWithAlpha.w *= alpha;
			ImGui::PushStyleColor(ImGuiCol_Text, colorWithAlpha);
			ImGui::TextUnformatted(notif.message.c_str());
			ImGui::PopStyleColor();

			yOffset += ImGui::GetWindowSize().y + 5.0f * scale;
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}
}

void EditorWindow::RefreshJsonAttachmentCache(const std::vector<Widget*>& widgets)
{
	for (auto* widget : widgets) {
		if (!widget) {
			continue;
		}
		if (!jsonAttachmentCache.contains(widget)) {
			jsonAttachmentCache.emplace(widget, widget->HasSavedFile());
		}
	}
}

bool EditorWindow::HasCachedJsonAttachment(Widget* widget) const
{
	if (!widget) {
		return false;
	}
	if (auto it = jsonAttachmentCache.find(widget); it != jsonAttachmentCache.end()) {
		return it->second;
	}
	return false;
}

void EditorWindow::InvalidateJsonAttachmentCache(Widget* widget)
{
	if (widget) {
		jsonAttachmentCache.erase(widget);
		return;
	}
	jsonAttachmentCache.clear();
}

void EditorWindow::OnWidgetJsonAttachmentChanged(Widget* widget)
{
	InvalidateJsonAttachmentCache(widget);
}

void EditorWindow::AddToRecent(const std::string& widgetId, const std::string& category)
{
	auto& categoryRecent = settings.recentWidgets[category];

	// Remove if already exists
	auto it = std::find(categoryRecent.begin(), categoryRecent.end(), widgetId);
	if (it != categoryRecent.end()) {
		categoryRecent.erase(it);
	}

	// Add to front
	categoryRecent.insert(categoryRecent.begin(), widgetId);

	// Limit size
	const auto maxRecent = static_cast<size_t>(std::clamp(settings.maxRecentWidgets, 5, 20));
	if (categoryRecent.size() > maxRecent) {
		categoryRecent.resize(maxRecent);
	}

	Save();
}

void EditorWindow::ToggleFavorite(const std::string& widgetId)
{
	auto it = std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId);
	if (it != settings.favoriteWidgets.end()) {
		settings.favoriteWidgets.erase(it);
	} else {
		settings.favoriteWidgets.push_back(widgetId);
	}
	Save();
}

bool EditorWindow::IsFavorite(const std::string& widgetId) const
{
	return std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId) != settings.favoriteWidgets.end();
}

bool EditorWindow::CanBeOpen()
{
	auto* player = globals::game::player;
	auto* state = globals::state;
	return player && player->parentCell && !state->isLoadingMenuOpen && !state->isMainMenuOpen;
}

bool EditorWindow::IsViewportActive() const
{
	return settings.showViewport && !(globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR);
}

void EditorWindow::HideGameMenus()
{
	if (gameMenusHidden)
		return;

	// ShowMenus(false) stops the game from rendering to the back buffer.
	// Without d3d12SwapChain, blur reads directly from that buffer and would freeze.
	if (!globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (auto ui = RE::UI::GetSingleton()) {
		ui->ShowMenus(false);
		gameMenusHidden = true;
		logger::info("Game menus hidden for CS editor");
	}
}

void EditorWindow::ShowGameMenus()
{
	if (!gameMenusHidden)
		return;

	if (auto ui = RE::UI::GetSingleton()) {
		ui->ShowMenus(true);
		gameMenusHidden = false;
		logger::info("Game menus restored after CS editor");
	}
}
