#pragma once

#include "RenderMap/Controller.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CSX::RenderMap
{
	struct CaptureArtifactContext
	{
		std::filesystem::path outputRoot;
		std::string createdAtUtc;
		nlohmann::json producer;
		std::vector<std::string> capabilities;
		nlohmann::json inputs;
		nlohmann::json environment;
		nlohmann::json scenario;
		nlohmann::json extensions = nlohmann::json::object();
	};

	struct CaptureArtifactBundle
	{
		bool success{ false };
		std::filesystem::path directory;
		nlohmann::json eventsArtifact;
		nlohmann::json manifestArtifact;
		std::string error;
	};
	/// Reports the loaded runtime family/version; contradictory observed shader identity is rejected.
	nlohmann::json BuildSkyrimModuleIdentity(bool a_virtualReality, std::string_view a_version,
		std::optional<bool> a_shaderVirtualReality = std::nullopt);

	CaptureArtifactBundle WriteCaptureArtifacts(
		const CompletedCapture& a_capture,
		const CaptureArtifactContext& a_context,
		std::uint32_t a_processId);

	nlohmann::json SerializeArtifactBundle(const CaptureArtifactBundle& a_bundle);
}
