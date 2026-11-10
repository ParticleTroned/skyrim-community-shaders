#include "LODBlendRingFix.h"

#include "Globals.h"
#include "State.h"

#include <RE/M/Misc.h>
#include <RE/T/TES.h>
#include <RE/U/UI.h>
#include <REL/IDDB.h>
#include <REL/Relocation.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr float kSlideSeconds = 0.25f;
	constexpr double kCellSize = 4096.0;
	constexpr float kPinnedLerpStart = std::numeric_limits<float>::max();

	bool IsFinite(const RE::NiPoint2& a_point)
	{
		return std::isfinite(a_point.x) && std::isfinite(a_point.y);
	}

	bool IsSingleCellShift(const RE::NiPoint2& a_from, const RE::NiPoint2& a_to)
	{
		return std::abs(static_cast<double>(a_to.x) - a_from.x) <= kCellSize &&
		       std::abs(static_cast<double>(a_to.y) - a_from.y) <= kCellSize;
	}
}

LODBlendRingFix& LODBlendRingFix::GetInstance()
{
	static LODBlendRingFix instance;
	return instance;
}

bool LODBlendRingFix::TryInstall()
{
	if (blendInProgress)
		return true;
#ifdef ENABLE_SKYRIM_VR
	if (REL::Module::IsVR() && !REL::IDDB::get().IsVRAddressLibraryAtLeastVersion("0.278.0", false)) {
		logger::warn("[LOD Blend Ring Fix] VR Address Library 0.278.0 or newer is required; retaining native transitions");
		return false;
	}
#endif
	// TES defers per-cell LOD blend reflagging until this flag clears.
	blendInProgress = reinterpret_cast<bool*>(REL::RelocationID(527641, 414593).address());
	return blendInProgress != nullptr;
}

bool LODBlendRingFix::OwnsSlide(const RE::BSShaderManager::State& a_state) const
{
	return sliding && a_state.gridArrayCenter == center && a_state.oldGridArrayCenter == center &&
	       a_state.gridArrayLerpStart == kPinnedLerpStart;
}

void LODBlendRingFix::FinishSlide(RE::BSShaderManager::State& a_state)
{
	// A new engine grid publication takes precedence over our previous slide.
	if (OwnsSlide(a_state)) {
		a_state.oldGridArrayCenter = a_state.gridArrayCenter = target;
		a_state.gridArrayLerpStart = nativeLerpStart;
		*blendInProgress = false;
	}
	sliding = false;
}

void LODBlendRingFix::Update()
{
	if (!blendInProgress)
		return;

	auto* shaderState = globals::game::smState;
	if (!shaderState) {
		sliding = false;
		worldSpace = nullptr;
		return;
	}
	auto& state = *shaderState;
	auto* tes = globals::game::tes;
	auto* ui = globals::game::ui;
	auto* runtime = globals::state;
	if (!tes || !ui || !runtime || runtime->IsWorldLoadTransitionActive() ||
		runtime->IsMainOrLoadingMenuOpen() || tes->interiorCell) {
		FinishSlide(state);
		worldSpace = nullptr;
		return;
	}
	auto* currentWorld = tes->GetRuntimeData2().worldSpace;
	if (!currentWorld) {
		FinishSlide(state);
		worldSpace = nullptr;
		return;
	}

	const bool changedWorld = currentWorld != worldSpace;
	if (changedWorld || (sliding && (!OwnsSlide(state) || !*blendInProgress)))
		FinishSlide(state);

	if (!IsFinite(state.gridArrayCenter) || !IsFinite(state.oldGridArrayCenter) ||
		!std::isfinite(state.gridArrayLerpStart)) {
		FinishSlide(state);
		worldSpace = nullptr;
		return;
	}
	if (ui->GameIsPaused())
		return;
	worldSpace = currentWorld;
	if (!*blendInProgress)
		return;

	const float delta = RE::GetSecondsSinceLastFrame();
	const bool validDelta = std::isfinite(delta) && delta >= 0.0f;
	if (sliding && !validDelta) {
		FinishSlide(state);
		return;
	}
	if (!sliding) {
		// Loads and multi-cell jumps snap to the native target instead of crossing unrelated terrain.
		if (changedWorld || !validDelta || state.oldGridArrayCenter == state.gridArrayCenter ||
			!IsSingleCellShift(state.oldGridArrayCenter, state.gridArrayCenter)) {
			state.oldGridArrayCenter = state.gridArrayCenter;
			*blendInProgress = false;
			return;
		}
		slideOrigin = state.oldGridArrayCenter;
		target = state.gridArrayCenter;
		nativeLerpStart = state.gridArrayLerpStart;
		slideProgress = 0.0f;
		sliding = true;
	}

	slideProgress = std::min(1.0f, slideProgress + std::min(delta, kSlideSeconds) / kSlideSeconds);
	const float eased = slideProgress * slideProgress * (3.0f - 2.0f * slideProgress);
	center = { std::lerp(slideOrigin.x, target.x, eased), std::lerp(slideOrigin.y, target.y, eased) };
	state.oldGridArrayCenter = state.gridArrayCenter = center;
	// Pin the native game-time lerp only while the frame-clock slide owns the ring.
	state.gridArrayLerpStart = kPinnedLerpStart;
	if (slideProgress >= 1.0f)
		FinishSlide(state);
}
