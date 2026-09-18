#pragma once

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
				actual["captureDiagnostics"] = snapshot();
			} catch (const std::exception& error) {
				actual["captureDiagnostics"] = {
					{ "schemaVersion", 1 }, { "finalized", true }, { "available", false },
					{ "reason", "companion_finalization_failed" }, { "detail", error.what() }
				};
			}
		}
	}
}
