#include "VRGrassLifetimeFix.h"

#include "Utils/VirtualFunctionHook.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace VRGrassLifetime
{
	namespace
	{
		constexpr std::uintptr_t kManagerRVA = 0x1F85130;
		constexpr std::uintptr_t kGroupLockRVA = 0x317C990;
		constexpr std::uintptr_t kOnVisibleRVA = 0xC9E0D0;
		constexpr std::uintptr_t kClearChildrenRVA = 0xC9D290;
		constexpr std::uintptr_t kGroupLockReferenceRVA = 0xD8F2CE;
		constexpr std::array<std::uint8_t, 16> kOnVisiblePrefix{
			0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20
		};
		constexpr std::array<std::uint8_t, 16> kClearChildrenPrefix{
			0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48
		};
		constexpr std::array<std::uint8_t, 7> kGroupLockReference{ 0x48, 0x8D, 0x05, 0xBB, 0xD6, 0x3E, 0x02 };

		using OnVisible = void (*)(RE::NiNode*, RE::NiCullingProcess*, std::int32_t);
		using ClearChildren = void (*)(RE::NiNode*);
		OnVisible onVisible = nullptr;
		ClearChildren clearChildren = nullptr;
		RE::BGSGrassManager** managerAddress = nullptr;
		volatile std::uint32_t* groupLockAddress = nullptr;
		bool installed = false;

		// These words are also acquired by native code; a separate C++ mutex cannot exclude its writers.
		class NativeLock
		{
		public:
			explicit NativeLock(volatile std::uint32_t& a_word) : word(reinterpret_cast<volatile LONG*>(&a_word)) {}
			void lock() const
			{
				while (InterlockedCompareExchange(word, 1, 0) != 0)
					Sleep(0);
			}
			void unlock() const { InterlockedExchange(word, 0); }

		private:
			volatile LONG* word;
		};

		volatile std::uint32_t* ChildLockFor(RE::NiNode* a_node)
		{
			auto* manager = *managerAddress;
			return manager && manager->grassNode.get() == a_node ? &manager->grassShapeLock : nullptr;
		}

		void CullGrass(RE::NiNode* a_node, RE::NiCullingProcess* a_process, std::int32_t a_alphaGroup)
		{
			auto* childWord = ChildLockFor(a_node);
			if (!childWord) {
				onVisible(a_node, a_process, a_alphaGroup);
				return;
			}
			// Match native publication order; culling also writes each group's visibility flag.
			NativeLock children{ *childWord };
			const std::lock_guard childGuard{ children };
			NativeLock groups{ *groupLockAddress };
			const std::lock_guard groupGuard{ groups };
			onVisible(a_node, a_process, a_alphaGroup);
		}

		void ClearGrassChildren(RE::NiNode* a_node)
		{
			if (auto* childWord = ChildLockFor(a_node)) {
				NativeLock children{ *childWord };
				const std::lock_guard childGuard{ children };
				clearChildren(a_node);
			} else {
				clearChildren(a_node);
			}
		}

		bool MatchesCode(std::uintptr_t a_rva, std::span<const std::uint8_t> a_expected)
		{
			std::array<std::uint8_t, 16> bytes{};
			SIZE_T read = 0;
			return a_expected.size() <= bytes.size() &&
			       ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(REL::Offset(a_rva).address()), bytes.data(), a_expected.size(), &read) &&
			       read == a_expected.size() && std::equal(a_expected.begin(), a_expected.end(), bytes.begin());
		}
	}
}

bool VRGrassLifetimeFix::TryInstall()
{
	using namespace VRGrassLifetime;
	static_assert(offsetof(RE::BGSGrassManager, grassShapeLock) == 0x40);
	static_assert(offsetof(RE::BGSGrassManager, grassNode) == 0x68);
	if (!REL::Module::IsVR())
		return false;
	if (installed)
		return true;
	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15 ||
		!MatchesCode(kOnVisibleRVA, kOnVisiblePrefix) || !MatchesCode(kClearChildrenRVA, kClearChildrenPrefix) ||
		!MatchesCode(kGroupLockReferenceRVA, kGroupLockReference)) {
		logger::warn("[Engine Fixes] Unsupported or modified VR grass lifetime sites; no hooks installed");
		return false;
	}

	managerAddress = reinterpret_cast<RE::BGSGrassManager**>(REL::Offset(kManagerRVA).address());
	groupLockAddress = reinterpret_cast<volatile std::uint32_t*>(REL::Offset(kGroupLockRVA).address());
	onVisible = reinterpret_cast<OnVisible>(REL::Offset(kOnVisibleRVA).address());
	clearChildren = reinterpret_cast<ClearChildren>(REL::Offset(kClearChildrenRVA).address());
	struct Operations
	{
		long Begin() const { return DetourTransactionBegin(); }
		long UpdateThread() const { return DetourUpdateThread(GetCurrentThread()); }
		long Attach(void**, void*) const
		{
			const auto result = DetourAttach(reinterpret_cast<void**>(&onVisible), reinterpret_cast<void*>(&CullGrass));
			return result == NO_ERROR ? DetourAttach(reinterpret_cast<void**>(&clearChildren), reinterpret_cast<void*>(&ClearGrassChildren)) : result;
		}
		long Abort() const { return DetourTransactionAbort(); }
		long Commit() const { return DetourTransactionCommit(); }
	};
	const auto result = Util::detail::AttachDetourTransaction(nullptr, nullptr, Operations{});
	if (result.error != NO_ERROR) {
		logger::error("[Engine Fixes] VR grass lifetime transaction failed ({}); no partial protection accepted", result.error);
		stl::report_and_fail("VR grass lifetime hooks could not be installed atomically. See CommunityShaders.log.");
	}
	installed = true;
	return true;
}
