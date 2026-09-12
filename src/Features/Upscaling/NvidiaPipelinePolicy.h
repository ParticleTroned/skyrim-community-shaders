#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace CSX::NvidiaPipelinePolicy
{
	inline constexpr std::uint32_t kRenderTargetOutputUsage = 0x20;

	enum class ProxyLifecycleState : std::uint8_t
	{
		Available,
		Constructing,
		Published,
		Retiring,
		TeardownQuarantined,
	};

	class ProxyLifecycleGate
	{
	public:
		[[nodiscard]] bool TryBeginConstruction() noexcept
		{
			std::scoped_lock lock(mutex);
			if (state != ProxyLifecycleState::Available)
				return false;
			state = ProxyLifecycleState::Constructing;
			return true;
		}

		[[nodiscard]] bool Publish(std::atomic_bool& a_active) noexcept
		{
			std::scoped_lock lock(mutex);
			if (state != ProxyLifecycleState::Constructing)
				return false;
			a_active.store(true, std::memory_order_release);
			state = ProxyLifecycleState::Published;
			return true;
		}

		[[nodiscard]] bool BeginRetirement() noexcept
		{
			std::scoped_lock lock(mutex);
			if (state != ProxyLifecycleState::Published)
				return false;
			state = ProxyLifecycleState::Retiring;
			return true;
		}

		void CompleteRetirement(bool a_teardownComplete, std::atomic_bool& a_active) noexcept
		{
			std::scoped_lock lock(mutex);
			if (state == ProxyLifecycleState::Retiring) {
				a_active.store(false, std::memory_order_release);
				state = a_teardownComplete ?
				            ProxyLifecycleState::Available :
				            ProxyLifecycleState::TeardownQuarantined;
			}
		}

		void CancelConstruction(bool a_teardownComplete, std::atomic_bool& a_active) noexcept
		{
			std::scoped_lock lock(mutex);
			if (state == ProxyLifecycleState::Constructing) {
				a_active.store(false, std::memory_order_release);
				state = a_teardownComplete ?
				            ProxyLifecycleState::Available :
				            ProxyLifecycleState::TeardownQuarantined;
			}
		}

		[[nodiscard]] ProxyLifecycleState GetState() const noexcept
		{
			std::scoped_lock lock(mutex);
			return state;
		}

	private:
		mutable std::mutex mutex;
		ProxyLifecycleState state = ProxyLifecycleState::Available;
	};

	struct PublicSwapChainContract
	{
		std::uint32_t bufferCount = 0;
		std::uint32_t sampleCount = 0;
		std::uint32_t sampleQuality = 0;
		std::uint32_t format = 0;
		bool windowed = false;
		bool hasOutputWindow = false;
		std::uint32_t usage = 0;
	};

	[[nodiscard]] constexpr bool IsPublicSwapChainContractSupported(
		const PublicSwapChainContract& a_contract) noexcept
	{
		return a_contract.bufferCount == 1 &&
		       a_contract.sampleCount == 1 &&
		       a_contract.sampleQuality == 0 &&
		       a_contract.format != 0 &&
		       a_contract.windowed &&
		       a_contract.hasOutputWindow &&
		       a_contract.usage == kRenderTargetOutputUsage;
	}

	enum class PresentResultDisposition : std::uint8_t
	{
		Presented,
		Retryable,
		Fatal,
	};

	[[nodiscard]] constexpr PresentResultDisposition ClassifyPresentResult(
		bool a_failed,
		bool a_retryable) noexcept
	{
		if (!a_failed)
			return PresentResultDisposition::Presented;
		return a_retryable ? PresentResultDisposition::Retryable : PresentResultDisposition::Fatal;
	}

	enum class BoundedCopyResult : std::uint8_t
	{
		Complete,
		Truncated,
		Unreadable,
	};

	[[nodiscard]] constexpr BoundedCopyResult ClassifyBoundedCopy(
		bool a_readable,
		bool a_terminatedWithinLimit) noexcept
	{
		if (!a_readable)
			return BoundedCopyResult::Unreadable;
		return a_terminatedWithinLimit ?
		           BoundedCopyResult::Complete :
		           BoundedCopyResult::Truncated;
	}

	class InteropFenceSequence
	{
	public:
		[[nodiscard]] std::optional<std::uint64_t> Next() noexcept
		{
			if (value == (std::numeric_limits<std::uint64_t>::max)())
				return std::nullopt;
			return ++value;
		}

		void Reset() noexcept { value = 0; }

	private:
		std::uint64_t value = 0;
	};

	[[nodiscard]] constexpr std::optional<std::uint32_t> ResolveBackendBufferCount(
		std::uint32_t a_requestedPublicCount,
		std::uint32_t a_currentPublicCount) noexcept
	{
		const auto publicCount = a_requestedPublicCount ?
		                             a_requestedPublicCount :
		                             a_currentPublicCount;
		return publicCount == 1 ? std::optional<std::uint32_t>{ 2 } : std::nullopt;
	}

	[[nodiscard]] constexpr bool CanContinueBasePresentation(
		bool a_providerCallSucceeded,
		bool a_vendorDisableConfirmed) noexcept
	{
		return a_providerCallSucceeded || a_vendorDisableConfirmed;
	}

	[[nodiscard]] constexpr bool MustRetainModuleAfterShutdown(
		bool a_shutdownWasRequired,
		bool a_shutdownSucceeded) noexcept
	{
		return a_shutdownWasRequired && !a_shutdownSucceeded;
	}

	struct OptionalRuntimeAvailability
	{
		bool core = false;
		bool dlss = false;
		bool reflex = false;
		bool pcl = false;
	};

	[[nodiscard]] constexpr OptionalRuntimeAvailability ResolveRuntimeAvailability(
		bool a_coreFilesValid,
		bool a_dlssFilesValid,
		bool a_reflexFilesValid,
		bool a_pclFilesValid) noexcept
	{
		return {
			.core = a_coreFilesValid,
			.dlss = a_coreFilesValid && a_dlssFilesValid,
			.reflex = a_coreFilesValid && a_reflexFilesValid,
			.pcl = a_coreFilesValid && a_pclFilesValid,
		};
	}
}
