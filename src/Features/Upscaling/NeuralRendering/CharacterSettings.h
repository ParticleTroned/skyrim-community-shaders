#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace NeuralRendering
{
	/** Semantic character categories authored by Skyrim lighting materials. */
	enum class CharacterCategory : std::uint32_t
	{
		None = 0,
		Face = 1,
		Skin = 2,
		Hair = 3,
	};

	/** Non-destructive developer visualization shown in the CS diagnostics UI. */
	enum class CharacterDebugView : std::uint32_t
	{
		Off = 0,
		CharacterMask = 1,
		RoiRectangles = 2,
		Dlss5Output = 3,
		Count,
	};

	/** Developer experiments for validating the Community Shaders isolation mask. */
	enum class CharacterMaskTestMode : std::uint32_t
	{
		Authored = 0,
		ForceZero = 1,
		ForceOne = 2,
		ForceHalf = 3,
		InvertAuthored = 4,
		AuthoredWithoutVisibilityDepth = 5,
		Count,
	};

	/** Authoritative outcome after the Feature 18 execution boundary. */
	enum class CharacterFeature18Disposition : std::uint32_t
	{
		Unresolved = 0,
		Evaluated = 1,
		EvaluationFailed = 2,
		EmptyBypass = 3,
		Aborted = 4,
	};

	/** Fail-closed reasons observed while classifying actor-owned geometry. */
	enum class CharacterClassificationRejection : std::uint32_t
	{
		Player = 0,
		BlendedMaterial = 1,
		AlphaTestAndBlend = 2,
		AmbiguousFaceGen = 3,
		UnsupportedMaterial = 4,
		Count,
	};

	namespace CharacterPolicy
	{
		inline constexpr bool kDefaultEnabled = false;
		inline constexpr bool kDefaultFaces = true;
		inline constexpr bool kDefaultSkin = true;
		inline constexpr bool kDefaultHair = false;
		inline constexpr float kDefaultFaceStrength = 1.0f;
		inline constexpr float kDefaultSkinStrength = 1.0f;
		inline constexpr float kDefaultHairStrength = 0.65f;
		inline constexpr float kDefaultMaximumDistanceMeters = 10.0f;
		inline constexpr bool kDefaultAdaptiveRoiSelection = false;
		inline constexpr std::uint32_t kDefaultMinimumFacePixelSize = 64;
		inline constexpr float kDefaultRoiMargin = 0.25f;
		inline constexpr std::uint32_t kDefaultRoiHoldFrames = 3;
		inline constexpr bool kDefaultDepthAwareFeather = false;
		inline constexpr bool kDefaultVisibilityDepthTest = true;
		inline constexpr std::uint32_t kDefaultFeatherRadius = 1;
		inline constexpr float kDefaultFeatherDepthThreshold = 0.002f;

		inline constexpr float kMinimumStrength = 0.0f;
		inline constexpr float kMaximumStrength = 1.0f;
		inline constexpr float kMinimumDistanceMeters = 0.0f;
		inline constexpr float kMaximumDistanceMeters = 30.0f;
		inline constexpr std::uint32_t kMinimumFacePixelSize = 1;
		inline constexpr std::uint32_t kMaximumFacePixelSize = 4096;
		inline constexpr float kMinimumRoiMargin = 0.0f;
		inline constexpr float kMaximumRoiMargin = 1.0f;
		inline constexpr std::uint32_t kMaximumRoiHoldFrames = 30;
		inline constexpr std::uint32_t kMaximumFeatherRadius = 4;
		inline constexpr float kMaximumFeatherDepthThreshold = 0.05f;
		inline constexpr std::uint32_t kMaximumObservationsPerFrame = 4096;
		inline constexpr std::size_t kMaximumEligibilityRegions = 16;
		inline constexpr std::uint32_t kCoverageSampleIntervalFrames = 30;
		inline constexpr std::size_t kPreparedFrameHistorySize = 8;

		[[nodiscard]] constexpr std::uint32_t CategoryBit(
			CharacterCategory a_category) noexcept
		{
			return a_category >= CharacterCategory::Face && a_category <= CharacterCategory::Hair ?
			           1u << static_cast<std::uint32_t>(a_category) :
			           0u;
		}
	}

	struct CharacterSettings
	{
		bool enabled = CharacterPolicy::kDefaultEnabled;
		bool faces = CharacterPolicy::kDefaultFaces;
		bool skin = CharacterPolicy::kDefaultSkin;
		bool hair = CharacterPolicy::kDefaultHair;
		float faceStrength = CharacterPolicy::kDefaultFaceStrength;
		float skinStrength = CharacterPolicy::kDefaultSkinStrength;
		float hairStrength = CharacterPolicy::kDefaultHairStrength;
		float maximumDistanceMeters =
			CharacterPolicy::kDefaultMaximumDistanceMeters;
		bool adaptiveRoiSelection =
			CharacterPolicy::kDefaultAdaptiveRoiSelection;
		/** Experimental independent Feature 18 region handles; deliberately opt-in. */
		bool multiRoi = false;
		std::uint32_t minimumFacePixelSize =
			CharacterPolicy::kDefaultMinimumFacePixelSize;
		float roiMargin = CharacterPolicy::kDefaultRoiMargin;
		std::uint32_t roiHoldFrames = CharacterPolicy::kDefaultRoiHoldFrames;
		bool depthAwareFeather = CharacterPolicy::kDefaultDepthAwareFeather;
		bool visibilityDepthTest = CharacterPolicy::kDefaultVisibilityDepthTest;
		std::uint32_t featherRadius = CharacterPolicy::kDefaultFeatherRadius;
		float featherDepthThreshold =
			CharacterPolicy::kDefaultFeatherDepthThreshold;
		CharacterDebugView debugView = CharacterDebugView::Off;
		CharacterMaskTestMode maskTestMode = CharacterMaskTestMode::Authored;

		bool operator==(const CharacterSettings&) const = default;
	};

	/** Clamps persisted and runtime controls before resource or dispatch decisions. */
	inline void SanitizeCharacterSettings(CharacterSettings& a_settings) noexcept
	{
		const auto finiteClamp = [](float value, float fallback, float minimum, float maximum) {
			return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
		};
		a_settings.faceStrength = finiteClamp(a_settings.faceStrength, CharacterPolicy::kDefaultFaceStrength,
			CharacterPolicy::kMinimumStrength, CharacterPolicy::kMaximumStrength);
		a_settings.skinStrength = finiteClamp(a_settings.skinStrength, CharacterPolicy::kDefaultSkinStrength,
			CharacterPolicy::kMinimumStrength, CharacterPolicy::kMaximumStrength);
		a_settings.hairStrength = finiteClamp(a_settings.hairStrength, CharacterPolicy::kDefaultHairStrength,
			CharacterPolicy::kMinimumStrength, CharacterPolicy::kMaximumStrength);
		a_settings.maximumDistanceMeters = finiteClamp(a_settings.maximumDistanceMeters,
			CharacterPolicy::kDefaultMaximumDistanceMeters, CharacterPolicy::kMinimumDistanceMeters,
			CharacterPolicy::kMaximumDistanceMeters);
		a_settings.minimumFacePixelSize = std::clamp(a_settings.minimumFacePixelSize,
			CharacterPolicy::kMinimumFacePixelSize, CharacterPolicy::kMaximumFacePixelSize);
		a_settings.roiMargin = finiteClamp(a_settings.roiMargin, CharacterPolicy::kDefaultRoiMargin,
			CharacterPolicy::kMinimumRoiMargin, CharacterPolicy::kMaximumRoiMargin);
		a_settings.roiHoldFrames = std::min(a_settings.roiHoldFrames, CharacterPolicy::kMaximumRoiHoldFrames);
		a_settings.featherRadius = std::min(a_settings.featherRadius, CharacterPolicy::kMaximumFeatherRadius);
		a_settings.featherDepthThreshold = finiteClamp(a_settings.featherDepthThreshold,
			CharacterPolicy::kDefaultFeatherDepthThreshold, 0.0f, CharacterPolicy::kMaximumFeatherDepthThreshold);
		if (a_settings.debugView >= CharacterDebugView::Count)
			a_settings.debugView = CharacterDebugView::Off;
		if (a_settings.maskTestMode >= CharacterMaskTestMode::Count)
			a_settings.maskTestMode = CharacterMaskTestMode::Authored;
	}

	[[nodiscard]] inline bool IsValidCharacterSettings(const CharacterSettings& a_settings) noexcept
	{
		auto sanitized = a_settings;
		SanitizeCharacterSettings(sanitized);
		return sanitized == a_settings;
	}

	[[nodiscard]] constexpr bool IsCharacterCategoryEnabled(
		CharacterCategory a_category, const CharacterSettings& a_settings) noexcept
	{
		switch (a_category) {
		case CharacterCategory::Face:
			return a_settings.faces && a_settings.faceStrength > 0.0f;
		case CharacterCategory::Skin:
			return a_settings.skin && a_settings.skinStrength > 0.0f;
		case CharacterCategory::Hair:
			return a_settings.hair && a_settings.hairStrength > 0.0f;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr std::uint32_t GetEnabledCharacterCategoryMask(
		const CharacterSettings& a_settings) noexcept
	{
		std::uint32_t result = 0;
		for (auto category : { CharacterCategory::Face, CharacterCategory::Skin, CharacterCategory::Hair }) {
			if (IsCharacterCategoryEnabled(category, a_settings))
				result |= CharacterPolicy::CategoryBit(category);
		}
		return result;
	}

}
