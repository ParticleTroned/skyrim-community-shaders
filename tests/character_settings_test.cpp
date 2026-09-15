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
		bool neuralCharacterRenderingEnabled = CharacterPolicy::kDefaultEnabled;
		bool neuralCharacterVisualIsolationEnabled = CharacterPolicy::kDefaultVisualIsolation;
		bool neuralCharacterFacesEnabled = CharacterPolicy::kDefaultFaces;
		bool neuralCharacterSkinEnabled = CharacterPolicy::kDefaultSkin;
		bool neuralCharacterHairEnabled = CharacterPolicy::kDefaultHair;
		float neuralCharacterFaceStrength = CharacterPolicy::kDefaultFaceStrength;
		float neuralCharacterSkinStrength = CharacterPolicy::kDefaultSkinStrength;
		float neuralCharacterHairStrength = CharacterPolicy::kDefaultHairStrength;
		float neuralCharacterMaximumDistanceMeters = CharacterPolicy::kDefaultMaximumDistanceMeters;
		bool neuralCharacterAdaptiveRoiSelectionEnabled = CharacterPolicy::kDefaultAdaptiveRoiSelection;
		bool neuralCharacterMultiRoiEnabled = false;
		bool neuralCharacterMultiRoiSavingsGateEnabled = true;
		std::uint32_t neuralCharacterMinimumFacePixelSize = CharacterPolicy::kDefaultMinimumFacePixelSize;
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
		const auto persisted = json::parse(R"({
			"neuralCharacterRenderingEnabled": true,
			"neuralCharacterVisualIsolationEnabled": false,
			"neuralCharacterFacesEnabled": false, "neuralCharacterSkinEnabled": false,
			"neuralCharacterHairEnabled": true, "neuralCharacterFaceStrength": 0.25,
			"neuralCharacterSkinStrength": 0.5, "neuralCharacterHairStrength": 0.75,
			"neuralCharacterMaximumDistanceMeters": 15.5,
			"neuralCharacterAdaptiveRoiSelectionEnabled": true,
			"neuralCharacterMultiRoiEnabled": true, "neuralCharacterMultiRoiSavingsGateEnabled": false,
			"neuralCharacterMinimumFacePixelSize": 128,
			"neuralCharacterRoiMargin": 0.5, "neuralCharacterRoiHoldFrames": 12,
			"neuralCharacterDepthAwareFeatherEnabled": true,
			"neuralCharacterVisibilityDepthTestEnabled": false,
			"neuralCharacterFeatherRadius": 4, "neuralCharacterDepthThreshold": 0.03125
		})");
		const auto settings = Read(persisted);
		json saved = json::object();
		WriteUpscalingCharacterSettingsJson(saved, settings);
		Require(saved == persisted, "Persisted flat VR character schema or complete round trip changed");
		Require(Read(json::parse(saved.dump())) == settings,
			"Character settings changed after persisted text round trip");
		Require(Read(json::object()) == FlatSettings{}, "Empty character object must use defaults");
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
		original.neuralCharacterHairEnabled = original.neuralCharacterMultiRoiEnabled = true;
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
		for (const char* key : { "neuralCharacterMinimumFacePixelSize", "neuralCharacterRoiHoldFrames", "neuralCharacterFeatherRadius" }) {
			for (const auto& wrong : std::array<json, 8>{ 1.5, 1.0, true, "1", nullptr,
					 json::array(), json::object(), json::parse("18446744073709551616") })
				rejects(json{ { "neuralCharacterRenderingEnabled", true }, { key, wrong } });
		}
		rejects(json{ { "neuralCharacterRenderingEnabled", 1 } });
		for (const auto& wrong : std::array<json, 5>{ 0, 1, "false", nullptr, json::array() })
			rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterMultiRoiSavingsGateEnabled", wrong } });
		rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterFaceStrength", "0.5" } });
		rejects(json{ { "neuralCharacterRenderingEnabled", true }, { "neuralCharacterHairStrength", false } });
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
				{ "neuralCharacterRoiMargin", value }, { "neuralCharacterDepthThreshold", value } }));
		};
		const auto positive = extreme(json::parse("1.0e300"));
		Require(positive.faceStrength == CharacterPolicy::kMaximumStrength &&
					positive.skinStrength == CharacterPolicy::kMaximumStrength &&
					positive.hairStrength == CharacterPolicy::kMaximumStrength &&
					positive.maximumDistanceMeters == CharacterPolicy::kMaximumDistanceMeters &&
					positive.roiMargin == CharacterPolicy::kMaximumRoiMargin &&
					positive.featherDepthThreshold == CharacterPolicy::kMaximumFeatherDepthThreshold,
			"Large finite positive character values must clamp upward before float narrowing");
		const auto negative = extreme(json::parse("-1.0e300"));
		Require(negative.faceStrength == CharacterPolicy::kMinimumStrength &&
					negative.skinStrength == CharacterPolicy::kMinimumStrength &&
					negative.hairStrength == CharacterPolicy::kMinimumStrength &&
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
		Require(peer.debugView == CharacterDebugView::RoiRectangles,
			"A diagnostic-only edit must not wait for category policy advancement");
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

	void PolicyHelpers()
	{
		static_assert(CharacterPolicy::CategoryBit(CharacterCategory::None) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(4)) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(32)) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(0xFFFFFFFFu)) == 0);
		CharacterSettings settings{};
		Require(IsValidCharacterSettings(settings), "Default character policy must be valid");
		Require(GetEnabledCharacterCategoryMask(settings) == 6u, "Default mask must include face and skin only");
		settings.skinStrength = 0.0f;
		settings.hair = true;
		Require(GetEnabledCharacterCategoryMask(settings) == 10u, "Category mask must respect strengths and switches");
		Require(!IsCharacterCategoryEnabled(static_cast<CharacterCategory>(4), settings), "Unknown category must remain disabled");
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

int main()
{
	try {
		JsonRoundTripAndDefaults();
		JsonIntegerBounds();
		JsonTypeErrorsAreTransactional();
		JsonFloatBounds();
		VrSettingsMapping();
		PolicyHelpers();
		FrameCategoryTransitions();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
