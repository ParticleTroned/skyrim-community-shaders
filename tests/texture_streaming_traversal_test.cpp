#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Util
{
#include "streaming_pointer_under_test.h"
}
namespace RE
{
#include "streaming_array_under_test.h"
}
#include "streaming_children_under_test.h"

namespace
{
	int failures = 0;
	void Check(bool accepted, const char* reason)
	{
		if (!accepted) {
			std::fprintf(stderr, "%s\n", reason);
			++failures;
		}
	}

	struct Object
	{
		std::uintptr_t references = 0;
	};
	struct Slot
	{
		Object* pointer = nullptr;
		Object* get() const { return pointer; }
	};
	struct Allocator
	{
		static Slot* Allocate(std::size_t count) { return new Slot[count]{}; }
		static void Deallocate(Slot* data) { delete[] data; }
	};
	using Children = RE::NiTArray<Slot, Allocator>;
	static_assert(sizeof(Children) == 0x18);

	void SetCounts(Children& children, std::uint16_t slots, std::uint16_t count)
	{
		// Populate native metadata without depending on a live engine allocator or scene writer.
		const std::array<std::uint16_t, 2> counts{ slots, count };
		std::memcpy(reinterpret_cast<std::byte*>(&children) + 0x12, counts.data(), sizeof(counts));
	}

	void SetData(Children& children, Slot* data)
	{
		std::memcpy(reinterpret_cast<std::byte*>(&children) + 0x08, &data, sizeof(data));
	}

	struct Inventory
	{
		std::vector<Object*> retained;
		bool Scan(const Children& children, std::size_t available = 64)
		{
			return VisitInventoryChildren(children, available, [&](Object* child) {
				++child->references;
				retained.push_back(child);
			});
		}
	};
}

namespace PreviewFixture
{
	namespace RE
	{
		using NiAVObject = Object;
	}
	struct RetainedRoot
	{
		Object* pointer;
		explicit RetainedRoot(Object* object) : pointer(object) { ++pointer->references; }
	};
	struct RootSnapshot
	{
		bool complete = true;
		std::vector<RetainedRoot> roots;
	};
	struct Data
	{
		struct Model
		{
			Slot spModel;
		};
		std::vector<Model> loadedModels;
	};
#include "streaming_preview_roots_under_test.h"

	void CheckRoots(Object* poison)
	{
		Object valid;
		RootSnapshot output;
		AddRoot(nullptr, output);
		Check(output.complete && output.roots.empty(), "A missing preview root invalidated an empty inventory");
		AddRoot(poison, output);
		Check(!output.complete && output.roots.empty(), "An invalid preview root reached reference acquisition");
		AddRoot(&valid, output);
		Check(!valid.references && output.roots.empty(), "A rejected preview snapshot continued retaining roots");
		output = {};
		AddRoot(&valid, output);
		Check(output.complete && output.roots.size() == 1 && output.roots[0].pointer == &valid && valid.references == 1,
			"A valid preview root was not retained");
		output = {};
		Data models{ { { { &valid } }, { { nullptr } }, { { poison } }, { { &valid } } } };
		AddModels(&models, output);
		Check(!output.complete && output.roots.size() == 1 && output.roots[0].pointer == &valid && valid.references == 2,
			"Mixed preview roots continued past rejection or marked partial coverage complete");
		output = {};
		models.loadedModels.resize(33);
		AddModels(&models, output);
		Check(!output.complete && output.roots.empty() && valid.references == 2,
			"An oversized preview root inventory acquired references");
		output = {};
		AddModels(static_cast<Data*>(nullptr), output);
		Check(output.complete && output.roots.empty(), "Missing preview runtime data was not treated as empty");
	}
}

int main()
{
	const auto poison = reinterpret_cast<Object*>(std::uintptr_t{ 0x4735F601C95B036C });
	PreviewFixture::CheckRoots(poison);
	{
		Object first, last;
		Children children(8);
		children[0].pointer = &first;
		children[3].pointer = &last;
		children[7].pointer = poison;
		SetCounts(children, 4, 2);
		Inventory inventory;
		Check(inventory.Scan(children), "Unused poisoned capacity was visited");
		Check(inventory.retained == std::vector<Object*>{ &first, &last } && first.references == 1 && last.references == 1,
			"Sparse active slots were truncated at the non-null count");
	}
	{
		Children empty;
		Inventory inventory;
		Check(inventory.Scan(empty, 0) && inventory.retained.empty(), "An empty native array was rejected");
		Children slack(4);
		slack[0].pointer = poison;
		Check(inventory.Scan(slack, 0) && inventory.retained.empty(), "An empty inventory read allocated spare slots");
		Children holes(4);
		SetCounts(holes, 4, 0);
		Check(inventory.Scan(holes, 0) && inventory.retained.empty(), "Null holes consumed the frontier budget");
	}
	for (auto* bad : { poison, reinterpret_cast<Object*>(0x8), reinterpret_cast<Object*>(0x10001), reinterpret_cast<Object*>(0xFFFF800000001000ULL) }) {
		Children children(1);
		children[0].pointer = bad;
		SetCounts(children, 1, 1);
		Inventory inventory;
		Check(!inventory.Scan(children) && inventory.retained.empty(), "A malformed active child reached reference acquisition");
	}
	{
		Object first;
		Children children(2);
		children[0].pointer = &first;
		children[1].pointer = poison;
		SetCounts(children, 2, 2);
		Inventory inventory;
		Check(!inventory.Scan(children) && inventory.retained == std::vector<Object*>{ &first },
			"A malformed later child was retained or a partial inventory was marked complete");
	}
	for (const auto counts : { std::array<std::uint16_t, 2>{ 3, 1 }, { 1, 2 }, { 0, 1 } }) {
		Object first;
		Children children(2);
		children[0].pointer = &first;
		SetCounts(children, counts[0], counts[1]);
		Inventory inventory;
		Check(!inventory.Scan(children) && inventory.retained.empty(), "Inconsistent native counts were trusted");
	}
	{
		Children children(1);
		SetCounts(children, 1, 1);
		auto* original = children.begin();
		Inventory inventory;
		SetData(children, nullptr);
		Check(!inventory.Scan(children), "Missing active array storage was accepted");
		SetData(children, reinterpret_cast<Slot*>(poison));
		Check(!inventory.Scan(children), "Malformed active array storage was read");
		SetData(children, original);
		Check(!inventory.Scan(children), "Missing non-null children were accepted as complete coverage");
	}
	{
		Object first;
		Children children(1);
		children[0].pointer = &first;
		SetCounts(children, 1, 0);
		Inventory inventory;
		Check(!inventory.Scan(children) && inventory.retained.empty(), "Unexpected non-null children escaped the count bound");
		SetCounts(children, 1, 1);
		Check(!inventory.Scan(children, 0) && !first.references, "Frontier overflow acquired a child reference");
		Check(inventory.Scan(children, 1) && first.references == 1, "An exact frontier fit was rejected");
	}
	std::printf("Texture streaming traversal: %d failures\n", failures);
	return failures ? 1 : 0;
}
