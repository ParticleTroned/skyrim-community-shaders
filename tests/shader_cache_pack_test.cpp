#include "Utils/ShaderCachePack.h"

#include <cassert>
#include <chrono>
#include <fstream>

using namespace Util::ShaderCachePack;

namespace
{
	Entry MakeEntry(std::string a_logical, std::string a_exact, std::uint8_t a_value, std::size_t a_size = 64)
	{
		Entry entry{ std::move(a_logical), std::move(a_exact), "{\"schema\":1}", {} };
		entry.bytecode.assign(a_size, static_cast<std::byte>(a_value));
		return entry;
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

	// A vanished backing file is an ordinary cache-lane failure, not an
	// exception escaping through shader compilation.
	std::filesystem::remove(a);
	error.clear();
	assert(!recovered.Find("water|source=newest|provider=1", &error));
	assert(!error.empty());
	error.clear();
	assert(!recovered.Compact(&error));
	assert(!error.empty());

	std::filesystem::remove_all(root);
	return 0;
}
