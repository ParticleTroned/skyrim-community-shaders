#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace CSX::NvidiaPipelinePolicy
{
	class InteropFenceSequence
	{
	public:
		[[nodiscard]] std::optional<std::uint64_t> Next() noexcept
		{
			if (value == std::numeric_limits<std::uint64_t>::max())
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
