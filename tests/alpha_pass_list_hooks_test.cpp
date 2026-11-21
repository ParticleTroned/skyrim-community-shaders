#include "EngineFix.h"
#include "EngineFixes/AlphaPassListPolicy.h"
#include "EngineFixes/VRAlphaPassListFix.h"
#include "Utils/VirtualFunctionHook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <format>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Test
{
	void Check(bool value, const std::source_location& location = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error("alpha hook check at line " + std::to_string(location.line()));
	}
	struct Attachment
	{
		void** original;
		void* replacement;
	};
	constexpr std::uintptr_t Base = 0x140000000;
	constexpr std::array<std::uintptr_t, 3> Sites{ 0x1347150, 0x134A6F0, 0x1347320 };
	const std::array<std::vector<std::uint8_t>, 3> Prefixes{
		std::vector<std::uint8_t>{ 0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF },
		std::vector<std::uint8_t>{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 },
		std::vector<std::uint8_t>{ 0x40, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30 }
	};
	struct Fixture
	{
		bool vr{ true }, transaction{}, published{}, engineFixInstalled{};
		std::uint32_t version{ 0x010400F0 };
		long beginError{}, updateError{}, commitError{};
		std::size_t failedAttach{}, unreadableSite{ 3 }, modifiedSite{ 3 };
		std::size_t ownershipQueries{}, nativeReads{};
		std::array<bool, 2> loadedModules{};
		std::uintptr_t hookDestination{};
		HMODULE hookOwner{};
		bool partialHookRead{};
		std::vector<std::pair<std::uintptr_t, std::vector<std::uint8_t>>> hookCode;
		std::vector<Attachment> attachments;
		std::string calls, infoMessage;
	} state;
	BOOL ReadMemory(HANDLE, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* read)
	{
		++state.nativeReads;
		const auto location = reinterpret_cast<std::uintptr_t>(address);
		for (const auto& [start, code] : state.hookCode) {
			if (location < start || location - start > code.size() || size > code.size() - (location - start))
				continue;
			std::memcpy(output, code.data() + (location - start), size);
			*read = state.partialHookRead && size == 6 ? size - 1 : size;
			return TRUE;
		}
		for (std::size_t i = 0; i < Sites.size(); ++i) {
			if (reinterpret_cast<std::uintptr_t>(address) != Base + Sites[i])
				continue;
			Check(size <= Prefixes[i].size());
			if (i == state.unreadableSite)
				return FALSE;
			std::memcpy(output, Prefixes[i].data(), size);
			if (i == state.modifiedSite)
				static_cast<std::uint8_t*>(output)[0] ^= 1;
			*read = size;
			return TRUE;
		}
		return FALSE;
	}
	HMODULE Module(std::size_t index) { return reinterpret_cast<HMODULE>(0x10000 + index * 0x10000); }
	HMODULE FindModule(const wchar_t* name)
	{
		const auto index = std::wcscmp(name, L"EngineFixes.dll") == 0 ? 0 : 1;
		Check(index == 0 || std::wcscmp(name, L"EngineFixesVR.dll") == 0);
		return state.loadedModules[index] ? Module(index) : nullptr;
	}
	BOOL FindOwner(DWORD flags, LPCWSTR address, HMODULE* owner)
	{
		Check(flags == (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT));
		if (reinterpret_cast<std::uintptr_t>(address) != state.hookDestination || !state.hookOwner)
			return FALSE;
		*owner = state.hookOwner;
		return TRUE;
	}
}
bool EngineFix::IsInstalledByEngineFixes(const char* name)
{
	Test::Check(std::strcmp(name, "AlphaPassListDuplicatePush") == 0);
	++Test::state.ownershipQueries;
	return Test::state.engineFixInstalled;
}
namespace RE
{
	struct NiPoint3
	{
		float x{}, y{}, z{};
	};
	struct BSRenderPass
	{
		std::array<std::byte, 0x30> storage{};
		BSRenderPass* passGroupNext{};
	};
	struct BSBatchRenderer
	{
		struct GeometryGroup
		{
			void* renderer{};
			struct
			{
				BSRenderPass* head{};
				BSRenderPass* tail{};
			} passList;
			void* unknown{};
			float depth{};
			std::uint16_t count{};
			std::uint8_t flags{ 1 };
		};
	};
}
namespace globals
{
	struct State
	{
		std::atomic_uint32_t frameCountAtomic{};
	} fixture;
	State* state = &fixture;
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
			static Module module;
			return module;
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
	void info(const char* message, Args&&...)
	{
		Test::state.infoMessage = message;
	}
}
long DetourTransactionBegin()
{
	Test::state.calls += 'B';
	if (Test::state.beginError)
		return Test::state.beginError;
	Test::state.transaction = true;
	return NO_ERROR;
}
long DetourUpdateThread(HANDLE)
{
	Test::state.calls += 'U';
	return Test::state.updateError;
}
long DetourAttach(void** original, void* replacement)
{
	Test::Check(Test::state.transaction);
	Test::state.calls += 'A';
	Test::state.attachments.push_back({ original, replacement });
	return Test::state.failedAttach == Test::state.attachments.size() ? ERROR_INVALID_BLOCK : NO_ERROR;
}
long DetourTransactionAbort()
{
	Test::state.calls += 'X';
	Test::state.transaction = false;
	return NO_ERROR;
}
long DetourTransactionCommit()
{
	Test::Check(Test::state.transaction && Test::state.attachments.size() == 3);
	Test::state.calls += 'C';
	Test::state.transaction = false;
	if (Test::state.commitError)
		return Test::state.commitError;
	for (const auto& attachment : Test::state.attachments)
		*attachment.original = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(*attachment.original) + 0x100);
	Test::state.published = true;
	return NO_ERROR;
}
#define ReadProcessMemory Test::ReadMemory
#define GetModuleHandleW Test::FindModule
#define GetModuleHandleExW Test::FindOwner
#include "alpha_pass_list_hooks.h"
#undef GetModuleHandleExW
#undef GetModuleHandleW
#undef ReadProcessMemory

