#include "OverlayRenderer.h"
#include "BackgroundBlur.h"
#include "HomePageRenderer.h"
#include "OverlayPolicy.h"
#include "PerformanceTuningRenderer.h"
#include "ThemeManager.h"

#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <winrt/base.h>

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/OverlayFeature.h"
#include "Features/RenderDoc.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/VR.h"
#include "Features/WeatherPicker.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	std::unordered_map<ImGuiID, float> s_windowOverlapAlpha;

	constexpr ImGuiWindowFlags SKIP_WINDOW_FLAGS = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
	constexpr const char* MAIN_WINDOW_ID = "###CommunityShaders";

	bool IsMainWindow(ImGuiWindow* win) { return win->Name && strstr(win->Name, MAIN_WINDOW_ID); }

	float GetVRSettingsWindowAspect()
	{
		return globals::features::vr.GetEffectiveMenuAttachMode() == VR::Settings::OverlayAttachMode::ControllerOnly ?
		           VR::Config::kOverlayAspect :
		           VR::Config::kHMDMenuAspect;
	}

	float GetVRMenuSafePadding()
	{
		return std::max(2.0f, ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale() * 0.5f);
	}

	void ExcludeShaderCompilationWindowFromTop(ImVec2& a_availableMin, const ImVec2& a_availableMax)
	{
		const char* topStatusWindows[] = {
			"ShaderCompilationInfo",
			"UWCacheCreationInfo",
			"ShaderBlockingInfo"
		};

		for (const char* windowName : topStatusWindows) {
			auto* statusWindow = ImGui::FindWindowByName(windowName);
			if (!statusWindow || !statusWindow->Active || statusWindow->Hidden)
				continue;

			const float statusBottom = statusWindow->Pos.y + statusWindow->Size.y + ImGui::GetStyle().ItemSpacing.y;
			if (statusBottom > a_availableMin.y)
				a_availableMin.y = std::min(statusBottom, a_availableMax.y);
		}
	}

	ImVec2 FitSizeToAspect(ImVec2 a_availableSize, float a_heightOverWidth)
	{
		a_availableSize.x = std::max(a_availableSize.x, 1.0f);
		a_availableSize.y = std::max(a_availableSize.y, 1.0f);
		a_heightOverWidth = std::isfinite(a_heightOverWidth) && a_heightOverWidth > 0.0f ? a_heightOverWidth : 1.0f;

		float width = a_availableSize.x;
		float height = width * a_heightOverWidth;
		if (height > a_availableSize.y) {
			height = a_availableSize.y;
			width = height / a_heightOverWidth;
		}

		return ImVec2(width, height);
	}

	bool ComputeDesktopMirrorTransform(
		float a_sourceWidth,
		float a_sourceHeight,
		float a_targetWidth,
		float a_targetHeight,
		float& a_scale,
		float& a_offsetX,
		float& a_offsetY)
	{
		if (a_sourceWidth <= 0.0f || a_sourceHeight <= 0.0f || a_targetWidth <= 0.0f || a_targetHeight <= 0.0f) {
			return false;
		}

		a_scale = std::min(a_targetWidth / a_sourceWidth, a_targetHeight / a_sourceHeight);
		if (!(a_scale > 0.0f)) {
			return false;
		}

		const float leftoverX = std::max(0.0f, a_targetWidth - a_sourceWidth * a_scale);
		const float leftoverY = std::max(0.0f, a_targetHeight - a_sourceHeight * a_scale);

		// Keep the mirror biased toward the desktop origin, but retain half of the
		// former centered padding so the desktop menu and dock targets do not sit
		// flush against the top-left edge.
		a_offsetX = leftoverX * 0.25f;
		a_offsetY = leftoverY * 0.25f;
		return true;
	}

	void DrawShaderCompilationFailures(uint64_t failed, const Menu::ThemeSettings& themeSettings)
	{
		ImGui::TextColored(themeSettings.StatusPalette.Error,
			"ERROR: %llu shaders failed to compile. Check installation and CommunityShaders.log",
			static_cast<unsigned long long>(failed));

		if (FeatureIssues::HasPotentialShaderModifyingFeatures()) {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "Features that may have modified shaders detected. Check Feature Issues in the Menu.");
		}
	}

	bool IsVisibleRootWindow(ImGuiWindow* win)
	{
		if (!win || !win->WasActive || win->Hidden)
			return false;
		return !(win->ParentWindow && !win->DockIsActive) && !(win->Flags & SKIP_WINDOW_FLAGS);
	}

	bool ShouldFilterVROverlaysFromDesktop()
	{
		return globals::features::vr.IsOpenVRCompatible() &&
		       globals::features::vr.ShouldUseInSceneOverlay() &&
		       globals::features::vr.openVRInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;
	}

	bool IsHiddenDesktopOverlayDrawList(const ImDrawList* drawList, const std::vector<OverlayFeature*>& overlays)
	{
		if (!drawList || !drawList->_OwnerName) {
			return false;
		}

		for (const auto* overlay : overlays) {
			if (!overlay || !overlay->HideFromDesktopWhenSubmittedToVR()) {
				continue;
			}

			const char* windowName = overlay->GetOverlayWindowName();
			if (windowName && strcmp(drawList->_OwnerName, windowName) == 0) {
				return true;
			}
		}

		return false;
	}

	ImDrawData* BuildDesktopDrawData(
		ImDrawData* drawData,
		const std::vector<OverlayFeature*>& overlays,
		ImDrawData& filteredDrawData)
	{
		if (!drawData || !ShouldFilterVROverlaysFromDesktop()) {
			return drawData;
		}

		bool removedAny = false;
		int totalIdxCount = 0;
		int totalVtxCount = 0;

		filteredDrawData = *drawData;
		filteredDrawData.CmdLists.clear();
		filteredDrawData.CmdLists.reserve(drawData->CmdListsCount);
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			auto* cmdList = drawData->CmdLists[i];
			if (IsHiddenDesktopOverlayDrawList(cmdList, overlays)) {
				removedAny = true;
				continue;
			}

			filteredDrawData.CmdLists.push_back(cmdList);
			totalIdxCount += cmdList->IdxBuffer.Size;
			totalVtxCount += cmdList->VtxBuffer.Size;
		}

		if (!removedAny) {
			return drawData;
		}

		filteredDrawData.CmdListsCount = filteredDrawData.CmdLists.Size;
		filteredDrawData.TotalIdxCount = totalIdxCount;
		filteredDrawData.TotalVtxCount = totalVtxCount;
		return &filteredDrawData;
	}

	bool BuildScaledDesktopDrawData(
		ImDrawData* drawData,
		ImDrawData& scaledDrawData,
		std::vector<ImDrawList*>& ownedCmdLists)
	{
		if (!drawData || !globals::game::isVR || !globals::features::vr.IsOpenVRCompatible() || !globals::d3d::swapChain) {
			return false;
		}

		const float sourceWidth = drawData->DisplaySize.x;
		const float sourceHeight = drawData->DisplaySize.y;
		if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
			return false;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		globals::d3d::swapChain->GetDesc(&desc);
		const float targetWidth = static_cast<float>(desc.BufferDesc.Width);
		const float targetHeight = static_cast<float>(desc.BufferDesc.Height);
		if (targetWidth <= 0.0f || targetHeight <= 0.0f) {
			return false;
		}

		float scale = 1.0f;
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		if (!ComputeDesktopMirrorTransform(sourceWidth, sourceHeight, targetWidth, targetHeight, scale, offsetX, offsetY)) {
			return false;
		}
		if (scale == 1.0f && offsetX == 0.0f && offsetY == 0.0f) {
			return false;
		}

		const ImVec2 sourceDisplayPos = drawData->DisplayPos;

		scaledDrawData = *drawData;
		scaledDrawData.DisplayPos = ImVec2(0.0f, 0.0f);
		scaledDrawData.DisplaySize = ImVec2(targetWidth, targetHeight);
		scaledDrawData.CmdLists.clear();
		scaledDrawData.CmdLists.reserve(drawData->CmdListsCount);

		int totalIdxCount = 0;
		int totalVtxCount = 0;
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			auto* sourceCmdList = drawData->CmdLists[i];
			auto* clonedCmdList = sourceCmdList ? sourceCmdList->CloneOutput() : nullptr;
			if (!clonedCmdList) {
				continue;
			}

			for (auto& vertex : clonedCmdList->VtxBuffer) {
				vertex.pos.x = (vertex.pos.x - sourceDisplayPos.x) * scale + offsetX;
				vertex.pos.y = (vertex.pos.y - sourceDisplayPos.y) * scale + offsetY;
			}

			for (auto& command : clonedCmdList->CmdBuffer) {
				command.ClipRect.x = (command.ClipRect.x - sourceDisplayPos.x) * scale + offsetX;
				command.ClipRect.y = (command.ClipRect.y - sourceDisplayPos.y) * scale + offsetY;
				command.ClipRect.z = (command.ClipRect.z - sourceDisplayPos.x) * scale + offsetX;
				command.ClipRect.w = (command.ClipRect.w - sourceDisplayPos.y) * scale + offsetY;
			}

			ownedCmdLists.push_back(clonedCmdList);
			scaledDrawData.CmdLists.push_back(clonedCmdList);
			totalIdxCount += clonedCmdList->IdxBuffer.Size;
			totalVtxCount += clonedCmdList->VtxBuffer.Size;
		}

		if (scaledDrawData.CmdLists.Size != drawData->CmdListsCount) {
			for (auto* cmdList : ownedCmdLists) {
				IM_DELETE(cmdList);
			}
			ownedCmdLists.clear();
			scaledDrawData.CmdLists.clear();
			return false;
		}

		scaledDrawData.CmdListsCount = scaledDrawData.CmdLists.Size;
		scaledDrawData.TotalIdxCount = totalIdxCount;
		scaledDrawData.TotalVtxCount = totalVtxCount;
		return true;
	}

	// Patches DrawList background vertices for windows involved in overlap.
	void PatchOverlappingWindowBackgrounds()
	{
		auto* ctx = ImGui::GetCurrentContext();
		if (!ctx)
			return;

		using C = ThemeManager::Constants;
		const float dt = ImGui::GetIO().DeltaTime;

		struct WinInfo
		{
			ImGuiWindow* win;
			ImRect rect;
		};
		std::vector<WinInfo> windows;
		for (int i = 0; i < ctx->Windows.Size; i++) {
			auto* win = ctx->Windows[i];
			if (IsVisibleRootWindow(win))
				windows.push_back({ win, win->Rect() });
		}

		std::unordered_set<ImGuiID> overlapping;
		for (size_t i = 0; i < windows.size(); i++)
			for (size_t j = i + 1; j < windows.size(); j++)
				if (windows[i].rect.Overlaps(windows[j].rect)) {
					auto* a = windows[i].win;
					auto* b = windows[j].win;
					// Main CSX window never dims; other windows yield to it
					if (IsMainWindow(a))
						overlapping.insert(b->ID);
					else if (IsMainWindow(b))
						overlapping.insert(a->ID);
					else
						overlapping.insert(a->FocusOrder > b->FocusOrder ? a->ID : b->ID);
				}

		const ImU32 bgRGB = ImGui::GetColorU32(ImGuiCol_WindowBg) & ~IM_COL32_A_MASK;

		for (auto& [win, rect] : windows) {
			const float target = overlapping.count(win->ID) ? C::OVERLAP_MIN_ALPHA : 0.0f;
			float& alpha = s_windowOverlapAlpha[win->ID];
			const float speed = (target > alpha) ? C::OVERLAP_FADEIN_SPEED : C::OVERLAP_FADEOUT_SPEED;
			alpha += (target - alpha) * (std::min)(1.0f, dt * speed);

			if (alpha < C::OVERLAP_ALPHA_EPSILON) {
				alpha = 0.0f;
				continue;
			}

			auto* dl = win->DrawList;
			if (!dl || dl->VtxBuffer.Size == 0)
				continue;

			// Clamp background rect vertex alpha (contiguous bgRGB block at start of DrawList)
			const ImU32 minA = static_cast<ImU32>(alpha * 255.0f);
			for (int v = 0; v < dl->VtxBuffer.Size; v++) {
				auto& vtx = dl->VtxBuffer[v];
				if ((vtx.col & ~IM_COL32_A_MASK) != bgRGB)
					break;
				ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
				if (a > 0 && a < minA)
					vtx.col = bgRGB | (minA << IM_COL32_A_SHIFT);
			}
		}

		// Prune stale entries
		for (auto it = s_windowOverlapAlpha.begin(); it != s_windowOverlapAlpha.end();)
			it->second < C::OVERLAP_ALPHA_EPSILON ? it = s_windowOverlapAlpha.erase(it) : ++it;
	}
}  // namespace

