#define NOMINMAX
#include <Windows.h>

#include "EngineFixes/VRGrassLifetimeFix.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <semaphore>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace RE
{
	class NiNode
	{
	public:
		struct Children
		{
			std::uint16_t slots = 1, capacity = 16, live = 1;
			std::uint16_t free_idx() const { return slots; }
		} children;
		Children& GetChildren();
	};
	class NiCullingProcess
	{};
	struct BGSGrassManager
	{
		std::array<std::byte, 0x40> prefix{};
		volatile std::uint32_t grassShapeLock = 0;
		std::array<std::byte, 0x24> middle{};
		struct NodePointer
		{
			NiNode* value = nullptr;
			NiNode* get() const { return value; }
		} grassNode;
	};
}

namespace
{
	void Require(bool a_value, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_value)
			throw std::runtime_error("Grass lifetime assertion at line " + std::to_string(a_location.line()));
	}
	RE::BGSGrassManager testManager;
	RE::BGSGrassManager* managerPointer = &testManager;
	RE::NiNode grassNode, otherNode;
	RE::NiCullingProcess process;
	volatile std::uint32_t groupsWord = 0;
	std::array<std::uint8_t, 16> cullCode{}, clearCode{}, lockCode{};
	bool vr = true;
	bool verifyChildOwnership = true;
	int version = 1415;
	std::function<void(RE::NiNode*)> cullAction, clearAction;
	void NativeCull(RE::NiNode* a_node, RE::NiCullingProcess* a_process, std::int32_t a_alpha)
	{
		Require(a_process == &process && a_alpha == -1);
		if (cullAction)
			cullAction(a_node);
	}
	void NativeClear(RE::NiNode* a_node)
	{
		if (clearAction)
			clearAction(a_node);
	}
	std::vector<std::pair<void**, void*>> pending;
	unsigned transactions = 0, attaches = 0, aborts = 0;
	unsigned failAttach = 0;
	long Word(volatile std::uint32_t& a_word)
	{
		return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&a_word), 0, 0);
	}
}
RE::NiNode::Children& RE::NiNode::GetChildren()
{
	if (verifyChildOwnership)
		Require(this == &grassNode && Word(testManager.grassShapeLock) == 1);
	return children;
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}
namespace stl
{
	[[noreturn]] void report_and_fail(const char* a_message) { throw std::runtime_error(a_message); }
}
namespace SKSE
{
	constexpr int RUNTIME_VR_1_4_15 = 1415;
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return vr; }
		static Module get() { return {}; }
		int version() const { return ::version; }
	};
	struct Offset
	{
		std::uintptr_t rva;
		std::uintptr_t address() const
		{
			switch (rva) {
			case 0xC9E0D0:
				return reinterpret_cast<std::uintptr_t>(cullCode.data());
			case 0xC9D290:
				return reinterpret_cast<std::uintptr_t>(clearCode.data());
			case 0xD8F2CE:
				return reinterpret_cast<std::uintptr_t>(lockCode.data());
			case 0x1F85130:
				return reinterpret_cast<std::uintptr_t>(&managerPointer);
			case 0x317C990:
				return reinterpret_cast<std::uintptr_t>(&groupsWord);
			default:
				throw std::runtime_error("Unexpected native address");
			}
		}
	};
}
long DetourTransactionBegin()
{
	++transactions;
	pending.clear();
	attaches = 0;
	return NO_ERROR;
}
long DetourUpdateThread(HANDLE) { return NO_ERROR; }
long DetourAttach(void** a_original, void* a_hook)
{
	if (++attaches == failAttach)
		return ERROR_INVALID_BLOCK;
	pending.emplace_back(a_original, a_hook);
	return NO_ERROR;
}
long DetourTransactionAbort()
{
	++aborts;
	pending.clear();
	return NO_ERROR;
}
long DetourTransactionCommit()
{
	Require(pending.size() == 2);
	*pending[0].first = reinterpret_cast<void*>(&NativeCull);
	*pending[1].first = reinterpret_cast<void*>(&NativeClear);
	pending.clear();
	return NO_ERROR;
}

#include "vr_grass_lifetime_under_test.h"

