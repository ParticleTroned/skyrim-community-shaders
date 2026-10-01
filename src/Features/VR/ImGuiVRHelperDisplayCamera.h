#pragma once

#include "ImGuiVRHelperSceneCapture.h"

#include <optional>

namespace ImGuiVRHelperDisplayCamera
{
	/// Freezes one display pose for a UI-only stereo pair on the render thread; never supplies world depth.
	/// The caller supplies pair provenance and must keep worldLayerEnabled false for this snapshot.
	[[nodiscard]] std::optional<ImGuiVRHelperSceneCapture::Snapshot> CaptureDisplayCamera() noexcept;
}
