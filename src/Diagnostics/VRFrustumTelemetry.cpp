#include "Diagnostics/VRFrustumTelemetry.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Globals.h"
#	include "Features/VRFrustumFastPath.h"
#	include "State.h"
#	include <cstring>
#	include <memory>
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
		constexpr std::array<const char*, CounterCount> CounterNames{ "calls", "completed", "rawTrue", "rawFalse" };
		constexpr std::array<const char*, 4> ConstructionNames{ "copy", "reset_camera", "append_intersection", "append_branch_planes" };
		struct ConstructionDetail
		{
			std::uint64_t generation = 0, generationAtEnd = 0;
			std::uint32_t frame = 0, operation = 0;
			std::uintptr_t owner = 0, argument = 0, caller = 0;
			Context context;
			Structure before, after;
			PlaneRange constructedPlanes;
		};

		struct ThreadSlot
		{
			std::atomic_bool claimed{ false };
			std::atomic_uint32_t threadId{ 0 };
			std::int64_t registeredAtQpc = 0;
			ThreadCounters counters;
			SamplingBudget detailBudget;
			SnapshotRing<Detail> details;
			SnapshotRing<ConstructionDetail> construction;
			std::array<std::atomic_uint64_t, 4> constructionCalls{}, constructionCompleted{};
			std::array<SamplingBudget, 4> constructionBudgets{};
			std::atomic_uint64_t detachedSphereCalls{ 0 };
			std::array<std::atomic_uint64_t, PassCount> passEntries{};
		};
		std::array<ThreadSlot, ThreadCapacity> g_threads{};
		std::atomic_uint64_t g_threadOverflowCalls{ 0 };
		std::atomic_uint64_t g_threadOverflowPassEntries{ 0 };
		// Low bits select counts/details; generation invalidates context across toggles.
		std::atomic_uint64_t g_control{ InitialControl };
		std::atomic_bool g_attempted{ false };
		std::atomic_bool g_installed{ false };
		std::atomic_long g_installError{ ERROR_NOT_READY };
		thread_local Context g_context;
		thread_local Traversal* g_traversal = nullptr;
		thread_local Detail* g_detail = nullptr;
		std::array<std::uintptr_t, 2> g_sphereCallers{};

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

		bool HasMemoryProtection(std::uintptr_t a_address, std::size_t a_size, DWORD a_allowedProtection);

		bool ReadDiagnostic(std::uintptr_t address, void* output, std::size_t size)
		{
			if (!HasMemoryProtection(address, size, ReadProtection))
				return false;
			__try {
				std::memcpy(output, reinterpret_cast<const void*>(address), size);
				return true;
			} __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}
		struct DetailScope
		{
			Detail* previous;
			explicit DetailScope(Detail* next) : previous(g_detail) { g_detail = next; }
			~DetailScope() { g_detail = previous; }
			DetailScope(const DetailScope&) = delete;
			DetailScope& operator=(const DetailScope&) = delete;
		};
		struct CompoundHook
		{
			static inline bool (*func)(void*, void*) = nullptr;
			static bool Run(void* owner, void* object, ThreadSlot& slot, RowKey key, Detail* detail)
			{
				Traversal batch;
				batch.lost = &slot.counters.unattributedCalls;
				batch.owner = reinterpret_cast<std::uintptr_t>(owner);
				batch.bound = reinterpret_cast<std::uintptr_t>(object) + 0xE4;
				batch.compound = slot.counters.Resolve(key);
				if (batch.compound)
					batch.compound->Increment(Counter::Calls);
				else
					Add(slot.counters.unattributedCalls);
				for (std::size_t i = 0; i < 2; ++i) {
					auto sphereKey = key;
					sphereKey.routine = static_cast<Routine>(i + 1);
					sphereKey.caller = g_sphereCallers[i];
					batch.sphereRows[i] = slot.counters.Resolve(sphereKey);
				}
				TraversalScope scope(g_traversal, &batch);
				DetailScope detailScope(detail);
				const bool result = func(owner, object);
				batch.Complete(result);
				if (detail) {
					detail->totals = batch.sphere;
					detail->Finish(result, ReadDiagnostic);
				}
				return result;
			}
			static __declspec(noinline) bool Detailed(void* owner, void* object, ThreadSlot& slot, RowKey key, std::uint64_t control, std::uint32_t frame)
			{
				VRFrustumFastPath::NativeScope nativeScope;
				Detail detail;
				detail.owner = reinterpret_cast<std::uintptr_t>(owner);
				detail.object = reinterpret_cast<std::uintptr_t>(object);
				detail.generation = Generation(control);
				detail.frame = frame;
				detail.context = key;
				detail.structure = ReadStructure(detail.owner, ReadDiagnostic);
				detail.boundValid = ReadBound(detail.object, detail.boundBits, ReadDiagnostic);
				detail.cursor = detail.structure.header.firstOp;
				detail.chainValid = detail.structure.header.valid;
				const bool result = Run(owner, object, slot, key, &detail);
				detail.generationAtEnd = Generation(g_control.load(std::memory_order_relaxed));
				slot.details.Publish(detail);
				return result;
			}
			static bool thunk(void* owner, void* object)
			{
				const auto control = g_control.load(std::memory_order_relaxed);
				if (!(control & 1)) {
					TraversalScope scope(g_traversal, nullptr);
					DetailScope detail(nullptr);
					return func(owner, object);
				}
				auto* slot = GetThreadSlot();
				if (!slot) {
					g_threadOverflowCalls.fetch_add(1, std::memory_order_relaxed);
					TraversalScope scope(g_traversal, nullptr);
					DetailScope detail(nullptr);
					return func(owner, object);
				}
				const bool known = globals::state != nullptr;
				const auto frame = known ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0;
				slot->counters.ObserveFrame(frame, known);
				const auto context = ActiveContext(g_context, control);
				const RowKey key{ Routine::Compound, context.pass, context.cameraIndex, reinterpret_cast<std::uintptr_t>(_ReturnAddress()) };
				if (slot->detailBudget.Admit(control, frame, known))
					return Detailed(owner, object, *slot, key, control, frame);
				return Run(owner, object, *slot, key, nullptr);
			}
		};
		template <Routine R>
		struct SphereHook
		{
			static inline bool (*func)(void*, void*, void*) = nullptr;
			static bool thunk(void* owner, void* bound, void* planes)
			{
				constexpr auto index = static_cast<std::size_t>(R) - 1;
				auto* batch = g_traversal;
				const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
				if (batch && batch->owner == reinterpret_cast<std::uintptr_t>(owner) && batch->bound == reinterpret_cast<std::uintptr_t>(bound) && caller == g_sphereCallers[index]) {
					auto& totals = batch->sphere[index];
					++totals.calls;
					auto* detail = g_detail;
					const auto address = reinterpret_cast<std::uintptr_t>(planes);
					const bool tracked = detail && detail->BeforeSphere(R, address, ReadDiagnostic);
					const bool result = func(owner, bound, planes);
					totals.Complete(result);
					if (tracked)
						detail->AfterSphere(address, result, ReadDiagnostic);
					return result;
				}
				const auto control = g_control.load(std::memory_order_relaxed);
				if (!(control & 1))
					return func(owner, bound, planes);
				auto* slot = GetThreadSlot();
				if (!slot) {
					g_threadOverflowCalls.fetch_add(1, std::memory_order_relaxed);
					return func(owner, bound, planes);
				}
				Add(slot->detachedSphereCalls);
				const auto context = ActiveContext(g_context, control);
				auto* row = slot->counters.Resolve({ R, context.pass, context.cameraIndex, caller });
				if (row)
					row->Increment(Counter::Calls);
				else
					Add(slot->counters.unattributedCalls);
				return Forward(row, func, owner, bound, planes);
			}
		};
		template <std::size_t Operation>
		struct ConstructionHook
		{
			static inline void (*func)(void*, void*) = nullptr;
			static __declspec(noinline) void Detailed(void* a, void* b, ThreadSlot& slot, std::uint64_t control, std::uint32_t frame, std::uintptr_t caller)
			{
				ConstructionDetail detail;
				detail.owner = reinterpret_cast<std::uintptr_t>(Operation == 0 ? b : a);
				detail.argument = reinterpret_cast<std::uintptr_t>(Operation == 0 ? a : b);
				detail.operation = Operation;
				detail.generation = Generation(control);
				detail.frame = frame;
				detail.caller = caller;
				detail.context = ActiveContext(g_context, control);
				detail.before = ReadStructure(detail.owner, ReadDiagnostic);
				func(a, b);
				Add(slot.constructionCompleted[Operation]);
				const auto operatorStart = Operation >= 2 && detail.before.header.valid ? detail.before.header.operatorCount : 0;
				detail.after = ReadStructure(detail.owner, ReadDiagnostic, operatorStart);
				const auto start = Operation >= 2 && detail.before.header.valid ? detail.before.header.planeCount : 0;
				detail.constructedPlanes = ReadPlanes(detail.after.header, start, ReadDiagnostic);
				detail.generationAtEnd = Generation(g_control.load(std::memory_order_relaxed));
				slot.construction.Publish(detail);
			}
			static void thunk(void* a, void* b)
			{
				const auto control = g_control.load(std::memory_order_relaxed);
				if (!(control & 1)) {
					func(a, b);
					return;
				}
				auto* slot = GetThreadSlot();
				if (!slot) {
					g_threadOverflowCalls.fetch_add(1, std::memory_order_relaxed);
					func(a, b);
					return;
				}
				Add(slot->constructionCalls[Operation]);
				const bool known = globals::state != nullptr;
				const auto frame = known ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0;
				if (slot->constructionBudgets[Operation].Admit(control, frame, known, ConstructionInterval)) {
					Detailed(a, b, *slot, control, frame, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
					return;
				}
				func(a, b);
				Add(slot->constructionCompleted[Operation]);
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
		const Context next{ a_pass, a_sampleCameraIndex ? ReadRendererCameraIndex() : ActiveContext(g_context, control).cameraIndex, Generation(control) };
		if (auto* slot = GetThreadSlot()) {
			auto& value = slot->passEntries[static_cast<std::size_t>(next.pass)];
			value.store(value.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		} else {
			g_threadOverflowPassEntries.fetch_add(1, std::memory_order_relaxed);
		}
		return ContextScope(g_context, next);
	}

	namespace
	{
		void SetControlBit(std::uint64_t bit, bool enabled)
		{
			auto control = g_control.load(std::memory_order_relaxed);
			while (ChangeControl(control, bit, enabled) != control) {
				if (g_control.compare_exchange_weak(control, ChangeControl(control, bit, enabled), std::memory_order_relaxed))
					break;
			}
		}
	}
	void SetEnabled(bool enabled) { SetControlBit(1, enabled); }
	void SetDetailEnabled(bool enabled) { SetControlBit(2, enabled); }
	void AdvanceCollectionGeneration() { g_control.fetch_add(4, std::memory_order_relaxed); }

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
		if ((!VRFrustumFastPath::IsInstalled() && !MatchesPrefix(base + 0xDA33C0, compoundPrefix)) ||
			!MatchesPrefix(base + 0xDA5410, spherePrefix) || !MatchesPrefix(base + 0xDA54B0, spherePrefix)) {
			g_installError.store(ERROR_INVALID_DATA);
			return;
		}
		constexpr std::array<std::uint8_t, 16> copyPrefix{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x41 };
		constexpr std::array<std::uint8_t, 16> resetPrefix{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9 };
		constexpr std::array<std::uint8_t, 16> intersectionPrefix{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x60, 0x8b };
		constexpr std::array<std::uint8_t, 16> branchPlanesPrefix{ 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81 };
		if (!MatchesPrefix(base + 0xDA3050, copyPrefix) || !MatchesPrefix(base + 0xDA3750, resetPrefix) || !MatchesPrefix(base + 0xDA38E0, intersectionPrefix) || !MatchesPrefix(base + 0xDA3C10, branchPlanesPrefix)) {
			g_installError.store(ERROR_INVALID_DATA);
			return;
		}
		g_sphereCallers = { base + 0xDA345E, base + 0xDA343F };
		struct Target
		{
			std::uintptr_t address;
			LONG (*attach)(std::uintptr_t);
		};
		const auto vtableTarget = [](REL::VariantID id, std::size_t slot) { return ReadVtableTarget(REL::Relocation<std::uintptr_t*>(id).get() + slot); };
		const std::array targets{
			Target{ base + 0xDA33C0, &Attach<CompoundHook> },
			Target{ base + 0xDA3050, &Attach<ConstructionHook<0>> },
			Target{ base + 0xDA3750, &Attach<ConstructionHook<1>> },
			Target{ base + 0xDA38E0, &Attach<ConstructionHook<2>> },
			Target{ base + 0xDA3C10, &Attach<ConstructionHook<3>> },
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

	namespace
	{
		using json = nlohmann::json;
		json HeaderJson(const NativeHeader& h) { return { { "valid", h.valid }, { "planesAddress", h.planes }, { "operatorsAddress", h.operators }, { "planeStorage", h.planeStorage }, { "operatorStorage", h.operatorStorage }, { "planeCount", h.planeCount }, { "operatorCount", h.operatorCount }, { "firstOperator", h.firstOp }, { "prethreaded", h.prethreaded } }; }
		json StructureJson(const Structure& s)
		{
			json ops = json::array();
			for (std::uint32_t i = 0; i < s.copied; ++i) {
				const auto& op = s.operators[i];
				ops.push_back({ { "index", s.start + i }, { "rawWords", { op.opcode, op.onTrue, op.onFalse } } });
			}
			return { { "header", HeaderJson(s.header) }, { "operatorSlots", std::move(ops) }, { "operatorStart", s.start }, { "truncated", s.truncated }, { "readFault", s.readFault } };
		}
		json ContextJson(Pass pass, std::int64_t camera) { return { { "pass", PassNames[static_cast<std::size_t>(pass)] }, { "rendererCameraIndex", camera == UnknownCameraIndex ? json(nullptr) : json(camera) } }; }
		json DetailJson(const Detail& d)
		{
			json j = ContextJson(d.context.pass, d.context.cameraIndex);
			j.update({ { "generation", d.generation }, { "generationAtEnd", d.generationAtEnd }, { "frame", d.frame }, { "ownerAddress", d.owner }, { "objectAddress", d.object }, { "callerAddress", d.context.caller },
				{ "structure", StructureJson(d.structure) }, { "boundValid", d.boundValid }, { "boundRawUint32Words", d.boundBits }, { "completionKind", std::array{ "unverified", "zero_bound", "empty_program", "operator_terminal" }[static_cast<std::size_t>(d.completionKind)] }, { "headerStable", d.headerStable }, { "boundStable", d.boundStable }, { "operatorChanged", d.operatorChanged }, { "chainValid", d.chainValid }, { "stepLimit", d.stepLimit }, { "planeLimit", d.planeLimit }, { "readFault", d.readFault },
				{ "completed", d.completed }, { "accepted", d.accepted }, { "terminalVerified", d.terminalVerified }, { "terminalOpcode", d.terminalOpcode }, { "terminalOperator", d.cursor },
				{ "steps", json::array() }, { "planeSets", json::array() }, { "sphereTotals", json::array() } });
			for (std::uint32_t i = 0; i < d.stepCount; ++i) {
				const auto& t = d.steps[i];
				j["steps"].push_back({ { "operatorIndex", t.op }, { "opcode", t.opcode }, { "planeSetIndex", t.plane }, { "onTrue", t.onTrue }, { "onFalse", t.onFalse }, { "rawResult", t.opcode == 7 || t.opcode == 8 ? json(t.result) : json(nullptr) }, { "branchSource", t.opcode == 7 || t.opcode == 8 ? "native_sphere_result" : "verified_control_fallthrough" }, { "activeMaskBefore", t.beforeMask }, { "activeMaskAfter", t.afterMask }, { "masksValid", t.maskValid } });
			}
			for (std::uint32_t i = 0; i < d.planeCount; ++i) j["planeSets"].push_back({ { "index", d.planes[i].index }, { "rawUint32WordsBeforeFirstTest", d.planes[i].bits } });
			for (std::size_t i = 0; i < 2; ++i) j["sphereTotals"].push_back({ { "routine", RoutineNames[i + 1] }, { "calls", d.totals[i].calls }, { "completed", d.totals[i].completed }, { "rawTrue", d.totals[i].rawTrue }, { "rawFalse", d.totals[i].completed - d.totals[i].rawTrue } });
			return j;
		}
		json PlaneRangeJson(const PlaneRange& range)
		{
			json j{ { "start", range.start }, { "readFault", range.readFault }, { "truncated", range.truncated }, { "planeSets", json::array() } };
			for (std::uint32_t i = 0; i < range.count; ++i) j["planeSets"].push_back({ { "index", range.planes[i].index }, { "rawUint32Words", range.planes[i].bits } });
			return j;
		}
		json ConstructionJson(const ConstructionDetail& d)
		{
			json j = ContextJson(d.context.pass, d.context.cameraIndex);
			j.update({ { "generation", d.generation }, { "generationAtEnd", d.generationAtEnd }, { "frame", d.frame }, { "operation", ConstructionNames[d.operation] }, { "ownerAddress", d.owner }, { "argumentAddress", d.argument }, { "callerAddress", d.caller }, { "before", StructureJson(d.before) }, { "after", StructureJson(d.after) }, { "constructedPlanes", PlaneRangeJson(d.constructedPlanes) } });
			return j;
		}
		template <class T, class Convert>
		json RingJson(SnapshotRing<T>& ring, Convert convert)
		{
			auto copy = std::make_unique<typename SnapshotRing<T>::Snapshot>();
			const bool available = ring.TrySnapshot(*copy);
			json j{ { "snapshotAvailable", available }, { "published", available ? json(copy->published) : json(nullptr) }, { "publishContentionDrops", available ? json(copy->dropped) : json(nullptr) }, { "capacity", DetailRingCapacity }, { "samples", json::array() } };
			if (available)
				for (const auto& item : *copy)
					if (item.sequence) {
						auto sample = convert(item.value);
						sample["sequence"] = item.sequence;
						j["samples"].push_back(std::move(sample));
					}
			j["retentionSemantics"] = "bounded recent samples; sequence gaps include ring eviction, not zero work";
			return j;
		}
	}
	nlohmann::json GetStatus()
	{
		LARGE_INTEGER started{}, finished{}, frequency{};
		const bool clockAvailable = QueryPerformanceCounter(&started) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0;
		const auto control = g_control.load(std::memory_order_relaxed);
		const bool installed = g_installed.load(std::memory_order_acquire);
		json result{ { "schemaVersion", 2 }, { "installed", installed }, { "active", installed && (control & 1) != 0 }, { "installError", g_installError.load() },
			{ "enabled", (control & 1) != 0 }, { "detailEnabled", (control & 2) != 0 }, { "collectionGeneration", Generation(control) },
			{ "threadCapacity", ThreadCapacity }, { "rowsPerThread", RowCapacity }, { "detailInterval", DetailInterval }, { "constructionDetailInterval", ConstructionInterval }, { "detailStepLimit", DetailSteps }, { "detailOperatorLimit", DetailOperators }, { "detailPlaneLimit", DetailPlanes },
			{ "threadOverflowCalls", g_threadOverflowCalls.load(std::memory_order_relaxed) }, { "threadOverflowPassEntries", g_threadOverflowPassEntries.load(std::memory_order_relaxed) }, { "threads", json::array() },
			{ "counterSemantics", "cumulative independent atomics; compound-owned sphere calls publish on compound return/unwind; in-flight counts may be absent; compare only unchanged enabled generation" },
			{ "eyeAttribution", "unknown: raw rendererCameraIndex is not a verified eye; asynchronous workers do not inherit pass context" },
			{ "detailSemantics", "first then every-256th compound; first then every-16th construction per operation; each at most one per known thread/frame; bounded recent samples, not an unbiased population; no per-test address hashing" },
			{ "constructionCoverage", "copy DA3050, camera reset DA3750, append intersection DA38E0, append branch planes DA3C10; other builders may be unobserved" },
			{ "outcomeSemantics", "compound true accepts at this stage; false rejects here; neither counts actual draws or GPU work saved; sphere booleans only choose operator branches" } };
		for (std::size_t index = 0; index < g_threads.size(); ++index) {
			auto& slot = g_threads[index];
			const auto tid = slot.threadId.load(std::memory_order_acquire);
			if (!tid)
				continue;
			json thread{ { "slot", index }, { "threadId", tid }, { "registeredAtQpc", slot.registeredAtQpc },
				{ "observedFrames", slot.counters.observedFrames.load(std::memory_order_relaxed) }, { "firstObservedFrame", slot.counters.firstObservedFrame.load(std::memory_order_relaxed) }, { "lastObservedFrame", slot.counters.lastObservedFrame.load(std::memory_order_relaxed) },
				{ "rowResolutionFailures", slot.counters.rowOverflow.load(std::memory_order_relaxed) }, { "rowLookups", slot.counters.rowLookups.load(std::memory_order_relaxed) }, { "unattributedCalls", slot.counters.unattributedCalls.load(std::memory_order_relaxed) },
				{ "detachedSphereCalls", slot.detachedSphereCalls.load(std::memory_order_relaxed) }, { "passEntries", json::array() }, { "rows", json::array() }, { "constructionCounters", json::array() } };
			for (std::size_t pass = 0; pass < PassCount; ++pass) {
				const auto count = slot.passEntries[pass].load(std::memory_order_relaxed);
				if (count)
					thread["passEntries"].push_back({ { "pass", PassNames[pass] }, { "calls", count } });
			}
			for (const auto& row : slot.counters.rows) {
				if (!row.published.load(std::memory_order_acquire))
					continue;
				json item = ContextJson(row.key.pass, row.key.cameraIndex);
				item.update({ { "routine", RoutineNames[static_cast<std::size_t>(row.key.routine)] }, { "callerAddress", row.key.caller }, { "skyrimImageBase", REL::Module::get().base() } });
				for (std::size_t c = 0; c < CounterCount; ++c) item[CounterNames[c]] = row.counters[c].load(std::memory_order_relaxed);
				if (row.key.routine == Routine::Compound) {
					item["workByOutcome"] = json::array();
					for (std::size_t accepted = 0; accepted < 2; ++accepted) item["workByOutcome"].push_back({ { "accepted", accepted != 0 }, { "sphereIntersectCalls", row.sphereCallsByOutcome[accepted][0].load(std::memory_order_relaxed) }, { "sphereNotFullyInsideCalls", row.sphereCallsByOutcome[accepted][1].load(std::memory_order_relaxed) }, { "objectsWithoutSphereTests", row.zeroTestObjectsByOutcome[accepted].load(std::memory_order_relaxed) } });
				}
				thread["rows"].push_back(std::move(item));
			}
			for (std::size_t i = 0; i < 4; ++i) thread["constructionCounters"].push_back({ { "operation", ConstructionNames[i] }, { "calls", slot.constructionCalls[i].load(std::memory_order_relaxed) }, { "completed", slot.constructionCompleted[i].load(std::memory_order_relaxed) } });
			thread["traversalDetails"] = RingJson(slot.details, DetailJson);
			thread["constructionDetails"] = RingJson(slot.construction, ConstructionJson);
			result["threads"].push_back(std::move(thread));
		}
		result["qpcValid"] = QueryPerformanceCounter(&finished) && clockAvailable;
		result["snapshotStartQpc"] = started.QuadPart;
		result["snapshotEndQpc"] = finished.QuadPart;
		result["qpcFrequency"] = frequency.QuadPart;
		result["collectionGenerationAtEnd"] = Generation(g_control.load(std::memory_order_relaxed));
		return result;
	}
}
#endif
