#include "Features/Upscaling/NvidiaBoundedLog.h"
#include "Features/Upscaling/NvidiaComIdentity.h"
#include "Features/Upscaling/NvidiaPipelinePolicy.h"
#include "Features/Upscaling/StreamlineFrameTokenPublication.h"

#include <array>
#include <atomic>
#include <future>
#include <optional>
#include <thread>

namespace
{
	struct Token
	{
		std::uint32_t value = 0;
	};

	using Coordinator =
		StreamlineFrameTokenPublication::Coordinator<Token*>;

	class MockUnknown final : public IUnknown
	{
	public:
		explicit MockUnknown(MockUnknown* a_identity = nullptr) :
			identity(a_identity ? a_identity : this)
		{}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_riid, void** a_object) override
		{
			if (!a_object)
				return E_POINTER;
			*a_object = nullptr;
			if (a_riid != IID_IUnknown)
				return E_NOINTERFACE;
			identity->AddRef();
			*a_object = static_cast<IUnknown*>(identity);
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override { return --references; }

	private:
		MockUnknown* identity;
		std::atomic_ulong references{ 1 };
	};

	bool TestConcurrentPublication()
	{
		Coordinator coordinator;
		Token previous{ 1 };
		Token current{ 2 };
		Token unexpected{ 99 };
		std::atomic_uint32_t acquisitions = 0;

		auto firstFrame = coordinator.Resolve(
			1,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &previous;
			});
		if (!firstFrame || firstFrame->token != &previous || !firstFrame->acquired)
			return false;

		std::promise<void> acquisitionEntered;
		std::promise<void> allowPublication;
		auto allowPublicationFuture = allowPublication.get_future();
		std::optional<Coordinator::Snapshot> firstCaller;
		std::optional<Coordinator::Snapshot> secondCaller;

		std::thread first([&]() {
			firstCaller = coordinator.Resolve(
				2,
				[&](std::uint32_t) -> std::optional<Token*> {
					++acquisitions;
					acquisitionEntered.set_value();
					allowPublicationFuture.wait();
					return &current;
				});
		});

		acquisitionEntered.get_future().wait();
		std::thread second([&]() {
			secondCaller = coordinator.Resolve(
				2,
				[&](std::uint32_t) -> std::optional<Token*> {
					++acquisitions;
					return &unexpected;
				});
		});

		allowPublication.set_value();
		first.join();
		second.join();

