#pragma once

#include "Utils/JsonCleanup.h"

#include <exception>
#include <functional>
#include <nlohmann/json.hpp>
#include <utility>

namespace CSX::ScreenshotPolicy
{
	using DiagnosticSnapshot = std::function<nlohmann::json()>;

	/** Pin exact CPU companions at acquisition; snapshot and release at terminal. */
	DiagnosticSnapshot RetainNeuralDiagnostics(const nlohmann::json& evidence, DiagnosticSnapshot execution = {});

	/** Seal once before the terminal receipt/manifest, including failure paths. */
	inline void FinalizeNeuralDiagnostics(DiagnosticSnapshot& pending, nlohmann::json& actual)
	{
		if (auto snapshot = std::exchange(pending, {})) {
			try {
				auto& destination = actual["captureDiagnostics"];
				auto diagnostic = snapshot();
				const Util::JsonCleanupGuard cleanup(diagnostic);
				destination.swap(diagnostic);
			} catch (const std::exception& error) {
				Util::PopulateJsonObject(actual["captureDiagnostics"], [&](nlohmann::json& diagnostic) {
					diagnostic["schemaVersion"] = 1;
					diagnostic["finalized"] = true;
					diagnostic["available"] = false;
					diagnostic["reason"] = "companion_finalization_failed";
					diagnostic["detail"] = error.what();
				});
			}
		}
	}
}
