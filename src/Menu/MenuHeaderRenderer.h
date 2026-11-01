#pragma once

#include "Menu.h"
#include "SettingsActions.h"
#include "Utils/UI.h"

class MenuHeaderRenderer
{
public:
	struct ActionIcon
	{
		using Glyph = Util::ActionGlyph;
		const char* id;
		const char* flashId;
		ID3D11ShaderResourceView* texture;
		const char* tooltip;
		std::function<void()> callback;
		Glyph glyph = Glyph::Texture;
		MenuUI::GlobalSettingsAction action = MenuUI::GlobalSettingsAction::None;
	};

	/** Draws responsive desktop branding, navigation and guarded global actions. */
	static void RenderHeader(bool isDocked, bool showLogo, bool canShowIcons, float uiScale, const Menu::UIIcons& uiIcons);
	/** Aligns the feature panel with the header's close control. */
	static float GetFeaturePanelWidth();

private:
	static std::vector<ActionIcon> BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons);
	static void DrawActionButton(const ActionIcon& icon, ImVec2 minimum, ImVec2 maximum, float iconInset = 0);
	static void DrawActionIcon(ImDrawList* draw, const ActionIcon& icon, ImVec2 minimum, ImVec2 maximum, ImU32 tint);
	static void RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons);
};
