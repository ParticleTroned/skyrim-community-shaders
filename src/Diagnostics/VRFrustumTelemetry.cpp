#include "Diagnostics/VRFrustumTelemetry.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Globals.h"
#	include "State.h"
#	include <cstring>
#	include <intrin.h>
#	include <nlohmann/json.hpp>

namespace VRFrustumTelemetry
{
	namespace
	{
		constexpr std::size_t ThreadCapacity = 64;
		constexpr std::size_t PassCount = static_cast<std::size_t>(Pass::Count);
		constexpr DWORD ExecutableReadProtection = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		constexpr DWORD ReadProtection = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | ExecutableReadProtection;
		constexpr std::array<const char*, 3> RoutineNames{ "compound_object", "sphere_intersect", "sphere_not_fully_inside" };
		constexpr std::array<const char*, PassCount> PassNames{ "unknown", "player_view", "depth", "world", "first_person", "water", "shadowmask", "directional_shadow", "spot_shadow", "point_shadow", "cubemap", "recovery" };
		constexpr std::array<const char*, CounterCount> CounterNames{ "calls", "completed", "rawTrue", "rawFalse", "firstAddressKeys", "repeatedAddressKeys", "trackingOverflowCalls", "unknownFrameCalls" };

		struct ThreadSlot
		{
			std::atomic_bool claimed{ false };
			std::atomic_uint32_t threadId{ 0 };
			std::int64_t registeredAtQpc = 0;
			ThreadCounters counters;
			std::array<std::atomic_uint64_t, PassCount> passEntries{};
		};
		std::array<ThreadSlot, ThreadCapacity> g_threads{};
		std::atomic_uint64_t g_threadOverflowCalls{ 0 };
		std::atomic_uint64_t g_threadOverflowPassEntries{ 0 };
		// Low bit enables collection; the remaining bits invalidate address tracking across toggles.
		std::atomic_uint64_t g_control{ 3 };
		std::atomic_bool g_attempted{ false };
		std::atomic_bool g_installed{ false };
		std::atomic_long g_installError{ ERROR_NOT_READY };
		thread_local Context g_context;

		ThreadSlot* GetThreadSlot()
		{
			thread_local ThreadSlot* slot = []() -> ThreadSlot* {
				for (auto& candidate : g_threads) {
					bool available = false;
					if (candidate.claimed.compare_exchange_strong(available, true, std::memory_order_relaxed)) {
						LARGE_INTEGER registered{};
						QueryPerformanceCounter(&registered);
						candidate.registeredAtQpc = registered.QuadPart;
						candidate.threadId.store(GetCurrentThreadId(), std::memory_order_release);
						return &candidate;
					}
				}
				return nullptr;
			}();
			return slot;
		}

		std::int64_t ReadRendererCameraIndex()
		{
			if (!globals::game::graphicsState)
				return UnknownCameraIndex;
			__try {
				// Preserve the raw field; its mapping to a culling eye is not established.
				return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::byte*>(globals::game::graphicsState) + 0x58);
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return UnknownCameraIndex;
			}
		}

		Row* Begin(Routine a_routine, const void* a_owner, const void* a_object, const void* a_planes, std::uintptr_t a_caller)
		{
			const auto control = g_control.load(std::memory_order_relaxed);
			if ((control & 1) == 0)
				return nullptr;
			auto* slot = GetThreadSlot();
			if (!slot) {
				g_threadOverflowCalls.fetch_add(1, std::memory_order_relaxed);
				return nullptr;
			}
			const bool frameKnown = globals::state != nullptr;
			const auto frame = frameKnown ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0;
			const auto context = ActiveContext(g_context, control);
			return slot->counters.Begin({ a_routine, context.pass, context.cameraIndex, a_caller },
				{ control >> 1, frame, 0, reinterpret_cast<std::uintptr_t>(a_owner), reinterpret_cast<std::uintptr_t>(a_object), reinterpret_cast<std::uintptr_t>(a_planes) }, frameKnown);
		}

