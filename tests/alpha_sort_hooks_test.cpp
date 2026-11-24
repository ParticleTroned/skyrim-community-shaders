#include "EngineFix.h"
#include "EngineFixes/AlphaSortPolicy.h"
#include "EngineFixes/VRAlphaPassListFix.h"
#include "EngineFixes/VRAlphaSortCode.h"
#include "Utils/VirtualFunctionHook.h"
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Test
{
	void Check(bool value, const std::source_location& location = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error("alpha sort hook check at line " + std::to_string(location.line()));
	}
	constexpr std::uintptr_t Base = 0x140000000;
	constexpr std::array<std::uintptr_t, 7> Sites{ 0x133FF00, 0x130A240, 0x134A552, 0x134A64C, 0x134A79B, 0x130A359, 0x15ADFFC };
	const std::array<std::vector<std::uint8_t>, 7> NativeBytes{
		std::vector<std::uint8_t>{ 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 },
		std::vector<std::uint8_t>{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 },
		std::vector<std::uint8_t>{
			0xF3, 0x0F, 0x10, 0x47, 0x7C, 0xF3, 0x0F, 0x10, 0x97, 0x88, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x8F, 0x94, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x06, 0xF3, 0x0F, 0x59, 0x56, 0x04, 0xF3, 0x0F, 0x59, 0x4E, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0x45, 0x84, 0xFF, 0x74, 0x05, 0xF3, 0x0F, 0x5C, 0x56, 0x0C },
		std::vector<std::uint8_t>{
			0xF3, 0x0F, 0x10, 0x90, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x52, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x98, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x02, 0xF3, 0x0F, 0x59, 0x80, 0xE4, 0x00, 0x00, 0x00, 0x8B, 0x05, 0xD5, 0xBB, 0x18, 0x02, 0xF3, 0x0F, 0x59, 0x4A, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0xF3, 0x0F, 0x5C, 0xD3 },
		std::vector<std::uint8_t>{
			0xF3, 0x0F, 0x10, 0x80, 0xE4, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x07, 0xF3, 0x0F, 0x10, 0xB0, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x77, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x4F, 0x08, 0xF3, 0x0F, 0x58, 0xF0, 0x41, 0x0F, 0x28, 0xC0, 0xF3, 0x0F, 0x59, 0x80, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0xF1, 0xF3, 0x0F, 0x5C, 0xF0 },
		std::vector<std::uint8_t>{
			0xF3, 0x0F, 0x10, 0x35, 0x9B, 0x3C, 0x2A, 0x00, 0x0F, 0x2F, 0xC6, 0x48, 0x8B, 0x8B, 0x58, 0x01, 0x00, 0x00, 0x76, 0x2A, 0x0F, 0x28, 0xDE, 0x89, 0x7C, 0x24, 0x20, 0x0F, 0x28, 0xD7, 0x48, 0x8D, 0x54, 0x24, 0x30, 0xE8, 0x6F, 0x03, 0x04, 0x00 },
		std::vector<std::uint8_t>{ 0xFF, 0xFF, 0x7F, 0xFF }
	};
	struct Fixture
	{
		bool vr{ true }, engineFixInstalled{}, duplicateFixInstalled{};
		std::uint32_t version{ 0x010400F0 };
		std::size_t queries{}, reads{}, modifiedSite{ 7 }, unreadableSite{ 7 }, shortReadSite{ 7 };
		long beginError{}, updateError{}, commitError{};
		std::size_t failedAttach{};
		std::string calls;
		std::vector<void**> originals;
	} state;
	BOOL ReadMemory(HANDLE, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* read)
	{
		++state.reads;
		for (std::size_t i = 0; i < Sites.size(); ++i) {
			if (reinterpret_cast<std::uintptr_t>(address) != Base + Sites[i])
				continue;
			Check(size == NativeBytes[i].size());
			if (i == state.unreadableSite)
				return FALSE;
			std::memcpy(output, NativeBytes[i].data(), size);
			if (i == state.modifiedSite)
				static_cast<std::uint8_t*>(output)[size - 1] ^= 1;
			*read = size - (i == state.shortReadSite ? 1 : 0);
			return TRUE;
		}
		return FALSE;
	}
}