void OverlayRenderer::RenderOverlay(
	Menu& menu,
	const std::function<void()>& processInputEventQueue,
	const std::function<void()>& drawSettings,
	const std::function<const char*(std::vector<InputCombo>)>& keyIdToString,
	float& cachedFontSize,
	float currentFontSize)
{
	// Advance closed-menu work before choosing the VR canvas. Completion can
	// reopen CS and restore the user's saved headset presentation this frame.
	PerformanceTuningRenderer::UpdateClosedMenuMeasurement();
	HandleVRSetup();
	ApplyVROverlayDisplaySize();
	processInputEventQueue();

	// Keep delayed light-reference cleanup moving before processing any close
	// transition that can enqueue another refresh, and before a no-overlay return.
	auto* editorWindow = EditorWindow::GetSingleton();
	editorWindow->AdvanceLightEditorDeferredWork();
	editorWindow->UpdateOpenState();

	auto drawableOverlays = CollectDrawableFeatureOverlays(menu);
	if (ShouldSkipRendering(menu, !drawableOverlays.empty())) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		globals::features::vr.DiscardQueuedImGuiClickOwners();
		s_windowOverlapAlpha.clear();
		globals::features::vr.HideOverlaysIfPresent();
		return;
	}

	HandleFontReload(menu, cachedFontSize, currentFontSize);
	InitializeImGuiFrame(menu);

	if (ShouldShowShaderCompilationStatus(menu))
		RenderShaderCompilationStatus(keyIdToString);
	RenderShaderBlockingStatus();
	PerformanceTuningRenderer::RenderClosedMenuMeasurementOverlay();

	if (editorWindow->open) {
		bool flying = editorWindow->IsPreviewFlying();
		auto& io = ImGui::GetIO();
		io.MouseDrawCursor = !flying;
		if (flying)
			io.MousePos = { -FLT_MAX, -FLT_MAX };  // prevent hover/tooltips during active flying
		editorWindow->Draw();
	} else if (menu.IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup()) {
		if (!globals::game::isVR || !globals::features::vr.IsOpenVRCompatible()) {
			ImGui::GetIO().MouseDrawCursor = true;
		}
		if (menu.IsEnabled) {
			drawSettings();
		}
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}

	RenderFeatureOverlays(drawableOverlays);
	RenderFirstTimeSetupOverlay();
	HandleABTesting();
	PatchOverlappingWindowBackgrounds();
	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.UpdateWandHoverFeedback();
	}
	FinalizeImGuiFrame(drawableOverlays);
}

