#pragma once

#include "RenderMap/Controller.h"

namespace CSX::RenderMap
{
	enum class CaptureStartPhase
	{
		kContext,
		kResponse,
		kRuntime,
	};

	/// Commits context and response before hook activation; discard must not throw or re-enter the controller.
	template <class RetainContext, class PrepareResponse, class DiscardContext>
	ControlStatus StartPreparedCapture(CaptureController& a_controller, CollectorConfig a_config,
		CaptureDescriptor& a_descriptor, CaptureStartPhase& a_phase,
		RetainContext&& a_retain, PrepareResponse&& a_response, DiscardContext&& a_discard)
	{
		a_descriptor = {};
		a_phase = CaptureStartPhase::kContext;
		ControlStatus status;
		try {
			status = a_controller.Start(a_config, a_descriptor, [&](const CaptureDescriptor& a_capture) {
				a_phase = CaptureStartPhase::kContext;
				a_retain(a_capture);
				a_phase = CaptureStartPhase::kResponse;
				a_response(a_capture);
				a_phase = CaptureStartPhase::kRuntime;
			});
		} catch (...) {
			status = ControlStatus::kAllocationFailed;
		}
		if (status != ControlStatus::kSuccess && !a_descriptor.captureId.empty())
			a_discard(a_descriptor.captureId);
		return status;
	}
}
