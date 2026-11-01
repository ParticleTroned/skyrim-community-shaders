#include "LightPicker.h"
#include "EditorWindow.h"
#include "Globals.h"
#include "I18n/I18n.h"

#include "RE/B/bhkPickData.h"
#include "RE/C/CrosshairPickData.h"
#include "RE/N/NiCamera.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TES.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESHavokUtilities.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESObjectSTAT.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <utility>

namespace
{
	constexpr float kSkyrimToHavok = 0.0142875f;
	constexpr float kRayLengthSkyrim = 100000.0f;

	struct ViewportPointerMapping
	{
		ImVec2 imageMin;
		ImVec2 imageSize;
		ImVec2 renderPosition;
		ImVec2 renderSize;
	};

	bool IsEditorMarker(const RE::TESBoundObject* baseObj)
	{
		const auto* stat = baseObj ? baseObj->As<RE::TESObjectSTAT>() : nullptr;
		return stat && (stat->GetFormFlags() & RE::TESObjectSTAT::RecordFlags::kIsMarker) != 0;
	}

	bool IsFinite(const RE::NiPoint3& point)
	{
		return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
	}

	bool TryGetViewportPointerMapping(ViewportPointerMapping& out)
	{
		const auto& viewport = EditorWindow::GetSingleton()->GetViewportImageRect();
		auto* viewportWindow = ImGui::FindWindowByName("###CSEditorViewport");
		auto* context = ImGui::GetCurrentContext();
		const ImVec2 mouse = ImGui::GetMousePos();
		const ImVec2 imageSize(viewport.max.x - viewport.min.x, viewport.max.y - viewport.min.y);
		if (!viewport.valid || !viewportWindow || !context || context->HoveredWindow != viewportWindow ||
			!std::isfinite(viewport.min.x) || !std::isfinite(viewport.min.y) ||
			!std::isfinite(viewport.max.x) || !std::isfinite(viewport.max.y) ||
			!std::isfinite(viewport.renderSize.x) || !std::isfinite(viewport.renderSize.y) ||
			!std::isfinite(mouse.x) || !std::isfinite(mouse.y) ||
			imageSize.x <= 0.0f || imageSize.y <= 0.0f ||
			viewport.renderSize.x <= 0.0f || viewport.renderSize.y <= 0.0f ||
			mouse.x < viewport.min.x || mouse.y < viewport.min.y ||
			mouse.x >= viewport.max.x || mouse.y >= viewport.max.y) {
			return false;
		}

		const ImVec2 uv(
			(mouse.x - viewport.min.x) / imageSize.x,
			(mouse.y - viewport.min.y) / imageSize.y);
		out.imageMin = viewport.min;
		out.imageSize = imageSize;
		out.renderPosition = ImVec2(uv.x * viewport.renderSize.x, uv.y * viewport.renderSize.y);
		out.renderSize = viewport.renderSize;
		return std::isfinite(out.renderPosition.x) && std::isfinite(out.renderPosition.y);
	}

	ImVec2 RenderPixelToViewport(const ViewportPointerMapping& mapping, const ImVec2& renderPixel)
	{
		return ImVec2(
			mapping.imageMin.x + renderPixel.x / mapping.renderSize.x * mapping.imageSize.x,
			mapping.imageMin.y + renderPixel.y / mapping.renderSize.y * mapping.imageSize.y);
	}
}

void LightPicker::PopulateFromRef(PickedMesh& out, RE::TESObjectREFR* refr, RE::TESBoundObject* baseObj)
{
	if (!refr || !baseObj)
		return;

	const RE::ObjectRefHandle handle = refr->GetHandle();
	if (!handle)
		return;

	out.refrHandle = handle;
	out.baseFormId = baseObj->GetFormID();
	out.baseLocalFormId = baseObj->GetLocalFormID();
	out.editorId = clib_util::editorID::get_editorID(baseObj);
	if (auto* model = baseObj->As<RE::TESModel>()) {
		if (const char* path = model->GetModel())
			out.modelPath = path;
	}
	if (const auto* file = baseObj->GetFile(0))
		out.sourcePlugin = file->fileName;
	out.refFormEntry = FormatRefFormEntry(refr);
	out.valid = true;
}

