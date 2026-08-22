#include "Api/UpscalingContract.h"
#include "VRAPI/CSserviceapi.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace CSX::UpscalingAPI;

static_assert(std::is_standard_layout_v<Profile001>);
static_assert(std::is_standard_layout_v<Capabilities001>);
static_assert(std::is_standard_layout_v<Snapshot001>);
static_assert(std::is_standard_layout_v<PreflightRequest001>);
static_assert(std::is_standard_layout_v<PreflightResult001>);
static_assert(std::is_standard_layout_v<ApplyRequest001>);
static_assert(std::is_standard_layout_v<ApplyResult001>);
static_assert(std::is_standard_layout_v<OperationSnapshot001>);
static_assert(std::is_standard_layout_v<EventQuery001>);
static_assert(std::is_standard_layout_v<Event001>);
static_assert(std::is_standard_layout_v<EventPage001>);
static_assert(std::is_standard_layout_v<Interface001>);
static_assert(std::is_trivially_copyable_v<Profile001>);
static_assert(static_cast<std::uint32_t>(DLSSProfile::kE) == 5);
static_assert(static_cast<std::uint32_t>(QualityMode::kUltraPerformance) == 6);
static_assert(static_cast<std::uint32_t>(FSRRuntime::kFSR4) == 1);
static_assert(CSX::ServiceAPI::kCapabilityTransactions != 0);

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	Profile001 QualityDLSSProfile()
	{
		Profile001 profile;
		profile.method = Method::kDLSS;
		profile.qualityMode = QualityMode::kQuality;
		profile.renderScaleMode = 1;
		profile.dlssProfile = DLSSProfile::kK;
		profile.fsrRuntime = FSRRuntime::kFSR4;
		return profile;
	}

	Status FakeGetSnapshot(const void*, Snapshot001* a_output)
	{
		if (!a_output || a_output->structSize < sizeof(Snapshot001))
			return Status::kStructureTooSmall;
		a_output->stateRevision = 42;
		a_output->profilePresence = kProfileConfigured | kProfileEffective | kProfileStable;
		a_output->configured = QualityDLSSProfile();
		a_output->effective = a_output->configured;
		a_output->stable = a_output->configured;
		return Status::kSuccess;
	}
}

int RunTest()
{
	Profile001 profile = QualityDLSSProfile();
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kSuccess, "valid DLSS profile was rejected");

	// Inactive backend selections are deliberately preserved as complete profile state.
	profile.method = Method::kTAA;
	profile.renderScaleMode = 0;
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kSuccess, "inactive backend selections were rejected");

	profile.renderScaleMode = 1;
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kUnsupportedProfile, "TAA render-scale mode was accepted");
	profile.method = Method::kDLSS;
	profile.qualityMode = QualityMode::kNativeAA;
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kUnsupportedProfile, "native-quality render-scale mode was accepted");

	profile = QualityDLSSProfile();
	profile.dlssProfile = static_cast<DLSSProfile>(6);
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kUnsupportedProfile, "unknown DLSS profile was accepted");
	profile = QualityDLSSProfile();
	profile.structSize = sizeof(std::uint32_t);
	Check(CSX::Api::ValidateUpscalingProfile(profile) == Status::kStructureTooSmall, "undersized profile was accepted");

	PreflightRequest001 preflight;
	preflight.target = QualityDLSSProfile();
	Check(CSX::Api::ValidateUpscalingPreflightRequest(preflight) == Status::kSuccess, "valid preflight was rejected");
	preflight.purpose = static_cast<RequestPurpose>(99);
	Check(CSX::Api::ValidateUpscalingPreflightRequest(preflight) == Status::kInvalidArgument, "unknown purpose was accepted");

	ApplyRequest001 apply;
	apply.clientId = "test.client";
	apply.commandId = "command-1";
	apply.reason = "contract test";
	apply.target = QualityDLSSProfile();
	Check(CSX::Api::ValidateUpscalingApplyRequest(apply) == Status::kSuccess, "valid apply request was rejected");
	apply.commandId = "";
	Check(CSX::Api::ValidateUpscalingApplyRequest(apply) == Status::kInvalidArgument, "empty command id was accepted");

	Check(!CSX::Api::IsTerminalUpscalingOperation(OperationState::kStabilizing), "stabilizing was classified as terminal");
	Check(CSX::Api::IsTerminalUpscalingOperation(OperationState::kCompleted), "completed was not terminal");
	Check(CSX::Api::IsTerminalUpscalingOperation(OperationState::kFailed), "failed was not terminal");
	Check(CSX::Api::IsTerminalUpscalingOperation(OperationState::kSuperseded), "superseded was not terminal");

	Interface001 interface;
	interface.GetSnapshot = FakeGetSnapshot;
	Snapshot001 snapshot;
	Check(interface.GetSnapshot(interface.context, &snapshot) == Status::kSuccess, "function-table snapshot failed");
	Check(snapshot.stateRevision == 42 && snapshot.effective.method == Method::kDLSS, "function-table snapshot was incoherent");

	return 0;
}

int main()
{
	try {
		return RunTest();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
