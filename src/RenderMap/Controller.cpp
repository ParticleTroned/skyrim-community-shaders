#include "RenderMap/Controller.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>

namespace CSX::RenderMap
{
	namespace
	{
		std::atomic_uint64_t nextCaptureNumericId{ 1 };

		std::string MakeCaptureId(std::uint64_t a_numericId)
		{
			const auto ticks = static_cast<std::uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			return std::format("capture-live-{:016x}-{}", ticks, a_numericId);
		}
	}

	CaptureController::CaptureController(std::size_t a_completedHistoryLimit) :
		completedHistoryLimit(std::max<std::size_t>(a_completedHistoryLimit, 1))
	{}

	ControlStatus CaptureController::Start(CollectorConfig a_config, CaptureDescriptor& a_output)
	{
		std::lock_guard lock(mutex);
		if (active || GetRuntime().IsCapturing())
			return ControlStatus::kBusy;

		const auto numericId = nextCaptureNumericId.fetch_add(1, std::memory_order_relaxed);
		a_config.captureNumericId = numericId;
		const auto result = GetRuntime().StartCapture(a_config);
		switch (result) {
		case StartResult::kAlreadyCapturing:
			return ControlStatus::kBusy;
		case StartResult::kInvalidBounds:
			return ControlStatus::kInvalidBounds;
		case StartResult::kAllocationFailed:
			return ControlStatus::kAllocationFailed;
		case StartResult::kStarted:
			break;
		}

		active = CaptureDescriptor{
			.captureId = MakeCaptureId(numericId),
			.numericId = numericId,
			.config = a_config,
		};
		a_output = *active;
		return ControlStatus::kSuccess;
	}

	ControlStatus CaptureController::Stop(
		std::string_view a_captureId,
		std::shared_ptr<const CompletedCapture>& a_output)
	{
		std::lock_guard lock(mutex);
		for (const auto& capture : completed) {
			if (capture->descriptor.captureId == a_captureId) {
				a_output = capture;
				return ControlStatus::kSuccess;
			}
		}
		if (!active)
			return ControlStatus::kNotCapturing;
		if (active->captureId != a_captureId)
			return ControlStatus::kCaptureNotFound;

		auto snapshot = GetRuntime().StopCapture();
		if (!snapshot)
			return ControlStatus::kNotCapturing;

		auto capture = std::make_shared<CompletedCapture>(CompletedCapture{
			.descriptor = *active,
			.snapshot = std::move(*snapshot),
		});
		active.reset();
		completed.push_back(capture);
		while (completed.size() > completedHistoryLimit)
			completed.pop_front();
		a_output = std::move(capture);
		return ControlStatus::kSuccess;
	}

	ControllerSnapshot CaptureController::GetStatus() const
	{
		std::lock_guard lock(mutex);
		ControllerSnapshot output{
			.active = active,
			.accepting = active.has_value() && GetRuntime().IsCapturing(),
		};
		output.completedCaptureIds.reserve(completed.size());
		for (const auto& capture : completed)
			output.completedCaptureIds.push_back(capture->descriptor.captureId);
		return output;
	}

	std::shared_ptr<const CompletedCapture> CaptureController::GetCompleted(std::string_view a_captureId) const
	{
		std::lock_guard lock(mutex);
		const auto found = std::find_if(completed.begin(), completed.end(), [&](const auto& a_capture) {
			return a_capture->descriptor.captureId == a_captureId;
		});
		return found == completed.end() ? nullptr : *found;
	}

	CaptureController& GetCaptureController() noexcept
	{
		static CaptureController controller;
		return controller;
	}
}
