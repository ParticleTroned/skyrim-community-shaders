#include "Features/Upscaling/FSRColorContractPolicy.h"
#include "Features/Upscaling/FSRColorContractReceiptPolicy.h"
#include "Features/Upscaling/VRSubmitColorContract.h"

#include <atomic>
#include <latch>
#include <mutex>
#include <thread>

namespace
{
	using namespace VRSubmitColorContract;

	constexpr bool CoversPresentationAndVendorAdmission()
	{
		const auto automatic = Resolve(true, SourceColorSpace::Automatic);
		const auto gamma = Resolve(true, SourceColorSpace::Gamma);
		const auto linear = Resolve(true, SourceColorSpace::Linear);
		if (automatic != gamma || !IsPresentationSupported(gamma) || !IsVendorSupported(gamma) || DLSSUsesHDR(gamma))
			return false;
		if (linear == gamma || !IsPresentationSupported(linear) || IsVendorSupported(linear) || DLSSUsesHDR(linear))
			return false;

		constexpr SourceColorSpace sources[]{ SourceColorSpace::Automatic, SourceColorSpace::Gamma, SourceColorSpace::Linear };
		for (const auto source : sources) {
			if (IsPresentationSupported(Resolve(false, source)) || IsVendorSupported(Resolve(false, source)))
				return false;
		}
		return !IsPresentationSupported(Resolve(true, SourceColorSpace::Unsupported)) &&
		       !IsVendorSupported(Resolve(true, static_cast<SourceColorSpace>(255)));
	}

	constexpr bool CoversIndependentRangeAndProcessing()
	{
		const Contract linearLDR{ Transfer::Linear, DynamicRange::LDR };
		const Contract linearHDR{ Transfer::Linear, DynamicRange::HDR };
		const Contract gammaHDR{ Transfer::Gamma, DynamicRange::HDR };
		return !DLSSUsesHDR(linearLDR) && DLSSUsesHDR(linearHDR) && DLSSUsesHDR(gammaHDR) &&
		       !IsPresentationSupported(linearHDR) && !IsVendorSupported(gammaHDR) &&
		       !IsPresentationSupported({}) && !IsVendorSupported({}) &&
		       kLegacyFsrHighDynamicRange;
	}

	constexpr bool CoversFsrProcessingControl()
	{
		using namespace FSRColorContractPolicy;
		const auto initial = Decode(kDefaultState);
		if (initial != Requested{} ||
			!ContextMatches(ContextState(kDefaultState), kDefaultState)) {
			return false;
		}

		const auto stale = PlanUpdate(kDefaultState, 0, false, false);
		if (stale.revisionMatched || stale.changed || stale.resultingRevision != 1)
			return false;
		const auto unchanged = PlanUpdate(kDefaultState, 1, true, true);
		if (!unchanged.revisionMatched || unchanged.changed ||
			unchanged.desiredState != kDefaultState || unchanged.resultingRevision != 1) {
			return false;
		}

		const auto changed = PlanUpdate(kDefaultState, 1, false, false);
		const auto decoded = Decode(changed.desiredState);
		if (!changed.revisionMatched || !changed.changed ||
			decoded.revision != 2 || decoded.highDynamicRangeInput || decoded.autoExposure) {
			return false;
		}
		const auto autoExposureOnly = PlanUpdate(changed.desiredState, 2, false, true);
		const auto autoExposureDecoded = Decode(autoExposureOnly.desiredState);
		if (!autoExposureOnly.revisionMatched || !autoExposureOnly.changed ||
			autoExposureDecoded.revision != 3 || autoExposureDecoded.highDynamicRangeInput ||
			!autoExposureDecoded.autoExposure) {
			return false;
		}
		constexpr std::uint32_t currentFrame = 17;
		if (GetReplacementState(
				ContextState(kDefaultState),
				0,
				0,
				0,
				kDefaultState,
				currentFrame) != ReplacementState::None ||
			GetReplacementState(
				ContextState(kDefaultState),
				currentFrame,
				0,
				0,
				changed.desiredState,
				currentFrame) != ReplacementState::Deferred ||
			GetReplacementState(
				ContextState(kDefaultState),
				currentFrame - 1,
				0,
				0,
				changed.desiredState,
				currentFrame) != ReplacementState::Ready ||
			GetReplacementState(
				ContextState(kDefaultState),
				currentFrame - 1,
				ContextState(kDefaultState),
				currentFrame,
				changed.desiredState,
				currentFrame) != ReplacementState::Deferred) {
			return false;
		}
		return !CanReuseContext(ContextState(kDefaultState), changed.desiredState, false) &&
		       CanReuseContext(ContextState(kDefaultState), changed.desiredState, true) &&
		       CanReuseContext(ContextState(changed.desiredState), changed.desiredState, false) &&
		       !CanReuseContext(ContextState(changed.desiredState), autoExposureOnly.desiredState, false);
	}

