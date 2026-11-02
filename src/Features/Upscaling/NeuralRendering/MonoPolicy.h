#pragma once

#include "CharacterActorGroups.h"
#include "CharacterActorPolicy.h"
#include "CharacterFocusPolicy.h"
#include "ModelResolutionPolicy.h"
#include "NetworkSelection.h"
#include "PipelinePolicy.h"
#include "Style.h"
#include "Utils/Finite.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	struct MonoSettings
	{
		bool enabled = false;
		std::uint32_t mode = 0;
		std::uint32_t modelResolutionPercent = 100;
		std::uint32_t peripheralCompressionPercent = 0;
		std::uint32_t network = kNetworkDefault;
		std::uint32_t style = kDefaultStyle;
		float intensity = 0.8f, localTone = 0.75f, localStructure = 0.9f, skinStructure = 0.9f;
		bool guidedReconstruction = true, pressureResolution = true, prewarm = false;
		std::uint32_t guidedConfidenceRejectionPercent = 0;
		bool pauseInCombat = false;
		std::uint32_t colorMode = 2;
		float detailStrength = 1.0f, lightingPreservation = 1.0f;
		float exposure = 1.0f;
		bool charactersOnly = false, sceneCategoryStrengths = false;
		std::array<float, 5> categoryStrengths{ 1.0f, 1.0f, 0.65f, 1.0f, 1.0f };
		std::uint32_t categoryMask = (1u << 1) | (1u << 2) | (1u << 3);
		std::uint32_t actorGroupMask = (1u << 0) | (1u << 1);
		float maximumDistanceMeters = 10.0f;
		std::uint32_t minimumFacePixelSize = 64, holdFrames = 3;
		bool adaptiveActorSelection = false, actorFocus = false;
		float focusScale = 0.75f;
		bool cropToCharacters = true, packedCharacters = true;
		bool captureExposure = false;
		std::uint32_t exposureSource = 0;
		std::uint32_t regionPercent = 100, featherPixels = 32;
		float horizontalScale = 1.0f;
		std::array<float, 2> regionOffset{};
		bool operator==(const MonoSettings&) const = default;
	};

	[[nodiscard]] inline float FiniteClamp(float value, float minimum, float maximum, float fallback) noexcept
	{
		return Util::IsFinite(value) ? std::clamp(value, minimum, maximum) : fallback;
	}

	/** A persisted profile is bounded before it controls allocations, shaders or provider calls. */
	inline void Sanitize(MonoSettings& settings) noexcept
	{
		settings.mode = static_cast<std::uint32_t>(ConfiguredRenderingMode(settings.mode));
		settings.modelResolutionPercent = std::clamp(settings.modelResolutionPercent, 30u, 100u);
		settings.peripheralCompressionPercent = std::min(settings.peripheralCompressionPercent, 100u);
		settings.guidedConfidenceRejectionPercent = std::min(settings.guidedConfidenceRejectionPercent, 100u);
		settings.network = SanitizeNetworkSelection(settings.network);
		settings.style = ClampStyle(settings.style);
		settings.intensity = FiniteClamp(settings.intensity, 0.0f, 2.0f, 0.8f);
		settings.localTone = FiniteClamp(settings.localTone, 0.0f, 2.0f, 0.75f);
		settings.localStructure = FiniteClamp(settings.localStructure, 0.0f, 2.0f, 0.9f);
		settings.skinStructure = FiniteClamp(settings.skinStructure, 0.0f, 2.0f, 0.9f);
		settings.exposureSource = std::min(settings.exposureSource, 2u);
		settings.colorMode = settings.colorMode == 0 || settings.colorMode == 2 || settings.colorMode == 3 ? settings.colorMode : 2;
		settings.detailStrength = FiniteClamp(settings.detailStrength, 0.0f, 2.0f, 1.0f);
		settings.lightingPreservation = FiniteClamp(settings.lightingPreservation, 0.0f, 1.0f, 1.0f);
		settings.exposure = FiniteClamp(settings.exposure, 1.0f / 256.0f, 256.0f, 1.0f);
		for (auto& strength : settings.categoryStrengths)
			strength = FiniteClamp(strength, 0.0f, 1.0f, 1.0f);
		settings.categoryMask &= 0x3eu;
		settings.actorGroupMask &= 0x1fu;
		settings.maximumDistanceMeters = FiniteClamp(settings.maximumDistanceMeters, 0.0f, 30.0f, 10.0f);
		settings.minimumFacePixelSize = std::clamp(settings.minimumFacePixelSize, 1u, 4096u);
		settings.holdFrames = std::min(settings.holdFrames, 30u);
		settings.focusScale = FiniteClamp(settings.focusScale, 0.25f, 1.0f, 0.75f);
		settings.regionPercent = std::clamp(settings.regionPercent, 25u, 100u);
		settings.featherPixels = std::min(settings.featherPixels, 256u);
		settings.horizontalScale = FiniteClamp(settings.horizontalScale, 0.5f, 2.0f, 1.0f);
		for (auto& offset : settings.regionOffset)
			offset = FiniteClamp(offset, -0.5f, 0.5f, 0.0f);
	}

	struct ActorObservation
	{
		std::uintptr_t identity = 0;
		std::uint32_t frame = 0;
		CharacterActorGroup group = CharacterActorGroup::Other;
		CharacterRect rectangle{};
		std::uint32_t viewportWidth = 0, viewportHeight = 0, facePixelSize = 0;
		float distanceMeters = 0.0f;
		bool projectionUncertain = false;
	};
	struct ActorAdmission
	{
		bool selected = false;
		std::uint32_t focusFade = 255;
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		CharacterRect rectangle{};
	};

	/** Requested FG and live DX12 ownership independently exclude NR. */
	[[nodiscard]] constexpr bool AllowsNeuralFrame(bool enabled, bool installed, bool nvidia,
		bool frameGenerationRequested, bool dx12FrameGeneration, bool loading, bool safeLoad,
		bool paused, bool combatPaused) noexcept
	{
		return enabled && installed && nvidia && !frameGenerationRequested && !dx12FrameGeneration &&
		       !loading && !safeLoad && !paused && !combatPaused;
	}

	/** Conservative support of a mono screen-space superellipse, including its feather. */
	[[nodiscard]] inline ComputeSubrect BuildMonoRegion(std::uint32_t width, std::uint32_t height, const MonoSettings& settings) noexcept
	{
		if (!width || !height || width > kMaximumNeuralImageDimension || height > kMaximumNeuralImageDimension)
			return {};
		if (settings.regionPercent == 100)
			return { 0, 0, width, height };
		const float scale = float(settings.regionPercent) * 0.01f;
		const auto axis = [](std::uint32_t extent, float halfSize, float offset, std::uint32_t feather) {
			const double center = (0.5 + offset) * extent;
			const double radius = halfSize * extent + feather;
			return std::array<std::uint32_t, 2>{ static_cast<std::uint32_t>(std::clamp(std::floor(center - radius), 0.0, double(extent))),
				static_cast<std::uint32_t>(std::clamp(std::ceil(center + radius), 0.0, double(extent))) };
		};
		const auto x = axis(width, scale * settings.horizontalScale * 0.5f, settings.regionOffset[0], settings.featherPixels);
		const auto y = axis(height, scale * 0.5f, settings.regionOffset[1], settings.featherPixels);
		return { x[0], y[0], x[1] - x[0], y[1] - y[0] };
	}
}
