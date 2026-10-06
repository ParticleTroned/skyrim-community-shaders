#pragma once

#include "DevelopmentDiagnostics.h"

#include "CharacterActorGroups.h"
#include "CharacterCropPolicy.h"
#include "Features/FoveatedCommon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	/** Semantic character categories authored by Skyrim lighting materials. */
	enum class CharacterCategory : std::uint32_t
	{
		None = 0,
		Face = 1,
		Skin = 2,
		Hair = 3,
		Armor = 4,
		Weapons = 5,
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
		inline constexpr std::array kCategories{ CharacterCategory::Face, CharacterCategory::Skin,
			CharacterCategory::Hair, CharacterCategory::Armor, CharacterCategory::Weapons };
		inline constexpr std::size_t kCategoryCount = kCategories.size();
		inline constexpr std::uint32_t kAllCategoryMask = 0x3Eu;
		inline constexpr bool kDefaultEnabled = false;
		inline constexpr bool kDefaultVisualIsolation = true;
		inline constexpr bool kDefaultFaces = true;
		inline constexpr bool kDefaultSkin = true;
		inline constexpr bool kDefaultHair = true;
		inline constexpr bool kDefaultArmor = false;
		inline constexpr bool kDefaultWeapons = false;
		inline constexpr float kDefaultFaceStrength = 1.0f;
		inline constexpr float kDefaultSkinStrength = 1.0f;
		inline constexpr float kDefaultHairStrength = 0.65f;
		inline constexpr float kDefaultArmorStrength = 1.0f;
		inline constexpr float kDefaultWeaponsStrength = 1.0f;
		inline constexpr float kDefaultMaximumDistanceMeters = 10.0f;
		inline constexpr bool kDefaultAdaptiveRoiSelection = false;
		inline constexpr float kDefaultFocusScale = 0.75f;
		inline constexpr std::uint32_t kDefaultMinimumFacePixelSize = 64;
		inline constexpr auto kDefaultCropMode = static_cast<std::uint32_t>(CharacterCropMode::Cropped);
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
			return a_category >= CharacterCategory::Face && a_category <= CharacterCategory::Weapons ?
			           1u << static_cast<std::uint32_t>(a_category) :
			           0u;
		}
	}

	struct CharacterSettings
	{
		/** Restrict coverage to actors; otherwise scene adjustments are opt-in. */
		bool enabled = CharacterPolicy::kDefaultEnabled;
		bool sceneStrengthsEnabled = false;
		bool providerBlending = false;
		bool humans = true;
		bool otherHumanoids = true;
		bool creatures = true;
		bool animals = true;
		bool otherActors = true;
		bool faces = CharacterPolicy::kDefaultFaces;
		bool skin = CharacterPolicy::kDefaultSkin;
		bool hair = CharacterPolicy::kDefaultHair;
		bool armor = CharacterPolicy::kDefaultArmor;
		bool weapons = CharacterPolicy::kDefaultWeapons;
		float faceStrength = CharacterPolicy::kDefaultFaceStrength;
		float skinStrength = CharacterPolicy::kDefaultSkinStrength;
		float hairStrength = CharacterPolicy::kDefaultHairStrength;
		float armorStrength = CharacterPolicy::kDefaultArmorStrength;
		float weaponsStrength = CharacterPolicy::kDefaultWeaponsStrength;
		float maximumDistanceMeters =
			CharacterPolicy::kDefaultMaximumDistanceMeters;
		float focusScale = CharacterPolicy::kDefaultFocusScale;
		bool adaptiveRoiSelection =
			CharacterPolicy::kDefaultAdaptiveRoiSelection;
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Use current spatial context only in the single-region pre-DLSS experiment. */
		bool experimentalCurrentContext = false;
		/** Select the reference mask dispatcher for same-process qualification. */
		bool experimentalGpuMaskSupport = false;
#endif
		std::uint32_t minimumFacePixelSize =
			CharacterPolicy::kDefaultMinimumFacePixelSize;
		std::uint32_t cropMode = CharacterPolicy::kDefaultCropMode;
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

	/** Actor-only coverage takes precedence over ordinary-scene adjustments. */
	[[nodiscard]] constexpr bool UsesSceneCharacterStrengths(const CharacterSettings& settings) noexcept
	{
		return settings.sceneStrengthsEnabled && !settings.enabled;
	}

	/** Both coverage scopes consume the same source-frame material mask. */
	[[nodiscard]] constexpr bool IsCharacterMaskActive(const CharacterSettings& settings) noexcept
	{
		return settings.enabled || settings.sceneStrengthsEnabled;
	}

	/** Resolve coverage scope without changing saved actor-only preferences. */
	[[nodiscard]] constexpr CharacterSettings ResolveCharacterScopeSettings(CharacterSettings settings) noexcept
	{
		if (settings.enabled)
			settings.sceneStrengthsEnabled = false;
		if (UsesSceneCharacterStrengths(settings)) {
			settings.maximumDistanceMeters = 0.0f;
			settings.focusScale = 1.0f;
			settings.minimumFacePixelSize = 1;
			settings.adaptiveRoiSelection = false;
			settings.cropMode = static_cast<std::uint32_t>(CharacterCropMode::Uncropped);
			settings.roiMargin = 0.0f;
			settings.roiHoldFrames = 0;
			settings.depthAwareFeather = false;
			settings.featherRadius = 0;
			settings.featherDepthThreshold = 0.0f;
			settings.visibilityDepthTest = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
			settings.experimentalCurrentContext = false;
			settings.experimentalGpuMaskSupport = false;
#endif
		}
		return settings;
	}

	[[nodiscard]] constexpr CharacterDebugView ClampCharacterDebugView(
		std::uint32_t a_value) noexcept
	{
		const auto value = static_cast<CharacterDebugView>(a_value);
		return value < CharacterDebugView::Count ? value : CharacterDebugView::Off;
	}

	[[nodiscard]] constexpr CharacterMaskTestMode ClampCharacterMaskTestMode(
		std::uint32_t a_value) noexcept
	{
		const auto value = static_cast<CharacterMaskTestMode>(a_value);
		return value < CharacterMaskTestMode::Count ?
		           value :
		           CharacterMaskTestMode::Authored;
	}

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
		a_settings.armorStrength = finiteClamp(a_settings.armorStrength, CharacterPolicy::kDefaultArmorStrength,
			CharacterPolicy::kMinimumStrength, CharacterPolicy::kMaximumStrength);
		a_settings.weaponsStrength = finiteClamp(a_settings.weaponsStrength, CharacterPolicy::kDefaultWeaponsStrength,
			CharacterPolicy::kMinimumStrength, CharacterPolicy::kMaximumStrength);
		a_settings.maximumDistanceMeters = finiteClamp(a_settings.maximumDistanceMeters,
			CharacterPolicy::kDefaultMaximumDistanceMeters, CharacterPolicy::kMinimumDistanceMeters,
			CharacterPolicy::kMaximumDistanceMeters);
		a_settings.focusScale = finiteClamp(a_settings.focusScale, CharacterPolicy::kDefaultFocusScale,
			FoveatedCommon::kCenterScaleMin, FoveatedCommon::kCenterScaleMax);
		a_settings.minimumFacePixelSize = std::clamp(a_settings.minimumFacePixelSize,
			CharacterPolicy::kMinimumFacePixelSize, CharacterPolicy::kMaximumFacePixelSize);
		if (a_settings.cropMode >= static_cast<std::uint32_t>(CharacterCropMode::Count))
			a_settings.cropMode = CharacterPolicy::kDefaultCropMode;
		a_settings.roiMargin = finiteClamp(a_settings.roiMargin, CharacterPolicy::kDefaultRoiMargin,
			CharacterPolicy::kMinimumRoiMargin, CharacterPolicy::kMaximumRoiMargin);
		a_settings.roiHoldFrames = std::min(a_settings.roiHoldFrames, CharacterPolicy::kMaximumRoiHoldFrames);
		a_settings.featherRadius = std::min(a_settings.featherRadius, CharacterPolicy::kMaximumFeatherRadius);
		a_settings.featherDepthThreshold = finiteClamp(a_settings.featherDepthThreshold,
			CharacterPolicy::kDefaultFeatherDepthThreshold, 0.0f, CharacterPolicy::kMaximumFeatherDepthThreshold);
		if constexpr (!kDevelopmentDiagnostics) {
			a_settings.debugView = CharacterDebugView::Off;
			a_settings.maskTestMode = CharacterMaskTestMode::Authored;
			a_settings.visibilityDepthTest = true;
			if (a_settings.cropMode == static_cast<std::uint32_t>(CharacterCropMode::Automatic))
				a_settings.cropMode = CharacterPolicy::kDefaultCropMode;
		}
		a_settings.debugView = ClampCharacterDebugView(static_cast<std::uint32_t>(a_settings.debugView));
		a_settings.maskTestMode = ClampCharacterMaskTestMode(static_cast<std::uint32_t>(a_settings.maskTestMode));
	}

	[[nodiscard]] inline bool IsValidCharacterSettings(const CharacterSettings& a_settings) noexcept
	{
		auto sanitized = a_settings;
		SanitizeCharacterSettings(sanitized);
		return sanitized == a_settings;
	}

	/** Actor selection precedes the shared material selection. */
	[[nodiscard]] constexpr bool IsCharacterActorGroupEnabled(
		CharacterActorGroup group, const CharacterSettings& settings) noexcept
	{
		switch (group) {
		case CharacterActorGroup::Humans:
			return settings.humans;
		case CharacterActorGroup::OtherHumanoids:
			return settings.otherHumanoids;
		case CharacterActorGroup::Creatures:
			return settings.creatures;
		case CharacterActorGroup::Animals:
			return settings.animals;
		case CharacterActorGroup::Other:
			return settings.otherActors;
		default:
			return false;
		}
	}

	namespace CharacterPolicy
	{
		struct CategoryControl
		{
			bool CharacterSettings::* selected;
			float CharacterSettings::* strength;
		};
		inline constexpr std::array<CategoryControl, kCategoryCount> kCategoryControls{ {
			{ &CharacterSettings::faces, &CharacterSettings::faceStrength },
			{ &CharacterSettings::skin, &CharacterSettings::skinStrength },
			{ &CharacterSettings::hair, &CharacterSettings::hairStrength },
			{ &CharacterSettings::armor, &CharacterSettings::armorStrength },
			{ &CharacterSettings::weapons, &CharacterSettings::weaponsStrength },
		} };
	}

	/** Selection remains meaningful at zero strength in ordinary-scene NR. */
	[[nodiscard]] constexpr bool IsCharacterCategorySelected(
		CharacterCategory category, const CharacterSettings& settings) noexcept
	{
		return CharacterPolicy::CategoryBit(category) != 0 &&
		       settings.*CharacterPolicy::kCategoryControls[static_cast<std::size_t>(category) - 1].selected;
	}

	/** Resolves the shared selection strength for eligibility and GPU masking. */
	[[nodiscard]] constexpr float GetCharacterCategoryStrength(
		CharacterCategory a_category, const CharacterSettings& a_settings) noexcept
	{
		return IsCharacterCategorySelected(a_category, a_settings) ?
		           a_settings.*CharacterPolicy::kCategoryControls[static_cast<std::size_t>(a_category) - 1].strength :
		           0.0f;
	}

	/** Uses the authored category order for mask constants and diagnostics. */
	[[nodiscard]] constexpr std::array<float, CharacterPolicy::kCategoryCount> GetCharacterCategoryStrengths(
		const CharacterSettings& a_settings) noexcept
	{
		std::array<float, CharacterPolicy::kCategoryCount> result{};
		for (std::size_t index = 0; index < result.size(); ++index)
			result[index] = GetCharacterCategoryStrength(CharacterPolicy::kCategories[index], a_settings);
		return result;
	}

	[[nodiscard]] constexpr bool IsCharacterCategoryEnabled(
		CharacterCategory a_category, const CharacterSettings& a_settings) noexcept
	{
		return UsesSceneCharacterStrengths(a_settings) ? IsCharacterCategorySelected(a_category, a_settings) :
		                                                 GetCharacterCategoryStrength(a_category, a_settings) > 0.0f;
	}

	/** Scene masks subtract selected edits from full NR; actor masks add them. */
	[[nodiscard]] constexpr std::array<float, CharacterPolicy::kCategoryCount> GetCharacterMaskStrengths(
		const CharacterSettings& settings) noexcept
	{
		auto strengths = GetCharacterCategoryStrengths(settings);
		if (UsesSceneCharacterStrengths(settings)) {
			for (std::size_t index = 0; index < strengths.size(); ++index)
				strengths[index] = IsCharacterCategoryEnabled(CharacterPolicy::kCategories[index], settings) ?
				                       1.0f - strengths[index] :
				                       0.0f;
		}
		return strengths;
	}

	[[nodiscard]] constexpr std::uint32_t GetEnabledCharacterCategoryMask(
		const CharacterSettings& a_settings) noexcept
	{
		std::uint32_t result = 0;
		for (auto category : CharacterPolicy::kCategories) {
			if (IsCharacterCategoryEnabled(category, a_settings))
				result |= CharacterPolicy::CategoryBit(category);
		}
		return result;
	}

	/** Freezes the complete character policy for every consumer of one source frame. */
	class CharacterCategoryFramePolicy
	{
	public:
		[[nodiscard]] CharacterSettings Resolve(std::uint32_t a_frame, CharacterSettings a_requested) noexcept
		{
			if (a_frame == std::numeric_limits<std::uint32_t>::max())
				return a_requested;
			const Entry* selected = nullptr;
			for (const auto& entry : entries_)
				if (entry.frame == a_frame)
					selected = &entry;
			if (!selected) {
				auto& entry = entries_[next_];
				entry = { a_frame, a_requested };
				selected = &entry;
				next_ = (next_ + 1) % entries_.size();
			}
			return selected->settings;
		}

	private:
		struct Entry
		{
			std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
			CharacterSettings settings{};
		};
		std::array<Entry, CharacterPolicy::kPreparedFrameHistorySize> entries_{};
		std::size_t next_ = 0;
	};
}
