#include "Features/Upscaling/DLSSFrameEvaluationPolicy.h"

namespace
{
	using namespace DLSSFrameEvaluationPolicy;

	constexpr EvaluationContract BaseContract()
	{
		EvaluationContract contract{};
		contract.sessionEpoch = 7;
		contract.frame = 100;
		contract.frameToken = 42;
		contract.viewport = 4;
		contract.eyeIndex = 1;
		contract.viewportRole = 2;
		contract.renderedInputGeneration = 12;
		contract.resourceGeneration = 3;
		contract.outputWidth = 2016;
		contract.outputHeight = 2240;
		contract.resources.colorInput = 0x100;
		contract.resources.colorOutput = 0x200;
		contract.constants.cameraFOV = 0x3F800000;
		return contract;
	}

	constexpr bool CoversFirstAdmission()
	{
		const auto contract = BaseContract();
		return ResolveAdmission(nullptr, contract, false, false) == Admission::Submit;
	}

	constexpr bool CoversExactReuseProof()
	{
		const auto contract = BaseContract();
		TicketState ticket{
			.valid = true,
			.constantsAttempted = true,
			.constantsSubmitted = true,
			.evaluationAttempted = true,
			.evaluationCompleted = true,
			.evaluationSucceeded = true,
			.outputReusable = true,
			.contract = contract,
		};
		auto notReusable = ticket;
		notReusable.outputReusable = false;
		return ResolveAdmission(&ticket, contract, true, true) == Admission::ReuseCompletedOutput &&
		       ResolveAdmission(&notReusable, contract, true, true) == Admission::RejectOutputUnavailable &&
		       ResolveAdmission(&ticket, contract, false, true) == Admission::RejectOutputUnavailable &&
		       ResolveAdmission(&ticket, contract, true, false) == Admission::RejectOutputUnavailable;
	}

	constexpr bool CoversIncompleteAndFailedAttempts()
	{
		const auto contract = BaseContract();
		TicketState incomplete{ .valid = true, .constantsAttempted = true, .contract = contract };
		TicketState malformedComplete{
			.valid = true,
			.evaluationCompleted = true,
			.evaluationSucceeded = true,
			.outputReusable = true,
			.contract = contract,
		};
		TicketState failed{
			.valid = true,
			.constantsAttempted = true,
			.constantsSubmitted = true,
			.evaluationAttempted = true,
			.evaluationCompleted = true,
			.evaluationSucceeded = false,
			.outputReusable = true,
			.contract = contract,
		};
		return ResolveAdmission(&incomplete, contract, true, true) == Admission::RejectIncompleteAttempt &&
		       ResolveAdmission(&malformedComplete, contract, true, true) == Admission::RejectIncompleteAttempt &&
		       ResolveAdmission(&failed, contract, true, true) == Admission::RejectOutputUnavailable;
	}

	constexpr bool CoversSameTokenConflicts()
	{
		const auto contract = BaseContract();
		TicketState ticket{ .valid = true, .contract = contract };

		auto changedInput = contract;
		++changedInput.renderedInputGeneration;
		auto changedResource = contract;
		++changedResource.resourceGeneration;
		auto changedConstants = contract;
		++changedConstants.constants.cameraFOV;
		auto changedMatrix = contract;
		++changedMatrix.constants.cameraViewToClip[0];
		auto changedBoolean = contract;
		++changedBoolean.constants.motionVectorsJittered;
		auto changedLateConstant = contract;
		++changedLateConstant.constants.minRelativeLinearDepthObjectSeparation;
		auto changedOutput = contract;
		++changedOutput.resources.colorOutput;
		auto changedEye = contract;
		changedEye.eyeIndex ^= 1u;
		auto changedRole = contract;
		++changedRole.viewportRole;

		return ResolveAdmission(&ticket, changedInput, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedResource, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedConstants, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedMatrix, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedBoolean, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedLateConstant, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedOutput, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedEye, true, true) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, changedRole, true, true) == Admission::RejectTemporalConflict;
	}

	constexpr bool CoversIndependentTemporalTuples()
	{
		const auto contract = BaseContract();
		TicketState ticket{ .valid = true, .contract = contract };

		auto nextToken = contract;
		++nextToken.frameToken;
		auto changedFrame = contract;
		++changedFrame.frame;
		auto nextFrameToken = changedFrame;
		++nextFrameToken.frameToken;
		auto otherViewport = contract;
		++otherViewport.viewport;
		auto nextSession = contract;
		++nextSession.sessionEpoch;

		return ResolveAdmission(&ticket, nextToken, false, false) == Admission::Submit &&
		       ResolveAdmission(&ticket, changedFrame, false, false) == Admission::RejectTemporalConflict &&
		       ResolveAdmission(&ticket, nextFrameToken, false, false) == Admission::Submit &&
		       ResolveAdmission(&ticket, otherViewport, false, false) == Admission::Submit &&
		       ResolveAdmission(&ticket, nextSession, false, false) == Admission::Submit;
	}

	static_assert(CoversFirstAdmission());
	static_assert(CoversExactReuseProof());
	static_assert(CoversIncompleteAndFailedAttempts());
	static_assert(CoversSameTokenConflicts());
	static_assert(CoversIndependentTemporalTuples());
}

int main() {}