std::string LightPicker::FormatFormEntry(RE::FormID localFormId, std::string_view ownerPlugin)
{
	if (ownerPlugin.empty())
		return {};
	return fmt::format("0x{:X}~{}", localFormId, ownerPlugin);
}

std::string LightPicker::FormatRefFormEntry(RE::TESObjectREFR* refr)
{
	if (!refr)
		return {};
	const auto* definingFile = refr->GetFile(0);
	if (!definingFile || !definingFile->fileName)
		return {};
	return FormatFormEntry(refr->GetLocalFormID(), definingFile->fileName);
}

LightPicker::PointerSource LightPicker::GetPointerSource(PickMode)
{
	ViewportPointerMapping mapping;
	return TryGetViewportPointerMapping(mapping) ? PointerSource::kFlatViewport : PointerSource::kUnavailable;
}

RE::NiCamera* LightPicker::GetPlayerNiCamera()
{
	auto* playerCamera = RE::PlayerCamera::GetSingleton();
	if (!playerCamera || !playerCamera->cameraRoot)
		return nullptr;

	RE::NiCamera* worldCamera = nullptr;
	for (auto& child : playerCamera->cameraRoot->GetChildren()) {
		if (auto* camera = netimmerse_cast<RE::NiCamera*>(child.get())) {
			if (worldCamera)
				return nullptr;
			worldCamera = camera;
		}
	}
	return worldCamera;
}

LightPicker::PickedMesh LightPicker::ResolveUnderCursor(bool logResult)
{
	const PointerSource source = GetPointerSource(PickMode::kCollision);

	PickedMesh out;
	if (source != PointerSource::kFlatViewport)
		return out;

	auto* niCamera = GetPlayerNiCamera();
	if (!niCamera)
		return out;

	ViewportPointerMapping mapping;
	if (!TryGetViewportPointerMapping(mapping))
		return out;

	RE::NiPoint3 origin;
	RE::NiPoint3 direction;
	if (!niCamera->WindowPointToRay(
			static_cast<std::int32_t>(mapping.renderPosition.x),
			static_cast<std::int32_t>(mapping.renderPosition.y),
			origin,
			direction,
			mapping.renderSize.x,
			mapping.renderSize.y) ||
		!IsFinite(origin) || !IsFinite(direction)) {
		return out;
	}

	const float directionLength = direction.Unitize();
	if (!std::isfinite(directionLength) || directionLength <= 0.0f)
		return out;
	const RE::NiPoint3 end = origin + direction * kRayLengthSkyrim;

	RE::bhkPickData pick{};
	pick.rayInput.from = RE::hkVector4(
		origin.x * kSkyrimToHavok,
		origin.y * kSkyrimToHavok,
		origin.z * kSkyrimToHavok,
		0.0f);
	pick.rayInput.to = RE::hkVector4(
		end.x * kSkyrimToHavok,
		end.y * kSkyrimToHavok,
		end.z * kSkyrimToHavok,
		0.0f);

	auto* tes = RE::TES::GetSingleton();
	if (!tes)
		return out;
	tes->Pick(pick);
	if (!pick.rayOutput.rootCollidable)
		return out;

	auto* rawRef = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
	if (!rawRef)
		return out;
	const RE::ObjectRefHandle handle = rawRef->GetHandle();
	const auto refr = handle.get();
	if (!refr || refr->IsDisabled() || refr->IsDeleted())
		return out;

	auto* baseObj = refr->GetObjectReference();
	if (!baseObj || IsEditorMarker(baseObj))
		return out;

	PopulateFromRef(out, refr.get(), baseObj);
	if (logResult && out.valid) {
		logger::info("[LightPicker] Hit ref 0x{:08X} '{}' model '{}' plugin '{}'",
			refr->GetFormID(), out.editorId, out.modelPath, out.sourcePlugin);
	}
	return out;
}

