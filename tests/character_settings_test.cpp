#include "Features/Upscaling/NeuralRendering/CharacterSettingsJson.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace NeuralRendering;
	using nlohmann::json;

	// Match the production flat settings interface without a game/D3D dependency.
	struct FlatSettings
	{
		bool neuralRenderingEnabled = false;
		bool neuralRenderingBatchedStereo = true;
		float foveatedRightEyeMaskOffsetX = 0.25f;
		bool neuralCharacterSceneStrengthsEnabled = false;
		bool neuralCharacterRenderingEnabled = CharacterPolicy::kDefaultEnabled;
		bool neuralCharacterVisualIsolationEnabled = CharacterPolicy::kDefaultVisualIsolation;
		bool neuralCharacterFacesEnabled = CharacterPolicy::kDefaultFaces;
		bool neuralCharacterSkinEnabled = CharacterPolicy::kDefaultSkin;
		bool neuralCharacterHairEnabled = CharacterPolicy::kDefaultHair;
		bool neuralCharacterHumansEnabled = true;
		bool neuralCharacterOtherHumanoidsEnabled = true;
		bool neuralCharacterCreaturesEnabled = true;
		bool neuralCharacterAnimalsEnabled = true;
		bool neuralCharacterOtherActorsEnabled = true;
		bool neuralCharacterProviderBlending = false;
		bool neuralCharacterArmorEnabled = false;
		bool neuralCharacterWeaponsEnabled = false;
		float neuralCharacterFaceStrength = CharacterPolicy::kDefaultFaceStrength;
		float neuralCharacterSkinStrength = CharacterPolicy::kDefaultSkinStrength;
		float neuralCharacterHairStrength = CharacterPolicy::kDefaultHairStrength;
		float neuralCharacterArmorStrength = CharacterPolicy::kDefaultArmorStrength;
		float neuralCharacterWeaponsStrength = CharacterPolicy::kDefaultWeaponsStrength;
		float neuralCharacterMaximumDistanceMeters = CharacterPolicy::kDefaultMaximumDistanceMeters;
		float neuralCharacterFocusScale = NeuralRendering::CharacterPolicy::kDefaultFocusScale;
		bool neuralCharacterAdaptiveRoiSelectionEnabled = CharacterPolicy::kDefaultAdaptiveRoiSelection;
		std::uint32_t neuralCharacterMinimumFacePixelSize = CharacterPolicy::kDefaultMinimumFacePixelSize;

		std::uint32_t neuralCharacterCropMode = CharacterPolicy::kDefaultCropMode;
		float neuralCharacterRoiMargin = CharacterPolicy::kDefaultRoiMargin;
		std::uint32_t neuralCharacterRoiHoldFrames = CharacterPolicy::kDefaultRoiHoldFrames;
		bool neuralCharacterDepthAwareFeatherEnabled = CharacterPolicy::kDefaultDepthAwareFeather;
		bool neuralCharacterVisibilityDepthTestEnabled = CharacterPolicy::kDefaultVisibilityDepthTest;
		std::uint32_t neuralCharacterFeatherRadius = CharacterPolicy::kDefaultFeatherRadius;
		float neuralCharacterDepthThreshold = CharacterPolicy::kDefaultFeatherDepthThreshold;
		std::uint32_t neuralCharacterDebugView = static_cast<std::uint32_t>(CharacterDebugView::Off);
		std::uint32_t neuralCharacterMaskTestMode = static_cast<std::uint32_t>(CharacterMaskTestMode::Authored);

		bool operator==(const FlatSettings&) const = default;
	};

	void Require(bool condition, const char* description)
	{
		if (!condition)
			throw std::runtime_error(description);
	}

	FlatSettings Read(const json& input)
	{
		FlatSettings settings{};
		ReadUpscalingCharacterSettingsJson(input, settings);
		return settings;
	}

	void JsonRoundTripAndDefaults()
	{
		Require(Read(json::object()).neuralCharacterCropMode == static_cast<std::uint32_t>(CharacterCropMode::Cropped),
			"Missing crop settings must use the stable crop without a calibration profile");
		Require(Read(json{ { "neuralCharacterCropMode", 999 } }).neuralCharacterCropMode == CharacterPolicy::kDefaultCropMode,
			"An invalid crop mode must recover to the canonical default");
		Require(!Read(json::object()).neuralCharacterSceneStrengthsEnabled, "Legacy scenes must not enable category adjustments");
		Require(!Read(json::object()).neuralCharacterProviderBlending, "Missing strength application keeps the CSX compositor");
		const auto legacy = Read(json{ { "neuralCharacterArmorEnabled", true }, { "neuralCharacterWeaponsEnabled", true } });
		Require(legacy.neuralCharacterArmorStrength == 1.0f && legacy.neuralCharacterWeaponsStrength == 1.0f,
			"Legacy selected armour and weapons must retain their full strength");
		const auto persisted = json::parse(R"({
			"neuralCharacterRenderingEnabled": true,
			"neuralCharacterSceneStrengthsEnabled": true,
			"neuralCharacterHumansEnabled": true, "neuralCharacterOtherHumanoidsEnabled": false,
			"neuralCharacterCreaturesEnabled": true, "neuralCharacterAnimalsEnabled": false,
			"neuralCharacterOtherActorsEnabled": false,
			"neuralCharacterProviderBlending": true,
			"neuralCharacterVisualIsolationEnabled": false,
			"neuralCharacterFacesEnabled": false, "neuralCharacterSkinEnabled": false,
			"neuralCharacterHairEnabled": true, "neuralCharacterArmorEnabled": true,
			"neuralCharacterWeaponsEnabled": true, "neuralCharacterFaceStrength": 0.25,
			"neuralCharacterSkinStrength": 0.5, "neuralCharacterHairStrength": 0.75,
			"neuralCharacterArmorStrength": 0.375, "neuralCharacterWeaponsStrength": 0.875,
			"neuralCharacterMaximumDistanceMeters": 15.5,
			"neuralCharacterAdaptiveRoiSelectionEnabled": true,
            "neuralCharacterFocusScale": 0.625,
			"neuralCharacterMinimumFacePixelSize": 128,
			"neuralCharacterCropMode": 1, "neuralCharacterRoiMargin": 0.5, "neuralCharacterRoiHoldFrames": 12,
			"neuralCharacterDepthAwareFeatherEnabled": true,
			"neuralCharacterVisibilityDepthTestEnabled": false,
			"neuralCharacterFeatherRadius": 4, "neuralCharacterDepthThreshold": 0.03125
		})");
		const auto settings = Read(persisted);
		json saved = json::object();
		WriteUpscalingCharacterSettingsJson(saved, settings);
		Require(saved == persisted, "Character settings changed during serialization");
		Require(Read(json::parse(saved.dump())) == settings,
			"Character settings changed after persisted text round trip");
		Require(Read(json::object()) == FlatSettings{}, "Empty character object must use defaults");
		Require(Read(json::object()).neuralCharacterHairEnabled,
			"Missing hair selection must inherit the enabled default");
		const auto hairOff = Read(json{ { "neuralCharacterHairEnabled", false } });
		Require(!hairOff.neuralCharacterHairEnabled && hairOff.neuralCharacterHairStrength == 0.65f,
			"Saved hair-off preference and existing default strength must remain intact");
		json hairOffSaved;
		WriteUpscalingCharacterSettingsJson(hairOffSaved, hairOff);
		Require(Read(hairOffSaved) == hairOff, "Saved hair-off preference must survive a round trip");
		auto expected = FlatSettings{};
		expected.neuralCharacterRenderingEnabled = true;
		expected.neuralCharacterMinimumFacePixelSize = 200;
		const auto partial = json::parse(R"({"neuralCharacterRenderingEnabled":true,
			"neuralCharacterMinimumFacePixelSize":200,"futureUnknownSetting":[null,"ignored"]})");
		Require(Read(partial) == expected, "Partial character object must use defaults and ignore unknown keys");
		auto destination = settings;
		ReadUpscalingCharacterSettingsJson(partial, destination);
		Require(destination == expected, "Partial read must reset missing persisted fields to defaults");
		saved["unrelatedField"] = 42;
		WriteUpscalingCharacterSettingsJson(saved, settings);
		Require(saved["unrelatedField"] == 42, "Character serialization removed another feature's setting");
	}

	void JsonIntegerBounds()
	{
		for (const auto wide : std::array<json, 3>{
				 json::parse("4294967297"), json::parse("18446744073709551615"),
				 std::numeric_limits<std::int64_t>::max() }) {
			const auto settings = GetUpscalingCharacterSettings(Read(json{
				{ "neuralCharacterMinimumFacePixelSize", wide },
				{ "neuralCharacterRoiHoldFrames", wide }, { "neuralCharacterFeatherRadius", wide } }));
			Require(settings.minimumFacePixelSize == CharacterPolicy::kMaximumFacePixelSize &&
						settings.roiHoldFrames == CharacterPolicy::kMaximumRoiHoldFrames &&
						settings.featherRadius == CharacterPolicy::kMaximumFeatherRadius,
				"Wide character counts wrapped before policy clamping");
		}
		for (const auto negative : { json::parse("-1"), json::parse("-9223372036854775808") }) {
			const auto settings = GetUpscalingCharacterSettings(Read(json{
				{ "neuralCharacterMinimumFacePixelSize", negative },
				{ "neuralCharacterRoiHoldFrames", negative }, { "neuralCharacterFeatherRadius", negative } }));
			Require(settings.minimumFacePixelSize == CharacterPolicy::kMinimumFacePixelSize &&
						settings.roiHoldFrames == 0 && settings.featherRadius == 0,
				"Negative character counts wrapped to their maximums");
		}
	}

	void JsonTypeErrorsAreTransactional()
	{
		FlatSettings original{};
		original.neuralCharacterHairEnabled = false;
		original.neuralCharacterRoiHoldFrames = 11;
		const auto rejects = [&](const json& input) {
			auto destination = original;
			bool rejected = false;
			try {
				ReadUpscalingCharacterSettingsJson(input, destination);
			} catch (const json::exception&) {
				rejected = true;
			}
			Require(rejected, "Wrong character JSON type must be rejected");
			Require(destination == original, "Rejected character JSON partially changed settings");
		};
		for (const char* key : { "neuralCharacterCropMode", "neuralCharacterMinimumFacePixelSize", "neuralCharacterRoiHoldFrames", "neuralCharacterFeatherRadius" }) {
			for (const auto& wrong : std::array<json, 8>{ 1.5, 1.0, true, "1", nullptr,
					 json::array(), json::object(), json::parse("18446744073709551616") })
				rejects(json{ { "neuralCharacterRenderingEnabled", true }, { key, wrong } });
		}
		rejects(json{ { "neuralCharacterRenderingEnabled", 1 } });
		rejects(json{ { "neuralCharacterSceneStrengthsEnabled", 1 } });
		for (const char* key : { "neuralCharacterHairEnabled", "neuralCharacterArmorEnabled", "neuralCharacterWeaponsEnabled",
				 "neuralCharacterHumansEnabled", "neuralCharacterOtherHumanoidsEnabled", "neuralCharacterCreaturesEnabled", "neuralCharacterAnimalsEnabled", "neuralCharacterOtherActorsEnabled", "neuralCharacterProviderBlending" })
			for (const auto& wrong : std::array<json, 5>{ 0, 1, "false", nullptr, json::array() })
				rejects(json{ { "neuralCharacterRenderingEnabled", true }, { key, wrong } });
		rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterFaceStrength", "0.5" } });
		rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterHairStrength", false } });
		for (const char* key : { "neuralCharacterArmorStrength", "neuralCharacterWeaponsStrength" })
			for (const auto& wrong : std::array<json, 5>{ true, "0.5", nullptr, json::array(), json::object() })
				rejects(json{ { "neuralCharacterRenderingEnabled", true }, { key, wrong } });
		rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterVisualIsolationEnabled", 0 } });
		for (const auto& wrong : std::array<json, 5>{ nullptr, 1, true, "settings", json::array() })
			rejects(wrong);
	}

	void JsonFloatBounds()
	{
		const auto extreme = [](const json& value) {
			return GetUpscalingCharacterSettings(Read(json{
				{ "neuralCharacterFaceStrength", value }, { "neuralCharacterSkinStrength", value },
				{ "neuralCharacterHairStrength", value }, { "neuralCharacterMaximumDistanceMeters", value },
				{ "neuralCharacterArmorStrength", value }, { "neuralCharacterWeaponsStrength", value },
				{ "neuralCharacterRoiMargin", value }, { "neuralCharacterDepthThreshold", value } }));
		};
		const auto positive = extreme(json::parse("1.0e300"));
		Require(positive.faceStrength == CharacterPolicy::kMaximumStrength &&
					positive.skinStrength == CharacterPolicy::kMaximumStrength &&
					positive.hairStrength == CharacterPolicy::kMaximumStrength &&
					positive.armorStrength == CharacterPolicy::kMaximumStrength &&
					positive.weaponsStrength == CharacterPolicy::kMaximumStrength &&
					positive.maximumDistanceMeters == CharacterPolicy::kMaximumDistanceMeters &&
					positive.roiMargin == CharacterPolicy::kMaximumRoiMargin &&
					positive.featherDepthThreshold == CharacterPolicy::kMaximumFeatherDepthThreshold,
			"Large finite positive character values must clamp upward before float narrowing");
		const auto negative = extreme(json::parse("-1.0e300"));
		Require(negative.faceStrength == CharacterPolicy::kMinimumStrength &&
					negative.skinStrength == CharacterPolicy::kMinimumStrength &&
					negative.hairStrength == CharacterPolicy::kMinimumStrength &&
					negative.armorStrength == CharacterPolicy::kMinimumStrength &&
					negative.weaponsStrength == CharacterPolicy::kMinimumStrength &&
					negative.maximumDistanceMeters == CharacterPolicy::kMinimumDistanceMeters &&
					negative.roiMargin == CharacterPolicy::kMinimumRoiMargin &&
					negative.featherDepthThreshold == 0.0f,
			"Large finite negative character values must clamp downward before float narrowing");
		for (const auto nonfinite : { std::numeric_limits<double>::quiet_NaN(),
				 std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
			Require(extreme(nonfinite) == CharacterSettings{},
				"Nonfinite character values must retain sanitizer defaults");
	}

	void VrSettingsMapping()
	{
		FlatSettings settings{};
		settings.neuralCharacterRenderingEnabled = true;
		settings.neuralCharacterVisualIsolationEnabled = false;
		settings.neuralCharacterDebugView = static_cast<std::uint32_t>(CharacterDebugView::RoiRectangles);
		settings.neuralCharacterMaskTestMode = static_cast<std::uint32_t>(CharacterMaskTestMode::ForceHalf);
		const auto original = settings;
		const auto policy = GetUpscalingCharacterSettings(settings);
		Require(policy.enabled && !settings.neuralRenderingEnabled,
			"Flat policy mapping must leave master rendering gating to the caller");
		ApplyUpscalingCharacterSettings(settings, policy);
		Require(settings == original, "Policy copy changed VR isolation, stereo or transient settings");
		json saved = json::object();
		WriteUpscalingCharacterSettingsJson(saved, settings);
		Require(!saved.contains("neuralCharacterDebugView") && !saved.contains("neuralCharacterMaskTestMode"),
			"Developer modes must remain absent from persisted VR settings");
		// VR ignores these persisted keys, including old or malformed mode values.
		for (const auto& ignored : std::array<json, 6>{ -1, 1.5, "invalid", nullptr,
				 std::numeric_limits<std::uint64_t>::max(), json::object() }) {
			saved["neuralCharacterDebugView"] = ignored;
			saved["neuralCharacterMaskTestMode"] = ignored;
			ReadUpscalingCharacterSettingsJson(saved, settings);
			Require(settings == original, "Loading ignored developer keys changed VR settings");
		}
	}

	void FrameCategoryTransitions()
	{
		CharacterCategoryFramePolicy policy;
		CharacterSettings face{};
		face.enabled = true;
		face.skin = face.hair = false;
		Require(GetEnabledCharacterCategoryMask(policy.Resolve(100, face)) == 2,
			"Source authoring must latch the initial face-only selection");
		auto expanded = face;
		expanded.skin = expanded.hair = true;
		expanded.faceStrength = 0.25f;
		expanded.debugView = CharacterDebugView::RoiRectangles;
		const auto peer = policy.Resolve(100, expanded);
		Require(GetEnabledCharacterCategoryMask(peer) == 2 && peer.faceStrength == 1.0f,
			"Capture and both eyes must retain the same frame's selection and strength");
		Require(peer == face,
			"All mask and planning policy must remain immutable for a retained source");
		expanded.maximumDistanceMeters = 30.0f;
		expanded.depthAwareFeather = true;
		expanded.featherRadius = 4;
		expanded.visibilityDepthTest = false;
		expanded.maskTestMode = CharacterMaskTestMode::ForceZero;
		expanded.roiMargin = 0.5f;
		expanded.roiHoldFrames = 12;
		expanded.minimumFacePixelSize = 100;
		expanded.adaptiveRoiSelection = true;
		Require(policy.Resolve(100, expanded) == face,
			"Producer policy edits between eyes must not change any prepared field");
		Require(policy.Resolve(101, expanded) == expanded,
			"Expanded categories must take effect on the next source frame");
		Require(GetEnabledCharacterCategoryMask(policy.Resolve(100, expanded)) == 2,
			"Retained source must not borrow a newer frame's expanded categories");
		auto zero = expanded;
		zero.faceStrength = zero.skinStrength = zero.hairStrength = 0.0f;
		Require(policy.Resolve(101, zero).skinStrength == expanded.skinStrength,
			"Zero-strength edits must not split the stereo selection policy");
		Require(GetEnabledCharacterCategoryMask(policy.Resolve(102, zero)) == 0,
			"Next-frame zero strength must produce empty selection");
		policy = {};
		Require(policy.Resolve(100, expanded) == expanded,
			"A resource reset must begin a new category policy lifetime");
		Require(policy.Resolve(UINT32_MAX, zero) == zero,
			"Invalid source frames must not allocate a reusable policy entry");
	}

	void EquipmentSettings()
	{
		CharacterSettings scene;
		Require(!IsCharacterMaskActive(scene), "Ordinary scene defaults must avoid mask work");
		scene.sceneStrengthsEnabled = true;
		scene.faceStrength = 0.0f;
		Require(IsCharacterMaskActive(scene) && UsesSceneCharacterStrengths(scene) &&
					IsCharacterCategoryEnabled(CharacterCategory::Face, scene) && GetCharacterMaskStrengths(scene)[0] == 1.0f,
			"Zero strength must still detect faces to restore their original appearance");
		scene.enabled = true;
		Require(!UsesSceneCharacterStrengths(scene) && !IsCharacterCategoryEnabled(CharacterCategory::Face, scene),
			"Actor-only coverage must take precedence while sharing material strengths");
		CharacterSettings settings{};
		Require(!settings.armor && !settings.weapons, "Equipment must be opt-in");
		settings.faces = settings.skin = settings.hair = false;
		settings.armor = true;
		Require(GetEnabledCharacterCategoryMask(settings) == 16u, "Armour-only mask");
		settings.weapons = true;
		Require(GetEnabledCharacterCategoryMask(settings) == 48u, "Independent weapon mask");
		settings.armor = false;
		Require(GetEnabledCharacterCategoryMask(settings) == 32u, "Weapon-only mask");
		CharacterCategoryFramePolicy policy;
		Require(policy.Resolve(1, settings).weapons, "Weapon policy latch");
		settings.weapons = false;
		Require(policy.Resolve(1, settings).weapons && !policy.Resolve(2, settings).weapons, "Equipment edits must latch by source frame");
	}

	void PolicyHelpers()
	{
		static_assert(CharacterPolicy::CategoryBit(CharacterCategory::None) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(6)) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(32)) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(0xFFFFFFFFu)) == 0);
		CharacterSettings settings{};
		Require(IsValidCharacterSettings(settings), "Default character policy must be valid");
		Require(GetEnabledCharacterCategoryMask(settings) == 14u, "Default mask must include face, skin and hair");
		settings.skinStrength = 0.0f;
		Require(GetEnabledCharacterCategoryMask(settings) == 10u, "Category mask must respect strengths and switches");
		Require(!IsCharacterCategoryEnabled(static_cast<CharacterCategory>(6), settings), "Unknown category must remain disabled");
		settings.faceStrength = std::numeric_limits<float>::quiet_NaN();
		settings.maximumDistanceMeters = std::numeric_limits<float>::infinity();
		settings.minimumFacePixelSize = 0;
		settings.roiHoldFrames = std::numeric_limits<std::uint32_t>::max();
		settings.featherRadius = std::numeric_limits<std::uint32_t>::max();
		settings.roiMargin = -1.0f;
		settings.featherDepthThreshold = -0.1f;
		settings.debugView = static_cast<CharacterDebugView>(0xFFFFFFFFu);
		settings.maskTestMode = static_cast<CharacterMaskTestMode>(0xFFFFFFFFu);
		Require(!IsValidCharacterSettings(settings), "Nonfinite and out-of-range policy must be invalid");
		SanitizeCharacterSettings(settings);
		Require(IsValidCharacterSettings(settings) && settings.faceStrength == CharacterPolicy::kDefaultFaceStrength &&
					settings.maximumDistanceMeters == CharacterPolicy::kDefaultMaximumDistanceMeters &&
					settings.minimumFacePixelSize == CharacterPolicy::kMinimumFacePixelSize &&
					settings.roiHoldFrames == CharacterPolicy::kMaximumRoiHoldFrames &&
					settings.featherRadius == CharacterPolicy::kMaximumFeatherRadius &&
					settings.roiMargin == 0.0f && settings.featherDepthThreshold == 0.0f &&
					settings.debugView == CharacterDebugView::Off && settings.maskTestMode == CharacterMaskTestMode::Authored,
			"Sanitizer did not apply policy bounds, defaults and enum fallbacks");
		const auto sanitized = settings;
		SanitizeCharacterSettings(settings);
		Require(settings == sanitized, "Character sanitizer must be idempotent");
		settings.debugView = CharacterDebugView::Count;
		Require(!IsValidCharacterSettings(settings), "Invalid character enum must invalidate policy");
	}
}

