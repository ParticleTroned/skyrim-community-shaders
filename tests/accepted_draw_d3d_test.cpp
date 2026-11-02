#include "CSXAPI/CSacceptedDrawapi.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#define STDMETHODCALLTYPE
using UINT = unsigned;
using INT = int;
constexpr long NO_ERROR = 0;
constexpr unsigned D3D11_DEVICE_CONTEXT_IMMEDIATE = 0;

// The first word models the COM table. Native entry points and publication are
// observable; the observer selection and forwarding code is production code.
struct ID3D11DeviceContext
{
	void** table;
	unsigned type = D3D11_DEVICE_CONTEXT_IMMEDIATE;
	unsigned GetType() const { return type; }
};

namespace logger
{
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

namespace Util
{
	enum class VirtualHookMethod
	{
		Detour,
		VTable,
		Clone
	};
	struct VirtualHookResult
	{
		long error = NO_ERROR;
		VirtualHookMethod method = VirtualHookMethod::Detour;
	};
	struct Record
	{
		void* target;
		void* hook;
	};
	std::map<void**, Record> storage;
	std::map<void*, void*> detours;
	VirtualHookMethod mode = VirtualHookMethod::Detour;
	unsigned calls = 0, failCall = 0;
	bool publishOnFailure = false;
	VirtualHookResult InstallVirtualFunctionHook(ID3D11DeviceContext* context, std::size_t slot, void** original, void* hook)
	{
		++calls;
		auto* native = context->table[slot];
		if (auto previous = storage.find(original); previous != storage.end()) {
			if (*original != previous->second.target || hook != previous->second.hook)
				throw std::runtime_error("Original call storage was republished with a different target");
		} else {
			if (*original)
				throw std::runtime_error("A populated trampoline was used for a new target");
			*original = native;
			storage.emplace(original, Record{ native, hook });
		}
		const bool failed = calls == failCall;
		if (!failed || publishOnFailure) {
			if (mode == VirtualHookMethod::Detour)
				detours[native] = hook;
			else
				context->table[slot] = hook;
		}
		return { failed ? 1 : NO_ERROR, mode };
	}
}

namespace CSX::Api
{
	using Replay = void (*)(ID3D11DeviceContext*, const CSXAcceptedDrawAPI::Arguments&);
	unsigned deliveries = 0, indexedCalls = 0, instancedCalls = 0;
	unsigned indexedTarget = 0, instancedTarget = 0;
	bool replayNext = false;
	CSXAcceptedDrawAPI::Arguments last{};
	void PublishAcceptedDraw(ID3D11DeviceContext* context, const CSXAcceptedDrawAPI::Arguments& args, Replay replay)
	{
		++deliveries;
		last = args;
		if (std::exchange(replayNext, false))
			replay(context, args);
	}
}

#include "accepted_draw_d3d_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template <unsigned Index>
	void Indexed(ID3D11DeviceContext*, UINT count, UINT start, INT base)
	{
		++CSX::Api::indexedCalls;
		CSX::Api::indexedTarget = Index;
		Require(count == 12 && start == 3 && base == -4, "Indexed draw arguments changed");
	}
	template <unsigned Index>
	void Instanced(ID3D11DeviceContext*, UINT count, UINT instances, UINT start, INT base, UINT first)
	{
		++CSX::Api::instancedCalls;
		CSX::Api::instancedTarget = Index;
		Require(count == 12 && instances == 2 && start == 3 && base == -4 && first == 5, "Instanced draw arguments changed");
	}
	template <std::size_t... Index>
	auto NativeIndexed(std::index_sequence<Index...>)
	{
		return std::array{ reinterpret_cast<void*>(&Indexed<Index>)... };
	}
	template <std::size_t... Index>
	auto NativeInstanced(std::index_sequence<Index...>)
	{
		return std::array{ reinterpret_cast<void*>(&Instanced<Index>)... };
	}

