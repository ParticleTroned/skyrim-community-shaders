#pragma once

#include <nlohmann/json.hpp>

namespace CSX::ScreenshotPolicy
{
	/** Join only independently accepted eyes from the same frozen render transaction. */
	inline nlohmann::json JoinNeuralEyeEvidence(const nlohmann::json& left, const nlohmann::json& right)
	{
		using Json = nlohmann::json;
		const auto unavailable = [&](const char* reason) {
			return Json{ { "schemaVersion", 1 }, { "available", false }, { "reason", reason },
				{ "submissions", { { "left", left }, { "right", right } } } };
		};
		if (!left.is_object() || !right.is_object())
			return unavailable("eye_evidence_malformed");
		for (const auto* evidence : { &left, &right }) {
			if (evidence->contains("available") && !evidence->at("available").is_boolean())
				return unavailable("eye_evidence_malformed");
		}
		if (!left.value("available", false) || !right.value("available", false))
			return unavailable("eye_evidence_unavailable");
		if (!left.contains("submittedEye") || !right.contains("submittedEye") ||
			left.at("submittedEye") != "left" || right.at("submittedEye") != "right")
			return unavailable("submitted_eye_identity_mismatch");
		for (const auto* key : { "schemaVersion", "transactionId", "configurationFingerprint", "configuration",
				 "submittedCycle", "cameraEvidence" }) {
			if (!left.contains(key) || !right.contains(key) || left.at(key) != right.at(key))
				return unavailable("eye_transaction_mismatch");
		}
		if ((left.contains("publicationSequence") || right.contains("publicationSequence")) &&
			(!left.contains("publicationSequence") || !right.contains("publicationSequence") ||
				left.at("publicationSequence") != right.at("publicationSequence")))
			return unavailable("eye_publication_mismatch");
		if (!left.contains("left") || !right.contains("right"))
			return unavailable("eye_outcome_missing");
		auto result = left;
		result.erase("submittedEye");
		result.erase("submittedTextureIdentity");
		result["right"] = right.at("right");
		result["submissions"] = { { "left", left }, { "right", right } };
		return result;
	}
}
