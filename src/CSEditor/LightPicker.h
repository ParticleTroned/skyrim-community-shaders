#pragma once

#include "RE/B/BSCoreTypes.h"      // RE::FormID
#include "RE/B/BSPointerHandle.h"  // RE::ObjectRefHandle
#include "Utils/Input.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace RE
{
	class NiCamera;
	class TESBoundObject;
	class TESObjectREFR;
}

/** @brief Resolves a world reference selected through a verified desktop or VR pointer source. */
struct LightPicker
{
	/** @brief Stable identity and metadata for a picked reference. */
	struct PickedMesh
	{
		RE::ObjectRefHandle refrHandle;
		RE::FormID baseFormId = 0;
		RE::FormID baseLocalFormId = 0;
		std::string editorId;
		std::string modelPath;
		std::string sourcePlugin;
		std::string refFormEntry;
		bool valid = false;
	};

	/** @brief Selects collision geometry or a nearby non-collidable effect mesh. */
	enum class PickMode
	{
		kCollision = 0,
		kEffect = 1
	};
	PickMode pickMode = PickMode::kCollision;

	/** @brief Enters pick mode and clears any previous result. */
	void BeginPick();
	/** @brief Leaves pick mode without producing a result. */
	void Cancel();
	[[nodiscard]] bool IsPicking() const { return picking; }

	/** @brief Clears the cached hover hit so the next valid pointer sample recomputes it. */
	void InvalidateHover();

	/** @brief Updates hover state, handles cancellation, and stores a successful world click. */
	void Update();

	/** @brief Returns and clears the last successful pick. */
	[[nodiscard]] PickedMesh TakeResult();

	/** @brief Formats a reference for a Light Placer filter list, or returns an empty string. */
	static std::string FormatRefFormEntry(RE::TESObjectREFR* refr);

	/** @brief Formats a plugin-local FormID and defining plugin for a Light Placer filter list. */
	static std::string FormatFormEntry(RE::FormID localFormId, std::string_view ownerPlugin);

private:
	enum class PointerSource : std::uint8_t
	{
		kUnavailable,
		kFlatViewport,
		kVRLeftController,
		kVRRightController
	};

	/** @brief Returns a source only when the overlay pointer has a demonstrable world mapping. */
	static PointerSource GetPointerSource(PickMode mode);
	/** @brief Maps one logical VR controller to its physical crosshair target. */
	static PointerSource GetVRPointerSource(InputDeviceType controller);
	/** @brief Finds the single flat-runtime world camera, or nullptr if the choice is ambiguous. */
	static RE::NiCamera* GetPlayerNiCamera();
	/** @brief Resolves collision geometry through a verified flat ray or the active VR crosshair target. */
	static PickedMesh ResolveUnderCursor(bool logResult = true);
	/** @brief Resolves the nearest flat-runtime effect mesh; unavailable in VR without a world ray. */
	static PickedMesh ResolveNearestToCursor();
	/** @brief Resolves a physical VR controller's engine-owned crosshair target. */
	static PickedMesh ResolveVRCollisionTarget(PointerSource source, bool logResult);
	/** @brief Copies reference metadata while the caller retains the reference through a smart pointer. */
	static void PopulateFromRef(PickedMesh& out, RE::TESObjectREFR* refr, RE::TESBoundObject* baseObj);

	bool picking = false;
	PickedMesh result;
	PickedMesh hoverMesh;
	PointerSource hoverPointerSource = PointerSource::kUnavailable;
	float lastMouseX = -1.0f;
	float lastMouseY = -1.0f;
};
