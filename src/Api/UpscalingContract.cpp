#include "Api/UpscalingContract.h"

namespace
{
	template <class T>
	bool HasCompleteStructure(const T& a_value) noexcept
	{
		return a_value.structSize >= sizeof(T);
	}

	bool IsBoundedString(const char* a_value, std::uint32_t a_maximum, bool a_required) noexcept
	{
		if (!a_value)
			return !a_required;
		std::size_t length = 0;
		while (length <= a_maximum && a_value[length] != '\0')
			++length;
		return length <= a_maximum && (!a_required || length != 0);
	}

	bool IsValidPurpose(CSX::UpscalingAPI::RequestPurpose a_value) noexcept
	{
		using enum CSX::UpscalingAPI::RequestPurpose;
		return a_value == kDirect || a_value == kEnvironmentProfileTransition;
	}

	bool IsValidPersistence(CSX::UpscalingAPI::PersistencePolicy a_value) noexcept
	{
		using enum CSX::UpscalingAPI::PersistencePolicy;
		return a_value == kRuntimeOnly || a_value == kPersistWhenStable;
	}
}

namespace CSX::Api
{
	UpscalingAPI::Status ValidateUpscalingProfile(const UpscalingAPI::Profile001& a_profile) noexcept
	{
		using namespace UpscalingAPI;
		if (!HasCompleteStructure(a_profile))
			return Status::kStructureTooSmall;
		if (a_profile.renderScaleMode > 1)
			return Status::kInvalidArgument;
		if (static_cast<std::uint32_t>(a_profile.method) > static_cast<std::uint32_t>(Method::kDLSS) ||
			static_cast<std::uint32_t>(a_profile.qualityMode) > static_cast<std::uint32_t>(QualityMode::kUltraPerformance) ||
			static_cast<std::uint32_t>(a_profile.dlssProfile) > static_cast<std::uint32_t>(DLSSProfile::kE) ||
			static_cast<std::uint32_t>(a_profile.fsrRuntime) > static_cast<std::uint32_t>(FSRRuntime::kFSR4)) {
			return Status::kUnsupportedProfile;
		}
		if (a_profile.renderScaleMode != 0 &&
			(a_profile.method != Method::kFSR && a_profile.method != Method::kDLSS)) {
			return Status::kUnsupportedProfile;
		}
		if (a_profile.renderScaleMode != 0 && a_profile.qualityMode == QualityMode::kNativeAA)
			return Status::kUnsupportedProfile;
		return Status::kSuccess;
	}

	UpscalingAPI::Status ValidateUpscalingPreflightRequest(const UpscalingAPI::PreflightRequest001& a_request) noexcept
	{
		using namespace UpscalingAPI;
		if (!HasCompleteStructure(a_request))
			return Status::kStructureTooSmall;
		if (!IsValidPurpose(a_request.purpose) || !IsValidPersistence(a_request.persistence))
			return Status::kInvalidArgument;
		return ValidateUpscalingProfile(a_request.target);
	}

	UpscalingAPI::Status ValidateUpscalingApplyRequest(const UpscalingAPI::ApplyRequest001& a_request) noexcept
	{
		using namespace UpscalingAPI;
		if (!HasCompleteStructure(a_request))
			return Status::kStructureTooSmall;
		if (!IsBoundedString(a_request.clientId, MaximumClientIdLength, true) ||
			!IsBoundedString(a_request.commandId, MaximumCommandIdLength, true) ||
			!IsBoundedString(a_request.reason, MaximumReasonLength, false)) {
			return Status::kInvalidArgument;
		}
		if (!IsValidPurpose(a_request.purpose) || !IsValidPersistence(a_request.persistence))
			return Status::kInvalidArgument;
		return ValidateUpscalingProfile(a_request.target);
	}

	bool IsTerminalUpscalingOperation(UpscalingAPI::OperationState a_state) noexcept
	{
		using enum UpscalingAPI::OperationState;
		return a_state == kCompleted || a_state == kFailed || a_state == kSuperseded;
	}
}
