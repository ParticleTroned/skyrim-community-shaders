#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	enum class RuntimeStatus
	{
		NotProbed,
		Initialized,
		FeatureCreateFailed,
		FeatureReleaseFailed,
		UnsafeAbandoned
	};

	enum class RuntimeFailureStage
	{
		None,
		FeatureCreate,
		FeatureRelease,
		UnsafeAbandon
	};

	struct ParameterResetFailure
	{};

	struct NVSDK_NGX_Parameter
	{
		std::uint32_t resetCalls = 0;
		std::function<void()> resetAction;

		void Reset()
		{
			++resetCalls;
			if (resetAction)
				resetAction();
		}
	};

	struct Runtime
	{
		static constexpr std::uint32_t kFeatureSlotCount = 4;
		static constexpr std::uint32_t kInjectedResult = 0xBADF00D;
		std::mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::Initialized;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::array<int, 3> backendTokens_{};
		NVSDK_NGX_Parameter parameterObject_;
		void* device_ = &backendTokens_[0];
		void* module_ = &backendTokens_[1];
		void* coreModule_ = &backendTokens_[2];
		void* parameters_ = &parameterObject_;
		std::uint64_t successfulFrames_ = 23;
		std::string detail_ = "initialized";
		std::uint32_t ngxResult_ = 1;
		std::array<bool, kFeatureSlotCount> resident_{ true, true, true, true };
		std::vector<std::uint32_t> nativeReleases_;
		std::uint32_t failSlot_ = kFeatureSlotCount;
		std::uint32_t abandonAfterSlot_ = kFeatureSlotCount;

		bool ResetFeaturesLocked(bool a_stopOnFailure = false);
		bool ResetFeatures();
		bool ReleasePassResources();

		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage,
			std::string detail, std::uint32_t result = 0)
		{
			status_ = status;
			failureStage_ = stage;
			detail_ = std::move(detail);
			ngxResult_ = result;
		}

		bool ResetFeatureLocked(std::uint32_t slot)
		{
			if (abandonRequested_.load() || abandoned_)
				return false;
			if (!resident_.at(slot))
				return true;
			nativeReleases_.push_back(slot);
			if (slot == failSlot_) {
				SetFailureLocked(RuntimeStatus::FeatureReleaseFailed, RuntimeFailureStage::FeatureRelease,
					"injected native release failure", kInjectedResult);
				return false;
			}
			resident_[slot] = false;
			status_ = RuntimeStatus::Initialized;
			failureStage_ = RuntimeFailureStage::None;
			ngxResult_ = 1;
			if (slot == abandonAfterSlot_)
				abandonRequested_.store(true);
			return true;
		}

		void RequireBackendRetained() const
		{
			Require(device_ == &backendTokens_[0] && module_ == &backendTokens_[1] &&
						coreModule_ == &backendTokens_[2] && parameters_ == &parameterObject_,
				"Warm retirement released initialized backend ownership");
		}
	};

