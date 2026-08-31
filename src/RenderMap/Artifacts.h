#pragma once

#include "RenderMap/Controller.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
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

	CaptureArtifactBundle WriteCaptureArtifacts(
		const CompletedCapture& a_capture,
		const CaptureArtifactContext& a_context,
		std::uint32_t a_processId);

	nlohmann::json SerializeArtifactBundle(const CaptureArtifactBundle& a_bundle);
}
