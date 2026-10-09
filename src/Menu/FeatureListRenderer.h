#pragma once

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct Feature;

class FeatureListRenderer
{
public:
	/** Menu features plus optional companion pages, without registering companions with the renderer. */
	static std::vector<Feature*> GetMenuFeatures();
	/** Explicit sidebar choice takes precedence over automatic hover hiding. */
	static void SetSidebarVisible(bool a_visible);
	static bool IsSidebarVisible();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Return an existing built-in menu entry ID without queuing navigation. */
	static std::string GetBuiltInPageMenuEntryId(const std::string& a_page);
#endif
	/** Resume the user's automatic visibility preference. */
	static void ResetSidebarVisibility();

	struct BuiltInMenu
	{
		std::string name;
		std::function<void()> func;
	};

	struct CategoryHeader
	{
		std::string name;
		int count = 0;
	};

	using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, Feature*>;

	static void RenderFeatureList(
		float footerHeight,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::string& pendingFeatureSelection,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

private:
	struct ListMenuVisitor
	{
		size_t listId;
		size_t& selectedMenuRef;
		std::map<std::string, bool>& categoryExpansionStates;

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string& label);
		void operator()(const CategoryHeader& header);
		void operator()(Feature* feat);
	};

	struct DrawMenuVisitor
	{
		explicit DrawMenuVisitor(std::string& pendingFeatureSelectionRef) :
			pendingFeatureSelection(pendingFeatureSelectionRef) {}

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string&);
		void operator()(const CategoryHeader&);
		void operator()(Feature* feat);

	private:
		std::string& pendingFeatureSelection;

		// Helper methods for Feature rendering
		static bool IsFeatureInstalled(const std::string& featureName);
		void RenderFeatureHeader(Feature* feat, bool isDisabled, bool isLoaded, bool sceneControlled);
		void RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled);
		static void RenderRestoreDefaultsButton(Feature* feat, bool isDisabled, bool isLoaded, float availableWidth);
		void RenderReactiveConstraintWarningDialog();
	};

	static std::vector<MenuFuncInfo> BuildMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

	static void HandlePendingFeatureSelection(
		std::string& pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu);

	static void RenderLeftColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates);

	static void RenderRightColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t selectedMenu,
		std::string& pendingFeatureSelection);
};
