#include "EngineFixes/AlphaPassListPolicy.h"
#include "EngineFixes/VRAlphaPassListFix.h"
#include "Utils/VirtualFunctionHook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
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
		bool vr{ true }, transaction{}, published{};
		std::uint32_t version{ 0x010400F0 };
		long beginError{}, updateError{}, commitError{};
		std::size_t failedAttach{}, unreadableSite{ 3 }, modifiedSite{ 3 };
		std::vector<Attachment> attachments;
		std::string calls;
	} state;
	BOOL ReadMemory(HANDLE, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* read)
	{
		for (std::size_t i = 0; i < Sites.size(); ++i) {
			if (reinterpret_cast<std::uintptr_t>(address) != Base + Sites[i])
				continue;
			Check(size == Prefixes[i].size());
			if (i == state.unreadableSite)
				return FALSE;
			std::memcpy(output, Prefixes[i].data(), size);
			if (i == state.modifiedSite)
				static_cast<std::uint8_t*>(output)[0] ^= 1;
			*read = size;
			return TRUE;
		}
		throw std::runtime_error("unexpected native entrypoint");
	}
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
	void info(const char*, Args&&...)
	{}
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
#include "alpha_pass_list_hooks.h"
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
		Check(!installed && state.calls.empty());
		Reset();
		state.version = 0;
		Install();
		Check(!installed && state.calls.empty() && !installationError.empty());
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
		Check(reinterpret_cast<std::uintptr_t>(original) == Base + Sites[0] + 0x100);
		Check(reinterpret_cast<std::uintptr_t>(originalConsume) == Base + Sites[1] + 0x100);
		Check(reinterpret_cast<std::uintptr_t>(originalRenderGroup) == Base + Sites[2] + 0x100);
		Check(state.attachments[0].replacement == reinterpret_cast<void*>(&GuardedAppend));
		Check(state.attachments[1].replacement == reinterpret_cast<void*>(&ConsumeQueue));
		Check(state.attachments[2].replacement == reinterpret_cast<void*>(&RenderQueue));
		auto calls = state.calls;
		Install();
		Check(state.calls == calls);
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
		Test::Hooks();
		std::cout << "PASS native alpha hooks, lifetime, admission, rollback and control\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
