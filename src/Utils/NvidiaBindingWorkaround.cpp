#include "Utils/NvidiaBindingWorkaround.h"

#ifdef CSX_NVIDIA_BINDING_WORKAROUND
#	include "Utils/NvidiaBindingWorkaroundPolicy.h"
#	include "Utils/CryptoHash.h"
#	include "Utils/VirtualFunctionHook.h"
#	include <Windows.h>
#	include <TlHelp32.h>
#	include <detours/detours.h>
#	include <winrt/base.h>
#	include <array>
#	include <atomic>
#	include <cstring>
#	include <memory>
#	include <span>
#	include <type_traits>
#	include <utility>
#	include <vector>

namespace Util::NvidiaBindingWorkaround
{
	namespace
	{
		using Policy::MemoryState;
		using NativeGrowth = std::int32_t (*)(std::uintptr_t, std::uintptr_t, std::uint64_t);
		constexpr unsigned Refresh = 1, Locked = 2, Stopped = 4;
		struct State
		{
			NativeGrowth first{}, second{};
			const DriverProfile* profile{};
			std::atomic_bool initialized{}, installed{};
			std::atomic_uint mode{ Refresh };
			std::atomic<const char*> reason{ "awaiting_device" };
			std::atomic<const char*> failure{};
			std::atomic<const char*> lastSkipped{ "none" };
			std::atomic_uint64_t candidates{}, applied{}, rejected{};
			std::atomic_uint64_t skipped{};
			std::atomic_uint64_t loggingFailures{};
			std::atomic_long installationError{};
		};
		State& Get()
		{
			static State state;
			return state;
		}
		bool Stop(const char* reason) noexcept
		{
			auto& state = Get();
			if (!state.initialized.load(std::memory_order_acquire))
				return false;
			const char* empty{};
			const bool first = state.failure.compare_exchange_strong(empty, reason, std::memory_order_acq_rel);
			state.mode.fetch_or(Stopped, std::memory_order_acq_rel);
			return first;
		}
		thread_local bool inside{};
		struct PreserveError
		{
			DWORD value = GetLastError();
			~PreserveError() { SetLastError(value); }
		};
		struct ReentryScope
		{
			ReentryScope() { inside = true; }
			~ReentryScope() { inside = false; }
		};
		bool Copy(std::uintptr_t address, void* destination, std::size_t size) noexcept
		{
			if (!address || size > UINTPTR_MAX - address)
				return false;
			__try {
				std::memcpy(destination, reinterpret_cast<const void*>(address), size);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
							EXCEPTION_EXECUTE_HANDLER :
							EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}
		struct Reader
		{
			bool valid = true;
			template <class T>
			T At(std::uintptr_t base, std::size_t offset = 0) noexcept
			{
				T result{};
				if (!base || offset > UINTPTR_MAX - base || !Copy(base + offset, &result, sizeof(result)))
					valid = false;
				return result;
			}
		};
		MemoryState ReadState(std::uintptr_t manager, std::uintptr_t context) noexcept
		{
			Reader r;
			return Policy::ReadState(manager, context, Get().profile->context, r);
		}
		const char* TargetRejection(const MemoryState& before) noexcept
		{
			if (!before.readable || !before.manager || !before.context || !before.wrapper || !before.object)
				return "target_unreadable_or_missing";
			if (!before.handle)
				return "target_handle_missing";
			if (before.primaryManager != before.manager)
				return "target_primary_manager_mismatch";
			if (before.secondManager != before.manager)
				return "target_second_manager_mismatch";
			if (before.capacity != Policy::OldManagedCapacity)
				return "target_capacity_mismatch";
			if (before.bytes != Policy::OldAllocationBytes)
				return "target_allocation_size_mismatch";
			if (!before.address || before.address > (1ull << 40) - before.bytes)
				return "target_address_invalid";
			if (before.firstCache != before.capacity)
				return "target_first_cache_mismatch";
			if (before.secondCache != before.capacity)
				return "target_second_cache_mismatch";
			return nullptr;
		}
		void RecordRefresh(const MemoryState& before, const MemoryState& after, std::uint64_t request, std::uint64_t refreshCount) noexcept
		try {
			logger::info("[NvidiaBindingWorkaround] event=refreshed driver={} refreshCount={} request={} oldGpuVa=0x{:X} oldBytes={} newGpuVa=0x{:X} newBytes={} secondBefore={} second={} normalRetirement=true shader=unavailable gpuExecutionVerified=false",
				Get().profile->version, refreshCount, request, before.address, before.bytes, after.address, after.bytes, before.secondCache, after.secondCache);
		} catch (...) {
			// A diagnostic failure must not stop later binding corrections.
			Get().loggingFailures.fetch_add(1, std::memory_order_relaxed);
		}
		void Reject(const char* reason) noexcept
		{
			++Get().rejected;
			Stop(reason);
		}
		std::int32_t Grow(std::uintptr_t manager, std::uintptr_t context, std::uint64_t request)
		{
			auto& state = Get();
			if (request <= Policy::OldManagedCapacity || request > Policy::ReplacementBytes ||
				!state.installed.load(std::memory_order_acquire) || (state.mode.load(std::memory_order_relaxed) & Stopped) || inside)
				return state.first(manager, context, request);
			const auto entryError = GetLastError();
			Reader reader;
			const auto capacity = reader.At<std::uint64_t>(manager, 0x100);
			if (!reader.valid || capacity != Policy::OldManagedCapacity) {
				SetLastError(entryError);
				return state.first(manager, context, request);
			}
			const auto before = ReadState(manager, context);
			if (const auto reason = TargetRejection(before)) {
				ReentryScope guard;
				SetLastError(entryError);
				const auto result = state.first(manager, context, request);
				PreserveError preserve;
				state.lastSkipped = reason;
				++state.skipped;
				return result;
			}
			const auto mode = state.mode.fetch_or(Locked, std::memory_order_acq_rel);
			ReentryScope guard;
			SetLastError(entryError);
			const auto result = state.first(manager, context, request);
			PreserveError preserve;
			++state.candidates;
			if (result < 0) {
				Reject("growth_failed");
				return result;
			}
			const auto after = ReadState(manager, context);
			if (const auto reason = Policy::RefreshRejection(before, after, request)) {
				auto stale = after;
				stale.secondCache = before.secondCache;
				if (Policy::CanRefresh(before, stale, request) && after.secondCache == after.capacity) {
					state.reason = "already_current";
				} else {
					Reject(reason);
				}
				return result;
			}
			if (state.mode.load(std::memory_order_acquire) & Stopped) {
				return result;
			}
			if (!(mode & Refresh)) {
				state.reason = "control_observed";
				return result;
			}
			// Stay on the native caller's thread; snapshots only reject observable changes.
			const auto current = ReadState(manager, context);
			if (current != after) {
				const auto changed = Policy::AllocationChange(after, current);
				Reject(changed ? changed : current.firstCache != after.firstCache ? "first_cache_changed" :
																					"second_cache_changed");
				return result;
			}
			const auto refreshed = state.second(manager, context, request);
			const auto verified = ReadState(manager, context);
			if (refreshed < 0 || !Policy::Refreshed(after, verified)) {
				const auto changed = Policy::AllocationChange(after, verified);
				Reject(refreshed < 0 ? "native_refresh_failed" : changed                             ? changed :
															 verified.firstCache != after.firstCache ? "first_cache_changed" :
																									   "second_cache_not_updated");
				return result;
			}
			state.reason = "refreshed";
			const auto refreshCount = state.applied.fetch_add(1, std::memory_order_relaxed) + 1;
			RecordRefresh(before, verified, request, refreshCount);
			return result;
		}

#	include "Utils/NvidiaBindingWorkaroundInstall.inl"
	}

