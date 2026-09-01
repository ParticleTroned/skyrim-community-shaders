#include "Features/ScreenshotApiPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/ScreenshotBurstPolicy.h"
#endif
#include "Utils/StringUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

using json = nlohmann::json;
namespace vr
{
	enum EVREye
	{
		Eye_Left,
		Eye_Right
	};
}
namespace globals::game
{
	inline bool isVR = true;
}
namespace logger
{
	template <class... T>
	void warn(T&&...)
	{}
}

// Only engine and filesystem dependencies are replaced; the settings methods
// and enums below are extracted from the production sources by CMake.
struct ScreenshotFeature
{
#include "screenshot_settings_types.h"
	SequenceDefaults sequenceDefaults{};
	bool runtimeEnabled = true, applyCropToScreenshot = true;
	bool sdrUsePng = true, frameCaptureUsePng = false, copyToClipboard = false;
	CaptureEye screenshotEye = CaptureEye::Left, frameCaptureEye = CaptureEye::Left;
	VRCaptureSource vrCaptureSource = VRCaptureSource::HMDSubmission;
	VRFramedView vrFramedView = VRFramedView::Left;
	vr::EVREye vrFramedDominantEye = vr::Eye_Left;
	std::string screenshotPath = "Pictures", frameCapturePath = "Videos";
	struct
	{
		void LoadSettings(json&) {}
	} subrect;
	void SetEnabled(bool value) { runtimeEnabled = value; }
	json BuildCaptureDescriptor(CaptureEye, bool, bool) const;
	void LoadSettings(json&);
};
bool IsFramedCapture(ScreenshotFeature::VRCaptureSource value)
{
	return value == ScreenshotFeature::VRCaptureSource::FramedEye || value == ScreenshotFeature::VRCaptureSource::FramedStereo;
}
std::filesystem::path ResolveCapturePath(const std::filesystem::path& path, bool) { return path; }
std::filesystem::path ResolveConfiguredCaptureDirectory(const std::filesystem::path& path, bool) { return path; }
constexpr uint32_t kMaximumSequenceFrames = 10000;
struct ScreenshotApi
{
	using json = nlohmann::json;
	json NormalizeCaptureDescriptor(const ScreenshotFeature&, const json&, bool = false) const;
	json ValidateSettingsPatch(const json&) const;
	json BuildCapabilities(const ScreenshotFeature&) const;
	json CheckSequenceBuildSupport(const json& a_request) const
	{
#include "screenshot_build_admission.h"
		return { { "ok", true } };
	}
	json MakeError(const json&, std::string_view code, std::string_view, std::string_view, bool retryable, std::string_view field) const
	{
		return { { "ok", false }, { "error", { { "code", code }, { "retryable", retryable }, { "field", field } } } };
	}

#include "screenshot_build_limits.h"
	void ApplySettingsPatch(ScreenshotFeature&, const json&) const;
	static std::filesystem::path ResolveDestinationDirectory(const ScreenshotFeature& feature, const json&, bool sequence = false)
	{
		const auto& path = sequence ? feature.frameCapturePath : feature.screenshotPath;
		if (path == "unavailable")
			throw std::runtime_error("capture directory unavailable");
		return path;
	}
};
#include "screenshot_settings_under_test.h"

