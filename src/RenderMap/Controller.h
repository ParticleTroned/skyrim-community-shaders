#pragma once

#include "RenderMap/Runtime.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace CSX::RenderMap
{
	enum class ControlStatus : std::uint8_t
	{
		kSuccess,
		kBusy,
		kInvalidBounds,
		kAllocationFailed,
		kNotCapturing,
		kCaptureNotFound,
		kDraining,
	};

	struct CaptureDescriptor
	{
		std::string captureId;
		std::uint64_t numericId{ 0 };
		CollectorConfig config;
	};

	struct CompletedCapture
	{
		CaptureDescriptor descriptor;
		CaptureSnapshot snapshot;
	};

	struct ControllerSnapshot
	{
		std::optional<CaptureDescriptor> active;
		bool accepting{ false };
		CaptureWindowSnapshot window;
		std::vector<std::string> completedCaptureIds;
	};

	class CaptureController
	{
	public:
		explicit CaptureController(std::size_t a_completedHistoryLimit = 4);

		/// Prepares external capture state before activating hooks; preparation must not re-enter the controller.
		ControlStatus Start(CollectorConfig a_config, CaptureDescriptor& a_output,
			const std::function<void(const CaptureDescriptor&)>& a_prepare = {});
		ControlStatus Stop(std::string_view a_captureId, std::shared_ptr<const CompletedCapture>& a_output);
		ControllerSnapshot GetStatus() const;
		std::shared_ptr<const CompletedCapture> GetCompleted(std::string_view a_captureId) const;

	private:
		mutable std::mutex mutex;
		std::optional<CaptureDescriptor> active;
		std::deque<std::shared_ptr<const CompletedCapture>> completed;
		std::size_t completedHistoryLimit{ 4 };
	};

	CaptureController& GetCaptureController() noexcept;
}