		struct CompoundHook
		{
			static inline bool (*func)(void*, void*) = nullptr;
			static bool thunk(void* owner, void* object)
			{
				return Forward(Begin(Routine::Compound, owner, object, nullptr, reinterpret_cast<std::uintptr_t>(_ReturnAddress())), func, owner, object);
			}
		};
		template <Routine R>
		struct SphereHook
		{
			static inline bool (*func)(void*, void*, void*) = nullptr;
			static bool thunk(void* owner, void* bound, void* planes)
			{
				return Forward(Begin(R, owner, bound, planes, reinterpret_cast<std::uintptr_t>(_ReturnAddress())), func, owner, bound, planes);
			}
		};
		template <Pass P, class... Args>
		struct PassHook
		{
			static inline void (*func)(Args...) = nullptr;
			static void thunk(Args... args)
			{
				auto scope = EnterScope(P, true);
				func(args...);
			}
		};

		bool HasMemoryProtection(std::uintptr_t a_address, std::size_t a_size, DWORD a_allowedProtection)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (!VirtualQuery(reinterpret_cast<void*>(a_address), &info, sizeof(info)) ||
				info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
				(info.Protect & a_allowedProtection) == 0)
				return false;
			const auto offset = a_address - reinterpret_cast<std::uintptr_t>(info.BaseAddress);
			return offset < info.RegionSize && a_size <= info.RegionSize - offset;
		}

		template <std::size_t N>
		bool MatchesPrefix(std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_prefix)
		{
			return HasMemoryProtection(a_address, N, ExecutableReadProtection) && std::memcmp(reinterpret_cast<void*>(a_address), a_prefix.data(), N) == 0;
		}

		std::uintptr_t ReadVtableTarget(const std::uintptr_t* a_slot)
		{
			if (!HasMemoryProtection(reinterpret_cast<std::uintptr_t>(a_slot), sizeof(*a_slot), ReadProtection))
				return 0;
			__try {
				return *a_slot;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return 0;
			}
		}

		template <class T>
		LONG Attach(std::uintptr_t a_address)
		{
			T::func = reinterpret_cast<decltype(T::func)>(a_address);
			return DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		}
	}

	ContextScope EnterScope(Pass a_pass, bool a_sampleCameraIndex)
	{
		const auto control = g_control.load(std::memory_order_relaxed);
		if ((control & 1) == 0 || !g_installed.load(std::memory_order_acquire))
			return ContextScope(g_context, {});
		const Context next{ a_pass, a_sampleCameraIndex ? ReadRendererCameraIndex() : ActiveContext(g_context, control).cameraIndex, control >> 1 };
		if (auto* slot = GetThreadSlot()) {
			auto& value = slot->passEntries[static_cast<std::size_t>(next.pass)];
			value.store(value.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		} else {
			g_threadOverflowPassEntries.fetch_add(1, std::memory_order_relaxed);
		}
		return ContextScope(g_context, next);
	}

	void SetEnabled(bool a_enabled)
	{
		auto control = g_control.load(std::memory_order_relaxed);
		while ((control & 1) != static_cast<std::uint64_t>(a_enabled)) {
			const auto next = (((control >> 1) + 1) << 1) | static_cast<std::uint64_t>(a_enabled);
			if (g_control.compare_exchange_weak(control, next, std::memory_order_relaxed))
				break;
		}
	}

	void Install()
	{
		if (g_attempted.exchange(true))
			return;
		if (!REL::Module::IsVR()) {
			g_installError.store(ERROR_NOT_SUPPORTED);
			return;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			g_installError.store(ERROR_REVISION_MISMATCH);
			return;
		}
		const auto base = REL::Module::get().base();
		// Prefixes and signatures come from the retained live VR 1.4.15 snapshot.
		constexpr std::array<std::uint8_t, 16> compoundPrefix{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xBA, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9 };
		constexpr std::array<std::uint8_t, 14> spherePrefix{ 0x41, 0x83, 0x78, 0x60, 0x00, 0x4C, 0x8B, 0xD2, 0x0F, 0x84, 0x81, 0x00, 0x00, 0x00 };
		if (!MatchesPrefix(base + 0xDA33C0, compoundPrefix) ||
			!MatchesPrefix(base + 0xDA5410, spherePrefix) || !MatchesPrefix(base + 0xDA54B0, spherePrefix)) {
			g_installError.store(ERROR_INVALID_DATA);
			return;
		}
		struct Target
		{
			std::uintptr_t address;
			LONG (*attach)(std::uintptr_t);
		};
		const auto vtableTarget = [](REL::VariantID id, std::size_t slot) { return ReadVtableTarget(REL::Relocation<std::uintptr_t*>(id).get() + slot); };
		const std::array targets{
			Target{ base + 0xDA33C0, &Attach<CompoundHook> },
			Target{ base + 0xDA5410, &Attach<SphereHook<Routine::SphereIntersect>> },
			Target{ base + 0xDA54B0, &Attach<SphereHook<Routine::SphereNotFullyInside>> },
			Target{ REL::RelocationID(100421, 107139).address(), &Attach<PassHook<Pass::Depth, bool, bool>> },
			Target{ REL::RelocationID(100424, 107142).address(), &Attach<PassHook<Pass::World, bool>> },
			Target{ REL::RelocationID(100411, 107129).address(), &Attach<PassHook<Pass::FirstPerson, bool, bool>> },
			Target{ REL::RelocationID(35561, 36560).address(), &Attach<PassHook<Pass::Water>> },
			Target{ REL::RelocationID(35560, 36559).address(), &Attach<PassHook<Pass::PlayerView, void*, bool, bool>> },
			Target{ REL::RelocationID(100422, 107140).address(), &Attach<PassHook<Pass::Shadowmask, bool>> },
			Target{ vtableTarget(RE::VTABLE_BSShadowDirectionalLight[0], 0xA), &Attach<PassHook<Pass::DirectionalShadow, RE::BSShadowLight*, void*>> },
			Target{ vtableTarget(RE::VTABLE_BSShadowFrustumLight[0], 0xA), &Attach<PassHook<Pass::SpotShadow, RE::BSShadowLight*, void*>> },
			Target{ vtableTarget(RE::VTABLE_BSShadowParabolicLight[0], 0xA), &Attach<PassHook<Pass::PointShadow, RE::BSShadowLight*, void*>> },
			Target{ vtableTarget(RE::VTABLE_BSCubeMapCamera[0], 0x35), &Attach<PassHook<Pass::Cubemap, RE::NiAVObject*, int, bool, bool, bool>> }
		};
		for (std::size_t index = 0; index < targets.size(); ++index) {
			if (!HasMemoryProtection(targets[index].address, 1, ExecutableReadProtection)) {
				g_installError.store(ERROR_INVALID_ADDRESS);
				return;
			}
			for (std::size_t earlier = 0; earlier < index; ++earlier) {
				if (targets[earlier].address == targets[index].address) {
					g_installError.store(ERROR_DUP_NAME);
					return;
				}
			}
		}
		LONG error = DetourTransactionBegin();
		if (error == NO_ERROR) {
			bool pending = true;
			const SKSE::stl::scope_exit abortUncommitted([&]() noexcept {
				if (pending)
					DetourTransactionAbort();
			});
			error = DetourUpdateThread(GetCurrentThread());
			for (const auto& target : targets) {
				if (error != NO_ERROR)
					break;
				error = target.attach(target.address);
			}
			if (error == NO_ERROR) {
				error = DetourTransactionCommit();
				pending = false;
			}
		}
		g_installError.store(error);
		g_installed.store(error == NO_ERROR, std::memory_order_release);
	}

	nlohmann::json GetStatus()
	{
		using json = nlohmann::json;
		LARGE_INTEGER started{}, finished{}, frequency{};
		const bool clockAvailable = QueryPerformanceCounter(&started) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0;
		const auto control = g_control.load(std::memory_order_relaxed);
		const bool installed = g_installed.load(std::memory_order_acquire);
		json result{ { "schemaVersion", 1 }, { "installed", installed }, { "active", installed && (control & 1) != 0 },
			{ "installError", g_installError.load() }, { "enabled", (control & 1) != 0 }, { "collectionGeneration", control >> 1 },
			{ "threadCapacity", ThreadCapacity }, { "rowsPerThread", RowCapacity }, { "identityCapacityPerThread", IdentityCapacity },
			{ "threadOverflowCalls", g_threadOverflowCalls.load(std::memory_order_relaxed) },
			{ "threadOverflowPassEntries", g_threadOverflowPassEntries.load(std::memory_order_relaxed) }, { "threads", json::array() },
			{ "counterSemantics", "cumulative; independently sampled atomics; compare deltas only within an unchanged enabled collection generation" },
			{ "eyeAttribution", "unknown: rendererCameraIndex is a raw State+0x58 sample, not a verified eye; worker/unscoped or stale-generation context remains unknown" },
			{ "repeatSemantics", "same thread/observed frame/routine/pass/raw-camera-index/caller/owner/object-or-bound/planes addresses; not proof of redundant work" } };
		const auto imageBase = REL::Module::get().base();
		for (std::size_t index = 0; index < g_threads.size(); ++index) {
			const auto& slot = g_threads[index];
			const auto tid = slot.threadId.load(std::memory_order_acquire);
			if (!tid)
				continue;
			json thread{ { "slot", index }, { "threadId", tid }, { "registeredAtQpc", slot.registeredAtQpc },
				{ "observedFrames", slot.counters.observedFrames.load(std::memory_order_relaxed) },
				{ "firstObservedFrame", slot.counters.firstObservedFrame.load(std::memory_order_relaxed) },
				{ "lastObservedFrame", slot.counters.lastObservedFrame.load(std::memory_order_relaxed) },
				{ "rowOverflowCalls", slot.counters.rowOverflow.load(std::memory_order_relaxed) },
				{ "passEntries", json::array() }, { "rows", json::array() } };
			for (std::size_t pass = 0; pass < PassCount; ++pass) {
				const auto count = slot.passEntries[pass].load(std::memory_order_relaxed);
				if (count)
					thread["passEntries"].push_back({ { "pass", PassNames[pass] }, { "calls", count } });
			}
			for (const auto& row : slot.counters.rows) {
				if (!row.published.load(std::memory_order_acquire))
					continue;
				json item{ { "routine", RoutineNames[static_cast<std::size_t>(row.key.routine)] },
					{ "pass", PassNames[static_cast<std::size_t>(row.key.pass)] }, { "rendererCameraIndex", row.key.cameraIndex == UnknownCameraIndex ? json(nullptr) : json(row.key.cameraIndex) },
					{ "callerAddress", row.key.caller }, { "skyrimImageBase", imageBase } };
				for (std::size_t counter = 0; counter < CounterCount; ++counter)
					item[CounterNames[counter]] = row.counters[counter].load(std::memory_order_relaxed);
				thread["rows"].push_back(std::move(item));
			}
			result["threads"].push_back(std::move(thread));
		}
		result["qpcValid"] = QueryPerformanceCounter(&finished) && clockAvailable;
		result["snapshotStartQpc"] = started.QuadPart;
		result["snapshotEndQpc"] = finished.QuadPart;
		result["qpcFrequency"] = frequency.QuadPart;
		result["collectionGenerationAtEnd"] = g_control.load(std::memory_order_relaxed) >> 1;
		return result;
	}
}

#endif