LightPicker::PickedMesh LightPicker::ResolveNearestToCursor(bool logResult)
{
	PickedMesh out;
	if (GetPointerSource(PickMode::kEffect) != PointerSource::kFlatViewport)
		return out;

	auto* niCamera = GetPlayerNiCamera();
	if (!niCamera)
		return out;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return out;
	auto* cell = player->GetParentCell();
	if (!cell)
		return out;

	ViewportPointerMapping mapping;
	if (!TryGetViewportPointerMapping(mapping))
		return out;
	const ImVec2 cursor = ImGui::GetMousePos();
	constexpr float kSearchRadius = 5000.0f;
	constexpr float kScreenThresholdSq = 64.0f * 64.0f;

	float bestDistanceSq = kScreenThresholdSq;
	RE::ObjectRefHandle bestHandle;
	const RE::NiPoint3 playerPosition = player->GetPosition();
	cell->ForEachReferenceInRange(playerPosition, kSearchRadius,
		[&](RE::TESObjectREFR* refr) -> RE::BSContainer::ForEachResult {
			if (!refr || refr->IsDisabled() || refr->IsDeleted())
				return RE::BSContainer::ForEachResult::kContinue;

			float screenX = 0.0f;
			float screenY = 0.0f;
			float screenZ = 0.0f;
			if (!niCamera->WorldPtToScreenPt3(refr->GetPosition(), screenX, screenY, screenZ, 1e-5f) ||
				!std::isfinite(screenX) || !std::isfinite(screenY) || !std::isfinite(screenZ) ||
				screenZ <= 0.0f) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const ImVec2 renderPixel(
				screenX * mapping.renderSize.x,
				(1.0f - screenY) * mapping.renderSize.y);
			const ImVec2 viewportPixel = RenderPixelToViewport(mapping, renderPixel);
			const float deltaX = viewportPixel.x - cursor.x;
			const float deltaY = viewportPixel.y - cursor.y;
			const float distanceSq = deltaX * deltaX + deltaY * deltaY;
			if (distanceSq >= bestDistanceSq || IsEditorMarker(refr->GetObjectReference()))
				return RE::BSContainer::ForEachResult::kContinue;

			const RE::ObjectRefHandle handle = refr->GetHandle();
			if (!handle)
				return RE::BSContainer::ForEachResult::kContinue;
			bestDistanceSq = distanceSq;
			bestHandle = handle;
			return RE::BSContainer::ForEachResult::kContinue;
		});

	const auto bestRef = bestHandle.get();
	if (!bestRef)
		return out;
	auto* baseObj = bestRef->GetObjectReference();
	if (!baseObj)
		return out;

	const PickedMesh collisionHit = ResolveUnderCursor(false);
	if (collisionHit.valid && collisionHit.refrHandle == bestHandle)
		return out;

	PopulateFromRef(out, bestRef.get(), baseObj);
	if (logResult && out.valid) {
		logger::info("[LightPicker] Effect-pick ref 0x{:08X} '{}' model '{}' plugin '{}'",
			bestRef->GetFormID(), out.editorId, out.modelPath, out.sourcePlugin);
	}
	return out;
}

void LightPicker::BeginPick()
{
	picking = true;
	result = {};
	InvalidateHover();
	logger::info("[LightPicker] Pick mode started");
}

void LightPicker::Cancel()
{
	if (picking)
		logger::info("[LightPicker] Pick mode cancelled");
	picking = false;
}

void LightPicker::InvalidateHover()
{
	hoverMesh = {};
	hoverPointerSource = PointerSource::kUnavailable;
	lastMouseX = -1.0f;
	lastMouseY = -1.0f;
	hoverDirty = false;
}

