#include "EngineFixes/CullPoolExhaustionFix.h"
#include "Utils/VirtualFunctionHook.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Engine overflow guard check failed at line " + std::to_string(a_location.line()));
	}

	struct ModuleState
	{
		bool loaded = false;
		bool exported = false;
		std::string installed;
	};
	std::array<ModuleState, 2> modules;
	bool QuerySE(const char* a_name) { return modules[0].installed == a_name; }
	bool QueryVR(const char* a_name) { return modules[1].installed == a_name; }
	HMODULE FindModule(const wchar_t* a_name)
	{
		const auto index = std::wcscmp(a_name, L"EngineFixes.dll") == 0 ? 0 : 1;
		return modules[index].loaded ? reinterpret_cast<HMODULE>(&modules[index]) : nullptr;
	}
	FARPROC FindExport(HMODULE a_module, const char* a_name)
	{
		Require(std::strcmp(a_name, "EngineFixes_IsFixInstalled") == 0);
		auto* module = reinterpret_cast<ModuleState*>(a_module);
		if (!module->exported)
			return nullptr;
		return reinterpret_cast<FARPROC>(module == &modules[0] ? QuerySE : QueryVR);
	}
}

#define GetModuleHandleW FindModule
#define GetProcAddress FindExport
#include "engine_fix_owner_under_test.h"
#undef GetProcAddress
#undef GetModuleHandleW

namespace logger
{
	unsigned warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

namespace REL
{
	struct Segment
	{
		static constexpr int textx = 0;
		std::uintptr_t start = 0;
		std::size_t length = 0;
		std::uintptr_t address() const { return start; }
		std::size_t size() const { return length; }
	};
	struct Module
	{
		Segment text;
		static Module& get()
		{
			static Module instance;
			return instance;
		}
		Segment segment(int) const { return text; }
	};
	struct VariantID
	{
		std::uintptr_t value = 0;
		std::uintptr_t address() const { return value; }
	};
}

namespace RE
{
	std::array<REL::VariantID, 1> VTABLE_BSCullingProcess;
	std::array<REL::VariantID, 1> VTABLE_BSParabolicCullingProcess;
	class BSCullingProcess
	{
	public:
		alignas(std::uint32_t) std::array<std::uint8_t, 0x301D8> bytes{};
	};
	class BSGeometry
	{};
}

namespace
{
	using Append = void (*)(RE::BSCullingProcess*, RE::BSGeometry*, std::int32_t);
	Append installedHook = nullptr;
	RE::BSCullingProcess* forwardedProcess = nullptr;
	RE::BSGeometry* forwardedGeometry = nullptr;
	std::int32_t forwardedIndex = -2;
	unsigned forwardedCalls = 0;
	unsigned detourCalls = 0;
	long detourError = ERROR_INVALID_BLOCK;
	std::uintptr_t expectedTarget = 0;
	void Forward(RE::BSCullingProcess* a_process, RE::BSGeometry* a_geometry, std::int32_t a_index)
	{
		++forwardedCalls;
		forwardedProcess = a_process;
		forwardedGeometry = a_geometry;
		forwardedIndex = a_index;
	}
}

namespace Util
{
	DetourAttachResult AttachDetour(void** a_original, void* a_hook)
	{
		++detourCalls;
		Require(reinterpret_cast<std::uintptr_t>(*a_original) == expectedTarget);
		if (detourError != NO_ERROR)
			return { detourError, true };
		installedHook = reinterpret_cast<Append>(a_hook);
		*a_original = reinterpret_cast<void*>(Forward);
		return {};
	}
}

#include "cull_pool_under_test.h"

namespace
{
	void TestOwnership()
	{
		constexpr auto poolName = "CullingProcessAppendVirtualPoolGuard";
		constexpr auto alphaName = "BatchRendererAlphaGeometryGroupOverflow";
		Require(!EngineFix::IsInstalledByEngineFixes(nullptr));
		Require(!EngineFix::IsInstalledByEngineFixes(poolName));
		modules[0] = { true, false, poolName };
		Require(!EngineFix::IsInstalledByEngineFixes(poolName));
		modules[0].exported = true;
		Require(EngineFix::IsInstalledByEngineFixes(poolName));
		Require(!EngineFix::IsInstalledByEngineFixes(alphaName));
		modules[1] = { true, true, alphaName };
		Require(EngineFix::IsInstalledByEngineFixes(alphaName));
		modules[0] = {};
		Require(EngineFix::IsInstalledByEngineFixes(alphaName));
		modules[1] = {};
		Require(!EngineFix::IsInstalledByEngineFixes(alphaName));
	}