ImVec2 OverlayRenderer::GetDefaultVRSettingsWindowSize(bool a_excludeShaderCompilationWindow)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	if (!viewport) {
		return ImVec2(1.0f, 1.0f);
	}

	const float safePadding = GetVRMenuSafePadding();
	ImVec2 availableMin(
		viewport->WorkPos.x + safePadding,
		viewport->WorkPos.y + safePadding);
	const ImVec2 availableMax(
		viewport->WorkPos.x + viewport->WorkSize.x - safePadding,
		viewport->WorkPos.y + viewport->WorkSize.y - safePadding);
	if (availableMax.x <= availableMin.x || availableMax.y <= availableMin.y) {
		return ImVec2(1.0f, 1.0f);
	}

	if (a_excludeShaderCompilationWindow) {
		ExcludeShaderCompilationWindowFromTop(availableMin, availableMax);
	}
	availableMin.y = std::min(availableMin.y, availableMax.y);

	const ImVec2 availableSpan(
		std::max(availableMax.x - availableMin.x, 0.0f),
		std::max(availableMax.y - availableMin.y, 0.0f));
	return FitSizeToAspect(availableSpan, GetVRSettingsWindowAspect());
}

float OverlayRenderer::GetDefaultVRLeftAnchorX(float a_windowWidth)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float safePadding = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	if (!viewport) {
		return safePadding;
	}

	const float minLeftEdge = viewport->WorkPos.x + safePadding;
	const float maxRightEdge = viewport->WorkPos.x + viewport->WorkSize.x - safePadding;
	const float centeredLeftEdge = viewport->WorkPos.x + std::max(viewport->WorkSize.x - a_windowWidth, 0.0f) * 0.5f;
	const float blendedLeftEdge = minLeftEdge + (centeredLeftEdge - minLeftEdge) * 0.5f;
	return std::clamp(blendedLeftEdge, minLeftEdge, std::max(minLeftEdge, maxRightEdge - a_windowWidth));
}