bool EngineFix::IsInstalledByEngineFixes(const char* name)
{
	++Test::state.queries;
	if (std::strcmp(name, "AlphaDistanceSort") == 0)
		return Test::state.engineFixInstalled;
	Test::Check(std::strcmp(name, "AlphaPassListDuplicatePush") == 0);
	return Test::state.duplicateFixInstalled;
}
namespace RE
{
	struct NiPoint3
	{
		float x{}, y{}, z{};
	};
	struct NiTransform
	{
		struct
		{
			float entry[3][3]{};
		} rotate;
		NiPoint3 translate;
		float scale{ 1 };
	};
	struct NiBound
	{
		NiPoint3 center;
		float radius{};
	};
#pragma pack(push, 4)
	struct NiAVObject
	{
		std::array<std::byte, 0x7C> padding{};
		NiTransform world;
		std::array<std::byte, 0x34> middle{};
		NiBound worldBound;
	};
#pragma pack(pop)
	struct BSGeometry : NiAVObject
	{};
	struct NiCamera : NiAVObject
	{};
	struct NiAccumulator
	{
		std::array<std::byte, 0x10> padding{};
		NiCamera* camera{};
	};
	struct BSRenderPass
	{
		std::array<std::byte, 0x10> padding{};
		BSGeometry* geometry{};
		std::array<std::byte, 0x18> middle{};
		BSRenderPass* passGroupNext{};
	};
}
namespace SKSE
{
	constexpr std::uint32_t RUNTIME_VR_1_4_15 = 0x010400F0;
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return Test::state.vr; }
		static Module& get()
		{
			static Module instance;
			return instance;
		}
		auto version() const { return Test::state.version; }
		auto base() const { return Test::Base; }
	};
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}
namespace VRAlphaPassListFix
{
	RetirementScope::RetirementScope() noexcept {}
	RetirementScope::~RetirementScope() = default;
}
long DetourTransactionBegin()
{
	Test::state.calls += 'B';
	return Test::state.beginError;
}
long DetourUpdateThread(HANDLE)
{
	Test::state.calls += 'U';
	return Test::state.updateError;
}
long DetourAttach(void** original, void*)
{
	Test::state.calls += 'A';
	Test::state.originals.push_back(original);
	return Test::state.failedAttach == Test::state.originals.size() ? ERROR_INVALID_BLOCK : NO_ERROR;
}
long DetourTransactionAbort()
{
	Test::state.calls += 'X';
	return NO_ERROR;
}
long DetourTransactionCommit()
{
	Test::state.calls += 'C';
	return Test::state.commitError;
}
#define ReadProcessMemory Test::ReadMemory
#include "alpha_sort_hooks.h"
#undef ReadProcessMemory

namespace Test
{
	using namespace VRAlphaSort;
	void Reset()
	{
		state = {};
		installed = attempted = engineFixesInstalled = false;
		originalSort = nullptr;
		originalRender = nullptr;
		originalKeys = {};
		for (auto& code : keyCode)
			code.reset();
		installationError.clear();
		sortedQueues = consumeScopes = 0;
	}
	void CheckProvider(const char* provider, bool active)
	{
		const auto result = GetDiagnostics();
		Check(result.at("provider") == provider && result.at("active") == active);
		Check(result.at("installed") == installed);
	}
	void Admission()
	{
		Reset();
		state.vr = false;
		Install();
		Check(!attempted && state.queries == 0 && state.reads == 0 && state.calls.empty());
		CheckProvider("none", false);
		Reset();
		state.version = 0;
		Install();
		Check(!installed && state.queries == 0 && state.reads == 0 && !installationError.empty());
		for (const bool duplicate : { false, true }) {
			Reset();
			state.engineFixInstalled = true;
			state.duplicateFixInstalled = duplicate;
			Install();
			Check(!installed && engineFixesInstalled && state.queries == 1 && state.reads == 0 && state.calls.empty());
			Check(!originalSort && !originalRender && originalKeys == std::array<void*, 3>{} && !keyCode[0]);
			Check(installationError.empty() && sortedQueues == 0 && consumeScopes == 0);
			CheckProvider("engineFixes", true);
			Install();
			Check(state.queries == 1 && state.calls.empty());
			Reset();
			state.duplicateFixInstalled = duplicate;
			Install();
			Check(installed && !engineFixesInstalled && state.queries == 1 && state.reads == 7);
			Check(state.calls == "BUAAAAAC" && state.originals.size() == 5 && installationError.empty());
			CheckProvider("csx", true);
			Install();
			Check(state.queries == 1 && state.reads == 7 && state.calls == "BUAAAAAC");
		}
	}
	void ConflictsAndFailures()
	{
		for (std::size_t site = 0; site < Sites.size(); ++site) {
			for (int failure = 0; failure < 3; ++failure) {
				Reset();
				if (failure == 0)
					state.modifiedSite = site;
				if (failure == 1)
					state.unreadableSite = site;
				if (failure == 2)
					state.shortReadSite = site;
				Install();
				Check(!installed && !engineFixesInstalled && !installationError.empty() && state.calls.empty());
				CheckProvider("none", false);
			}
		}
		Reset();
		state.beginError = ERROR_INVALID_OPERATION;
		Install();
		Check(!installed && state.calls == "B");
		Reset();
		state.updateError = ERROR_ACCESS_DENIED;
		Install();
		Check(!installed && state.calls == "BUX");
		for (std::size_t failed = 1; failed <= 5; ++failed) {
			Reset();
			state.failedAttach = failed;
			Install();
			Check(!installed && state.calls == "BU" + std::string(failed, 'A') + "X");
			CheckProvider("none", false);
		}
		Reset();
		state.commitError = ERROR_INVALID_DATA;
		Install();
		Check(!installed && state.calls == "BUAAAAAC" && !installationError.empty());
		CheckProvider("none", false);
	}
}
int main()
{
	try {
		Test::Admission();
		Test::ConflictsAndFailures();
		std::cout << "PASS alpha-sort provider handoff, independent fallback, guards and rollback\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