	void TestInstallation()
	{
		CullPoolExhaustionFix fix;
		std::array<std::uintptr_t, 0x19> baseTable{};
		std::array<std::uintptr_t, 0x19> parabolicTable{};
		std::array<std::uint8_t, 64> code{};
		std::copy(kExpectedPrologue.begin(), kExpectedPrologue.end(), code.begin());
		expectedTarget = reinterpret_cast<std::uintptr_t>(code.data());
		RE::VTABLE_BSCullingProcess[0].value = reinterpret_cast<std::uintptr_t>(baseTable.data());
		RE::VTABLE_BSParabolicCullingProcess[0].value = reinterpret_cast<std::uintptr_t>(parabolicTable.data());
		REL::Module::get().text = { expectedTarget, code.size() };
		Require(!fix.TryInstall() && detourCalls == 0);
		baseTable[0x18] = expectedTarget;
		Require(!fix.TryInstall() && detourCalls == 0);
		parabolicTable[0x18] = expectedTarget;
		code[0] ^= 1;
		Require(!fix.TryInstall() && detourCalls == 0);
		code[0] ^= 1;
		REL::Module::get().text.start = expectedTarget + 1;
		Require(!fix.TryInstall() && detourCalls == 0);
		REL::Module::get().text = { expectedTarget, 24 };
		Require(!fix.TryInstall() && detourCalls == 0);
		REL::Module::get().text = { expectedTarget, code.size() };
		RE::VTABLE_BSCullingProcess[0].value = 0;
		Require(!fix.TryInstall() && detourCalls == 0);
		RE::VTABLE_BSCullingProcess[0].value = reinterpret_cast<std::uintptr_t>(baseTable.data());
		Require(!fix.TryInstall() && detourCalls == 1 && !installedHook);
		detourError = NO_ERROR;
		Require(fix.TryInstall() && detourCalls == 2 && installedHook);
		Require(fix.TryInstall() && detourCalls == 2);
		Require(baseTable[0x18] == expectedTarget && parabolicTable[0x18] == expectedTarget);
	}

	void TestPoolPaths()
	{
		auto process = std::make_unique<RE::BSCullingProcess>();
		RE::BSGeometry geometry;
		auto* head = new (process->bytes.data() + 0x30150) std::uint32_t(0);
		auto* tail = new (process->bytes.data() + 0x30158) std::uint32_t(0);
		logger::warnings = 0;
		installedHook(process.get(), &geometry, -1);
		Require(forwardedCalls == 1 && forwardedProcess == process.get() && forwardedGeometry == &geometry && forwardedIndex == -1);
		installedHook(process.get(), &geometry, 0);
		Require(forwardedCalls == 1 && logger::warnings == 1);
		process->bytes[0x301D5] = 1;
		installedHook(process.get(), &geometry, -1);
		Require(forwardedCalls == 1 && logger::warnings == 1);
		*tail = 63;
		installedHook(process.get(), &geometry, 7);
		Require(forwardedCalls == 1);
		*tail = 64;
		installedHook(process.get(), &geometry, 7);
		Require(forwardedCalls == 2 && forwardedIndex == 7);
		*head = std::numeric_limits<std::uint32_t>::max() - 31;
		*tail = 32;
		installedHook(process.get(), &geometry, 0);
		Require(forwardedCalls == 3);
		*tail = 31;
		installedHook(process.get(), &geometry, 0);
		Require(forwardedCalls == 3 && logger::warnings == 1);
		process->bytes[0x301D5] = 0;
		installedHook(process.get(), &geometry, -1);
		Require(forwardedCalls == 4 && forwardedIndex == -1);
	}
}

int main()
{
	try {
		TestOwnership();
		TestInstallation();
		TestPoolPaths();
		std::cout << "Engine overflow guard checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
