#pragma once

#include "Utils/NvidiaBindingWorkaroundProfiles.h"
#include <cstdint>

namespace Util::NvidiaBindingWorkaround::Policy
{
	inline constexpr std::uint64_t OldAllocationBytes = 46399488, OldManagedCapacity = 46350336;
	inline constexpr std::uint64_t ReplacementBytes = 69ull * 1024 * 1024;
	struct MemoryState
	{
		std::uintptr_t manager{}, context{}, wrapper{}, object{}, primaryManager{}, secondManager{};
		std::uint64_t capacity{}, address{}, bytes{}, firstCache{}, secondCache{};
		std::uint32_t handle{};
		bool readable{};
		bool operator==(const MemoryState&) const = default;
	};
	/** Reads the pinned layout through the caller's guarded reader. */
	template <class Reader>
	MemoryState ReadState(std::uintptr_t manager, std::uintptr_t context, const ContextLayout& layout, Reader& reader) noexcept
	{
		MemoryState state;
		state.manager = manager;
		state.context = context;
		state.wrapper = reader.template At<std::uintptr_t>(manager);
		state.object = reader.template At<std::uintptr_t>(state.wrapper);
		state.capacity = reader.template At<std::uint64_t>(manager, 0x100);
		state.address = reader.template At<std::uint64_t>(state.object, 0x48);
		state.bytes = reader.template At<std::uint64_t>(state.object, 0x40);
		state.handle = reader.template At<std::uint32_t>(state.object, 0x90);
		state.firstCache = reader.template At<std::uint64_t>(context, layout.firstCache);
		state.secondCache = reader.template At<std::uint64_t>(context, layout.secondCache);
		state.primaryManager = reader.template At<std::uintptr_t>(context, layout.primaryManager);
		state.secondManager = reader.template At<std::uintptr_t>(context, layout.secondManager);
		state.readable = reader.valid;
		return state;
	}
	/** Returns the first failed replacement guard, or null when refresh is eligible. */
	inline const char* RefreshRejection(const MemoryState& before, const MemoryState& after, std::uint64_t request) noexcept
	{
		if (!before.readable)
			return "before_unreadable";
		if (!after.readable)
			return "after_unreadable";
		if (!before.manager || before.manager != after.manager)
			return "manager_changed_or_missing";
		if (!before.context || before.context != after.context)
			return "context_changed_or_missing";
		if (before.primaryManager != before.manager)
			return "before_primary_manager_mismatch";
		if (before.secondManager != before.manager)
			return "before_second_manager_mismatch";
		if (after.primaryManager != after.manager)
			return "after_primary_manager_mismatch";
		if (after.secondManager != after.manager)
			return "after_second_manager_mismatch";
		if (before.bytes != OldAllocationBytes)
			return "old_allocation_size_mismatch";
		if (before.capacity != OldManagedCapacity)
			return "old_capacity_mismatch";
		if (!before.wrapper)
			return "old_wrapper_missing";
		if (!before.object)
			return "old_object_missing";
		if (!before.address)
			return "old_address_missing";
		if (!after.wrapper)
			return "new_wrapper_missing";
		if (!after.object)
			return "new_object_missing";
		if (!after.handle)
			return "new_handle_missing";
		if (!after.address || after.address > (1ull << 40) - ReplacementBytes)
			return "new_address_invalid";
		if (before.wrapper == after.wrapper)
			return "wrapper_not_replaced";
		if (before.object == after.object)
			return "object_not_replaced";
		if (before.address == after.address)
			return "address_not_replaced";
		if (request <= before.capacity)
			return "request_not_growth";
		if (after.capacity < request)
			return "request_not_satisfied";
		if (after.capacity != ReplacementBytes)
			return "new_capacity_mismatch";
		if (after.bytes != ReplacementBytes)
			return "new_allocation_size_mismatch";
		if (after.firstCache != after.capacity)
			return "first_cache_not_current";
		if (after.secondCache != before.secondCache)
			return "second_cache_changed";
		if (after.secondCache >= after.capacity)
			return "second_cache_not_stale";
		return nullptr;
	}
	/** Requires an actual replacement on the same manager/context and an already-satisfied request. */
	inline bool CanRefresh(const MemoryState& before, const MemoryState& after, std::uint64_t request) noexcept
	{
		return !RefreshRejection(before, after, request);
	}
	/** Identifies the first changed identity field without dereferencing either snapshot. */
	inline const char* AllocationChange(const MemoryState& before, const MemoryState& after) noexcept
	{
		if (!before.readable || !after.readable)
			return "state_unreadable";
		if (before.manager != after.manager)
			return "manager_changed";
		if (before.context != after.context)
			return "context_changed";
		if (before.primaryManager != after.primaryManager)
			return "primary_manager_changed";
		if (before.secondManager != after.secondManager)
			return "second_manager_changed";
		if (before.wrapper != after.wrapper)
			return "wrapper_changed";
		if (before.object != after.object)
			return "object_changed";
		if (before.handle != after.handle)
			return "handle_changed";
		if (before.address != after.address)
			return "address_changed";
		if (before.bytes != after.bytes)
			return "allocation_size_changed";
		if (before.capacity != after.capacity)
			return "capacity_changed";
		return nullptr;
	}
	inline bool SameAllocation(const MemoryState& before, const MemoryState& after) noexcept
	{
		return !AllocationChange(before, after);
	}
	inline bool Refreshed(const MemoryState& before, const MemoryState& after) noexcept
	{
		return SameAllocation(before, after) && before.firstCache == after.firstCache && after.secondCache == before.capacity;
	}
}