namespace
{
	using namespace VRGrassLifetime;
	void Installation()
	{
		cullCode = kOnVisiblePrefix;
		clearCode = kClearChildrenPrefix;
		std::copy(kGroupLockReference.begin(), kGroupLockReference.end(), lockCode.begin());
		VRGrassLifetimeFix fix;
		vr = false;
		Require(!fix.TryInstall() && transactions == 0);
		vr = true;
		version = 1416;
		Require(!fix.TryInstall() && transactions == 0);
		version = 1415;
		for (auto* code : { &cullCode, &clearCode, &lockCode }) {
			(*code)[0] ^= 0xFF;
			Require(!fix.TryInstall() && transactions == 0);
			(*code)[0] ^= 0xFF;
		}
		for (const auto fail : { 1U, 2U }) {
			failAttach = fail;
			bool rejected = false;
			try {
				fix.TryInstall();
			} catch (const std::runtime_error&) {
				rejected = true;
			}
			Require(rejected && !installed && pending.empty() && aborts == fail);
		}
		failAttach = 0;
		Require(fix.TryInstall() && installed);
		const auto before = transactions;
		Require(fix.TryInstall() && transactions == before);
	}

	void PassthroughAndUnwind()
	{
		NativeLock children{ testManager.grassShapeLock }, groups{ groupsWord };
		children.lock();
		groups.lock();
		unsigned forwarded = 0;
		cullAction = [&](RE::NiNode* a_node) { Require(a_node == &otherNode); ++forwarded; };
		clearAction = cullAction;
		CullGrass(&otherNode, &process, -1);
		ClearGrassChildren(&otherNode);
		Require(forwarded == 2);
		groups.unlock();
		children.unlock();
		bool enteredWithLocks = false;
		cullAction = [&](RE::NiNode*) {
			enteredWithLocks = Word(testManager.grassShapeLock) == 1 && Word(groupsWord) == 1;
			throw std::runtime_error("native callback exception");
		};
		try {
			CullGrass(&grassNode, &process, -1);
		} catch (const std::runtime_error&) {}
		Require(enteredWithLocks && Word(testManager.grassShapeLock) == 0 && Word(groupsWord) == 0);
		clearAction = [&](RE::NiNode* a_node) {
			if (a_node == &grassNode) {
				Require(Word(testManager.grassShapeLock) == 1 && Word(groupsWord) == 0);
				ClearGrassChildren(&otherNode);
			}
		};
		ClearGrassChildren(&grassNode);
		Require(Word(testManager.grassShapeLock) == 0);
	}

	void Race(bool a_bulkClear, bool a_grow)
	{
		std::vector<std::unique_ptr<int>> slots;
		slots.push_back(std::make_unique<int>(37));
		std::binary_semaphore borrowed{ 0 }, finishRead{ 0 }, writerStarted{ 0 };
		cullAction = [&](RE::NiNode*) {
			auto* array = slots.data();
			auto* group = array[0].get();
			borrowed.release();
			finishRead.acquire();
			Require(*group == 37 && array == slots.data() && slots.size() == 1);
			*group = 41;
		};
		clearAction = [&](RE::NiNode*) { Require(*slots[0] == 41); slots.clear(); };
		auto reader = std::async(std::launch::async, [&] { CullGrass(&grassNode, &process, -1); });
		borrowed.acquire();
		auto writer = std::async(std::launch::async, [&] {
			writerStarted.release();
			if (a_bulkClear) {
				ClearGrassChildren(&grassNode);
			} else {
				NativeLock groups{ groupsWord };
				const std::lock_guard lock{ groups };
				Require(*slots[0] == 41);
				if (a_grow)
					slots.resize(4096);
				else
					slots[0].reset();
			}
		});
		writerStarted.acquire();
		Require(Word(testManager.grassShapeLock) == 1 && Word(groupsWord) == 1);
		finishRead.release();
		reader.get();
		writer.get();
		Require(a_bulkClear ? slots.empty() : a_grow ? slots.size() == 4096 :
													   !slots[0]);
	}

	void RemovalBeforeBorrow()
	{
		NativeLock children{ testManager.grassShapeLock };
		children.lock();
		std::unique_ptr<int> shape = std::make_unique<int>(1);
		std::binary_semaphore started{ 0 };
		cullAction = [&](RE::NiNode*) { Require(!shape); };
		auto reader = std::async(std::launch::async, [&] { started.release(); CullGrass(&grassNode, &process, -1); });
		started.acquire();
		shape.reset();
		children.unlock();
		reader.get();
		shape = std::make_unique<int>(2);
		cullAction = [&](RE::NiNode*) { Require(*shape == 2); };
		CullGrass(&grassNode, &process, -1);
	}