	struct ReceiptSnapshot
	{
		std::uint64_t requestState = 0;
		int dispatchMarker = 0;
	};

	class ContentionProbeMutex
	{
	public:
		void lock()
		{
			if (reportNextAttempt.exchange(false, std::memory_order_acq_rel))
				contenderAttempted.count_down();
			mutex.lock();
		}

		void unlock() { mutex.unlock(); }
		void ReportNextAttempt() { reportNextAttempt.store(true, std::memory_order_release); }
		void WaitForAttempt() { contenderAttempted.wait(); }

	private:
		std::mutex mutex;
		std::atomic_bool reportNextAttempt{ false };
		std::latch contenderAttempted{ 1 };
	};

	bool CoversConcurrentSetReceipt(bool a_firstAccepted)
	{
		using namespace FSRColorContractPolicy;
		using namespace FSRColorContractReceiptPolicy;

		ContentionProbeMutex mutex;
		const Requested initialRequest = a_firstAccepted ? Requested{} : Requested{ 2, false, false };
		std::atomic<std::uint64_t> requestState{ Pack(initialRequest) };
		int dispatchMarker = 7;
		std::latch firstSnapshotEntered{ 1 };
		std::latch releaseFirstSnapshot{ 1 };
		SetReceipt<ReceiptSnapshot> first{};
		SetReceipt<ReceiptSnapshot> second{};

		std::thread firstCaller([&]() {
			first = ApplySet<ReceiptSnapshot>(
				mutex,
				requestState,
				1,
				false,
				false,
				[&]() { dispatchMarker = 0; },
				[&]() {
					firstSnapshotEntered.count_down();
					releaseFirstSnapshot.wait();
					return ReceiptSnapshot{ requestState.load(std::memory_order_acquire), dispatchMarker };
				});
		});
		firstSnapshotEntered.wait();

		mutex.ReportNextAttempt();
		std::thread secondCaller([&]() {
			second = ApplySet<ReceiptSnapshot>(
				mutex,
				requestState,
				2,
				false,
				true,
				[&]() { dispatchMarker = 0; },
				[&]() { return ReceiptSnapshot{ requestState.load(std::memory_order_acquire), dispatchMarker }; });
		});
		mutex.WaitForAttempt();
		releaseFirstSnapshot.count_down();
		firstCaller.join();
		secondCaller.join();

		const auto firstState = Decode(first.status.requestState);
		const auto secondState = Decode(second.status.requestState);
		if (a_firstAccepted) {
			return first.accepted && first.resultingRevision == 2 &&
			       firstState.revision == 2 && !firstState.highDynamicRangeInput &&
			       !firstState.autoExposure && first.status.dispatchMarker == 0 &&
			       second.accepted && second.resultingRevision == 3 &&
			       secondState.revision == 3 && !secondState.highDynamicRangeInput &&
			       secondState.autoExposure;
		}

		return !first.accepted && first.resultingRevision == 2 &&
		       firstState == initialRequest && first.status.dispatchMarker == 7 &&
		       second.accepted && second.resultingRevision == 3 &&
		       secondState.revision == 3 && !secondState.highDynamicRangeInput &&
		       secondState.autoExposure;
	}

	static_assert(CoversPresentationAndVendorAdmission());
	static_assert(CoversIndependentRangeAndProcessing());
	static_assert(CoversFsrProcessingControl());
}

int main()
{
	return CoversPresentationAndVendorAdmission() && CoversIndependentRangeAndProcessing() &&
	               CoversFsrProcessingControl() && CoversConcurrentSetReceipt(true) &&
	               CoversConcurrentSetReceipt(false) ?
	           0 :
	           1;
}
