#pragma once

#include "ProviderFloorContract.h"
#include "Utils/CryptoHash.h"

#include <Psapi.h>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>

namespace NrReplay
{
	/** Own-process-only provider experiment; restoration requires an explicit GPU-idle proof. */
	class ProviderFloorProbe
	{
	public:
		explicit ProviderFloorProbe(nlohmann::json& receipt) : receipt_(receipt) {}
		ProviderFloorProbe(const ProviderFloorProbe&) = delete;
		ProviderFloorProbe& operator=(const ProviderFloorProbe&) = delete;
		~ProviderFloorProbe() noexcept
		{
			if (modified_)
				Abandon();
			else if (module_)
				FreeLibrary(module_);
		}

		/** Applies only before native feature creation, after runtime initialization. */
		void Apply(const std::filesystem::path& provider, std::string_view diskHash)
		{
			using namespace ProviderFloor;
			Require(!module_ && !modified_, "provider floor probe cannot apply twice without restoration");
			const auto providerHash = CanonicalSha256(diskHash);
			receipt_["transitions"].push_back({ { "state", "validating_loaded_provider" }, { "providerDiskSha256", providerHash } });
			auto& record = receipt_["transitions"].back();
			try {
				Require(GetModuleHandleExW(0, provider.c_str(), &module_) != 0, "provider floor loaded module is unavailable");
				std::vector<wchar_t> path(32768);
				const auto length = GetModuleFileNameW(module_, path.data(), static_cast<DWORD>(path.size()));
				Require(length && length < path.size() && std::filesystem::equivalent(provider, std::filesystem::path(path.data())),
					"provider floor loaded module path differs from admitted provider");
				MODULEINFO info{};
				Require(GetModuleInformation(GetCurrentProcess(), module_, &info, sizeof(info)) != 0 && info.lpBaseOfDll == module_,
					"provider floor loaded module bounds unavailable");
				base_ = static_cast<std::uint8_t*>(info.lpBaseOfDll);
				moduleBytes_ = info.SizeOfImage;
				ValidatePages(kCodeRva, kCodeBytes);
				const auto codeHash = Hash({ base_ + kCodeRva, kCodeBytes });
				ValidateIdentity(256, providerHash, codeHash);
				ValidateGuard(kPatchRva, { base_ + kGuardRva, kGuard.size() });
				record.update({ { "providerPath", provider.string() }, { "moduleBase", reinterpret_cast<std::uintptr_t>(base_) },
					{ "moduleSize", moduleBytes_ }, { "originalCodeSha256", codeHash }, { "patchRva", kPatchRva },
					{ "guardRva", kGuardRva }, { "guardBytes", kGuard }, { "originalBytes", kOriginal }, { "replacementBytes", kReplacement } });
				Write(kReplacement);
				record["experimentalCodeSha256"] = Hash({ base_ + kCodeRva, kCodeBytes });
				record["state"] = "applied_own_replay_process_only";
				receipt_["inMemoryState"] = "experimental_floor_256";
			} catch (const std::exception& error) {
				record["state"] = "apply_failed";
				record["reason"] = error.what();
				// No feature exists yet, so a partial application can be retired immediately.
				(void)Restore(true);
				throw;
			}
		}

		/** Restores after all native work is idle; failure retains the module until process exit. */
		bool Restore(bool gpuIdle) noexcept
		{
			using namespace ProviderFloor;
			if (!module_)
				return true;
			if (!gpuIdle) {
				Abandon();
				return false;
			}
			try {
				if (modified_) {
					Write(kOriginal);
					Require(Hash({ base_ + kCodeRva, kCodeBytes }) == kCodeSha256, "restored provider code hash mismatch");
					modified_ = false;
					receipt_["transitions"].push_back({ { "state", "restored" }, { "gpuIdleProven", true },
						{ "restoredCodeSha256", kCodeSha256 }, { "originalProtectionRestored", true } });
					receipt_["inMemoryState"] = "original_provider_code";
				}
				Require(FreeLibrary(module_) != 0, "provider floor module reference release failed");
				module_ = nullptr;
				return true;
			} catch (const std::exception& error) {
				try {
					receipt_["transitions"].push_back({ { "state", "restore_failed" }, { "reason", error.what() }, { "gpuIdleProven", true } });
					receipt_["inMemoryState"] = "restoration_failed_module_retained_until_process_exit";
				} catch (...) {
					std::fputs("Provider floor restoration evidence failed\n", stderr);
				}
				module_ = nullptr;
				modified_ = false;
				return false;
			}
		}

	private:
		nlohmann::json& receipt_;
		HMODULE module_ = nullptr;
		std::uint8_t* base_ = nullptr;
		std::size_t moduleBytes_ = 0;
		bool modified_ = false;
		DWORD originalProtection_ = 0;
		static std::string Hash(std::span<const std::uint8_t> bytes)
		{
			return ProviderFloor::CanonicalSha256(Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(bytes))));
		}
		void Abandon() noexcept
		{
			try {
				receipt_["inMemoryState"] = "restoration_unproven_module_retained_until_process_exit";
				receipt_["transitions"].push_back({ { "state", "restore_not_attempted" }, { "gpuIdleProven", false } });
			} catch (...) {
				std::fputs("Provider floor abandonment evidence failed\n", stderr);
			}
			module_ = nullptr;
			modified_ = false;
		}
		void ValidatePages(std::size_t rva, std::size_t length)
		{
			using namespace ProviderFloor;
			ValidateMemory(moduleBytes_, rva, length, true, true, true, false);
			for (std::size_t offset = rva; offset < rva + length;) {
				MEMORY_BASIC_INFORMATION memory{};
				Require(VirtualQuery(base_ + offset, &memory, sizeof(memory)) == sizeof(memory), "provider floor VirtualQuery failed");
				ValidateMemory(moduleBytes_, offset, rva + length - offset, memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE,
					memory.AllocationBase == base_, memory.Protect == PAGE_EXECUTE_READ, (memory.Protect & PAGE_GUARD) != 0);
				const auto end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
				Require(end > reinterpret_cast<std::uintptr_t>(base_ + offset), "provider floor memory region does not advance");
				offset = static_cast<std::size_t>(end - reinterpret_cast<std::uintptr_t>(base_));
			}
		}
		void Write(std::span<const std::uint8_t, 5> bytes)
		{
			using namespace ProviderFloor;
			auto* address = base_ + kPatchRva;
			DWORD previous = 0;
			Require(VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &previous) != 0, "provider floor writable protection failed");
			if (!originalProtection_)
				originalProtection_ = previous;
			modified_ = true;
			std::memcpy(address, bytes.data(), bytes.size());
			DWORD replaced = 0;
			const bool protectedAgain = VirtualProtect(address, bytes.size(), originalProtection_, &replaced) != 0;
			const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, bytes.size()) != 0;
			Require(protectedAgain && flushed && std::ranges::equal(std::span(address, bytes.size()), bytes),
				"provider floor write verification, instruction flush or protection restoration failed");
			ValidatePages(kPatchRva, bytes.size());
		}
	};
}