bool OverlayRenderer::MoveWindowBelowShaderCompilationStatus(ImVec2& position, const ImVec2& windowSize, const ImVec2& pivot)
{
	if (windowSize.x <= 0.0f || windowSize.y <= 0.0f) {
		return false;
	}

	const ImVec2 windowMin(
		position.x - windowSize.x * pivot.x,
		position.y - windowSize.y * pivot.y);
	const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
	const ImRect windowRect(windowMin, windowMax);

	const char* kStatusWindows[] = {
		"ShaderCompilationInfo",
		"UWCacheCreationInfo",
		"ShaderBlockingInfo"
	};

	float targetMinY = windowRect.Min.y;
	bool overlappedAny = false;
	for (const char* windowName : kStatusWindows) {
		auto* statusWin = ImGui::FindWindowByName(windowName);
		if (!statusWin || !statusWin->Active || statusWin->Hidden) {
			continue;
		}

		const ImRect statusRect(
			statusWin->Pos,
			ImVec2(statusWin->Pos.x + statusWin->Size.x, statusWin->Pos.y + statusWin->Size.y));
		if (!windowRect.Overlaps(statusRect)) {
			continue;
		}

		targetMinY = std::max(targetMinY, statusRect.Max.y + ImGui::GetStyle().ItemSpacing.y);
		overlappedAny = true;
	}

	if (!overlappedAny) {
		return false;
	}

	position.y += targetMinY - windowRect.Min.y;
	return true;
}

