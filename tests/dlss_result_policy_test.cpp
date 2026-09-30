#include "Features/Upscaling/DLSSResultPolicy.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void CheckEvaluationResults()
	{
		using DLSSResultPolicy::IsEvaluationSuccessful;
		using sl::Result;
		Require(IsEvaluationSuccessful(Result::eOk), "Ordinary success rejected");
		Require(IsEvaluationSuccessful(Result::eWarnOutOfVRAM), "Valid output rejected after budget warning");
		for (int code = static_cast<int>(Result::eErrorIO); code <= static_cast<int>(Result::eErrorInvalidState); ++code)
			Require(!IsEvaluationSuccessful(static_cast<Result>(code)), "SDK error accepted as valid output");
		Require(!IsEvaluationSuccessful(static_cast<Result>(-1)), "Unknown negative result accepted");
		Require(!IsEvaluationSuccessful(static_cast<Result>(static_cast<int>(Result::eWarnOutOfVRAM) + 1)), "Unknown future result accepted");
	}

	void CheckWarningCadence()
	{
		DLSSResultPolicy::BudgetWarningThrottle firstInstance, secondInstance;
		Require(firstInstance.ShouldLog(0, 0), "First frame-zero warning suppressed");
		Require(!firstInstance.ShouldLog(0, 0), "Duplicate same-eye warning logged");
		Require(firstInstance.ShouldLog(1, 0), "Left-eye warning suppressed the right eye");
		Require(!firstInstance.ShouldLog(99, 0), "Out-of-range eye escaped bounded cadence");
		Require(secondInstance.ShouldLog(0, 0), "One instance suppressed another instance");
		Require(!firstInstance.ShouldLog(0, 299), "Warning repeated before interval");
		Require(firstInstance.ShouldLog(0, 300), "Warning did not repeat at interval");
		Require(firstInstance.ShouldLog(1, 300), "One eye consumed the other eye's repeat");
		firstInstance.Reset();
		Require(firstInstance.ShouldLog(0, 300), "Reinitialization retained left-eye cadence");
		Require(firstInstance.ShouldLog(1, 300), "Reinitialization retained right-eye cadence");
	}

	void CheckFrameWrap()
	{
		DLSSResultPolicy::BudgetWarningThrottle throttle;
		constexpr auto lastFrame = std::numeric_limits<uint32_t>::max();
		Require(throttle.ShouldLog(0, lastFrame), "First warning at UINT32_MAX suppressed");
		Require(!throttle.ShouldLog(0, lastFrame), "UINT32_MAX treated as an uninitialized sentinel");
		Require(!throttle.ShouldLog(0, 0), "Counter wrap bypassed interval");
		Require(!throttle.ShouldLog(0, 298), "Wrap interval ended one frame early");
		Require(throttle.ShouldLog(0, 299), "Warning did not repeat across counter wrap");
	}
}

int main()
{
	try {
		CheckEvaluationResults();
		CheckWarningCadence();
		CheckFrameWrap();
		std::cout << "DLSS result and warning policy checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