		return acquisitions == 2 &&
		       firstCaller && firstCaller->frame == 2 &&
		       firstCaller->token == &current && firstCaller->acquired &&
		       secondCaller && secondCaller->frame == 2 &&
		       secondCaller->token == &current && !secondCaller->acquired;
	}

	bool TestFailureAndReset()
	{
		Coordinator coordinator;
		Token current{ 7 };
		std::atomic_uint32_t acquisitions = 0;

		auto failed = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return std::nullopt;
			});
		if (failed)
			return false;

		auto recovered = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &current;
			});
		if (!recovered || recovered->token != &current || !recovered->acquired)
			return false;

		coordinator.Reset();
		auto reacquired = coordinator.Resolve(
			7,
			[&](std::uint32_t) -> std::optional<Token*> {
				++acquisitions;
				return &current;
			});
		return reacquired && reacquired->acquired && acquisitions == 3;
	}

	bool TestStaleFrameCannotReplacePublication()
	{
		Coordinator coordinator;
		Token frameN{ 10 };
		Token frameNPlusOne{ 11 };
		Token duplicateFrameN{ 99 };
		std::atomic_uint32_t acquisitions = 0;

		auto first = coordinator.Resolve(10, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &frameN;
		});
		auto next = coordinator.Resolve(11, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &frameNPlusOne;
		});
		auto stale = coordinator.Resolve(10, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &duplicateFrameN;
		});

		return first && next && !stale && acquisitions == 2;
	}

	bool TestFrameCounterWrapRemainsMonotonic()
	{
		Coordinator coordinator;
		Token beforeWrap{ UINT32_MAX };
		Token afterWrap{ 0 };
		std::atomic_uint32_t acquisitions = 0;

		auto first = coordinator.Resolve(UINT32_MAX, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &beforeWrap;
		});
		auto wrapped = coordinator.Resolve(0, [&](std::uint32_t) -> std::optional<Token*> {
			++acquisitions;
			return &afterWrap;
		});

		return first && wrapped && wrapped->token == &afterWrap && acquisitions == 2;
	}

	bool TestOptionalPipelinePolicies()
	{
		using namespace CSX::NvidiaPipelinePolicy;

		InteropFenceSequence fences;
		const auto firstProducer = fences.Next();
		const auto firstConsumer = fences.Next();
		if (!firstProducer || *firstProducer != 1u ||
			!firstConsumer || *firstConsumer != 2u)
			return false;
		fences.Reset();
		if (fences.Next() != 1u)
			return false;

		if (ResolveBackendBufferCount(0u, 1u) != 2u ||
			ResolveBackendBufferCount(1u, 1u) != 2u ||
			ResolveBackendBufferCount(2u, 1u))
			return false;

		if (!CanContinueBasePresentation(true, false) ||
			!CanContinueBasePresentation(false, true) ||
			CanContinueBasePresentation(false, false))
			return false;

		if (MustRetainModuleAfterShutdown(false, false) ||
			MustRetainModuleAfterShutdown(true, true) ||
			!MustRetainModuleAfterShutdown(true, false))
			return false;

		const auto dlssOnly = ResolveRuntimeAvailability(true, true, false, false);
		const auto reflexOnly = ResolveRuntimeAvailability(true, false, true, false);
		const auto noCore = ResolveRuntimeAvailability(false, true, true, true);
		return dlssOnly.core && dlssOnly.dlss && !dlssOnly.reflex && !dlssOnly.pcl &&
		       reflexOnly.core && !reflexOnly.dlss && reflexOnly.reflex && !reflexOnly.pcl &&
		       !noCore.core && !noCore.dlss && !noCore.reflex && !noCore.pcl;
	}

	bool TestProxyLifecycleAndPublicContracts()
	{
		using namespace CSX::NvidiaPipelinePolicy;

		std::atomic_bool quarantinedActive{ false };
		ProxyLifecycleGate quarantined;
		if (!quarantined.TryBeginConstruction())
			return false;
		auto competingConstruction = std::async(std::launch::async, [&]() {
			return quarantined.TryBeginConstruction();
		});
		if (competingConstruction.get() || !quarantined.Publish(quarantinedActive) ||
			!quarantined.BeginRetirement())
			return false;
		auto duringRetirement = std::async(std::launch::async, [&]() {
			return quarantined.TryBeginConstruction();
		});
		quarantined.CompleteRetirement(false, quarantinedActive);
		if (duringRetirement.get() ||
			quarantinedActive.load(std::memory_order_acquire) ||
			quarantined.GetState() != ProxyLifecycleState::TeardownQuarantined ||
			quarantined.TryBeginConstruction())
			return false;

		std::atomic_bool reusableActive{ false };
		ProxyLifecycleGate reusable;
		if (!reusable.TryBeginConstruction() || !reusable.Publish(reusableActive) ||
			!reusable.BeginRetirement())
			return false;
		reusable.CompleteRetirement(true, reusableActive);
		if (reusable.GetState() != ProxyLifecycleState::Available ||
			reusableActive.load(std::memory_order_acquire) ||
			!reusable.TryBeginConstruction())
			return false;
		reusable.CancelConstruction(true, reusableActive);
		ProxyLifecycleGate failedCandidate;
		std::atomic_bool failedCandidateActive{ false };
		if (!failedCandidate.TryBeginConstruction())
			return false;
		failedCandidate.CancelConstruction(false, failedCandidateActive);
		if (failedCandidate.GetState() != ProxyLifecycleState::TeardownQuarantined ||
			failedCandidate.TryBeginConstruction())
			return false;

		const PublicSwapChainContract supported{
			.bufferCount = 1,
			.sampleCount = 1,
			.sampleQuality = 0,
			.format = 28,
			.windowed = true,
			.hasOutputWindow = true,
			.usage = kRenderTargetOutputUsage,
		};
		auto unsupported = supported;
		unsupported.sampleCount = 4;
		auto unsupportedUsage = supported;
		unsupportedUsage.usage |= 0x10;
		if (!IsPublicSwapChainContractSupported(supported) ||
			IsPublicSwapChainContractSupported(unsupported) ||
			IsPublicSwapChainContractSupported(unsupportedUsage))
			return false;

		if (ClassifyPresentResult(false, false) != PresentResultDisposition::Presented ||
			ClassifyPresentResult(true, true) != PresentResultDisposition::Retryable ||
			ClassifyPresentResult(true, false) != PresentResultDisposition::Fatal)
			return false;

		return ClassifyBoundedCopy(true, true) == BoundedCopyResult::Complete &&
		       ClassifyBoundedCopy(true, false) == BoundedCopyResult::Truncated &&
		       ClassifyBoundedCopy(false, false) == BoundedCopyResult::Unreadable;
	}

	bool TestComIdentityPolicy()
	{
		MockUnknown firstIdentity;
		MockUnknown firstView(&firstIdentity);
		MockUnknown secondIdentity;
		return CSX::NvidiaComIdentity::IsSame(&firstIdentity, &firstView) &&
		       !CSX::NvidiaComIdentity::IsSame(&firstIdentity, &secondIdentity) &&
		       !CSX::NvidiaComIdentity::IsSame(&firstIdentity, nullptr);
	}

	bool TestBoundedLogCopy()
	{
		using CSX::NvidiaPipelinePolicy::BoundedCopyResult;
		std::array<char, 5> destination{};
		if (CSX::NvidiaBoundedLog::Copy(nullptr, destination.data(), destination.size()) !=
				BoundedCopyResult::Unreadable ||
			CSX::NvidiaBoundedLog::Copy("", destination.data(), destination.size()) !=
				BoundedCopyResult::Complete ||
			destination[0] != '\0' ||
			CSX::NvidiaBoundedLog::Copy("1234", destination.data(), destination.size()) !=
				BoundedCopyResult::Complete ||
			std::string_view(destination.data()) != "1234" ||
			CSX::NvidiaBoundedLog::Copy("12345", destination.data(), destination.size()) !=
				BoundedCopyResult::Truncated)
			return false;

		const std::array<char, 5> nonterminated{ 'a', 'b', 'c', 'd', 'e' };
		if (CSX::NvidiaBoundedLog::Copy(nonterminated.data(), destination.data(), destination.size()) !=
			BoundedCopyResult::Truncated)
			return false;
#ifdef _MSC_VER
		if (CSX::NvidiaBoundedLog::Copy(
				reinterpret_cast<const char*>(static_cast<std::uintptr_t>(1)),
				destination.data(),
				destination.size()) != BoundedCopyResult::Unreadable)
			return false;
#endif
		return true;
	}
}

int main()
{
	return TestConcurrentPublication() &&
	               TestFailureAndReset() &&
	               TestStaleFrameCannotReplacePublication() &&
	               TestFrameCounterWrapRemainsMonotonic() &&
	               TestOptionalPipelinePolicies() &&
	               TestProxyLifecycleAndPublicContracts() &&
	               TestComIdentityPolicy() &&
	               TestBoundedLogCopy() ?
	           0 :
	           1;
}