void OverlayRenderer::HandleVRSetup()
{
	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.RecreateOverlayTexturesIfNeeded();
	}
}

bool OverlayRenderer::ShouldSkipRendering(const Menu& menu, bool hasDrawableFeatureOverlay)
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	auto* renderDoc = RenderDoc::GetSingleton();

	const bool hasShaderCompilationStatus =
		ShouldShowShaderCompilationStatus(menu) &&
		(shaderCache->IsCompiling() || (failed && !hide) || renderDoc->IsAvailable());

	return !(hasShaderCompilationStatus ||
			 menu.IsEnabled ||
			 menu.HasClosedMenuOverlay() ||
			 HomePageRenderer::ShouldShowFirstTimeSetup() ||
			 EditorWindow::GetSingleton()->open ||
			 abTestingManager->IsEnabled() ||
			 hasDrawableFeatureOverlay);
}

bool OverlayRenderer::ShouldShowShaderCompilationStatus(const Menu& menu)
{
	const auto* state = globals::state;
	const bool hasRenderedWorldFrame =
		state && state->lastWorldRenderFrame != std::numeric_limits<uint32_t>::max();
	return OverlayPolicy::ShouldShowShaderCompilationStatus({
		.hasRenderedWorldFrame = hasRenderedWorldFrame,
		.menuSessionOpen = menu.IsMenuSessionOpen(),
		.performanceOverlayOpen = menu.overlayVisible,
	});
}

std::vector<OverlayFeature*> OverlayRenderer::CollectDrawableFeatureOverlays(const Menu& menu)
{
	std::vector<OverlayFeature*> overlays;
	for (auto* feat : Feature::GetFeatureList()) {
		if (!feat || !feat->loaded) {
			continue;
		}

		auto* overlay = dynamic_cast<OverlayFeature*>(feat);
		if (overlay && ShouldDrawFeatureOverlay(*overlay, menu)) {
			overlays.push_back(overlay);
		}
	}

	return overlays;
}

bool OverlayRenderer::ShouldDrawFeatureOverlay(const OverlayFeature& overlay, const Menu& menu)
{
	if (!overlay.IsOverlayVisible()) {
		return false;
	}

	return !overlay.RequiresGlobalOverlayToggle() || menu.overlayVisible;
}

void OverlayRenderer::HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize)
{
	bool fontSizeChanged = std::abs(cachedFontSize - currentFontSize) > ThemeManager::Constants::FONT_CACHE_EPSILON;
	std::string desiredSignature = menu.BuildFontSignature(currentFontSize);
	bool signatureChanged = desiredSignature != menu.cachedFontSignature;

	if (fontSizeChanged || signatureChanged) {
		if (!ThemeManager::ReloadFont(menu, cachedFontSize)) {
			logger::warn("OverlayRenderer::HandleFontReload() - Font reload failed");
		}
	}
}

bool OverlayRenderer::ApplyVROverlayDisplaySize()
{
	uint32_t canvasW = 0;
	uint32_t canvasH = 0;
	if (!globals::game::isVR || !globals::features::vr.GetMenuCanvasSize(canvasW, canvasH))
		return false;

	auto& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(canvasW), static_cast<float>(canvasH));
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	return true;
}