static void ActorGroupCombinations()
{
	using namespace NeuralRendering;
	for (unsigned mask = 0; mask < 32; ++mask) {
		CharacterSettings settings{};
		settings.humans = (mask & 1) != 0;
		settings.otherHumanoids = (mask & 2) != 0;
		settings.creatures = (mask & 4) != 0;
		settings.animals = (mask & 8) != 0;
		settings.otherActors = (mask & 16) != 0;
		FlatSettings flat{};
		ApplyUpscalingCharacterSettings(flat, settings);
		nlohmann::json saved;
		WriteUpscalingCharacterSettingsJson(saved, flat);
		const auto roundTrip = GetUpscalingCharacterSettings(Read(saved));
		Require(roundTrip == settings, "Actor types did not round-trip independently");
		for (unsigned group = 0; group < 5; ++group)
			Require(IsCharacterActorGroupEnabled(static_cast<CharacterActorGroup>(group), roundTrip) == ((mask & (1u << group)) != 0),
				"Actor type selections are not independent");
		Require(GetEnabledCharacterCategoryMask(roundTrip) == 14, "Actor types changed the material selection");
	}
	Require(!IsCharacterActorGroupEnabled(CharacterActorGroup::Count, {}), "Invalid actor group did not fail closed");
}

static void CoverageScopePreferences()
{
	using namespace NeuralRendering;
	CharacterSettings saved;
	saved.enabled = true;
	saved.sceneStrengthsEnabled = true;
	saved.visibilityDepthTest = false;
	saved.depthAwareFeather = true;
	saved.featherRadius = 4;
	saved.featherDepthThreshold = 0.05f;
	const auto actor = ResolveCharacterScopeSettings(saved);
	Require(actor.enabled && !actor.sceneStrengthsEnabled && actor.maximumDistanceMeters == saved.maximumDistanceMeters &&
				!actor.visibilityDepthTest && actor.depthAwareFeather && actor.featherRadius == 4,
		"Actor-only coverage must preserve its controls and ignore the saved scene preference");
	saved.enabled = false;
	const auto scene = ResolveCharacterScopeSettings(saved);
	Require(scene.sceneStrengthsEnabled && scene.maximumDistanceMeters == 0.0f && scene.focusScale == 1.0f &&
				scene.minimumFacePixelSize == 1 && !scene.adaptiveRoiSelection &&
				scene.cropMode == static_cast<std::uint32_t>(CharacterCropMode::Uncropped) &&
				scene.roiMargin == 0.0f && scene.roiHoldFrames == 0 && !scene.depthAwareFeather &&
				scene.featherRadius == 0 && scene.featherDepthThreshold == 0.0f && scene.visibilityDepthTest,
		"Scene coverage must ignore hidden actor-only controls and keep occlusion rejection");
	Require(saved.maximumDistanceMeters == CharacterPolicy::kDefaultMaximumDistanceMeters &&
				!saved.visibilityDepthTest && saved.featherRadius == 4,
		"Resolving scene coverage must not overwrite saved actor-only preferences");
	Require(ResolveCharacterScopeSettings(scene) == scene, "Effective scope resolution must be idempotent");
	for (const auto invalid : { CharacterCategory::None, static_cast<CharacterCategory>(6), static_cast<CharacterCategory>(~0u) })
		Require(!IsCharacterCategorySelected(invalid, saved) && GetCharacterCategoryStrength(invalid, saved) == 0.0f,
			"Invalid material categories must fail closed without indexing settings");
}

