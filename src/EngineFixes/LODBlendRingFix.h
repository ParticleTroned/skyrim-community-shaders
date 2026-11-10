#pragma once

#include "EngineFix.h"

#include <RE/B/BSShaderManager.h>
#include <RE/N/NiPoint2.h>

namespace RE
{
	class TESWorldSpace;
}

/** @brief Advances terrain LOD grid transitions using frame time. */
struct LODBlendRingFix : EngineFix
{
	std::string GetName() override { return "LOD Blend Ring Fix"; }
	/** @brief Retains native behavior when the required VR address is unavailable. */
	bool TryInstall() override;

	static LODBlendRingFix& GetInstance();

	/** @brief Updates the shared world-space ring once at the render-frame boundary. */
	void Update();

private:
	bool OwnsSlide(const RE::BSShaderManager::State& a_state) const;
	void FinishSlide(RE::BSShaderManager::State& a_state);

	bool* blendInProgress = nullptr;
	RE::TESWorldSpace* worldSpace = nullptr;
	RE::NiPoint2 center{};
	RE::NiPoint2 target{};
	RE::NiPoint2 slideOrigin{};
	float nativeLerpStart = 0.0f;
	float slideProgress = 0.0f;
	bool sliding = false;
};
