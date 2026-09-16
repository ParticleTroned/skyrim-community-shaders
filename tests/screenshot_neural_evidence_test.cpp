#include "Features/ScreenshotNeuralEvidence.h"
#include "Features/Upscaling/NeuralRendering/CaptureEvidence.h"
#include <stdexcept>

int main()
{
	using Json = nlohmann::json;
	const auto require = [](bool valid) {
		if (!valid)
			throw std::runtime_error("frozen HMD evidence invariant failed");
	};
	const Json source{ { "schemaVersion", 1 }, { "available", true }, { "transactionId", "tx1" },
		{ "configurationFingerprint", "hash1" }, { "configuration", { { "mode", "managed" } } },
		{ "submittedCycle", 12 }, { "cameraEvidence", { { "available", true } } },
		{ "left", { { "outputCommitted", true } } }, { "right", { { "outputCommitted", true } } } };
	auto left = source, right = source;
	left["submittedEye"] = "left";
	right["submittedEye"] = "right";
	auto accepted = CSX::ScreenshotPolicy::JoinNeuralEyeEvidence(left, right);
	require(accepted.at("available"));
	left["configuration"]["mode"] = "legacy_raw";
	require(accepted.at("configuration").at("mode") == "managed");
	require(!CSX::ScreenshotPolicy::JoinNeuralEyeEvidence(left, right).at("available"));
	left = source;
	left["submittedEye"] = "left";
	for (const auto* field : { "transactionId", "configurationFingerprint", "submittedCycle", "cameraEvidence", "submittedEye" }) {
		auto changed = right;
		changed[field] = "mismatch";
		const auto rejected = CSX::ScreenshotPolicy::JoinNeuralEyeEvidence(left, changed);
		require(!rejected.at("available"));
		require(rejected.at("submissions").at("right") == changed);
	}
	require(!CSX::ScreenshotPolicy::JoinNeuralEyeEvidence(left, Json::object()).at("available"));
	NeuralRendering::CaptureInputs inputs;
	inputs.valid = true;
	inputs.route = 1;
	inputs.frame = 22;
	inputs.sourceWorldFrame = 21;
	inputs.generation = 3;
	inputs.insertion = 1;
	require(inputs.Matches(1, 22, 21, 3, 1));
	require(!inputs.Matches(0, 22, 21, 3, 1));
	require(!inputs.Matches(1, 23, 21, 3, 1));
	require(!inputs.Matches(1, 22, 22, 3, 1));
	require(!inputs.Matches(1, 22, 21, 4, 1));
	require(!inputs.Matches(1, 22, 21, 3, 0));
	return 0;
}
