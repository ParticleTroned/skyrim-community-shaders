#include "VRLoadingMenuClear.h"

#include "Globals.h"

#include <atomic>

namespace Util::VRLoadingMenuClear
{
	namespace
	{
		std::atomic<Installation> installation{ Installation::NotInstalled };
		std::atomic_uint64_t attempted{}, executed{}, deferred{}, missingRenderer{};
		using NativeClear = void (*)();
		NativeClear originalClear = nullptr;

		void ClearFromLoadingMessage()
		{
			attempted.fetch_add(1, std::memory_order_relaxed);
			auto* const renderer = globals::game::renderer;
			static_assert(sizeof(REX::W32::CRITICAL_SECTION) == sizeof(CRITICAL_SECTION));
			static_assert(alignof(REX::W32::CRITICAL_SECTION) == alignof(CRITICAL_SECTION));
			auto* const lock = renderer ? reinterpret_cast<CRITICAL_SECTION*>(&renderer->GetLock()) : nullptr;
			const auto result = TryExecute(lock, []() {
				originalClear();
				executed.fetch_add(1, std::memory_order_relaxed);
			});
			if (result == Result::Deferred)
				deferred.fetch_add(1, std::memory_order_relaxed);
			else if (result == Result::MissingRenderer)
				missingRenderer.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void Install()
	{
		if (installation.load(std::memory_order_acquire) == Installation::Installed)
			return;
		const bool isVR = REL::Module::IsVR();
		const bool supportedRuntime = isVR && REL::Module::get().version() == SKSE::RUNTIME_VR_1_4_15;
		if (!isVR || !supportedRuntime) {
			installation.store(*ValidateAdmission(isVR, supportedRuntime, {}, {}), std::memory_order_release);
			if (isVR)
				logger::error("[VR loading clear] Unsupported runtime {}; renderer ownership guard not installed", REL::Module::get().version().string());
			return;
		}

		const auto messageCall = REL::Offset(kMessageCallRva).address();
		const auto renderRetryCall = REL::Offset(kRenderRetryCallRva).address();
		const auto rejected = ValidateAdmission(isVR, supportedRuntime,
			{ reinterpret_cast<const std::uint8_t*>(messageCall), kMessageCall.size() },
			{ reinterpret_cast<const std::uint8_t*>(renderRetryCall), kRenderRetryCall.size() });
		if (rejected) {
			installation.store(*rejected, std::memory_order_release);
			const bool messageRejected = *rejected == Installation::UnexpectedMessageCall;
			logger::error("[VR loading clear] Unexpected {} call at SkyrimVR+0x{:x}; renderer ownership guard not installed",
				messageRejected ? "loading-message" : "render-side retry", messageRejected ? kMessageCallRva : kRenderRetryCallRva);
			return;
		}

		originalClear = reinterpret_cast<NativeClear>(REL::Offset(kNativeClearRva).address());
		// The render-side clear consumes the same pending flags. A loading message
		// must not wait for its renderer owner while holding unrelated UI locks.
		SKSE::GetTrampoline().write_call<5>(messageCall, &ClearFromLoadingMessage);
		installation.store(Installation::Installed, std::memory_order_release);
		logger::info("[VR loading clear] Renderer ownership guard installed at SkyrimVR+0x{:x}; contended clears remain pending for native rendering", kMessageCallRva);
	}

	Status GetStatus() noexcept
	{
		auto result = DescribeInstallation(installation.load(std::memory_order_acquire));
		result.attempted = attempted.load(std::memory_order_relaxed);
		result.executed = executed.load(std::memory_order_relaxed);
		result.deferred = deferred.load(std::memory_order_relaxed);
		result.missingRenderer = missingRenderer.load(std::memory_order_relaxed);
		return result;
	}
}
