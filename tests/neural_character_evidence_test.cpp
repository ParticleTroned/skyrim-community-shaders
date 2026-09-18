#include "Features/Upscaling/NeuralRendering/CharacterPreparationEvidenceJson.h"
#include <stdexcept>

namespace
{
	void Require(bool value, const char* detail)
	{
		if (!value)
			throw std::runtime_error(detail);
	}
}

int main()
{
	using namespace NeuralRendering;
	using namespace NeuralRendering::Evidence;
	CharacterPreparationKey key{ 101, 100, 1, 3, 7, 19, 23, 5,
		{ { 900, 700 }, { 101, 43, 801, 643 }, { 1800, 1400 }, { 202, 86, 1602, 1286 } }, 0.25f, -0.5f };
	const auto changedKey = [&](auto mutate) {
		auto changed = key;
		mutate(changed);
		Require(changed != key, "identity change was not preserved");
		Require(CharacterPreparationKeyJson(changed) != CharacterPreparationKeyJson(key), "serialized identity collapsed");
	};
	changedKey([](auto& k) { ++k.frame; });
	changedKey([](auto& k) { ++k.sourceWorldFrame; });
	changedKey([](auto& k) { ++k.generation; });
	changedKey([](auto& k) { ++k.contentSerial; });
	changedKey([](auto& k) { ++k.captureEpoch; });
	changedKey([](auto& k) { ++k.settingsKey; });
	changedKey([](auto& k) { k.eye = 0; });
	changedKey([](auto& k) { k.featureSlot = 1; });
	changedKey([](auto& k) { ++k.crop.input.left; });
	changedKey([](auto& k) { ++k.crop.output.left; });
	changedKey([](auto& k) { ++k.crop.fullInput.width; });
	changedKey([](auto& k) { ++k.crop.fullOutput.height; });
	changedKey([](auto& k) { k.jitterX = -0.25f; });
	changedKey([](auto& k) { k.jitterY = 0.5f; });

	Require(CharacterPreparationJson({})["available"] == false, "absent preparation must be unavailable");
	auto preparation = std::make_shared<CharacterPreparationEvidence>();
	preparation->key = key;
	preparation->prepared = true;
	preparation->requiresEvaluation = false;
	preparation->outcome = "no_work";
	preparation->support = std::make_shared<CharacterMaskSupportCapture>(key);
	preparation->support->Complete(0);
	const auto empty = CharacterPreparationJson(preparation);
	Require(empty["outcome"] == "no_work" && empty["regions"].empty(), "empty selection must not invent inference regions");
	Require(empty["maskSupport"]["state"] == "unavailable" && empty["maskSupport"]["pixels"].is_null(), "CPU empty proof is not a GPU measurement");
	Require(empty["timing"]["explicitWait"]["milliseconds"].is_null(), "no wait is not a measured zero");
	Require(empty["timing"]["mask"]["gpu"]["inclusiveMs"].is_null(), "missing GPU timing must remain absent");

	preparation->requiresEvaluation = true;
	preparation->outcome = "success";
	preparation->computeSubrect = { 3, 5, 113, 71 };
	preparation->computeRegions.count = 2;
	preparation->computeRegions.regions = { ComputeSubrect{ 3, 5, 30, 71 }, ComputeSubrect{ 80, 5, 36, 71 } };
	preparation->boundsReady = true;
	preparation->boundsUsed = false;
	preparation->boundsStatus = "early_bounds_empty";
	preparation->support->Pending();
	std::shared_ptr<const CharacterPreparationEvidence> captured = preparation;
	preparation = std::make_shared<CharacterPreparationEvidence>(*preparation);
	++preparation->key.frame;
	++preparation->key.contentSerial;
	++preparation->key.captureEpoch;
	preparation->computeRegions = {};
	preparation->support = std::make_shared<CharacterMaskSupportCapture>(preparation->key);
	const auto pending = CharacterPreparationJson(captured);
	Require(pending["regions"].size() == 2 && pending["key"]["contentSerial"] == 19, "later slot reuse changed frozen ROI membership");
	Require(pending["bounds"]["ready"] == true && pending["bounds"]["used"] == false, "ready bounds must not imply consumption");
	Require(pending["maskSupport"]["state"] == "pending" && pending["maskSupport"]["pixels"].is_null(), "pending coverage became a zero");
	captured->support->Complete(37);
	captured->support->Complete(99);
	const auto complete = CharacterPreparationJson(captured);
	Require(complete["maskSupport"]["pixels"] == 37 && complete["maskSupport"]["producer"]["captureEpoch"] == 5,
		"delayed coverage lost exact original contents or accepted a duplicate completion");
	Require(CharacterPreparationJson(preparation)["maskSupport"]["pixels"].is_null(), "old coverage crossed into replacement contents");
	Require(pending["maskSupport"]["state"] == "pending", "serialized acquisition changed after finalization");
	preparation->support->Pending();
	preparation->support->Fail("coverage_resources_retired");
	preparation->support->Complete(88);
	Require(CharacterPreparationJson(preparation)["maskSupport"]["state"] == "failed" &&
				CharacterPreparationJson(preparation)["maskSupport"]["pixels"].is_null(),
		"retired readback cannot later become successful");

	auto source = std::make_shared<CharacterSourceEvidence>();
	source->eyeWidth = 900;
	source->height = 700;
	source->eyeCount = 2;
	source->sourceWorldFrame = 100;
	source->captureEpoch = 5;
	preparation->source = source;
	const auto stereo = CharacterPreparationJson(preparation);
	Require(stereo["sourceCapture"]["authoredEyeOrigin"] == Json::array({ 900, 0 }), "packed source eye origin lost");
	Require(stereo["sourceCapture"]["detectionCpu"]["milliseconds"].is_null(), "unobserved detection cost fabricated");
	source = std::make_shared<CharacterSourceEvidence>(*source);
	source->eyeCount = 1;
	preparation->source = source;
	preparation->key.eye = 0;
	const auto mono = CharacterPreparationJson(preparation);
	Require(mono["sourceCapture"]["authoredGrid"]["width"] == 900 &&
				mono["sourceCapture"]["authoredEyeOrigin"] == Json::array({ 0, 0 }),
		"mono source acquired a stereo stride");
}
