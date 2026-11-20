#pragma once

#include "RenderMap/Controller.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string_view>

namespace CSX::RenderMap
{
	/** Shared loss and failure reasons; event truncation and evidence completeness are separate projections. */
	struct CaptureCompleteness
	{
		std::uint64_t lostEventCount{ 0 };
		std::uint64_t transferAdmissionFailures{ 0 };
		bool structurallyIncomplete{ false };
		bool terminalFailure{ false };
		nlohmann::json reasons = nlohmann::json::array();
		nlohmann::json errors = nlohmann::json::array();

		bool EvidenceTruncated() const noexcept { return lostEventCount != 0 || structurallyIncomplete; }
		bool Incomplete() const noexcept { return EvidenceTruncated() || terminalFailure; }
	};

	/** Evaluate retained event, catalogue, scope, transfer, and lifecycle evidence once for all outputs. */
	CaptureCompleteness EvaluateCaptureCompleteness(const CaptureSnapshot& a_snapshot);

	nlohmann::json SerializeBounds(const CollectorConfig& a_config);
	nlohmann::json SerializeCaptureWindow(const CaptureWindowSnapshot& a_window);
	nlohmann::json SerializeEventKindMask(EventKindMask a_mask);
	nlohmann::json SerializeGeometryShaderTypeMask(std::uint64_t a_mask);
	nlohmann::json SerializeControllerStatus(const ControllerSnapshot& a_status);
	nlohmann::json SerializeCaptureSummary(const CompletedCapture& a_capture);
	nlohmann::json SerializeEvent(
		const EventRecord& a_event,
		std::string_view a_captureId,
		std::uint32_t a_processId,
		const CaptureSnapshot* a_snapshot = nullptr);
	nlohmann::json SerializeEventPage(
		const CompletedCapture& a_capture,
		std::size_t a_offset,
		std::size_t a_limit,
		std::uint32_t a_processId);
}
