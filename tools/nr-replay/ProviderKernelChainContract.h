#pragma once

#include "ProviderFloorContract.h"

#include <cstddef>
#include <string_view>

namespace NrReplay::KernelChain
{
	inline constexpr std::size_t kCacheRva = 0x1157d08;
	inline constexpr unsigned kInterfaceId = 0x24973538;
	inline constexpr std::size_t kCreateModuleCacheRva = 0x1157cd8;
	inline constexpr unsigned kCreateModuleInterfaceId = 0xad1a677d;
	inline constexpr std::size_t kCreateFunctionCacheRva = 0x1157ce8;
	inline constexpr unsigned kCreateFunctionInterfaceId = 0xe2436e22;
	inline constexpr unsigned kDestroyModuleInterfaceId = 0x41c65285;
	inline constexpr unsigned kDestroyFunctionInterfaceId = 0xdf295ea6;
	inline constexpr char kReplacementSchema[] = "nr-n1-kernel-replacement-v1";
	inline constexpr char kReplacementOriginalModuleSha256[] = "53d6eabda1f533a7106e3385deaad3664ba458e15a1e580f48bc54603f6a736c";
	inline constexpr char kReplacementEntry[] = "cc_tinlayout_fused_swin_1h_32_1_inpview_tilesync_fp8";
	inline constexpr char kReplacementCandidateSha256[] = "0dce6033814b77b9798ef20f9981ba22055553a8287667328ef41b6ee76b9315";
	inline constexpr char kPairReplacementSchema[] = "nr-pair-kernel-replacement-v1";
	inline constexpr char kPairCandidateSha256[] = "a7193ca94af0b5ef33da0e2cf0ee503c6e63babc813273a43a042822e3ed72b9";
	inline constexpr char kIndexedCandidateSha256[] = "13ffb20e31779e38ca35e24403fe2609e52c13bdc1ff76028a95506186daa9a9";
	inline constexpr char kIndexedPairCandidateSha256[] = "20de0a9113261692fbae9727cb24ca8a760fe5cd9d6937bfe00708127937d6af";
	inline constexpr char kIndexedScheduledCandidateSha256[] = "f6e83e14db1e8a033b050302e7661646f79d774962469f46688c58ba9525d9cb";
	inline constexpr char kIndexedScheduledPairCandidateSha256[] = "b2721d77b0ea1fee6982e2e6ca596f1826c3ceb65aab56f9128a4e11ff534a9d";
	inline constexpr std::size_t kReplacementParameterBytes = 96;
	inline constexpr std::size_t kMaximumModuleBytes = 64 * 1024 * 1024;
	inline constexpr std::size_t kMaximumCapturedModuleBytes = 256 * 1024 * 1024;
	inline constexpr std::size_t kMaximumModules = 64;
	inline constexpr std::size_t kMaximumFunctions = 4096;
	inline constexpr std::size_t kMaximumFunctionNameBytes = 1024;
	inline constexpr std::size_t kMaximumDescriptors = 64;
	inline constexpr std::size_t kMaximumParameterBytes = 4096;
	inline constexpr std::size_t kMaximumSampleDescriptors = 32768;
	inline constexpr std::size_t kMaximumSampleBytes = 64 * 1024 * 1024;
	inline constexpr std::size_t kMaximumSampleEvents = 131072;

	/** Pins reconstructed and indexed candidates to their exact parameter ABI. */
	inline bool ReplacementCandidateAdmitted(std::string_view hash, bool pair) noexcept
	{
		return pair ? hash == kPairCandidateSha256 : hash == kReplacementCandidateSha256 || hash == kIndexedCandidateSha256;
	}

	/** Bounds identity capture independently of launch timing and descriptor storage. */
	inline void ValidateModuleBudget(std::size_t bytes, std::size_t count, std::size_t capturedBytes)
	{
		ProviderFloor::Require(bytes && bytes <= kMaximumModuleBytes && count < kMaximumModules &&
								   capturedBytes <= kMaximumCapturedModuleBytes - bytes,
			"kernel-chain module capture budget exceeded");
	}

	/** Admits only bounded descriptor captures with complete owned parameter bytes. */
	inline void ValidateDescriptorBudget(std::size_t count, std::size_t capturedCount, std::size_t bytes, std::size_t capturedBytes)
	{
		ProviderFloor::Require(count && count <= kMaximumDescriptors && capturedCount <= kMaximumSampleDescriptors - count,
			"kernel-chain descriptor capture budget exceeded");
		ProviderFloor::Require(bytes && bytes <= kMaximumParameterBytes && capturedBytes <= kMaximumSampleBytes - bytes,
			"kernel-chain parameter capture budget exceeded");
	}
	inline bool GroupMode(std::string_view mode)
	{
		ProviderFloor::Require(mode == "forward" || mode == "group", "kernel-chain mode must be forward or group");
		return mode == "group";
	}
	struct ExchangeResult
	{
		bool succeeded = false;
		bool rollbackProven = false;
		bool protectionRestored = false;
		void* observed = nullptr;
	};
	/** Tracks readback and rollback so an uncertain cache slot never loses its module owners. */
	template <class Operations>
	ExchangeResult ExchangePointer(Operations& operations, void* expected, void* replacement)
	{
		if (!operations.MakeWritable())
			return { false, operations.Read() == expected && operations.ProtectionMatches(), operations.ProtectionMatches(), operations.Read() };
		const bool exchanged = operations.CompareExchange(replacement, expected) == expected;
		bool restored = operations.RestoreProtection();
		const bool restoredInitially = restored;
		if (!restored) {
			if (exchanged)
				(void)operations.CompareExchange(expected, replacement);
			restored = operations.RestoreProtection();
		}
		const auto observed = operations.Read();
		return { exchanged && restoredInitially && observed == replacement, restored && observed == expected, restored, observed };
	}
}