void OverlayRenderer::InitializeImGuiFrame(Menu& menu)
{
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	// ImGui_ImplWin32_NewFrame() restores DisplaySize from the desktop window.
	// Re-apply the active VR overlay canvas so input and rendering stay 1:1 with the menu texture.
	const bool usingVROverlayCanvas = ApplyVROverlayDisplaySize();
	if (globals::d3d::swapChain) {
		DXGI_SWAP_CHAIN_DESC desc{};
		globals::d3d::swapChain->GetDesc(&desc);
		if (usingVROverlayCanvas) {
			// The desktop mirror uses an aspect-correct presentation of the VR canvas,
			// anchored to the desktop origin. Mirror mouse input through the same transform.
			auto& io = ImGui::GetIO();
			const float sourceWidth = io.DisplaySize.x;
			const float sourceHeight = io.DisplaySize.y;
			const float targetWidth = static_cast<float>(desc.BufferDesc.Width);
			const float targetHeight = static_cast<float>(desc.BufferDesc.Height);
			float scale = 1.0f;
			float offsetX = 0.0f;
			float offsetY = 0.0f;
			if (ComputeDesktopMirrorTransform(sourceWidth, sourceHeight, targetWidth, targetHeight, scale, offsetX, offsetY)) {
				POINT cursorPos{};
				if (GetCursorPos(&cursorPos) && ScreenToClient(desc.OutputWindow, &cursorPos)) {
					const float localX = static_cast<float>(cursorPos.x);
					const float localY = static_cast<float>(cursorPos.y);
					const bool insideScaledCanvas =
						localX >= offsetX &&
						localX <= offsetX + sourceWidth * scale &&
						localY >= offsetY &&
						localY <= offsetY + sourceHeight * scale;

					if (insideScaledCanvas && scale > 0.0f) {
						const float mappedX = (localX - offsetX) / scale;
						const float mappedY = (localY - offsetY) / scale;
						io.MousePos = ImVec2(mappedX, mappedY);
						io.AddMousePosEvent(mappedX, mappedY);
					} else {
						io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
						io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
					}
				} else {
					io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
					io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
				}
			}
		} else {
			const float displayW = static_cast<float>(desc.BufferDesc.Width);
			const float displayH = static_cast<float>(desc.BufferDesc.Height);
			Util::UpdateImGuiInput(desc.OutputWindow, displayW, displayH);
		}
	}

	if (globals::features::vr.IsOpenVRCompatible()) {
		// Let desktop/Win32 input establish the baseline first, then queue active
		// VR controller or wand ownership before ImGui consumes this frame.
		globals::features::vr.ProcessControllerInputForImGui();
	}

	ImGui::NewFrame();

	// Detect display size change (cross-session via ini handler, mid-session via member)
	const float2 currentDisplaySize{ ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y };
	if (menu.lastDisplaySize.x > 0.f && menu.lastDisplaySize != currentDisplaySize) {
		logger::info("Display size changed: {}x{} -> {}x{}, resetting window layout",
			menu.lastDisplaySize.x, menu.lastDisplaySize.y, currentDisplaySize.x, currentDisplaySize.y);
		menu.resetLayout = true;
		EditorWindow::GetSingleton()->resetLayout = true;
		globals::features::performanceOverlay.ResetWindowLayout();
		globals::features::weatherPicker.ResetWindowLayout();
	}
	menu.lastDisplaySize = currentDisplaySize;

	ThemeManager::SetupImGuiStyle(menu);
}