	template <std::size_t... Index>
	void ResetOriginals(std::index_sequence<Index...>)
	{
		((CSX::Api::SubmissionHooks<Index>::originalIndexed = nullptr,
			 CSX::Api::SubmissionHooks<Index>::originalInstanced = nullptr),
			...);
	}
	void Reset(Util::VirtualHookMethod mode)
	{
		CSX::Api::indexedTargets.fill({});
		CSX::Api::instancedTargets.fill({});
		ResetOriginals(std::make_index_sequence<8>{});
		Util::storage.clear();
		Util::detours.clear();
		Util::mode = mode;
		Util::calls = Util::failCall = 0;
		Util::publishOnFailure = false;
		CSX::Api::deliveries = CSX::Api::indexedCalls = CSX::Api::instancedCalls = 0;
		CSX::Api::replayNext = false;
	}
	std::array<void*, 21> Table(unsigned indexed = 0, unsigned instanced = 0)
	{
		static const auto indexedEntries = NativeIndexed(std::make_index_sequence<9>{});
		static const auto instancedEntries = NativeInstanced(std::make_index_sequence<9>{});
		std::array<void*, 21> table{};
		table[12] = indexedEntries[indexed];
		table[20] = instancedEntries[instanced];
		return table;
	}
	void* DispatchTarget(ID3D11DeviceContext& context, unsigned slot)
	{
		auto target = context.table[slot];
		const auto detour = Util::detours.find(target);
		return detour == Util::detours.end() ? target : detour->second;
	}
	void Draw(ID3D11DeviceContext& context)
	{
		reinterpret_cast<CSX::Api::SubmissionHooks<0>::Indexed>(DispatchTarget(context, 12))(&context, 12, 3, -4);
		reinterpret_cast<CSX::Api::SubmissionHooks<0>::Instanced>(DispatchTarget(context, 20))(&context, 12, 2, 3, -4, 5);
	}
}

int main()
try {
	using CSX::Api::InstallAcceptedDrawD3DHooks;
	for (const auto mode : { Util::VirtualHookMethod::Detour, Util::VirtualHookMethod::VTable, Util::VirtualHookMethod::Clone }) {
		Reset(mode);
		std::array<std::array<void*, 21>, 24> tables;
		for (auto& table : tables) {
			table = Table();
			ID3D11DeviceContext context{ table.data() };
			Require(InstallAcceptedDrawD3DHooks(&context), "Same native targets exhausted observer capacity");
			const auto calls = Util::calls;
			Require(InstallAcceptedDrawD3DHooks(&context) && calls == Util::calls, "Repeated install republished an observer");
			Draw(context);
		}
		Require(CSX::Api::deliveries == 48 && CSX::Api::indexedCalls == 24 && CSX::Api::instancedCalls == 24,
			"Shared targets duplicated or lost accepted draw delivery");
		Require(Util::calls == (mode == Util::VirtualHookMethod::Detour ? 2u : 48u), "Wrong target/table coverage");
		ID3D11DeviceContext last{ tables.back().data() };
		CSX::Api::replayNext = true;
		Draw(last);
		Require(CSX::Api::deliveries == 50 && CSX::Api::indexedCalls == 26 && CSX::Api::instancedCalls == 25,
			"Replay recursively published an accepted draw or lost the native draw");
	}

	Reset(Util::VirtualHookMethod::Detour);
	for (unsigned index = 0; index < 8; ++index) {
		auto table = Table(0, index);
		ID3D11DeviceContext context{ table.data() };
		Require(InstallAcceptedDrawD3DHooks(&context), "Independent native methods consumed the wrong slot bank");
		Draw(context);
		Require(CSX::Api::indexedTarget == 0 && CSX::Api::instancedTarget == index,
			"Observer forwarded to another native target");
	}
	Require(Util::calls == 9, "Shared indexed target was detoured more than once");
	auto ninth = Table(0, 8);
	ID3D11DeviceContext overflow{ ninth.data() };
	Require(!InstallAcceptedDrawD3DHooks(&overflow) && Util::calls == 9, "Capacity overflow changed dispatch");

	for (bool partial : { false, true }) {
		Reset(Util::VirtualHookMethod::VTable);
		Util::failCall = 2;
		Util::publishOnFailure = partial;
		auto table = Table();
		ID3D11DeviceContext context{ table.data() };
		Require(!InstallAcceptedDrawD3DHooks(&context), "Partial installation reported full readiness");
		Require(!InstallAcceptedDrawD3DHooks(&context) && Util::calls == 2, "A failed target was retried with another trampoline");
		auto otherTable = Table();
		ID3D11DeviceContext other{ otherTable.data() };
		Require(!InstallAcceptedDrawD3DHooks(&other) && Util::calls == 3, "Failure state was not shared across tables");
	}
	Reset(Util::VirtualHookMethod::Detour);
	auto table = Table();
	ID3D11DeviceContext deferred{ table.data(), 1 };
	Require(!InstallAcceptedDrawD3DHooks(nullptr) && !InstallAcceptedDrawD3DHooks(&deferred) && Util::calls == 0,
		"Invalid/deferred contexts changed native dispatch");
	std::cout << "PASS: shared target reuse, independent slots, replay, capacity and partial-publication failure\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
