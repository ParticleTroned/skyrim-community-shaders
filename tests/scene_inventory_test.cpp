#include "Utils/SceneInventory.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
	struct Node
	{
		int value;
	};
	struct Slot
	{
		Node* value = nullptr;
		Node* get() const { return value; }
	};
	struct Children
	{
		std::size_t capacityValue = 0, freeValue = 0, countValue = 0;
		const Slot* data = nullptr;
		std::size_t capacity() const { return capacityValue; }
		std::size_t free_idx() const { return freeValue; }
		std::size_t size() const { return countValue; }
		const Slot* begin() const { return data; }
	};
}

int main()
{
	Node a{ 1 }, b{ 2 }, unused{ 9 };
	const Slot slots[]{ { &a }, {}, { &b }, { &unused } };
	std::vector<int> visited;
	const auto valid = [](const void* value) { return value != nullptr; };
	const auto visit = [&](Node* node) { visited.push_back(node->value); };
	assert(Util::VisitSceneInventoryChildren(Children{ 4, 3, 2, slots }, 2, valid, visit));
	assert((visited == std::vector{ 1, 2 }));
	for (const Children& invalid : { Children{ 2, 3, 2, slots }, Children{ 4, 2, 3, slots }, Children{ 4, 3, 2, nullptr } }) {
		visited.clear();
		assert(!Util::VisitSceneInventoryChildren(invalid, 4, valid, visit));
		assert(visited.empty());
	}
	visited.clear();
	assert(!Util::VisitSceneInventoryChildren(Children{ 4, 3, 2, slots }, 1, valid, visit));
	assert(visited.empty());
	assert(!Util::VisitSceneInventoryChildren(Children{ 4, 3, 3, slots }, 3, valid, visit));
	visited.clear();
	assert(!Util::VisitSceneInventoryChildren(Children{ 4, 3, 1, slots }, 3, valid, visit));
	assert((visited == std::vector{ 1 }));
	visited.clear();
	assert(!Util::VisitSceneInventoryChildren(Children{ 4, 3, 2, slots }, 2, [&](const void* value) { return value && value != &b; }, visit));
	assert((visited == std::vector{ 1 }));
	assert(Util::VisitSceneInventoryChildren(Children{}, 0, valid, visit));
}