int main()
{
	try {
		using namespace NeuralRendering;
		CharacterSettings materials{};
		materials.armor = materials.weapons = true;
		materials.armorStrength = 0.375f;
		materials.weaponsStrength = 0.875f;
		Require(GetCharacterCategoryStrengths(materials) == std::array{ 1.0f, 1.0f, 0.65f, 0.375f, 0.875f },
			"Every material must supply its authored mask strength");
		CharacterCategoryFramePolicy framePolicy;
		const auto frozen = framePolicy.Resolve(100, materials);
		materials.armorStrength = 0.0f;
		materials.weapons = false;
		Require(framePolicy.Resolve(100, materials) == frozen, "Strength edits must not split the stereo frame policy");
		const auto next = framePolicy.Resolve(101, materials);
		Require(!IsCharacterCategoryEnabled(CharacterCategory::Armor, next) &&
					!IsCharacterCategoryEnabled(CharacterCategory::Weapons, next) && GetEnabledCharacterCategoryMask(next) == 14,
			"Zero strength and deselected equipment must leave the next frame mask");
		const auto strengths = GetCharacterCategoryStrengths(next);
		Require(strengths[3] == 0.0f && strengths[4] == 0.0f, "Disabled equipment must supply zero effective mask strength");
		JsonRoundTripAndDefaults();
		ActorGroupCombinations();
		JsonIntegerBounds();
		JsonTypeErrorsAreTransactional();
		JsonFloatBounds();
		VrSettingsMapping();
		PolicyHelpers();
		EquipmentSettings();
		FrameCategoryTransitions();
		CoverageScopePreferences();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