	void LockOrder()
	{
		NativeLock groups{ groupsWord };
		groups.lock();
		cullAction = {};
		auto reader = std::async(std::launch::async, [&] { CullGrass(&grassNode, &process, -1); });
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (Word(testManager.grassShapeLock) == 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		const bool childFirst = Word(testManager.grassShapeLock) == 1;
		groups.unlock();
		reader.get();
		Require(childFirst && Word(testManager.grassShapeLock) == 0);
	}

	void ManagerLifetime()
	{
		NativeLock children{ testManager.grassShapeLock }, groups{ groupsWord };
		children.lock();
		groups.lock();
		unsigned calls = 0;
		cullAction = [&](RE::NiNode*) { ++calls; };
		clearAction = cullAction;
		managerPointer = nullptr;
		CullGrass(&grassNode, &process, -1);
		ClearGrassChildren(&grassNode);
		managerPointer = &testManager;
		testManager.grassNode.value = &otherNode;
		CullGrass(&grassNode, &process, -1);
		ClearGrassChildren(&grassNode);
		Require(calls == 4);
		testManager.grassNode.value = &grassNode;
		groups.unlock();
		children.unlock();
	}

	void EmptyTraversal()
	{
		NativeLock groups{ groupsWord };
		groups.lock();
		grassNode.children.slots = 0;
		grassNode.children.live = 0;
		unsigned calls = 0;
		cullAction = [&](RE::NiNode*) {
			Require(Word(testManager.grassShapeLock) == 1 && Word(groupsWord) == 1);
			++calls;
		};
		auto reader = std::async(std::launch::async, [&] { CullGrass(&grassNode, &process, -1); });
		const bool bypassed = reader.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		groups.unlock();
		reader.get();
		Require(bypassed && calls == 1 && Word(testManager.grassShapeLock) == 0);

		// A zero live count is not the native early-out when traversal slots still contain holes.
		grassNode.children.slots = 3;
		cullAction = [&](RE::NiNode*) { Require(Word(groupsWord) == 1); };
		CullGrass(&grassNode, &process, -1);
		grassNode.children = {};
	}

	void EmptyAfterChildAcquisition()
	{
		NativeLock children{ testManager.grassShapeLock }, groups{ groupsWord };
		children.lock();
		groups.lock();
		std::binary_semaphore started{ 0 };
		cullAction = [&](RE::NiNode*) { Require(grassNode.children.slots == 0); };
		auto reader = std::async(std::launch::async, [&] {
			started.release();
			CullGrass(&grassNode, &process, -1);
		});
		started.acquire();
		grassNode.children.slots = 0;
		children.unlock();
		const bool bypassed = reader.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		groups.unlock();
		reader.get();
		Require(bypassed);
		grassNode.children = {};
	}

	__declspec(noinline) double TimeCalls(OnVisible a_function, RE::NiNode* a_node, std::uint64_t a_count)
	{
		const auto start = std::chrono::steady_clock::now();
		for (std::uint64_t i = 0; i < a_count; ++i)
			a_function(a_node, &process, -1);
		return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / static_cast<double>(a_count);
	}

	void Benchmark()
	{
		verifyChildOwnership = false;
		constexpr std::uint64_t count = 1000000;
		std::uint64_t calls = 0;
		cullAction = [&](RE::NiNode*) { ++calls; };
		TimeCalls(&NativeCull, &grassNode, count / 10);
		TimeCalls(&CullGrass, &otherNode, count / 10);
		TimeCalls(&CullGrass, &grassNode, count / 10);
		const std::array<OnVisible, 4> functions{ &NativeCull, &CullGrass, &CullGrass, &CullGrass };
		const std::array<RE::NiNode*, 4> nodes{ &grassNode, &otherNode, &grassNode, &grassNode };
		std::cout << "round,lane,ns_per_call\n";
		for (unsigned round = 0; round < 6; ++round) {
			for (unsigned offset = 0; offset < 4; ++offset) {
				const auto lane = (round + offset) % 4;
				grassNode.children.slots = lane == 3 ? 0 : 1;
				std::cout << round << ',' << lane << ',' << TimeCalls(functions[lane], nodes[lane], count) << '\n';
			}
		}
		grassNode.children = {};
		Require(calls == count * 24 + count * 3 / 10);
		verifyChildOwnership = true;
	}
}

int main(int a_argc, char** a_argv)
{
	try {
		testManager.grassNode.value = &grassNode;
		Installation();
		if (a_argc == 2 && std::string_view(a_argv[1]) == "--benchmark-only") {
			Benchmark();
			return 0;
		}
		PassthroughAndUnwind();
		Race(false, false);
		Race(false, true);
		Race(true, false);
		RemovalBeforeBorrow();
		LockOrder();
		ManagerLifetime();
		EmptyTraversal();
		EmptyAfterChildAcquisition();
		std::cout << "VR grass lifetime: installation, removal, growth, clear, pre-borrow ownership, nested child clear, unwind and lock order passed\n";
		if (a_argc == 2 && std::string_view(a_argv[1]) == "--benchmark")
			Benchmark();
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
