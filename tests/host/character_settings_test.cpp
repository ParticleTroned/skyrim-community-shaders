#include "Features/Upscaling/NeuralRendering/CharacterSettingsJson.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace NeuralRendering;
	using nlohmann::json;

	void Require(bool condition, const char* description)
	{
		if (!condition)
			throw std::runtime_error(description);
	}

	void JsonRoundTripAndDefaults()
	{
		const auto persisted = json::parse(R"({
			"enabled": true, "faces": false, "skin": false, "hair": true,
			"faceStrength": 0.25, "skinStrength": 0.5, "hairStrength": 0.75,
			"maximumDistanceMeters": 15.5, "adaptiveRoiSelection": true,
			"multiRoi": true, "minimumFacePixelSize": 128, "roiMargin": 0.5,
			"roiHoldFrames": 12, "depthAwareFeather": true,
			"visibilityDepthTest": false, "featherRadius": 4,
			"featherDepthThreshold": 0.03125, "debugView": 3, "maskTestMode": 2
		})");
		const auto settings = persisted.get<CharacterSettings>();
		Require(json(settings) == persisted, "Character JSON schema or complete round trip changed");
		Require(json::parse(json(settings).dump()).get<CharacterSettings>() == settings,
			"Character settings changed after persisted text round trip");
		Require(json::object().get<CharacterSettings>() == CharacterSettings{},
			"Empty character object must use defaults");
		auto expected = CharacterSettings{};
		expected.enabled = true;
		expected.minimumFacePixelSize = 200;
		const auto partial = json{ { "enabled", true }, { "minimumFacePixelSize", 200 },
			{ "futureUnknownSetting", json::array({ nullptr, "ignored" }) } };
		Require(partial.get<CharacterSettings>() == expected,
			"Partial character object must use defaults and ignore unknown keys");
		auto destination = settings;
		partial.get_to(destination);
		Require(destination == expected, "Partial get_to must reset missing fields to defaults");
	}

	void JsonIntegerBounds()
	{
		for (const auto wide : { std::uint64_t{ 4294967297 }, std::numeric_limits<std::uint64_t>::max() }) {
			const auto settings = json{ { "minimumFacePixelSize", wide },
				{ "roiHoldFrames", wide }, { "featherRadius", wide },
				{ "debugView", wide }, { "maskTestMode", wide } }
			                          .get<CharacterSettings>();
			Require(settings.minimumFacePixelSize == CharacterPolicy::kMaximumFacePixelSize &&
						settings.roiHoldFrames == CharacterPolicy::kMaximumRoiHoldFrames &&
						settings.featherRadius == CharacterPolicy::kMaximumFeatherRadius,
				"Wide character counts wrapped before policy clamping");
			Require(settings.debugView == CharacterDebugView::Off &&
						settings.maskTestMode == CharacterMaskTestMode::Authored,
				"Wide character enums aliased a valid mode");
		}
		for (const auto negative : { std::int64_t{ -1 }, std::numeric_limits<std::int64_t>::min() }) {
			const auto settings = json{ { "minimumFacePixelSize", negative },
				{ "roiHoldFrames", negative }, { "featherRadius", negative },
				{ "debugView", negative }, { "maskTestMode", negative } }
			                          .get<CharacterSettings>();
			Require(settings.minimumFacePixelSize == CharacterPolicy::kMinimumFacePixelSize &&
						settings.roiHoldFrames == 0 && settings.featherRadius == 0,
				"Negative character counts wrapped to their maximums");
			Require(settings.debugView == CharacterDebugView::Off &&
						settings.maskTestMode == CharacterMaskTestMode::Authored,
				"Negative character enums did not fall back");
		}
		const auto invalidEnums = json{ { "debugView", CharacterDebugView::Count },
			{ "maskTestMode", CharacterMaskTestMode::Count } }
		                              .get<CharacterSettings>();
		Require(invalidEnums.debugView == CharacterDebugView::Off &&
					invalidEnums.maskTestMode == CharacterMaskTestMode::Authored,
			"Out-of-range character enum did not fall back");
		const auto clamped = json{ { "faceStrength", -1.0f }, { "hairStrength", 2.0f },
			{ "maximumDistanceMeters", 100.0f } }
		                         .get<CharacterSettings>();
		Require(clamped.faceStrength == 0.0f && clamped.hairStrength == 1.0f &&
					clamped.maximumDistanceMeters == CharacterPolicy::kMaximumDistanceMeters,
			"Character JSON must apply the authoritative float sanitizer");
	}

	void JsonTypeErrorsAreTransactional()
	{
		CharacterSettings original{};
		original.hair = original.multiRoi = true;
		original.roiHoldFrames = 11;
		const auto rejects = [&](const json& input) {
			auto destination = original;
			bool rejected = false;
			try {
				input.get_to(destination);
			} catch (const json::exception&) {
				rejected = true;
			}
			Require(rejected, "Wrong character JSON type must be rejected");
			Require(destination == original, "Rejected character JSON partially changed settings");
		};
		for (const char* key : { "minimumFacePixelSize", "roiHoldFrames", "featherRadius", "debugView", "maskTestMode" }) {
			for (const auto& wrong : std::array<json, 7>{ 1.5, 1.0, true, "1", nullptr, json::array(), json::object() })
				rejects(json{ { "enabled", true }, { key, wrong } });
		}
		rejects(json{ { "enabled", 1 } });
		rejects(json{ { "enabled", true }, { "faceStrength", "0.5" } });
		rejects(json{ { "enabled", true }, { "hairStrength", false } });
		for (const auto& wrong : std::array<json, 5>{ nullptr, 1, true, "settings", json::array() })
			rejects(wrong);
	}

	void JsonFloatBounds()
	{
		const auto extreme = [](double value) {
			return json{ { "faceStrength", value }, { "skinStrength", value },
				{ "hairStrength", value }, { "maximumDistanceMeters", value },
				{ "roiMargin", value }, { "featherDepthThreshold", value } }
			    .get<CharacterSettings>();
		};
		const auto positive = extreme(1.0e300);
		Require(positive.faceStrength == CharacterPolicy::kMaximumStrength &&
					positive.skinStrength == CharacterPolicy::kMaximumStrength &&
					positive.hairStrength == CharacterPolicy::kMaximumStrength &&
					positive.maximumDistanceMeters == CharacterPolicy::kMaximumDistanceMeters &&
					positive.roiMargin == CharacterPolicy::kMaximumRoiMargin &&
					positive.featherDepthThreshold == CharacterPolicy::kMaximumFeatherDepthThreshold,
			"Large finite positive character values must clamp upward before float narrowing");
		const auto negative = extreme(-1.0e300);
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

	void PolicyHelpers()
	{
		static_assert(CharacterPolicy::CategoryBit(CharacterCategory::None) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(32)) == 0);
		static_assert(CharacterPolicy::CategoryBit(static_cast<CharacterCategory>(0xFFFFFFFFu)) == 0);
		CharacterSettings settings{};
		Require(IsValidCharacterSettings(settings), "Default character policy must be valid");
		Require(GetEnabledCharacterCategoryMask(settings) == 6u,
			"Default mask must include face and skin only");
		settings.skinStrength = 0.0f;
		settings.hair = true;
		Require(GetEnabledCharacterCategoryMask(settings) == 10u,
			"Category mask must respect strengths and category switches");
		Require(!IsCharacterCategoryEnabled(static_cast<CharacterCategory>(4), settings),
			"Unknown character category must remain disabled");
		settings.faceStrength = std::numeric_limits<float>::quiet_NaN();
		Require(!IsValidCharacterSettings(settings), "Nonfinite character policy must be invalid");
		SanitizeCharacterSettings(settings);
		Require(IsValidCharacterSettings(settings), "Sanitized character policy must be valid");
		settings.debugView = CharacterDebugView::Count;
		Require(!IsValidCharacterSettings(settings), "Invalid character enum must invalidate policy");
	}
}

