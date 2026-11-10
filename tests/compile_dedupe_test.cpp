#include "Utils/CompileDedupe.h"

#include <array>
#include <barrier>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace Util::CompileDedupe;
using Util::ContentHash::Hash128;

namespace Util::CompileDedupe
{
	struct RegistryTestAccess
	{
		static bool HasWaiter(Registry& a_registry, const Hash128& a_key)
		{
			std::scoped_lock lock(a_registry.mapMutex);
			const auto found = a_registry.map.find(a_key);
			// The map and owner hold two references; a waiter retains a third.
			return found != a_registry.map.end() && found->second.use_count() >= 3;
		}
	};
}

namespace
{
	Hash128 Key(std::string_view a_text)
	{
		return MakeKey({ a_text, "main", "ps_5_0", 0, "test-compiler" });
	}

	void WaitForWaiter(Registry& a_registry, const Hash128& a_key)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!RegistryTestAccess::HasWaiter(a_registry, a_key)) {
			assert(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
	}

	void KeySeparation()
	{
		const KeyInputs input{ "source", "main", "ps_5_0", 4, "compiler" };
		const auto key = MakeKey(input);
		for (int field = 0; field < 5; ++field) {
			auto changed = input;
			switch (field) {
			case 0:
				changed.preprocessed = "other";
				break;
			case 1:
				changed.entryPoint = "other";
				break;
			case 2:
				changed.profile = "vs_5_0";
				break;
			case 3:
				changed.flags ^= 1;
				break;
			case 4:
				changed.compilerId = "other";
				break;
			}
			assert(MakeKey(changed) != key);
		}
		assert(MakeKey({ "ab", "c", "d", 0, "e" }) != MakeKey({ "a", "bc", "d", 0, "e" }));
		assert(Key(std::string_view("a\0b", 3)) != Key("a"));
		const std::string first = "#line 1 \"one.hlsl\"\nfloat x;\n#line 5 \"inc.hlsli\"\nfloat y;";
		const std::string second = "#line 9 \"two.hlsl\"\nfloat x;\n#line 8 \"inc.hlsli\"\nfloat y;";
		assert(StripLineDirectives(first) == "float x;\nfloat y;");
		assert(Key(StripLineDirectives(first)) == Key(StripLineDirectives(second)));
		assert(Key(first) != Key(second));
		assert(StripLineDirectives("").empty());
		assert(StripLineDirectives("#line 2\r\ncode\r\n") == "code\r\n");
		const std::string blankLines = "\n \t\r\nfloat x;\n\nfloat y;";
		assert(StripLineDirectives(blankLines) == "float x;\nfloat y;");
		assert(Key(StripLineDirectives(blankLines)) == Key(StripLineDirectives(first)));
		assert(Key(blankLines) != Key(first));
		assert(StripLineDirectives(" \t\r\n\t ").empty());
		assert(StripLineDirectives("#pragma line\nx = 1; // #line 3\n") == "#pragma line\nx = 1; // #line 3\n");
	}

	void ConcurrentOwner()
	{
		Registry registry;
		constexpr int count = 12;
		std::barrier start(count);
		std::atomic<int> owners = 0;
		std::vector<std::future<void>> tasks;
		const std::array<char, 4> bytes{ 'a', '\0', 'b', '\xff' };
		for (int i = 0; i < count; ++i) {
			tasks.push_back(std::async(std::launch::async, [&] {
				start.arrive_and_wait();
				auto acquired = registry.Acquire(Key("shared"));
				if (acquired.ticket) {
					++owners;
					acquired.ticket->Publish(bytes.data(), bytes.size());
				} else {
					assert(acquired.blob);
					assert(*acquired.blob == std::vector<char>(bytes.begin(), bytes.end()));
				}
			}));
		}
		for (auto& task : tasks)
			task.get();
		assert(owners == 1);
		assert(registry.RetainedBytes() == bytes.size());
		assert(registry.Acquire(Key("different")).ticket);
	}

	void FailedOwnerRetries()
	{
		Registry registry;
		const auto key = Key("retry");
		auto owner = registry.Acquire(key);
		auto waiter = std::async(std::launch::async, [&] { return std::make_unique<Acquired>(registry.Acquire(key)); });
		WaitForWaiter(registry, key);
		assert(waiter.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
		owner.ticket.reset();
		auto retry = waiter.get();
		assert(retry->ticket && !retry->blob);
		retry->ticket->Publish("ok", 2);
		assert(*registry.Acquire(key).blob == std::vector<char>({ 'o', 'k' }));
	}

	void ClearWithPendingWaiter()
	{
		Registry registry(4);
		const auto key = Key("clear");
		auto oldOwner = registry.Acquire(key);
		auto waiter = std::async(std::launch::async, [&] { return std::make_unique<Acquired>(registry.Acquire(key)); });
		WaitForWaiter(registry, key);
		registry.Clear();
		auto newOwner = registry.Acquire(key);
		oldOwner.ticket->Publish("old!", 4);
		assert(*waiter.get()->blob == std::vector<char>({ 'o', 'l', 'd', '!' }));
		assert(registry.RetainedBytes() == 0);
		assert(registry.DroppedBlobs() == 0);
		newOwner.ticket->Publish("new!", 4);
		assert(registry.RetainedBytes() == 4);
		assert(*registry.Acquire(key).blob == std::vector<char>({ 'n', 'e', 'w', '!' }));
		registry.Clear();
		assert(registry.RetainedBytes() == 0);
		assert(registry.Acquire(key).ticket);

		auto abandoned = registry.Acquire(Key("abandon"));
		registry.Clear();
		auto replacement = registry.Acquire(Key("abandon"));
		abandoned.ticket.reset();
		replacement.ticket->Publish("live", 4);
		assert(registry.Acquire(Key("abandon")).blob);
	}

	void RetentionCap()
	{
		Registry registry(3);
		const auto key = Key("large");
		auto owner = registry.Acquire(key);
		auto waiter = std::async(std::launch::async, [&] { return std::make_unique<Acquired>(registry.Acquire(key)); });
		WaitForWaiter(registry, key);
		owner.ticket->Publish("four", 4);
		assert(waiter.get()->blob->size() == 4);
		assert(registry.RetainedBytes() == 0);
		assert(registry.DroppedBlobs() == 1);
		assert(registry.Acquire(key).ticket);
		registry.Acquire(Key("fits")).ticket->Publish("fit", 3);
		assert(registry.RetainedBytes() == 3);
		registry.Acquire(Key("over")).ticket->Publish("x", 1);
		assert(registry.DroppedBlobs() == 2);
		registry.Clear();
		assert(registry.RetainedBytes() == 0 && registry.DroppedBlobs() == 0);
	}
}

int main()
{
	KeySeparation();
	ConcurrentOwner();
	FailedOwnerRetries();
	ClearWithPendingWaiter();
	RetentionCap();
}
