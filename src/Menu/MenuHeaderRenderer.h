#pragma once

#include "Menu.h"

class MenuHeaderRenderer
{
public:
	struct ActionIcon
	{
		ID3D11ShaderResourceView* texture;
		const char* tooltip;
		std::function<void()> callback;
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

private:
	static std::vector<ActionIcon> BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons);
	static void RenderActionIcons(const std::vector<ActionIcon>& actionIcons, bool isDocked, float uiScale);
	static void RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale);
	static void RenderSteamVRDockHandle(float uiScale, ImGuiID dockSpaceId);
	static void RenderStableHeader(const std::string& title, bool showLogo, const std::vector<ActionIcon>& actionIcons, float uiScale, const Menu::UIIcons& uiIcons);
	static void RenderWatermarkLogo(const Menu::UIIcons& uiIcons);
};