#include "neural_runtime_retirement_under_test.h"

	void CheckWarmSuccess()
	{
		Runtime runtime;
		runtime.parameterObject_.resetAction = [&] {
			Require(std::none_of(runtime.resident_.begin(), runtime.resident_.end(), [](bool resident) { return resident; }),
				"Parameter bindings were cleared before all native features retired");
		};
		Require(runtime.ReleasePassResources(), "Healthy warm retirement failed");
		Require(runtime.nativeReleases_ == std::vector<std::uint32_t>{ 0, 1, 2, 3 }, "Warm retirement missed a native feature");
		Require(runtime.parameterObject_.resetCalls == 1 && runtime.successfulFrames_ == 0, "Warm retirement retained bindings or success history");
		Require(runtime.status_ == RuntimeStatus::Initialized && runtime.failureStage_ == RuntimeFailureStage::None,
			"Warm retirement left the backend unusable");
		runtime.RequireBackendRetained();
		Require(runtime.ReleasePassResources(), "Repeated empty warm retirement failed");
		Require(runtime.nativeReleases_.size() == Runtime::kFeatureSlotCount && runtime.parameterObject_.resetCalls == 2,
			"Empty retirement released a feature twice or skipped clearing bindings");
	}

	void CheckReleaseFailure()
	{
		for (std::uint32_t failed = 0; failed < Runtime::kFeatureSlotCount; ++failed) {
			Runtime runtime;
			runtime.failSlot_ = failed;
			Require(!runtime.ReleasePassResources(), "Native release failure allowed warm reuse");
			Require(runtime.nativeReleases_.size() == failed + 1 && runtime.nativeReleases_.back() == failed,
				"Warm retirement continued after the first failed release");
			Require(runtime.parameterObject_.resetCalls == 0 && runtime.successfulFrames_ == 23,
				"Failed retirement cleared bindings or success history");
			Require(runtime.status_ == RuntimeStatus::FeatureReleaseFailed && runtime.failureStage_ == RuntimeFailureStage::FeatureRelease &&
						runtime.ngxResult_ == Runtime::kInjectedResult && runtime.detail_ == "injected native release failure",
				"Warm retirement overwrote the first native failure");
			for (std::uint32_t slot = 0; slot < Runtime::kFeatureSlotCount; ++slot)
				Require(runtime.resident_[slot] == (slot >= failed), "Failed retirement lost ownership of an unreleased feature");
			runtime.RequireBackendRetained();
			Require(!runtime.ReleasePassResources() && runtime.nativeReleases_.size() == failed + 1,
				"A failed backend was reused by a later warm release");
		}
	}

	void CheckRejectedState()
	{
		for (unsigned condition = 0; condition < 5; ++condition) {
			Runtime runtime;
			switch (condition) {
			case 0:
				runtime.status_ = RuntimeStatus::NotProbed;
				break;
			case 1:
				runtime.status_ = RuntimeStatus::FeatureCreateFailed;
				break;
			case 2:
				runtime.failureStage_ = RuntimeFailureStage::FeatureCreate;
				break;
			case 3:
				runtime.abandonRequested_.store(true);
				break;
			case 4:
				runtime.abandoned_ = true;
				break;
			}
			const auto status = runtime.status_;
			const auto stage = runtime.failureStage_;
			Require(!runtime.ReleasePassResources(), "Uninitialized or unsafe state allowed warm retirement");
			Require(runtime.nativeReleases_.empty() && runtime.parameterObject_.resetCalls == 0,
				"Rejected warm retirement accessed native ownership");
			Require(runtime.status_ == status && runtime.failureStage_ == stage, "Rejected state lost its original failure evidence");
			runtime.RequireBackendRetained();
		}
		for (const auto member : { &Runtime::device_, &Runtime::module_, &Runtime::coreModule_, &Runtime::parameters_ }) {
			Runtime runtime;
			runtime.*member = nullptr;
			Require(!runtime.ReleasePassResources(), "Incomplete backend allowed warm retirement");
			Require(runtime.nativeReleases_.empty() && runtime.parameterObject_.resetCalls == 0,
				"Incomplete backend was accessed by warm retirement");
			Require(runtime.status_ == RuntimeStatus::FeatureReleaseFailed && runtime.failureStage_ == RuntimeFailureStage::FeatureRelease,
				"Incomplete backend did not report a release failure");
		}
	}

	void CheckAbandonDuringRelease()
	{
		for (const auto slot : { 0u, Runtime::kFeatureSlotCount - 1 }) {
			Runtime runtime;
			runtime.abandonAfterSlot_ = slot;
			Require(!runtime.ReleasePassResources(), "Abandonment during release allowed warm reuse");
			Require(runtime.nativeReleases_.size() == slot + 1 && runtime.parameterObject_.resetCalls == 0,
				"Abandonment did not stop native access before parameter reset");
			runtime.RequireBackendRetained();
		}
	}

	void CheckParameterException()
	{
		Runtime runtime;
		runtime.parameterObject_.resetAction = [] { throw ParameterResetFailure{}; };
		bool propagated = false;
		try {
			(void)runtime.ReleasePassResources();
		} catch (const ParameterResetFailure&) {
			propagated = true;
		}
		Require(propagated && runtime.parameterObject_.resetCalls == 1, "Parameter reset failure was swallowed");
		Require(runtime.nativeReleases_.size() == Runtime::kFeatureSlotCount && runtime.detail_ == "initialized",
			"Parameter reset exception skipped release or published false success");
		runtime.RequireBackendRetained();
	}

	void CheckColdResetStillVisitsAllSlots()
	{
		Runtime runtime;
		runtime.failSlot_ = 1;
		Require(!runtime.ResetFeatures(), "Cold reset concealed a native failure");
		Require(runtime.nativeReleases_.size() == Runtime::kFeatureSlotCount && runtime.resident_[1] && !runtime.resident_[3],
			"Warm fail-fast behavior changed cold reset cleanup");
		Require(runtime.parameterObject_.resetCalls == 0, "Cold feature reset unexpectedly cleared parameter bindings");
	}
}

int main()
{
	try {
		CheckWarmSuccess();
		CheckReleaseFailure();
		CheckRejectedState();
		CheckAbandonDuringRelease();
		CheckParameterException();
		CheckColdResetStillVisitsAllSlots();
		std::cout << "Neural Runtime retirement tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