void LightPicker::Update()
{
	if (!picking)
		return;

	const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	if (escapePressed || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		if (escapePressed && ImGui::IsKeyDown(ImGuiKey_Escape))
			EditorWindow::GetSingleton()->suppressNextEditorEscape = true;
		Cancel();
		return;
	}

	ViewportPointerMapping mapping;
	const bool cursorOverViewport = TryGetViewportPointerMapping(mapping);
	if (ImGui::GetIO().WantCaptureMouse && !cursorOverViewport) {
		InvalidateHover();
		return;
	}

	const PointerSource pointerSource = GetPointerSource(pickMode);
	if (pointerSource != hoverPointerSource) {
		hoverMesh = {};
		lastMouseX = -1.0f;
		lastMouseY = -1.0f;
		hoverDirty = true;
		hoverPointerSource = pointerSource;
	}
	const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	PointerSource clickPointerSource = pointerSource;

	if (pointerSource == PointerSource::kUnavailable) {
		const ImVec2 mouse = ImGui::GetMousePos();
		if (std::isfinite(mouse.x) && std::isfinite(mouse.y)) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(T("editor.light_picker.world_picking_is_unavailable_because_the_curs_92849f2b", "World picking is unavailable because the cursor and game viewport do not match."));
			ImGui::EndTooltip();
		}
		if (clickPointerSource == PointerSource::kUnavailable)
			return;
	}

	constexpr double kHoverRefreshSeconds = 0.05;
	const ImVec2 mouse = ImGui::GetMousePos();
	const bool mouseMoved = mouse.x != lastMouseX || mouse.y != lastMouseY;

	lastMouseX = mouse.x;
	lastMouseY = mouse.y;
	hoverDirty |= mouseMoved;
	const double now = ImGui::GetTime();
	if (hoverDirty && now - lastHoverTime >= kHoverRefreshSeconds) {
		lastHoverTime = now;
		hoverDirty = false;
		hoverMesh = pickMode == PickMode::kEffect ? ResolveNearestToCursor(false) : ResolveUnderCursor(false);
	}

	if (hoverMesh.valid) {
		ImGui::BeginTooltip();
		if (!hoverMesh.editorId.empty())
			ImGui::Text(T("feature.light_editor.picked_editor_id", "EditorID: %s"), hoverMesh.editorId.c_str());
		if (!hoverMesh.modelPath.empty())
			ImGui::Text(T("feature.light_editor.picked_mesh", "Mesh: %s"), hoverMesh.modelPath.c_str());
		ImGui::Text(T("feature.light_editor.picked_base_form_id", "Base FormID: 0x%08X"), hoverMesh.baseFormId);
		if (!hoverMesh.refFormEntry.empty())
			ImGui::Text(T("feature.light_editor.picked_reference_form_id", "Reference FormID: %s"), hoverMesh.refFormEntry.c_str());
		if (!hoverMesh.sourcePlugin.empty())
			ImGui::Text(T("feature.light_editor.picked_plugin", "Plugin: %s"), hoverMesh.sourcePlugin.c_str());
		ImGui::EndTooltip();
	}

	if (leftClicked) {
		PickedMesh hit;
		if (clickPointerSource == PointerSource::kFlatViewport) {
			hit = pickMode == PickMode::kEffect ? ResolveNearestToCursor(false) : ResolveUnderCursor(false);
		}
		if (hit.valid) {
			logger::info("[LightPicker] Picked base 0x{:08X} '{}' model '{}' ref '{}' plugin '{}'",
				hit.baseFormId, hit.editorId, hit.modelPath, hit.refFormEntry, hit.sourcePlugin);
			result = std::move(hit);
			picking = false;
			InvalidateHover();
		}
	}
}

LightPicker::PickedMesh LightPicker::TakeResult()
{
	PickedMesh out = std::move(result);
	result = {};
	return out;
}