void OverlayRenderer::RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>& keyIdToString)
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();

	const float scale = Util::GetUIScale();
	float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const bool isVR = REL::Module::IsVR();
	if (isVR) {
		pos = GetDefaultVRLeftAnchorX(GetDefaultVRSettingsWindowSize(false).x);
	}

	const uint64_t totalShaders = shaderCache->GetTotalTasks();
	const uint64_t completedShaders = shaderCache->GetCompletedTasks();
	const uint64_t failedTasks = shaderCache->GetFailedTasks();
	const uint64_t processedShaders = std::min(completedShaders + failedTasks, totalShaders);
	const uint64_t diskCacheHits = std::min(shaderCache->GetDiskHitTasks(), completedShaders);
	const bool hasSourceCompilation = shaderCache->GetSourceCompileTasks() != 0 || failedTasks != 0;

	auto state = globals::state;
	auto* menu = Menu::GetSingleton();
	auto& themeSettings = menu->GetTheme();
	auto* renderDoc = RenderDoc::GetSingleton();
	bool renderDocAvailable = renderDoc->IsAvailable();
	const auto renderDocInformation = renderDoc->GetOverlayWarningMessage();
	const bool backgroundCompilation = shaderCache->backgroundCompilation.load(std::memory_order_relaxed);

	const char* progressAction = "Preparing Shaders";
	if (hasSourceCompilation) {
		progressAction = diskCacheHits == 0 ? "Compiling Shaders" : "Loading / Compiling Shaders";
	} else if (processedShaders != 0) {
		progressAction = "Loading Shader Cache";
	}
	auto progressTitle = fmt::format("{}{}: {}",
		backgroundCompilation ? "Background " : "",
		progressAction,
		shaderCache->GetShaderStatsString(!state->IsDeveloperMode()).c_str());
	auto percent = totalShaders > 0 ? static_cast<float>(processedShaders) / static_cast<float>(totalShaders) : 0.0f;
	auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", processedShaders, totalShaders, 100 * percent);

	if (shaderCache->IsCompiling()) {
		const bool hasFeatureSetRevertPending = shaderCache->HasFeatureSetRevertPending();
		const bool hasFeatureSetChanges = shaderCache->HasFeatureSetChanges();
		const bool isDiskCacheHeld = shaderCache->IsDiskCacheHeld();
		const bool hasFeatureIssues = FeatureIssues::HasFeatureIssues();

		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(progressTitle.c_str());
		ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());
		if (hasFeatureSetRevertPending) {
			ImGui::TextColored(themeSettings.StatusPalette.Warning, "%s",
				"Previous cache restored.\n"
				"Restart to use it.");
		} else if (hasFeatureSetChanges) {
			if (shaderCache->HasFeatureSetCacheBackup()) {
				const char* restoreStatus = shaderCache->HasPreviousDiskCache() ?
				                                "Previous cache saved. Restore is available after compilation finishes." :
				                                "Previous cache saved. Restore availability will be verified after compilation finishes.";
				ImGui::TextColored(themeSettings.StatusPalette.Warning, "%s\n%s",
					"Feature setup changed. Building a new shader cache for this setup.",
					restoreStatus);
			} else {
				ImGui::TextColored(themeSettings.StatusPalette.Warning, "%s",
					"Feature setup changed. Building shaders in memory until the cache can be rebuilt.\n"
					"Previous cache is not available for restore.");
			}
		} else if (isDiskCacheHeld) {
			ImGui::TextColored(themeSettings.StatusPalette.Warning, "%s",
				"Saved shader cache cannot be used.\n"
				"A required feature is missing or failed to load.");
		}
		if (hasFeatureIssues) {
			const size_t issueCount = FeatureIssues::GetFeatureIssues().size();
			const auto issueMessage = fmt::format(
				"WARNING: {} feature{} failed to load (bad install or version mismatch).\n"
				"Check CSX menu > Feature Issues tab, then quit, fix your mod setup, and restart.\n"
				"Compiling now bakes the wrong shaders and you will have to recompile after fixing.",
				issueCount,
				issueCount == 1 ? "" : "s");
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", issueMessage.c_str());
		}
		if (state->IsDeveloperMode()) {
			int32_t threadLimit = backgroundCompilation ? shaderCache->backgroundCompilationThreadCount : shaderCache->compilationThreadCount;
			int compilationRunning = (int)shaderCache->compilationPool.get_tasks_running();
			int heavyInFlight = shaderCache->GetHeavyTasksInFlight();
			int heavyLimit = static_cast<int>(Util::GetPerformanceCoreCount());
			uint64_t slow = shaderCache->GetSlowTasks();
			uint64_t verySlow = shaderCache->GetVerySlowTasks();
			ImGui::Text("Threads: %d / %d limit | Heavy: %d / %d P-cores | %d workers",
				compilationRunning,
				threadLimit,
				heavyInFlight,
				heavyLimit,
				(int)shaderCache->compilationPool.get_thread_count());
			if (slow > 0) {
				ImGui::Text("Slow shaders: %llu (very slow: %llu)", slow, verySlow);
			}
		}
		if (!backgroundCompilation && shaderCache->menuLoaded.load(std::memory_order_relaxed)) {
			auto skipShadersText = fmt::format(
				"Press {} to proceed without completing shader compilation. ",
				keyIdToString(menu->GetSettings().SkipCompilationKey));
			ImGui::TextUnformatted(skipShadersText.c_str());
			ImGui::TextUnformatted("WARNING: Uncompiled shaders will have visual errors or cause stuttering when loading.");
		}
		if (failed && !hide) {
			DrawShaderCompilationFailures(failed, themeSettings);
		}

		if (renderDocAvailable)
			ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());

		ImGui::End();
		return;
	}

	if (failed) {
		if (!hide) {
			ImGui::SetNextWindowPos(ImVec2(pos, pos));
			if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::End();
				return;
			}

			DrawShaderCompilationFailures(failed, themeSettings);

			if (renderDocAvailable)
				ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());

			ImGui::End();
		}
	} else if (renderDocAvailable) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}
		ImGui::TextColored(themeSettings.StatusPalette.Warning, renderDocInformation.c_str());
		ImGui::End();
	}
}

