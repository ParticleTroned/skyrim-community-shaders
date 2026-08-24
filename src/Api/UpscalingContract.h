#pragma once

#include "VRAPI/CSupscalingapi.h"

namespace CSX::Api
{
	UpscalingAPI::Status ValidateUpscalingProfile(const UpscalingAPI::Profile001& a_profile) noexcept;
	UpscalingAPI::Status ValidateUpscalingPreflightRequest(const UpscalingAPI::PreflightRequest001& a_request) noexcept;
	UpscalingAPI::Status ValidateUpscalingApplyRequest(const UpscalingAPI::ApplyRequest001& a_request) noexcept;
	bool IsTerminalUpscalingOperation(UpscalingAPI::OperationState a_state) noexcept;
}
