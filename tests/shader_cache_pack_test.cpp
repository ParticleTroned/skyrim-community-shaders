#include "Utils/ShaderCachePack.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>

using namespace Util::ShaderCachePack;

namespace
{
	Entry MakeEntry(std::string a_logical, std::string a_exact, std::uint8_t a_value, std::size_t a_size = 64)
	{
		Entry entry{ std::move(a_logical), std::move(a_exact), "{\"schema\":1}", {} };
		entry.bytecode.assign(a_size, static_cast<std::byte>(a_value));
		return entry;
	}

	std::filesystem::path ActivePack(const std::filesystem::path& a_first, const std::filesystem::path& a_second)
	{
		return std::filesystem::file_size(a_first) >= std::filesystem::file_size(a_second) ? a_first : a_second;
	}

	template <class T>
	void Overwrite(const std::filesystem::path& a_path, std::uint64_t a_offset, const T& a_value)
	{
		std::fstream stream(a_path, std::ios::binary | std::ios::in | std::ios::out);
		assert(stream);
		stream.seekp(static_cast<std::streamoff>(a_offset));
		stream.write(reinterpret_cast<const char*>(std::addressof(a_value)), sizeof(a_value));
		stream.flush();
		assert(stream);
	}
}

int main(int argc, char** argv)
{
	static_assert(!ShouldReadLooseBlob(false, false));
	static_assert(!ShouldReadLooseBlob(false, true));
	static_assert(ShouldReadLooseBlob(true, false));
	static_assert(!ShouldReadLooseBlob(true, true));

	if (argc == 4) {
		std::string error;
		Store external(argv[1], argv[2], Lane::Optimized);
		assert(external.Open(&error));
		const auto record = external.Find(argv[3], &error);
		assert(record && !record->bytecode.empty());
		return 0;
	}
	assert(argc == 1);
	const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto root = std::filesystem::temp_directory_path() / ("csx-pack-test-" + unique);
	std::filesystem::create_directories(root);

	// A live index must not trust payload bytes that changed after Open().
	{
		const auto mutationRoot = root / "mutation";
		std::filesystem::create_directories(mutationRoot);
		const auto first = mutationRoot / "Optimized.A.csxpack";
		const auto second = mutationRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string mutationError;
		Store mutation(first, second, Lane::Optimized);
		assert(mutation.Open(&mutationError));
		assert(mutation.Append(MakeEntry("logical", "exact", 0x71), &mutationError));
		assert(mutation.Checkpoint(&mutationError));
		const char changed = 'X';
		Overwrite(ActivePack(first, second), 160, changed);
		mutationError.clear();
		assert(!mutation.Find("exact", &mutationError));
		assert(!mutationError.empty());
	}

	// Oversized record dimensions are contained as a corrupt tail instead of
	// wrapping record arithmetic or addressing beyond the mapped payload.
	{
		const auto overflowRoot = root / "overflow";
		std::filesystem::create_directories(overflowRoot);
		const auto first = overflowRoot / "Optimized.A.csxpack";
		const auto second = overflowRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		std::string overflowError;
		Store writer(first, second, Lane::Optimized);
		assert(writer.Open(&overflowError));
		assert(writer.Append(MakeEntry("logical", "exact", 0x72), &overflowError));
		assert(writer.Checkpoint(&overflowError));
		const auto maximum = std::numeric_limits<std::uint32_t>::max();
		Overwrite(ActivePack(first, second), 104, maximum);
		Store reader(first, second, Lane::Optimized);
		assert(reader.Open(&overflowError));
		assert(reader.GetStats().corruptTailBytes > 0);
		assert(!reader.Find("exact", &overflowError));
	}

	// A nonzero pack-set identity is also the cross-process writer lease key.
	{
		const auto leaseRoot = root / "lease";
		std::filesystem::create_directories(leaseRoot);
		const auto first = leaseRoot / "Optimized.A.csxpack";
		const auto second = leaseRoot / "Optimized.B.csxpack";
		std::ofstream(first, std::ios::binary).close();
		std::ofstream(second, std::ios::binary).close();
		PackSetId setID{};
		setID.front() = std::byte{ 0x53 };
		std::string leaseError;
		Store owner(first, second, Lane::Optimized, setID);
		assert(owner.Open(&leaseError));
		Store contender(first, second, Lane::Optimized, setID);
		assert(!contender.Open(&leaseError));
		assert(!leaseError.empty());
	}

	const auto a = root / "Optimized.A.csxpack";
	const auto b = root / "Optimized.B.csxpack";
	std::ofstream(a, std::ios::binary).close();
	std::ofstream(b, std::ios::binary).close();

	std::string error;
	Store store(a, b, Lane::Optimized);
	assert(store.Open(&error));
	assert(store.Append(MakeEntry("water|provider=1", "water|source=old|provider=1", 0x11), &error));
	assert(store.Append(MakeEntry("water|provider=1", "water|source=new|provider=1", 0x22), &error));
	assert(store.Append(MakeEntry("water|provider=2", "water|source=new|provider=2", 0x33), &error));
	assert(store.Checkpoint(&error));

	auto old = store.Find("water|source=old|provider=1", &error);
	auto current = store.Find("water|source=new|provider=1", &error);
	assert(old && current);
	assert(old->bytecode.front() == std::byte{ 0x11 });
	assert(current->bytecode.front() == std::byte{ 0x22 });
	const auto before = store.GetStats();
	assert(before.recordCount == 3 && before.liveRecordCount == 2 && before.supersededBytes > 0);

	// A committed prefix remains readable when a process dies during the next append.
	{
		std::ofstream tail(a, std::ios::binary | std::ios::app);
		const char incomplete[] = "CSXREC1";
		tail.write(incomplete, sizeof(incomplete));
	}
	Store recovered(a, b, Lane::Optimized);
	assert(recovered.Open(&error));
	assert(recovered.Find("water|source=new|provider=1", &error));
	assert(recovered.GetStats().corruptTailBytes > 0);

	// Compaction retains the latest exact record per logical/compatibility key in
	// the new generation while the previous generation remains searchable.
	assert(recovered.Compact(&error));
	const auto compacted = recovered.GetStats();
	assert(compacted.activeGeneration == 2);
	assert(compacted.recordCount == 2);
	assert(compacted.liveRecordCount == 2);
	assert(compacted.supersededBytes == 0);
	assert(compacted.Fragmentation() == 0.0);
	assert(recovered.Find("water|source=new|provider=1", &error));
	assert(recovered.Find("water|source=new|provider=2", &error));
	assert(recovered.Find("water|source=old|provider=1", &error));

	// Exercise the opposite A/B role: B is active after the first compaction,
	// and the next compaction must safely initialize and promote A.
	assert(recovered.Append(MakeEntry("water|provider=1", "water|source=newest|provider=1", 0x44), &error));
	assert(recovered.Checkpoint(&error));
	assert(recovered.Compact(&error));
	assert(recovered.GetStats().activeGeneration == 3);
	assert(recovered.Find("water|source=newest|provider=1", &error));

	// Reset first commits a new generation barrier, so a failed cleanup cannot
	// make any record from the prior generation visible again.
	assert(recovered.Reset(&error));
	assert(recovered.GetStats().activeGeneration == 5);
	assert(!recovered.Find("water|source=newest|provider=1", &error));
	assert(recovered.Append(MakeEntry("water|provider=1", "water|source=reset|provider=1", 0x55), &error));
	assert(recovered.Checkpoint(&error));
	assert(recovered.Find("water|source=reset|provider=1", &error));

	// A vanished backing file is an ordinary cache-lane failure, not an
	// exception escaping through shader compilation.
	std::filesystem::remove(a);
	error.clear();
	assert(!recovered.Find("water|source=reset|provider=1", &error));
	assert(!error.empty());
	error.clear();
	assert(!recovered.Compact(&error));
	assert(!error.empty());

	std::filesystem::remove_all(root);
	return 0;
}
