#pragma once

#include "Menu.h"
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
	};

	static void RenderHeader(
		bool isDocked,
		bool showLogo,
		bool canShowIcons,
		float uiScale,
		const Menu::UIIcons& uiIcons,
		bool forceStableHeader = false,
		bool showSteamVRDockHandle = false,
		ImGuiID steamVRDockSpaceId = 0);
	static void RenderSteamVRResizeHandles(float uiScale);
	/** Width of a bordered feature panel whose content ends at the header close icon. */
	static float GetFeaturePanelWidth();
	/** Bottom-right grip; custom controls bypass NoResize only when native resizing is suppressed. */
	static void RenderResizeGrip(float uiScale, bool customResizeControls = false);

private:
	static void RenderResizeHandles(float uiScale, bool steamVRControls, bool customResizeControls = false);
	static void DrawActionIcon(ImDrawList* a_draw, const ActionIcon& a_icon, ImVec2 a_min, ImVec2 a_max, ImU32 a_tint);
	static std::vector<ActionIcon> BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons);
	static void RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderSteamVRDockHandle(float uiScale, ImGuiID dockSpaceId);
	static void RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons, bool showSteamVRDockHandle = false, ImGuiID steamVRDockSpaceId = 0);
};
