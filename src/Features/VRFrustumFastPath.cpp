#include "Features/VRFrustumFastPath.h"
#include "Features/VRFrustumFastPathPolicy.h"
#include "Features/VRFrustumFastPathSignatures.h"
#include <atomic>
#include <cstring>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/VRFrustumTelemetry.h"
#	include <nlohmann/json.hpp>
#endif

namespace VRFrustumFastPath
{
	namespace
	{
		std::atomic_bool g_enabled{ false }, g_installed{ false };
		thread_local bool g_depth = false;
		bool (*g_native)(void*, void*) = nullptr;
		void (*g_depthNative)(bool, bool) = nullptr;
		bool Read(std::uintptr_t address, void* output, std::size_t size)
		{
			std::memcpy(output, reinterpret_cast<const void*>(address), size);
			return true;
		}
		template <class Observe>
		Result TryEvaluate(void* owner, void* object, Observe&& observe)
		{
			__try {
				return Evaluate(reinterpret_cast<std::uintptr_t>(owner), reinterpret_cast<std::uintptr_t>(object), Read, observe);
			} __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return {};
			}
		}
		template <std::size_t N>
		bool Matches(std::uintptr_t address, const std::array<std::uint8_t, N>& bytes)
		{
			__try {
				return std::memcmp(reinterpret_cast<const void*>(address), bytes.data(), N) == 0;
			} __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_bool g_verify{ false }, g_mismatch{ false };
		thread_local bool g_nativeOnly = false;
		struct Counts
		{
			std::uint64_t attempts = 0, handled = 0, tests = 0, fallbacks = 0, verified = 0, mismatches = 0;
		};
		thread_local Counts g_counts;
		std::array<std::atomic_uint64_t, 6> g_totals{};
		void PublishCounts()
		{
			const std::array values{ g_counts.attempts, g_counts.handled, g_counts.tests, g_counts.fallbacks, g_counts.verified, g_counts.mismatches };
			for (std::size_t i = 0; i < values.size(); ++i) g_totals[i].fetch_add(values[i], std::memory_order_relaxed);
			g_counts = {};
		}
		struct Mask
		{
			std::uintptr_t address;
			std::uint32_t value;
		};
		bool MasksMatch(const Mask* masks, std::size_t count)
		{
			__try {
				for (std::size_t i = 0; i < count; ++i) {
					std::uint32_t value;
					Read(masks[i].address, &value, sizeof(value));
					if (value != masks[i].value)
						return false;
				}
				return true;
			} __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}
		__declspec(noinline) bool Verify(void* owner, void* object)
		{
			std::array<Mask, StepLimit> masks;
			std::size_t count = 0;
			const auto candidate = TryEvaluate(owner, object, [&](auto a, auto m) { masks[count++] = { a, m }; });
			const bool native = g_native(owner, object);
			if (candidate.handled) {
				++g_counts.verified;
				if (candidate.accepted != native || !MasksMatch(masks.data(), count)) {
					++g_counts.mismatches;
					if (!g_mismatch.exchange(true, std::memory_order_acq_rel))
						VRFrustumTelemetry::AdvanceCollectionGeneration();
				}
			} else
				++g_counts.fallbacks;
			return native;
		}
#endif

		bool Compound(void* owner, void* object)
		{
			if (!g_depth || !g_enabled.load(std::memory_order_relaxed))
				return g_native(owner, object);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (g_nativeOnly || g_mismatch.load(std::memory_order_acquire))
				return g_native(owner, object);
			++g_counts.attempts;
			if (g_verify.load(std::memory_order_relaxed))
				return Verify(owner, object);
#endif
			const auto candidate = TryEvaluate(owner, object, [](auto, auto) {});
			if (candidate.handled) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				++g_counts.handled;
				g_counts.tests += candidate.tests;
#endif
				return candidate.accepted;
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			++g_counts.fallbacks;
#endif
			return g_native(owner, object);
		}
		void Depth(bool a, bool b)
		{
			const bool previous = g_depth;
			g_depth = true;
			const SKSE::stl::scope_exit restore([previous]() noexcept {
				g_depth = previous;
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (!previous)
					PublishCounts();
#endif
			});
			g_depthNative(a, b);
		}
	}

	void SetEnabled(bool enabled)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (g_enabled.exchange(enabled, std::memory_order_relaxed) != enabled)
			VRFrustumTelemetry::AdvanceCollectionGeneration();
#else
		g_enabled.store(enabled, std::memory_order_relaxed);
#endif
	}
	bool IsInstalled() { return g_installed.load(std::memory_order_acquire); }
	void Install()
	{
		static bool attempted = false;
		if (attempted)
			return;
		attempted = true;
		if (!REL::Module::IsVR() || REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
			return;
		const auto base = REL::Module::get().base();
		if (!Matches(base + 0xDA33C0, NativeCode::Compound) || !Matches(base + 0xDA54B0, NativeCode::NotFullyInside))
			return;
		// The native scalar negation constant is part of the admitted floating-point contract.
		constexpr std::array<std::uint8_t, 4> sign{ 0, 0, 0, 0x80 };
		std::int32_t displacement;
		std::memcpy(&displacement, NativeCode::NotFullyInside.data() + 0x12, 4);
		if (!Matches(base + 0xDA54B0 + 0x16 + displacement, sign))
			return;
		g_native = reinterpret_cast<decltype(g_native)>(base + 0xDA33C0);
		g_depthNative = reinterpret_cast<decltype(g_depthNative)>(REL::RelocationID(100421, 107139).address());
		LONG error = DetourTransactionBegin();
		if (error != NO_ERROR)
			return;
		bool pending = true;
		const SKSE::stl::scope_exit abort([&]() noexcept { if(pending) DetourTransactionAbort(); });
		error = DetourUpdateThread(GetCurrentThread());
		if (error == NO_ERROR)
			error = DetourAttach(reinterpret_cast<PVOID*>(&g_native), reinterpret_cast<PVOID>(Compound));
		if (error == NO_ERROR)
			error = DetourAttach(reinterpret_cast<PVOID*>(&g_depthNative), reinterpret_cast<PVOID>(Depth));
		if (error == NO_ERROR) {
			error = DetourTransactionCommit();
			pending = false;
		}
		g_installed.store(error == NO_ERROR, std::memory_order_release);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	NativeScope::NativeScope() : previous(g_nativeOnly) { g_nativeOnly = true; }
	NativeScope::~NativeScope() { g_nativeOnly = previous; }
	bool SetVerification(bool enabled)
	{
		if (!IsInstalled() || g_mismatch.load(std::memory_order_acquire))
			return false;
		if (g_verify.exchange(enabled, std::memory_order_relaxed) != enabled)
			VRFrustumTelemetry::AdvanceCollectionGeneration();
		return true;
	}
	nlohmann::json GetStatus()
	{
		const bool installed = IsInstalled(), enabled = g_enabled.load(), verification = g_verify.load(), mismatch = g_mismatch.load();
		const auto mode = !installed || !enabled || mismatch ? "native" : verification ? "verify" :
		                                                                                 "fast";
		return { { "installed", installed }, { "enabled", enabled }, { "verification", verification }, { "effectiveMode", mode },
			{ "mismatchLatched", mismatch }, { "attempts", g_totals[0].load() }, { "handled", g_totals[1].load() },
			{ "firstPlaneTests", g_totals[2].load() }, { "fallbacks", g_totals[3].load() }, { "verified", g_totals[4].load() },
			{ "mismatches", g_totals[5].load() },
			{ "scope", "calling-thread native depth pass only; complete unchanged-mask paths; detail samples force native; counters flush at depth return" } };
	}
#endif
}
