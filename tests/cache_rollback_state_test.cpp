#include "Utils/CacheRollbackState.h"

#include <atomic>
#include <barrier>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	using Store = Util::CacheRollbackState;
	using Mismatch = Util::CacheInvalidation::CacheMismatch;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	Store::Snapshot MakeHeldState()
	{
		return {
			.flags = { .diskCacheHeld = true, .featureSetChanged = true },
			.cacheMismatches = { { Mismatch::Kind::EnabledFlip, "Water", "Water", "held", true } },
		};
	}

	Store::Snapshot MakeBackupState()
	{
		return {
			.flags = {
				.featureSetChanged = true,
				.featureSetCacheBackedUp = true,
				.featureSetCacheSelectivelySeeded = true,
				.previousDiskCacheAvailable = true,
			},
			.cacheMismatches = { { Mismatch::Kind::EnabledFlip, "Snow", "Snow", "current", false } },
			.previousCacheMismatches = { { Mismatch::Kind::EnabledFlip, "Snow", "Snow", "previous", false } },
		};
	}

	bool IsCoherent(const Store::Snapshot& a_state)
	{
		static const auto heldFlags = MakeHeldState().flags;
		static const auto backupFlags = MakeBackupState().flags;
		if (a_state.flags == heldFlags) {
			return a_state.cacheMismatches.size() == 1 &&
			       a_state.cacheMismatches[0].shortName == "Water" &&
			       a_state.cacheMismatches[0].detail == "held" &&
			       a_state.previousCacheMismatches.empty();
		}
		if (a_state.flags == backupFlags) {
			return a_state.cacheMismatches.size() == 1 &&
			       a_state.cacheMismatches[0].shortName == "Snow" &&
			       a_state.cacheMismatches[0].detail == "current" &&
			       a_state.previousCacheMismatches.size() == 1 &&
			       a_state.previousCacheMismatches[0].shortName == "Snow" &&
			       a_state.previousCacheMismatches[0].detail == "previous";
		}
		return false;
	}
}

int main()
try {
	Store store;
	Require(store.GetFlags() == Store::Flags{}, "initial flags must permit the normal cache path");
	Require(store.GetSnapshot().previousCacheMismatches.empty(), "initial restore details must be empty");

	auto backup = MakeBackupState();
	store.Publish(backup);
	Require(store.GetFlags().AllowsDiskCache(), "a completed backup must permit writes to the active cache");
	auto retained = store.GetSnapshot();
	backup.previousCacheMismatches.clear();
	Require(IsCoherent(store.GetSnapshot()), "published details must not alias the caller's data");
	store.Publish(MakeHeldState());
	Require(!store.GetFlags().AllowsDiskCache(), "a held cache must block both disk reads and writes");
	Require(IsCoherent(retained) && retained.flags.previousDiskCacheAvailable,
		"a UI snapshot must retain its old details across later publications");
	retained.cacheMismatches.clear();
	Require(IsCoherent(store.GetSnapshot()), "editing an owned snapshot must not change shared state");

	Store::Snapshot restored;
	restored.flags.featureSetRevertPending = true;
	store.Publish(restored);
	Require(!store.GetFlags().AllowsDiskCache(), "restore must block disk access until restart");
	Require(store.GetFlags() == restored.flags && store.GetSnapshot().flags == restored.flags,
		"restore must publish the restart/write gate to both reader paths");
	store.Publish({});
	const auto cleared = store.GetSnapshot();
	Require(store.GetFlags().AllowsDiskCache() && store.GetFlags() == Store::Flags{} &&
				cleared.flags == Store::Flags{} &&
				cleared.cacheMismatches.empty() && cleared.previousCacheMismatches.empty(),
		"cache deletion must clear flags and both detail lists together");

	const auto held = MakeHeldState();
	const auto seeded = MakeBackupState();
	store.Publish(held);
	std::barrier start(3);
	std::atomic_bool coherent{ true };
	std::jthread writer([&] {
		start.arrive_and_wait();
		for (unsigned i = 0; i < 20000; ++i)
			store.Publish((i & 1) ? held : seeded);
	});
	std::jthread flagReader([&] {
		start.arrive_and_wait();
		for (unsigned i = 0; i < 20000; ++i) {
			const auto flags = store.GetFlags();
			if (flags != held.flags && flags != seeded.flags)
				coherent.store(false);
		}
	});
	start.arrive_and_wait();
	for (unsigned i = 0; i < 20000; ++i) {
		if (!IsCoherent(store.GetSnapshot()))
			coherent.store(false);
	}
	writer.join();
	flagReader.join();
	Require(coherent.load(), "concurrent readers must never observe mixed flags or mismatch generations");
	std::cout << "Rollback publication, owned snapshots and concurrency checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