void OverlayRenderer::RenderFeatureOverlays(const std::vector<OverlayFeature*>& overlays)
{
	for (auto* overlay : overlays) {
		overlay->DrawOverlay();
	}
}

void OverlayRenderer::HandleABTesting()
{
	// A/B Testing management
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->Update();

	// Always update test data during TEST phase, regardless of overlay visibility
	if (abTestingManager->IsEnabled()) {
		globals::features::performanceOverlay.UpdateAllShaderTestData();

		// Add A/B test aggregator data collection here
		auto& overlay = globals::features::performanceOverlay;
		auto [mainRows, summaryRows] = overlay.BuildDrawCallRows();
		std::vector<DrawCallRow> allRows = mainRows;
		allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());

		// Update the A/B test aggregator with current frame data
		abTestingManager->GetAggregator().OnFrame(allRows);
	}

	// Draw A/B testing overlay
	abTestingManager->DrawOverlayUI();
}

void OverlayRenderer::FinalizeImGuiFrame(const std::vector<OverlayFeature*>& overlays)
{
	ImGui::Render();

	// Apply background blur behind ImGui windows before rendering them
	BackgroundBlur::RenderBackgroundBlur();

	ImDrawData filteredDrawData;
	ImDrawData scaledDesktopDrawData;
	std::vector<ImDrawList*> ownedDesktopCmdLists;
	ImDrawData* desktopDrawData = BuildDesktopDrawData(ImGui::GetDrawData(), overlays, filteredDrawData);
	if (BuildScaledDesktopDrawData(desktopDrawData, scaledDesktopDrawData, ownedDesktopCmdLists)) {
		desktopDrawData = &scaledDesktopDrawData;
	}
	ImGui_ImplDX11_RenderDrawData(desktopDrawData);
	for (auto* cmdList : ownedDesktopCmdLists) {
		IM_DELETE(cmdList);
	}

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.SubmitOverlayFrame();
	}
}

void OverlayRenderer::RenderFirstTimeSetupOverlay()
{
	if (HomePageRenderer::ShouldShowFirstTimeSetup()) {
		HomePageRenderer::RenderFirstTimeSetupDialog();
	}
}

void OverlayRenderer::RenderShaderBlockingStatus()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!state->IsDeveloperMode() || shaderCache->blockedKey.empty()) {
		return;
	}

	const float scale = Util::GetUIScale();
	float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	if (REL::Module::IsVR()) {
		pos = GetDefaultVRLeftAnchorX(GetDefaultVRSettingsWindowSize(false).x);
	}

	// Stack below shader compilation window if visible
	float yPos = pos;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			yPos = shaderWin->Pos.y + shaderWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	// Also stack below water cache overlay if visible
	if (auto* waterWin = ImGui::FindWindowByName("UWCacheCreationInfo")) {
		if (waterWin->Active && waterWin->Pos.y + waterWin->Size.y > yPos) {
			yPos = waterWin->Pos.y + waterWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	ImGui::SetNextWindowPos(ImVec2(pos, yPos));
	if (!ImGui::Begin("ShaderBlockingInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	Util::Text::Error("Shader Blocking Active");
	ImGui::Text("Blocked: %s", shaderCache->blockedKey.c_str());

	// Try to get more details from active shaders
	auto activeShaders = shaderCache->GetActiveShaders();

	// Find the index of the blocked shader in the active list (or show N/A if not found)
	size_t blockedIndex = 0;
	bool foundBlocked = false;
	for (size_t i = 0; i < activeShaders.size(); ++i) {
		if (activeShaders[i].key == shaderCache->blockedKey) {
			blockedIndex = i + 1;  // 1-based indexing for display
			foundBlocked = true;
			break;
		}
	}

	if (foundBlocked) {
		ImGui::Text("Index: %zu/%zu", blockedIndex, activeShaders.size());
	} else {
		ImGui::Text("Index: N/A (%zu active)", activeShaders.size());
	}

	for (const auto& shader : activeShaders) {
		if (shader.key == shaderCache->blockedKey) {
			ImGui::Text("Type: %s | Class: %s | Descriptor: 0x%X",
				magic_enum::enum_name(shader.shaderType).data(),
				magic_enum::enum_name(shader.shaderClass).data(),
				shader.descriptor);
			break;
		}
	}

	ImGui::End();
}