	void Disable(const char* reason) noexcept
	{
		Stop(reason);
	}
	bool Select(bool refresh) noexcept
	{
		auto& state = Get();
		if (!state.installed.load(std::memory_order_acquire))
			return false;
		auto mode = state.mode.load(std::memory_order_acquire);
		while (!(mode & (Locked | Stopped))) {
			if (state.mode.compare_exchange_weak(mode, refresh ? Refresh : 0, std::memory_order_acq_rel))
				return true;
		}
		return false;
	}
	Snapshot Status() noexcept
	{
		auto& state = Get();
		const auto mode = state.mode.load(std::memory_order_acquire);
		const auto failure = state.failure.load(std::memory_order_acquire);
		const auto installed = state.installed.load(std::memory_order_acquire);
		return { true, installed, installed && !(mode & Stopped), (mode & Refresh) != 0, (mode & Locked) != 0,
			failure ? failure : state.reason.load(), state.candidates.load(), state.applied.load(), state.rejected.load(),
			state.skipped.load(), state.lastSkipped.load(), false, state.loggingFailures.load(), state.installationError.load() };
	}
	void Initialize() noexcept
	try {
		auto& state = Get();
		if (state.initialized.exchange(true))
			return;
		if (const auto error = Install(); error != NO_ERROR) {
			state.installationError.store(error, std::memory_order_relaxed);
			Disable("driver_verification_or_install_failed");
		} else {
			state.reason = "armed";
			state.installed.store(true, std::memory_order_release);
		}
	} catch (...) {
		Disable("initialization_exception");
	}
}
#endif