int main()
{
	using namespace NeuralRendering;
	CharacterSettings settings{};
	const auto defaults = settings;
	SanitizeCharacterSettings(settings);
	if (settings != defaults)
		return 1;

	settings.enabled = settings.multiRoi = settings.hair = true;
	settings.faceStrength = std::numeric_limits<float>::quiet_NaN();
	settings.skinStrength = -1.0f;
	settings.hairStrength = 2.0f;
	settings.maximumDistanceMeters = std::numeric_limits<float>::infinity();
	settings.minimumFacePixelSize = 0;
	settings.roiMargin = -1.0f;
	settings.roiHoldFrames = std::numeric_limits<std::uint32_t>::max();
	settings.featherRadius = 100;
	settings.featherDepthThreshold = -0.1f;
	settings.debugView = static_cast<CharacterDebugView>(0xFFFFFFFFu);
	settings.maskTestMode = static_cast<CharacterMaskTestMode>(0xFFFFFFFFu);
	SanitizeCharacterSettings(settings);
	if (!settings.enabled || !settings.multiRoi || !settings.hair ||
		settings.faceStrength != defaults.faceStrength || settings.skinStrength != 0.0f ||
		settings.hairStrength != 1.0f || settings.maximumDistanceMeters != defaults.maximumDistanceMeters ||
		settings.minimumFacePixelSize != CharacterPolicy::kMinimumFacePixelSize ||
		settings.roiMargin != 0.0f || settings.roiHoldFrames != CharacterPolicy::kMaximumRoiHoldFrames ||
		settings.featherRadius != CharacterPolicy::kMaximumFeatherRadius || settings.featherDepthThreshold != 0.0f ||
		settings.debugView != CharacterDebugView::Off || settings.maskTestMode != CharacterMaskTestMode::Authored)
		return 2;

	settings.maximumDistanceMeters = 0.0f;
	settings.minimumFacePixelSize = 0xFFFFFFFFu;
	settings.roiMargin = std::numeric_limits<float>::quiet_NaN();
	settings.featherDepthThreshold = std::numeric_limits<float>::quiet_NaN();
	settings.debugView = CharacterDebugView::CharacterMask;
	settings.maskTestMode = CharacterMaskTestMode::ForceHalf;
	SanitizeCharacterSettings(settings);
	if (settings.maximumDistanceMeters != 0.0f ||
		settings.minimumFacePixelSize != CharacterPolicy::kMaximumFacePixelSize ||
		settings.roiMargin != defaults.roiMargin || settings.featherDepthThreshold != defaults.featherDepthThreshold ||
		settings.debugView != CharacterDebugView::CharacterMask || settings.maskTestMode != CharacterMaskTestMode::ForceHalf)
		return 3;
	const auto sanitized = settings;
	SanitizeCharacterSettings(settings);
	if (settings != sanitized)
		return 4;
	try {
		JsonRoundTripAndDefaults();
		JsonIntegerBounds();
		JsonFloatBounds();
		JsonTypeErrorsAreTransactional();
		PolicyHelpers();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 5;
	}
	return 0;
}
