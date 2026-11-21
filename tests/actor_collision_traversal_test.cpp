#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace RE
{
#include "collision_array_under_test.h"
#include "collision_pointer_under_test.h"

	struct RefCounted
	{
		unsigned references = 0;
		bool* destroyed = nullptr;
		virtual ~RefCounted()
		{
			if (destroyed)
				*destroyed = true;
		}
		void IncRefCount() { ++references; }
		void DecRefCount()
		{
			if (--references == 0)
				delete this;
		}
	};
	struct NiCollisionObject : RefCounted
	{};
	struct bhkNiCollisionObject : NiCollisionObject
	{
		explicit bhkNiCollisionObject(int value) : id(value) {}
		int id;
	};
	struct NiNode;
	struct NiAVObject : RefCounted
	{
		NiPointer<NiCollisionObject> collisionObject;
		virtual NiNode* AsNode() { return nullptr; }
	};
	using Child = NiPointer<NiAVObject>;
	struct ChildAllocator
	{
		static Child* Allocate(std::size_t count) { return new Child[count]; }
		static void Deallocate(Child* data) { delete[] data; }
	};
	using Children = NiTArray<Child, ChildAllocator>;
	static_assert(sizeof(Children) == 0x18);
	struct NiNode : NiAVObject
	{
		explicit NiNode(int id, std::uint16_t capacity = 0) : children(capacity)
		{
			collisionObject.reset(new bhkNiCollisionObject(id));
		}
		NiNode* AsNode() override { return this; }
		Children& GetChildren() { return children; }
		Children children;
	};
	namespace BSVisit
	{
		enum class BSVisitControl
		{
			kContinue,
			kStop
		};
	}
}

namespace Util
{
#include "collision_traversal_under_test.h"
}

namespace
{
	using Control = RE::BSVisit::BSVisitControl;
	int failures = 0;
	void Check(bool value, const char* message)
	{
		if (!value) {
			std::fprintf(stderr, "%s\n", message);
			++failures;
		}
	}

	// Model native child-storage teardown with the pinned array layout and real NiPointer ownership.
	void SetStorage(RE::Children& children, RE::Child* data)
	{
		std::memcpy(reinterpret_cast<std::byte*>(&children) + 0x08, &data, sizeof(data));
	}
	void SetCapacity(RE::Children& children, std::uint16_t capacity)
	{
		std::memcpy(reinterpret_cast<std::byte*>(&children) + 0x10, &capacity, sizeof(capacity));
	}

	void CheckOrderAndStop()
	{
		RE::NiPointer<RE::NiNode> root(new RE::NiNode(0, 4));
		root->children[1].reset(new RE::NiNode(1, 1));
		root->children[1]->AsNode()->children[0].reset(new RE::NiNode(2));
		root->children[3].reset(new RE::NiNode(3));
		std::vector<int> visited;
		const auto callback = [&](RE::bhkNiCollisionObject* collision) {
			visited.push_back(collision->id);
			return Control::kContinue;
		};
		Check(Util::TraverseCollision(nullptr, callback) == Control::kContinue && visited.empty(), "Null root invoked callback");
		Check(Util::TraverseCollision(root.get(), {}) == Control::kContinue, "Empty callback was not ignored");
		Util::TraverseCollision(root.get(), callback);
		Check(visited == std::vector<int>({ 0, 1, 2, 3 }), "Sparse traversal changed native preorder");
		visited.clear();
		Check(Util::TraverseCollision(root.get(), [&](RE::bhkNiCollisionObject* collision) {
			visited.push_back(collision->id);
			return collision->id == 2 ? Control::kStop : Control::kContinue;
		}) == Control::kStop,
			"Nested stop was not propagated");
		Check(visited == std::vector<int>({ 0, 1, 2 }), "Traversal continued after stop");
		Check(root->references == 1 && root->children[1]->references == 1, "Traversal leaked node references");

		auto* storage = root->children.begin();
		SetStorage(root->children, nullptr);
		visited.clear();
		Util::TraverseCollision(root.get(), callback);
		Check(visited == std::vector<int>({ 0 }), "Missing child storage was dereferenced");
		SetStorage(root->children, storage);
	}

	void CheckStorageChanges()
	{
		for (int mode = 0; mode < 3; ++mode) {
			bool childDestroyed = false, collisionDestroyed = false;
			RE::NiPointer<RE::NiNode> root(new RE::NiNode(0, 2));
			root->children[0].reset(new RE::NiNode(1));
			root->children[1].reset(new RE::NiNode(2));
			auto* child = root->children[0].get();
			child->destroyed = &childDestroyed;
			child->collisionObject->destroyed = &collisionDestroyed;
			std::vector<int> visited;
			Util::TraverseCollision(root.get(), [&](RE::bhkNiCollisionObject* collision) {
				visited.push_back(collision->id);
				if (collision->id == 1) {
					child->collisionObject.reset();
					if (mode == 2) {
						SetCapacity(root->children, 1);
						root->children[0].reset();
					} else {
						auto* old = root->children.begin();
						auto* replacement = mode == 1 ? RE::ChildAllocator::Allocate(2) : nullptr;
						if (replacement)
							replacement[1].reset(new RE::NiNode(9));
						SetStorage(root->children, replacement);
						RE::ChildAllocator::Deallocate(old);
					}
					Check(!childDestroyed && !collisionDestroyed, "Callback lost retained node or collision");
				}
				return Control::kContinue;
			});
			Check(visited == std::vector<int>({ 0, 1 }), "Traversal used cleared, replaced or shrunken child storage");
			Check(childDestroyed && collisionDestroyed, "Traversal retained removed objects after completion");
		}
	}

	void CheckRootLifetimeAndCallbackCopies()
	{
		bool destroyed = false;
		RE::NiPointer<RE::NiNode> root(new RE::NiNode(0, 1));
		root->destroyed = &destroyed;
		root->children[0].reset(new RE::NiNode(1));
		int visits = 0;
		Util::TraverseCollision(root.get(), [&](RE::bhkNiCollisionObject*) {
			root.reset();
			Check(!destroyed, "Root died while its children were visited");
			++visits;
			return Control::kContinue;
		});
		Check(destroyed && visits == 2, "Root retention failed or leaked");

		struct Callback
		{
			int& copies;
			explicit Callback(int& count) : copies(count) {}
			Callback(const Callback& other) : copies(other.copies) { ++copies; }
			Control operator()(RE::bhkNiCollisionObject*) const { return Control::kContinue; }
		};
		int copies = 0;
		std::function<Control(RE::bhkNiCollisionObject*)> callback = Callback(copies);
		root.reset(new RE::NiNode(0, 1));
		root->children[0].reset(new RE::NiNode(1));
		copies = 0;
		Util::TraverseCollision(root.get(), callback);
		Check(copies == 0, "Traversal copied its callback during recursion");
	}
}

int main()
{
	CheckOrderAndStop();
	CheckStorageChanges();
	CheckRootLifetimeAndCallbackCopies();
	return failures == 0 ? 0 : 1;
}
