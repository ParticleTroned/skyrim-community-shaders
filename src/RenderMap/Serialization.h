#pragma once

#include "RenderMap/Controller.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string_view>

namespace CSX::RenderMap
{
	nlohmann::json SerializeBounds(const CollectorConfig& a_config);
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