int main()
{
	using Eye = ScreenshotFeature::CaptureEye;
	int failures = 0;
	auto check = [&](bool ok, std::string_view message) {
		if (!ok) {
			std::cerr << message << '\n';
			++failures;
		}
	};
	ScreenshotApi api;
	ScreenshotFeature feature;
	for (const bool vr : { false, true }) {
		globals::game::isVR = vr;
		const auto capabilities = api.BuildCapabilities(feature);
		check(capabilities.at("sources").size() == (vr ? 2 : 1), "runtime capture sources changed");
		check(capabilities.at("formats") == json({ "png", "bmp" }), "ordinary image formats disappeared");
		check(api.CheckSequenceBuildSupport({ { "sequence", { { "frameCount", 2 } } } }).at("ok"), "ordinary sequences rejected");
		for (const auto burst : { json(nullptr), json::object(), json{ { "regions", json::array() } } }) {
			const auto admission = api.CheckSequenceBuildSupport({ { "sequence", { { "frameCount", 2 }, { "burst", burst } } } });
#ifdef DEVBENCH_BRIDGE_ENABLED
			check(capabilities.contains("burst") && capabilities.at("burst").at("deferredEncoding"), "DevBench lost burst capability");
			check(admission.at("ok"), "DevBench blocked burst before descriptor validation");
#else
			check(!capabilities.contains("burst"), "production advertised a diagnostic burst");
			check(!admission.at("ok").get<bool>() && admission.at("error").at("code") == "unsupported" &&
					  !admission.at("error").at("retryable").get<bool>() && admission.at("error").at("field") == "sequence.burst",
				"production did not explicitly reject diagnostic bursts");
#endif
		}
	}
	feature.frameCaptureEye = Eye::Right;
	json partial = { { "FrameCaptureUsePng", true } };
	feature.LoadSettings(partial);
	check(feature.frameCaptureEye == Eye::Right, "partial settings reset the canonical eye");
	for (bool separate : { false, true }) {
		json legacy = { { "Sequence", { { "Outputs", { { "SeparateEyes", separate } } } } } };
		feature.LoadSettings(legacy);
		check(feature.frameCaptureEye == (separate ? Eye::Both : Eye::Left), "nested legacy eye migration failed");
		json canonical = legacy;
		canonical["FrameCaptureEye"] = "Right";
		feature.LoadSettings(canonical);
		check(feature.frameCaptureEye == Eye::Right && !feature.sequenceDefaults.saveSeparateEyes, "canonical eye did not win on load");
		json flat = { { "SequenceSaveSeparateEyes", separate } };
		feature.LoadSettings(flat);
		check(feature.frameCaptureEye == (separate ? Eye::Both : Eye::Left), "flat legacy migration failed");
		api.ApplySettingsPatch(feature, { { "Sequence", { { "Eye", "right" }, { "Outputs", { { "SeparateEyes", separate } } } } } });
		check(feature.frameCaptureEye == Eye::Right && !feature.sequenceDefaults.saveSeparateEyes, "canonical eye did not win on patch");
	}
	for (const auto eye : { Eye::Left, Eye::Right, Eye::Both }) {
		feature.frameCaptureEye = eye;
		feature.frameCaptureUsePng = false;
		feature.screenshotEye = Eye::Right;
		feature.sdrUsePng = true;
		feature.copyToClipboard = true;
		const auto still = api.NormalizeCaptureDescriptor(feature, { { "useSettings", true } });
		const auto sequence = api.NormalizeCaptureDescriptor(feature, { { "useSettings", true } }, true);
		check(still["outputs"][0]["view"] == "right_eye" && still["outputs"][0]["encoding"]["format"] == "png", "still settings not used");
		check(sequence["outputs"].size() == (eye == Eye::Both ? 2 : 1), "sequence added unwanted outputs");
		check(sequence["outputs"][0]["view"] == (eye == Eye::Right ? "right_eye" : "left_eye"), "sequence eye not used");
		check(sequence["outputs"][0]["encoding"]["format"] == "bmp" && sequence["clipboard"] == "none", "sequence inherited still encoding/clipboard");
		check(sequence["destination"]["resolvedDirectory"] == "Videos", "sequence resolved the still destination");
		check(sequence["source"]["fallback"] == "reject", "HMD capture allowed implicit desktop fallback");
	}
	feature.screenshotPath = "unavailable";
	try {
		(void)api.NormalizeCaptureDescriptor(feature, { { "useSettings", true } }, true);
	} catch (...) {
		check(false, "unavailable still folder rejected a valid sequence");
	}
	feature.screenshotPath = "Pictures";
	feature.vrCaptureSource = ScreenshotFeature::VRCaptureSource::FramedStereo;
	feature.frameCaptureEye = Eye::Both;
	feature.vrFramedDominantEye = vr::Eye_Right;
	const auto framed = api.NormalizeCaptureDescriptor(feature, { { "useSettings", true } }, true);
	check(framed["outputs"].size() == 1 && framed["outputs"][0]["dominantEye"] == "right", "framed combined lost dominant eye");
	globals::game::isVR = false;
	const auto desktop = api.NormalizeCaptureDescriptor(feature, { { "useSettings", true } }, true);
	check(desktop["source"]["kind"] == "desktop_mirror" && desktop["outputs"][0]["view"] == "source_native", "SE/AE generated HMD-only output");
	globals::game::isVR = true;
	for (const auto patch : { json{ { "VR", { { "Eye", "bad" } } } }, json{ { "Sequence", { { "Eye", 9 } } } }, json{ { "Sequence", { { "Encoding", { { "Format", "jpeg" } } } } } } })
		check(!api.ValidateSettingsPatch(patch)["valid"].get<bool>(), "invalid eye/encoding accepted");
	json explicitRequest = { { "useSettings", false }, { "capture", { { "source", { { "kind", "hmd_submission" } } }, { "outputs", { { { "view", "left_eye" }, { "encoding", { { "format", "png" } } } } } } } } };
	const auto explicitResult = api.NormalizeCaptureDescriptor(feature, explicitRequest, true);
	check(explicitResult["outputs"].size() == 1 && explicitResult["outputs"][0]["view"] == "left_eye" && explicitResult["outputs"][0]["encoding"]["format"] == "png", "explicit descriptor changed");
	return failures ? 1 : 0;
}