namespace Test
{
	using namespace VRAlphaPassListFix;
	void Reset()
	{
		state = {};
		installed = false;
		installationError.clear();
		original = nullptr;
		originalConsume = nullptr;
		originalRenderGroup = nullptr;
		enabled = true;
		++generation;
		duplicates = 0;
	}
	void Installation()
	{
		Reset();
		state.vr = false;
		Install();
		Check(!installed && state.calls.empty() && state.ownershipQueries == 0);
		Reset();
		state.version = 0;
		Install();
		Check(!installed && state.calls.empty() && !installationError.empty() && state.ownershipQueries == 0);
		Reset();
		state.engineFixInstalled = true;
		Install();
		Check(!installed && state.ownershipQueries == 1 && state.nativeReads == 0 && state.calls.empty());
		Check(!original && !originalConsume && !originalRenderGroup && installationError.empty());
		Check(state.infoMessage.find("already installed by Engine Fixes") != std::string::npos);
		Check(!Control(nlohmann::json::object()).at("active").get<bool>());
		Reset();
		state.modifiedSite = 0;
		Install();
		Check(!installationError.empty());
		state.engineFixInstalled = true;
		Install();
		Check(!installed && installationError.empty() && state.calls.empty());
		for (std::size_t site = 0; site < 3; ++site) {
			Reset();
			state.modifiedSite = site;
			Install();
			Check(!installed && state.calls.empty());
			Reset();
			state.unreadableSite = site;
			Install();
			Check(!installed && state.calls.empty());
		}
		Reset();
		state.beginError = ERROR_INVALID_OPERATION;
		Install();
		Check(!installed && state.calls == "B");
		Reset();
		state.updateError = ERROR_ACCESS_DENIED;
		Install();
		Check(!installed && state.calls == "BUX");
		for (std::size_t site = 1; site <= 3; ++site) {
			Reset();
			state.failedAttach = site;
			Install();
			Check(!installed && !state.published && !state.transaction && state.calls == "BU" + std::string(site, 'A') + "X");
		}
		Reset();
		state.commitError = ERROR_INVALID_DATA;
		Install();
		Check(!installed && !state.published && state.calls == "BUAAAC");
		state = {};
		Install();
		Check(installed && state.published && installationError.empty());
		Check(state.ownershipQueries == 1 && state.nativeReads == 3);
		Check(reinterpret_cast<std::uintptr_t>(original) == Base + Sites[0] + 0x100);
		Check(reinterpret_cast<std::uintptr_t>(originalConsume) == Base + Sites[1] + 0x100);
		Check(reinterpret_cast<std::uintptr_t>(originalRenderGroup) == Base + Sites[2] + 0x100);
		Check(state.attachments[0].replacement == reinterpret_cast<void*>(&GuardedAppend));
		Check(state.attachments[1].replacement == reinterpret_cast<void*>(&ConsumeQueue));
		Check(state.attachments[2].replacement == reinterpret_cast<void*>(&RenderQueue));
		auto calls = state.calls;
		Install();
		Check(state.calls == calls && state.ownershipQueries == 1);
	}
	void SetAppendHook(bool relay, bool indirect)
	{
		const auto entry = Base + Sites[0];
		const auto relayAddress = entry - 0x1000;
		state.loadedModules[0] = true;
		state.hookDestination = entry + 0x2000;
		state.hookOwner = Module(0);
		std::vector<std::uint8_t> code(Prefixes[0].size(), 0);
		if (indirect) {
			code[0] = 0xFF;
			code[1] = 0x25;
			std::memcpy(code.data() + 6, &state.hookDestination, sizeof(state.hookDestination));
		} else {
			code[0] = 0xE9;
			const auto target = relay ? relayAddress : state.hookDestination;
			const auto displacement = static_cast<std::int32_t>(target - (entry + 5));
			std::memcpy(code.data() + 1, &displacement, sizeof(displacement));
		}
		state.hookCode.emplace_back(entry, std::move(code));
		if (relay) {
			std::vector<std::uint8_t> relayCode(14, 0);
			relayCode[0] = 0xFF;
			relayCode[1] = 0x25;
			std::memcpy(relayCode.data() + 6, &state.hookDestination, sizeof(state.hookDestination));
			state.hookCode.emplace_back(relayAddress, std::move(relayCode));
		}
	}
	void PrivateOwnership()
	{
		for (const auto module : { 0u, 1u }) {
			for (const auto shape : { 0, 1, 2 }) {
				Reset();
				SetAppendHook(shape == 1, shape == 2);
				state.loadedModules = {};
				state.loadedModules[module] = true;
				state.hookOwner = Module(module);
				Install();
				Check(!installed && state.ownershipQueries == 1 && state.calls.empty());
				Check(state.nativeReads <= 4 && installationError.empty());
				Check(!original && !originalConsume && !originalRenderGroup);
				Check(state.infoMessage.find("append hook owned by Engine Fixes") != std::string::npos);
				bool rejected = false;
				try {
					Control({ { "alphaPassIntegrity", { { "enabled", true } } } });
				} catch (const std::invalid_argument&) {
					rejected = true;
				}
				Check(rejected);
			}
		}
		for (const auto failure : { 0, 1, 2, 3, 4, 5, 6, 7, 8 }) {
			Reset();
			SetAppendHook(true, false);
			switch (failure) {
			case 0:
				state.loadedModules = {};
				break;
			case 1:
				state.hookOwner = Module(2);
				break;
			case 2:
				state.hookCode.pop_back();
				break;
			case 3:
				state.hookCode.back().second.resize(6);
				break;
			case 4:
				state.hookCode.front().second[0] = 0xE8;
				break;
			case 5:
				state.partialHookRead = true;
				break;
			case 6:
				{
					const auto entry = Base + Sites[0];
					std::memcpy(state.hookCode.back().second.data() + 6, &entry, sizeof(entry));
					break;
				}
			case 7:
				std::fill_n(state.hookCode.back().second.data() + 6, sizeof(std::uintptr_t), 0);
				break;
			case 8:
				state.hookCode.clear();
				state.unreadableSite = 0;
				break;
			}
			Install();
			Check(!installed && !installationError.empty() && state.calls.empty() && state.infoMessage.empty());
			Check(state.nativeReads <= 4);
		}
		Reset();
		state.loadedModules = { true, true };
		Install();
		Check(installed && state.published && state.nativeReads == 3);
	}
	std::size_t forwarded{};
	std::uint8_t receivedFront{};
	void NativeAppend(Group* group, RE::BSRenderPass* pass, std::uint8_t front)
	{
		++forwarded;
		receivedFront = front;
		if (!group || !pass || !(group->flags & 1))
			return;
		if (front) {
			pass->passGroupNext = group->passList.head;
			if (!group->passList.head)
				group->passList.tail = pass;
			group->passList.head = pass;
		} else {
			if (group->passList.head)
				group->passList.tail->passGroupNext = pass;
			else
				group->passList.head = pass;
			group->passList.tail = pass;
			pass->passGroupNext = nullptr;
		}
		++group->count;
	}
	void Hooks()
	{
		Reset();
		original = NativeAppend;
		forwarded = 0;
		Group group;
		RE::BSRenderPass a, b, c;
		GuardedAppend(&group, &a, 0);
		GuardedAppend(&group, &b, 0);
		GuardedAppend(&group, &c, 7);
		Check(forwarded == 3 && receivedFront == 7 && group.passList.head == &c && c.passGroupNext == &a);
		GuardedAppend(&group, &a, 0);
		GuardedAppend(&group, &b, 1);
		Check(forwarded == 3 && duplicates == 2 && group.count == 3);
		group.flags = 0;
		GuardedAppend(&group, &a, 0);
		Check(forwarded == 4);
		GuardedAppend(nullptr, &a, 0);
		GuardedAppend(&group, nullptr, 0);
		Check(forwarded == 6);
		group.flags = 1;
		enabled = false;
		GuardedAppend(&group, &a, 0);
		Check(forwarded == 7);
		std::uintptr_t next{};
		Check(!ReadNext(0, next));
		originalRenderGroup = [](Group*, std::uint32_t flags) { Check(flags == 123); };
		auto epoch = generation.load();
		group.flags = 0;
		RenderQueue(&group, 123);
		Check(generation == epoch);
		group.flags = 1;
		RenderQueue(&group, 123);
		Check(generation == epoch + 2);
		originalRenderGroup = [](Group*, std::uint32_t) { throw std::runtime_error("native exception"); };
		epoch = generation.load();
		bool threw = false;
		try {
			RenderQueue(&group, 0);
		} catch (const std::runtime_error&) {
			threw = true;
		}
		Check(threw && generation == epoch + 2);
		originalConsume = [](void* renderer, const RE::NiPoint3* direction, float closest, float minimum, std::uint32_t flags) {
			Check(renderer == reinterpret_cast<void*>(1) && direction->x == 2 && closest == 3 && minimum == 4 && flags == 5);
		};
		RE::NiPoint3 direction{ 2, 0, 0 };
		epoch = generation.load();
		ConsumeQueue(reinterpret_cast<void*>(1), &direction, 3, 4, 5);
		Check(generation == epoch + 2);
		installed = true;
		auto result = Control({ { "alphaPassIntegrity", { { "enabled", true } } } });
		Check(result.at("active") && result.at("duplicatesSuppressed") == 2);
		bool foreignFieldRejected = false;
		try {
			Control({ { "alphaPassIntegrity", { { "enabled", false } } }, { "reverseZEnabled", true } });
		} catch (const std::invalid_argument&) {
			foreignFieldRejected = true;
		}
		Check(foreignFieldRejected && enabled);
		for (const auto& settings : { nlohmann::json::object(), nlohmann::json{ { "enabled", 1 } }, nlohmann::json{ { "enabled", false }, { "extra", true } } }) {
			bool rejected = false;
			try {
				Control({ { "alphaPassIntegrity", settings } });
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Check(rejected && enabled);
		}
	}
}
int main()
{
	try {
		Test::Installation();
		Test::PrivateOwnership();
		Test::Hooks();
		std::cout << "PASS native alpha hooks, lifetime, admission, rollback and control\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
