#include "VRShadowBatch.h"

#include "ShadowBatchSubmissions.h"
#include "Utils/VirtualFunctionHook.h"
#include "VRShadowBatchLayout.h"
#include "VRShadowBatchPolicy.h"

#include <array>
#include <atomic>
#include <bit>
#include <intrin.h>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace VRShadowBatch
{
	namespace
	{
		using Renderer = RE::BSBatchRenderer;
		using Pass = RE::BSRenderPass;
		// Initialized contiguous words retain the complete draw identity without a hash staging buffer.
		using Key = std::array<std::uint64_t, 8>;
		struct KeyHash
		{
			using is_avalanching = void;
			std::uint64_t operator()(const Key& a_key) const noexcept
			{
				return ankerl::unordered_dense::hash<std::string_view>{}({ reinterpret_cast<const char*>(a_key.data()), a_key.size() * sizeof(a_key[0]) });
			}
		};

		struct Payload
		{
			Pass pass{};
			RE::NiPointer<RE::BSGeometry> geometry;
			RE::NiPointer<RE::BSShaderProperty> property;
		};
		using Store = ShadowBatch::Submissions<Key, Payload, KeyHash, false>;
		struct BatchState
		{
			std::recursive_mutex mutex;
			Store submissions;
		};
		std::mutex registryMutex;
		std::unordered_map<Renderer*, std::shared_ptr<BatchState>> registry;
		std::atomic_uint64_t registryRevision{};
		std::atomic_flag conflictReported{};
		std::atomic_flag allocationFailureReported{};

		void ReleaseRecords(BatchState& state, Store::Record* retired)
		{
			if (!retired)
				return;
			auto* tail = retired;
			for (auto* record = retired; record; record = record->next) {
				record->payload = {};
				tail = record;
			}
			const std::lock_guard lock{ state.mutex };
			state.submissions.Recycle(retired, tail);
		}

		struct ReclaimOnExit
		{
			BatchState& state;
			bool pending{ true };
			~ReclaimOnExit()
			{
				if (!pending)
					return;
				Store::Record* retired;
				{
					const std::lock_guard lock{ state.mutex };
					retired = state.submissions.TakeRetired();
				}
				ReleaseRecords(state, retired);
			}
		};

		void Prune(Renderer* a_renderer, BatchState& a_state);

		struct NativeRead
		{
			BatchState& state;
			Renderer* renderer;
			bool completed{};
			std::optional<Store::ReadScope> reader;
			NativeRead(BatchState& a_state, Renderer* a_renderer) : state(a_state), renderer(a_renderer)
			{
				const std::lock_guard lock{ state.mutex };
				reader.emplace(state.submissions);
			}
			~NativeRead()
			{
				Store::Record* retired;
				{
					const std::lock_guard lock{ state.mutex };
					if (completed)
						Prune(renderer, state);
					reader.reset();
					retired = state.submissions.TakeRetired();
				}
				ReleaseRecords(state, retired);
			}
		};

		void ReportAllocationFailure()
		{
			if (!allocationFailureReported.test_and_set(std::memory_order_relaxed))
				logger::error("[VR shadow batch] Ownership allocation failed; affected shadow submission skipped");
		}

		std::shared_ptr<BatchState> FindState(Renderer* a_renderer, bool a_create = false)
		{
			struct Cache
			{
				Renderer* renderer{};
				std::uint64_t revision{};
				bool valid{}, missing{};
				std::weak_ptr<BatchState> state;
			};
			thread_local std::array<Cache, 8> caches;
			const auto address = reinterpret_cast<std::uintptr_t>(a_renderer);
			auto& cache = caches[((address >> 4) ^ (address >> 12)) & (caches.size() - 1)];
			// A weak cache cannot retain retired pools; the revision rejects reused renderer addresses.
			if (cache.valid && cache.renderer == a_renderer && cache.revision == registryRevision.load(std::memory_order_acquire)) {
				if (cache.missing && !a_create)
					return {};
				if (auto state = cache.state.lock())
					return state;
			}
			const std::lock_guard lock{ registryMutex };
			std::shared_ptr<BatchState> state;
			if (const auto found = registry.find(a_renderer); found != registry.end()) {
				state = found->second;
			} else if (a_create) {
				state = std::make_shared<BatchState>();
				registry.emplace(a_renderer, state);
				// Creation must invalidate negative entries held by every observing thread.
				registryRevision.fetch_add(1, std::memory_order_release);
			}
			cache.renderer = a_renderer;
			cache.revision = registryRevision.load(std::memory_order_relaxed);
			cache.valid = true;
			cache.missing = !state;
			cache.state = state;
			return state;
		}

		bool BucketEmpty(Renderer* a_renderer, std::uint64_t a_key)
		{
			return NativeLayout::BucketEmpty(a_renderer, static_cast<std::uint32_t>(a_key >> 32), static_cast<std::uint32_t>(a_key));
		}

		void Prune(Renderer* a_renderer, BatchState& a_state)
		{
			a_state.submissions.Prune([&](std::uint64_t key) { return BucketEmpty(a_renderer, key); });
		}

		Key MakeKey(Pass* a_pass, bool a_sorted)
		{
			const auto bytes = static_cast<std::uint32_t>(a_pass->accumulationHint) |
			                   (static_cast<std::uint32_t>(a_pass->extraParam) << 8) |
			                   (static_cast<std::uint32_t>(std::bit_cast<std::uint8_t>(a_pass->LODMode)) << 16) |
			                   (static_cast<std::uint32_t>(a_pass->unk21) << 24);
			return { reinterpret_cast<std::uintptr_t>(a_pass), reinterpret_cast<std::uintptr_t>(a_pass->shader),
				reinterpret_cast<std::uintptr_t>(a_pass->shaderProperty), reinterpret_cast<std::uintptr_t>(a_pass->geometry),
				static_cast<std::uint64_t>(a_pass->passEnum) | (static_cast<std::uint64_t>(bytes) << 32),
				static_cast<std::uint64_t>(a_pass->unk24) | (static_cast<std::uint64_t>(a_sorted) << 32),
				a_pass->shaderProperty->flags.underlying(), reinterpret_cast<std::uintptr_t>(a_pass->shaderProperty->material) };
		}

		void ReportDuplicate(Renderer* a_renderer, Pass* a_pass, std::uint64_t a_bucket, std::uintptr_t a_firstCaller, std::uintptr_t a_caller)
		{
			if (conflictReported.test_and_set(std::memory_order_relaxed))
				return;
			logger::warn("[VR shadow batch] Duplicate suppressed: renderer={:p} pass={:p} geometry={:p} property={:p} technique={:X} bucket={} firstCaller={:X} caller={:X}",
				static_cast<void*>(a_renderer), static_cast<void*>(a_pass), static_cast<void*>(a_pass->geometry),
				static_cast<void*>(a_pass->shaderProperty), a_bucket >> 32, static_cast<std::uint32_t>(a_bucket), a_firstCaller, a_caller);
			std::array<void*, 16> stack{};
			const auto count = CaptureStackBackTrace(0, static_cast<DWORD>(stack.size()), stack.data(), nullptr);
			for (USHORT i = 0; i < count; ++i)
				logger::warn("[VR shadow batch] Conflict stack[{}]={:p}", i, stack[i]);
		}

		using Register = void (*)(Renderer*, Pass*, std::uint32_t);
		Register registerSorted{}, registerUnsorted{};
		using Reset = void (*)(Renderer*);
		Reset clearPasses{}, clearMap{}, destroy{};
		using RenderRange = void (*)(Renderer*, std::uint32_t, std::uint32_t, std::uint32_t);
		RenderRange renderRange{};
		using RenderStep = bool (*)(Renderer*, std::uint32_t*, std::uint32_t*, void*, std::uint32_t);
		RenderStep renderStep{};

		void RegisterSubmission(Renderer* a_renderer, Pass* a_pass, std::uint32_t a_technique, bool a_sorted, std::uintptr_t a_caller)
		{
			const auto original = a_sorted ? registerSorted : registerUnsorted;
			if (!a_pass || !a_pass->shader || !a_pass->geometry || !a_pass->shaderProperty ||
				!IsIsolatedShadowPass(a_pass->shader->shaderType.get() == RE::BSShader::Type::Utility,
					a_technique, a_pass->numLights, a_pass->numShadowLights)) {
				original(a_renderer, a_pass, a_technique);
				return;
			}
			auto& geometry = a_pass->geometry->GetGeometryRuntimeData();
			auto* shaderProperty = static_cast<RE::BSShaderProperty*>(geometry.shaderProperty.get());
			auto* alpha = static_cast<RE::NiAlphaProperty*>(geometry.alphaProperty.get());
			const auto bucket = BucketKey(a_technique, BucketIndex(shaderProperty->flags.underlying(), alpha ? alpha->alphaFlags : 0));
			std::shared_ptr<BatchState> state;
			try {
				state = FindState(a_renderer, true);
			} catch (const std::bad_alloc&) {
				ReportAllocationFailure();
				return;
			}
			ReclaimOnExit reclaim{ *state, false };
			std::unique_lock lock{ state->mutex };
			Store::Admission admission{};
			try {
				admission = state->submissions.Admit(bucket, MakeKey(a_pass, a_sorted), a_caller, [&](Payload& payload) {
					payload.geometry.reset(a_pass->geometry);
					payload.property.reset(a_pass->shaderProperty);
					payload.pass = *a_pass;
					payload.pass.next = nullptr;
					payload.pass.passGroupNext = nullptr;
					payload.pass.sceneLights = nullptr; }, [&] { return BucketEmpty(a_renderer, bucket); });
				reclaim.pending = state->submissions.HasRetired();
			} catch (const std::bad_alloc&) {
				reclaim.pending = true;
				lock.unlock();
				ReportAllocationFailure();
				return;
			}
			if (!admission.inserted) {
				const auto firstCaller = admission.record->caller;
				lock.unlock();
				ReportDuplicate(a_renderer, a_pass, bucket, firstCaller, a_caller);
				return;
			}
			original(a_renderer, &admission.record->payload.pass, a_technique);
		}

		void RegisterSorted(Renderer* a_renderer, Pass* a_pass, std::uint32_t a_technique)
		{
			RegisterSubmission(a_renderer, a_pass, a_technique, true, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
		}
		void RegisterUnsorted(Renderer* a_renderer, Pass* a_pass, std::uint32_t a_technique)
		{
			RegisterSubmission(a_renderer, a_pass, a_technique, false, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
		}

		template <Reset& Original>
		void Clear(Renderer* a_renderer)
		{
			if (auto state = FindState(a_renderer)) {
				const ReclaimOnExit reclaim{ *state };
				const std::lock_guard lock{ state->mutex };
				Original(a_renderer);
				state->submissions.Clear();
			} else {
				Original(a_renderer);
			}
		}

		void Destroy(Renderer* a_renderer)
		{
			auto state = FindState(a_renderer);
			destroy(a_renderer);
			if (state) {
				const ReclaimOnExit reclaim{ *state };
				{
					const std::lock_guard lock{ state->mutex };
					state->submissions.Clear();
				}
				const std::lock_guard registryLock{ registryMutex };
				registry.erase(a_renderer);
				registryRevision.fetch_add(1, std::memory_order_release);
			}
		}

		void RenderActiveRange(Renderer* a_renderer, std::uint32_t a_first, std::uint32_t a_last, std::uint32_t a_flags)
		{
			if (auto state = FindState(a_renderer)) {
				NativeRead reader{ *state, a_renderer };
				renderRange(a_renderer, a_first, a_last, a_flags);
				reader.completed = true;
			} else {
				renderRange(a_renderer, a_first, a_last, a_flags);
			}
		}

		bool RenderBatch(Renderer* a_renderer, std::uint32_t* a_pass, std::uint32_t* a_bucket, void* a_list, std::uint32_t a_flags)
		{
			if (auto state = FindState(a_renderer)) {
				NativeRead reader{ *state, a_renderer };
				const auto result = renderStep(a_renderer, a_pass, a_bucket, a_list, a_flags);
				reader.completed = true;
				return result;
			}
			return renderStep(a_renderer, a_pass, a_bucket, a_list, a_flags);
		}
	}

	void Install()
	{
		static_assert(sizeof(Renderer::PassGroup) == 0x30);
		static_assert(offsetof(Pass, passGroupNext) == 0x30);
		if (!REL::Module::IsVR())
			return;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
			stl::report_and_fail("VR shadow batch isolation requires Skyrim VR 1.4.15.");
		for (const auto& site : kHookSites) {
			if (!Matches({ reinterpret_cast<const std::uint8_t*>(REL::Offset(site.rva).address()), site.prefix.size() }, site)) {
				logger::critical("[VR shadow batch] Unexpected hook bytes at SkyrimVR+{:X}; no isolation hooks installed", site.rva);
				stl::report_and_fail("Incompatible VR shadow batch hook. See CommunityShaders.log.");
			}
		}
		if (!Matches({ reinterpret_cast<const std::uint8_t*>(REL::Offset(kContiguousGroups.rva).address()), kContiguousGroups.prefix.size() }, kContiguousGroups))
			stl::report_and_fail("Unrecognized VR shadow batch array layout. See CommunityShaders.log.");

		struct Hook
		{
			void** original;
			void* replacement;
		};
		const std::array hooks{
			Hook{ reinterpret_cast<void**>(&registerSorted), reinterpret_cast<void*>(&RegisterSorted) },
			Hook{ reinterpret_cast<void**>(&registerUnsorted), reinterpret_cast<void*>(&RegisterUnsorted) },
			Hook{ reinterpret_cast<void**>(&clearPasses), reinterpret_cast<void*>(&Clear<clearPasses>) },
			Hook{ reinterpret_cast<void**>(&clearMap), reinterpret_cast<void*>(&Clear<clearMap>) },
			Hook{ reinterpret_cast<void**>(&renderRange), reinterpret_cast<void*>(&RenderActiveRange) },
			Hook{ reinterpret_cast<void**>(&renderStep), reinterpret_cast<void*>(&RenderBatch) },
			Hook{ reinterpret_cast<void**>(&destroy), reinterpret_cast<void*>(&Destroy) }
		};
		for (std::size_t i = 0; i < hooks.size(); ++i)
			*hooks[i].original = reinterpret_cast<void*>(REL::Offset(kHookSites[i].rva).address());
		struct Operations
		{
			std::span<const Hook> hooks;
			long Begin() const { return DetourTransactionBegin(); }
			long UpdateThread() const { return DetourUpdateThread(GetCurrentThread()); }
			long Attach(void**, void*) const
			{
				for (const auto& hook : hooks) {
					const auto result = DetourAttach(hook.original, hook.replacement);
					if (result != NO_ERROR)
						return result;
				}
				return NO_ERROR;
			}
			long Abort() const { return DetourTransactionAbort(); }
			long Commit() const { return DetourTransactionCommit(); }
		};
		const auto result = Util::detail::AttachDetourTransaction(nullptr, nullptr, Operations{ hooks });
		if (result.error != NO_ERROR) {
			logger::critical("[VR shadow batch] Atomic hook installation failed: {}", result.error);
			stl::report_and_fail("VR shadow batch isolation could not be installed. See CommunityShaders.log.");
		}
		logger::info("[VR shadow batch] Installed lightless shadow-map submission isolation and batch retirement hooks");
	}
}
