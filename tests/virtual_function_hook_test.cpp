// Include the implementation so failure injection stays outside the production API.
#include "Utils/VirtualFunctionHook.cpp"

#include <array>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Virtual hook check failed at line " + std::to_string(a_location.line()));
	}

	struct FakeTransaction
	{
		mutable std::string calls;
		long begin = NO_ERROR;
		long update = NO_ERROR;
		long attach = NO_ERROR;
		long abort = NO_ERROR;
		long commit = NO_ERROR;
		long Begin() const
		{
			calls += 'B';
			return begin;
		}
		long UpdateThread() const
		{
			calls += 'U';
			return update;
		}
		long Attach(void**, void*) const
		{
			calls += 'A';
			return attach;
		}
		long Abort() const
		{
			calls += 'X';
			return abort;
		}
		long Commit() const
		{
			calls += 'C';
			return commit;
		}
	};

	void TestTransactionOwnership()
	{
		void* original = nullptr;
		const auto check = [&](FakeTransaction& operations, long error, bool fallback, const char* calls) {
			const auto result = Util::detail::AttachDetourTransaction(&original, nullptr, operations);
			Require(result.error == error && result.fallbackAllowed == fallback && operations.calls == calls);
		};
		FakeTransaction success;
		check(success, NO_ERROR, false, "BUAC");
		FakeTransaction busy;
		busy.begin = ERROR_INVALID_OPERATION;
		check(busy, ERROR_INVALID_OPERATION, false, "B");
		FakeTransaction threadFailure;
		threadFailure.update = ERROR_ACCESS_DENIED;
		check(threadFailure, ERROR_ACCESS_DENIED, true, "BUX");
		FakeTransaction attachFailure;
		attachFailure.attach = ERROR_INVALID_BLOCK;
		check(attachFailure, ERROR_INVALID_BLOCK, true, "BUAX");
		FakeTransaction abortFailure;
		abortFailure.attach = ERROR_INVALID_BLOCK;
		abortFailure.abort = ERROR_INVALID_OPERATION;
		check(abortFailure, ERROR_INVALID_OPERATION, false, "BUAX");
		FakeTransaction commitFailure;
		commitFailure.commit = ERROR_INVALID_DATA;
		check(commitFailure, ERROR_INVALID_DATA, true, "BUAC");
	}

	using FakeMethod = int (*)(void*);
	void* firstOriginal = nullptr;
	void* secondOriginal = nullptr;
	int OriginalMethod(void*) { return 10; }
	int FirstHook(void* a_object) { return reinterpret_cast<FakeMethod>(firstOriginal)(a_object) + 1; }
	int SecondHook(void* a_object) { return reinterpret_cast<FakeMethod>(secondOriginal)(a_object) + 2; }
	int TailMethod(void*) { return 99; }

	struct FakeObject
	{
		void** table;
		int Call(std::size_t a_slot) { return reinterpret_cast<FakeMethod>(table[a_slot])(this); }
	};

	std::array<void*, 4> OriginalTable()
	{
		return { reinterpret_cast<void*>(OriginalMethod), reinterpret_cast<void*>(OriginalMethod),
			reinterpret_cast<void*>(OriginalMethod), reinterpret_cast<void*>(TailMethod) };
	}

	std::size_t FourSlots(void*) { return 4; }
	Util::DetourAttachResult DenyDetour(void**, void*) { return { ERROR_INVALID_BLOCK, true }; }
	Util::DetourAttachResult BusyDetour(void**, void*) { return { ERROR_INVALID_OPERATION, false }; }
	Util::DetourAttachResult SimulateDetour(void** a_original, void*)
	{
		*a_original = reinterpret_cast<void*>(TailMethod);
		return {};
	}
	BOOL WINAPI DenyProtection(LPVOID, SIZE_T, DWORD, PDWORD)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	void TestNativeDetourAndBusyTransaction()
	{
		HookState state;
		auto table = OriginalTable();
		FakeObject object{ table.data() };
		void* original = nullptr;
		const auto busy = InstallHook(state, &object, 0, 4, FourSlots, &original,
			reinterpret_cast<void*>(FirstHook), BusyDetour);
		Require(busy.error == ERROR_INVALID_OPERATION && object.table == table.data());
		Require(state.hooks.empty() && state.clones.empty());
		original = nullptr;
		const auto installed = InstallHook(state, &object, 0, 4, FourSlots, &original,
			reinterpret_cast<void*>(FirstHook), SimulateDetour);
		Require(installed.error == NO_ERROR && installed.method == Util::VirtualHookMethod::Detour);
		Require(original == reinterpret_cast<void*>(TailMethod));
		const auto repeated = InstallHook(state, &object, 0, 4, FourSlots, &original,
			reinterpret_cast<void*>(FirstHook), BusyDetour);
		Require(repeated.error == NO_ERROR && original == reinterpret_cast<void*>(TailMethod));
		Require(state.hooks.size() == 1 && state.clones.empty());
	}

	void TestFallbackChaining(bool a_clone)
	{
		HookState state;
		auto table = OriginalTable();
		FakeObject object{ table.data() };
		firstOriginal = nullptr;
		secondOriginal = nullptr;
		const auto protect = a_clone ? DenyProtection : VirtualProtect;
		const auto install = [&](void** original, void* hook) {
			return InstallHook(state, &object, 0, 4, FourSlots, original, hook, DenyDetour, protect);
		};
		const auto first = install(&firstOriginal, reinterpret_cast<void*>(FirstHook));
		Require(first.error == NO_ERROR && first.detourError == ERROR_INVALID_BLOCK);
		Require(first.method == (a_clone ? Util::VirtualHookMethod::Clone : Util::VirtualHookMethod::VTable));
		Require(object.Call(0) == 11 && object.Call(3) == 99);
		Require((object.table != table.data()) == a_clone);
		Require(install(&firstOriginal, reinterpret_cast<void*>(FirstHook)).error == NO_ERROR);
		Require(object.Call(0) == 11 && firstOriginal == reinterpret_cast<void*>(OriginalMethod));
		Require(install(&secondOriginal, reinterpret_cast<void*>(SecondHook)).error == NO_ERROR);
		Require(object.Call(0) == 13 && object.Call(3) == 99);
		Require(secondOriginal == reinterpret_cast<void*>(FirstHook));
		Require(state.clones.size() == (a_clone ? 1u : 0u));
		if (a_clone) {
			void** oldClone = object.table;
			object.table = table.data();
			Require(install(&firstOriginal, reinterpret_cast<void*>(FirstHook)).error == NO_ERROR);
			Require(object.Call(0) == 11 && state.clones.size() == 2);
			Require(reinterpret_cast<FakeMethod>(oldClone[0])(&object) == 13);
		}
	}

	void TestIncompleteCloneIsNotPublished()
	{
		SYSTEM_INFO info{};
		GetSystemInfo(&info);
		auto* memory = static_cast<char*>(VirtualAlloc(nullptr, info.dwPageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		Require(memory != nullptr);
		const auto release = [](char* address) { VirtualFree(address, 0, MEM_RELEASE); };
		std::unique_ptr<char, decltype(release)> allocation(memory, release);
		DWORD oldProtection = 0;
		Require(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection) != FALSE);
		auto** table = reinterpret_cast<void**>(memory + info.dwPageSize - 2 * sizeof(void*));
		table[0] = table[1] = reinterpret_cast<void*>(OriginalMethod);
		FakeObject object{ table };
		HookState state;
		void* original = nullptr;
		const auto result = InstallHook(state, &object, 0, 4, FourSlots, &original,
			reinterpret_cast<void*>(FirstHook), DenyDetour, DenyProtection);
		Require(result.error == ERROR_NOACCESS && object.table == table && object.Call(0) == 10);
		Require(original == reinterpret_cast<void*>(OriginalMethod) && state.clones.empty() && state.hooks.empty());
	}

	void TestInvalidAndChangedTargets()
	{
		HookState state;
		auto table = OriginalTable();
		FakeObject object{ table.data() };
		void* original = nullptr;
		void* hook = reinterpret_cast<void*>(FirstHook);
		Require(InstallHook(state, nullptr, 0, 4, FourSlots, &original, hook).error == ERROR_INVALID_PARAMETER);
		Require(InstallHook(state, &object, 4, 4, FourSlots, &original, hook).error == ERROR_INVALID_PARAMETER);
		Require(original == nullptr && state.hooks.empty());
		Require(InstallHook(state, &object, 0, 4, FourSlots, &original, hook, DenyDetour, DenyProtection).error == NO_ERROR);
		auto changed = OriginalTable();
		changed[0] = reinterpret_cast<void*>(TailMethod);
		object.table = changed.data();
		Require(InstallHook(state, &object, 0, 4, FourSlots, &original, hook, DenyDetour).error == ERROR_INVALID_FUNCTION);
		Require(original == reinterpret_cast<void*>(OriginalMethod) && object.Call(0) == 99);
	}

	unsigned protectionCalls = 0;
	BOOL WINAPI FailProtectionRestore(LPVOID a_address, SIZE_T a_size, DWORD a_protection, PDWORD a_previous)
	{
		if (++protectionCalls == 1)
			return VirtualProtect(a_address, a_size, a_protection, a_previous);
		SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	void TestProtectionRestoreFailureIsReported()
	{
		HookState state;
		auto table = OriginalTable();
		FakeObject object{ table.data() };
		firstOriginal = nullptr;
		protectionCalls = 0;
		const auto result = InstallHook(state, &object, 0, 4, FourSlots, &firstOriginal,
			reinterpret_cast<void*>(FirstHook), DenyDetour, FailProtectionRestore);
		Require(result.error == ERROR_ACCESS_DENIED && protectionCalls == 2);
		Require(firstOriginal == reinterpret_cast<void*>(OriginalMethod) && object.Call(0) == 11);
		Require(state.hooks.size() == 1);
	}

	void TestReadOnlyObjectRejectsClonePublication()
	{
		auto* memory = VirtualAlloc(nullptr, sizeof(FakeObject), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Require(memory != nullptr);
		const auto release = [](void* address) { VirtualFree(address, 0, MEM_RELEASE); };
		std::unique_ptr<void, decltype(release)> allocation(memory, release);
		auto table = OriginalTable();
		auto* object = new (memory) FakeObject{ table.data() };
		DWORD oldProtection = 0;
		Require(VirtualProtect(memory, sizeof(FakeObject), PAGE_READONLY, &oldProtection) != FALSE);
		HookState state;
		void* original = nullptr;
		const auto result = InstallHook(state, object, 0, 4, FourSlots, &original,
			reinterpret_cast<void*>(FirstHook), DenyDetour, DenyProtection);
		Require(result.error == ERROR_NOACCESS && object->table == table.data());
		Require(state.hooks.empty() && state.clones.empty());
	}
}

int main()
{
	try {
		TestTransactionOwnership();
		TestNativeDetourAndBusyTransaction();
		TestFallbackChaining(false);
		TestFallbackChaining(true);
		TestIncompleteCloneIsNotPublished();
		TestInvalidAndChangedTargets();
		TestProtectionRestoreFailureIsReported();
		TestReadOnlyObjectRejectsClonePublication();
		std::cout << "Virtual function hook checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
