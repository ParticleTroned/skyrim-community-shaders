#include "VRAlphaPassListFix.h"

#include "EngineFixes/AlphaPassListPolicy.h"
#include "Globals.h"
#include "State.h"
#include "Utils/VirtualFunctionHook.h"
#include <RE/B/BSBatchRenderer.h>
#include <RE/B/BSRenderPass.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace VRAlphaPassListFix
{
	namespace
	{
		constexpr std::uintptr_t AppendRVA = 0x1347150;
		constexpr std::array<std::uint8_t, 18> AppendPrefix{
			0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF
		};
		using Group = RE::BSBatchRenderer::GeometryGroup;
		using Append = void (*)(Group*, RE::BSRenderPass*, std::uint8_t);
		Append original{};
		using Consume = void (*)(void*, const RE::NiPoint3*, float, float, std::uint32_t);
		using RenderGroup = void (*)(Group*, std::uint32_t);
		Consume originalConsume{};
		RenderGroup originalRenderGroup{};
		std::atomic_uint64_t generation{};
		bool installed{};
		std::string installationError;
		std::atomic_flag allocationWarning{}, membershipWarning{};
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_bool enabled{ true };
		std::atomic_uint64_t duplicates{}, allocationFailures{}, unverifiableMemberships{};
#endif
		AlphaPassList::Queue Inspect(Group* group)
		{
			const auto epoch = generation.load(std::memory_order_acquire);
			return { reinterpret_cast<std::uintptr_t>(group), reinterpret_cast<std::uintptr_t>(group->passList.head),
				reinterpret_cast<std::uintptr_t>(group->passList.tail), epoch,
				globals::state ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0, group->count, group->flags };
		}
		void ConsumeQueue(void* renderer, const RE::NiPoint3* direction, float closest, float minimum, std::uint32_t flags)
		{
			RetirementScope lifetime;
			originalConsume(renderer, direction, closest, minimum, flags);
		}
		void RenderQueue(Group* group, std::uint32_t flags)
		{
			if (!(group->flags & 1)) {
				originalRenderGroup(group, flags);
				return;
			}
			RetirementScope lifetime;
			originalRenderGroup(group, flags);
		}
		bool ReadNext(std::uintptr_t node, std::uintptr_t& next) noexcept
		{
			__try {
				std::memcpy(&next, reinterpret_cast<const std::byte*>(node) + offsetof(RE::BSRenderPass, passGroupNext), sizeof(next));
				return true;
			} __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}
		void ReportAllocationFailure() noexcept
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			allocationFailures.fetch_add(1, std::memory_order_relaxed);
#endif
			if (!allocationWarning.test_and_set(std::memory_order_relaxed))
				logger::warn("[VR alpha list] Membership cache allocation failed; using bounded native-list scans");
		}
		void GuardedAppend(Group* group, RE::BSRenderPass* pass, std::uint8_t front)
		{
			if (!group || !pass || !(group->flags & 1)
#ifdef DEVBENCH_BRIDGE_ENABLED
				|| !enabled.load(std::memory_order_acquire)
#endif
			) {
				original(group, pass, front);
				return;
			}
			thread_local std::unique_ptr<AlphaPassList::Cache> cache;
			if (!cache) {
				try {
					cache = std::make_unique<AlphaPassList::Cache>();
				} catch (const std::bad_alloc&) {
					ReportAllocationFailure();
				}
			}
			const auto result = AlphaPassList::AppendUnique(cache.get(), Inspect(group), reinterpret_cast<std::uintptr_t>(pass), front != 0, ReadNext, [&] { original(group, pass, front); }, [&] { return Inspect(group); }, ReportAllocationFailure);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (!result.forwarded)
				duplicates.fetch_add(1, std::memory_order_relaxed);
			if (!result.membershipKnown)
				unverifiableMemberships.fetch_add(1, std::memory_order_relaxed);
#endif
			if (!result.membershipKnown && !membershipWarning.test_and_set(std::memory_order_relaxed))
				logger::warn("[VR alpha list] Membership is unreadable or bounded; retaining the native append");
		}
	}
	RetirementScope::RetirementScope() noexcept { generation.fetch_add(1, std::memory_order_acq_rel); }
	RetirementScope::~RetirementScope() { generation.fetch_add(1, std::memory_order_acq_rel); }
	void Install()
	{
		static_assert(offsetof(Group, count) == 0x24 && offsetof(Group, flags) == 0x26);
		static_assert(offsetof(RE::BSRenderPass, passGroupNext) == 0x30);
		if (installed || !REL::Module::IsVR())
			return;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			installationError = "Verified alpha-list entrypoint requires Skyrim VR 1.4.15";
			logger::warn("[VR alpha list] {}", installationError);
			return;
		}
		struct Site
		{
			std::uintptr_t rva;
			std::span<const std::uint8_t> prefix;
			void** original;
			void* hook;
		};
		constexpr std::array<std::uint8_t, 16> consumePrefix{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		constexpr std::array<std::uint8_t, 10> groupPrefix{ 0x40, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30 };
		const std::array sites{
			Site{ AppendRVA, AppendPrefix, reinterpret_cast<void**>(&original), reinterpret_cast<void*>(&GuardedAppend) },
			Site{ 0x134A6F0, consumePrefix, reinterpret_cast<void**>(&originalConsume), reinterpret_cast<void*>(&ConsumeQueue) },
			Site{ 0x1347320, groupPrefix, reinterpret_cast<void**>(&originalRenderGroup), reinterpret_cast<void*>(&RenderQueue) }
		};
		const auto base = REL::Module::get().base();
		for (const auto& site : sites) {
			std::array<std::uint8_t, AppendPrefix.size()> actual{};
			SIZE_T read{};
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(base + site.rva), actual.data(), site.prefix.size(), &read) ||
				read != site.prefix.size() || !std::equal(site.prefix.begin(), site.prefix.end(), actual.begin())) {
				installationError = "Native alpha-list entrypoint is modified or unsupported; existing hooks retained";
				logger::warn("[VR alpha list] {}", installationError);
				return;
			}
			*site.original = reinterpret_cast<void*>(base + site.rva);
		}
		struct Operations
		{
			std::span<const Site> sites;
			long Begin() const { return DetourTransactionBegin(); }
			long UpdateThread() const { return DetourUpdateThread(GetCurrentThread()); }
			long Attach(void**, void*) const
			{
				for (const auto& site : sites) {
					const auto result = DetourAttach(site.original, site.hook);
					if (result != NO_ERROR)
						return result;
				}
				return NO_ERROR;
			}
			long Abort() const { return DetourTransactionAbort(); }
			long Commit() const { return DetourTransactionCommit(); }
		};
		const auto result = Util::detail::AttachDetourTransaction(nullptr, nullptr, Operations{ sites });
		if (result.error != NO_ERROR) {
			installationError = std::format("Atomic alpha-list hook installation failed ({})", result.error);
			logger::error("[VR alpha list] {}", installationError);
			return;
		}
		installationError.clear();
		installed = true;
		logger::info("[VR alpha list] Installed linked-pass duplicate protection");
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Control(const nlohmann::json& args)
	{
		for (const auto& [name, value] : args.items()) {
			(void)value;
			if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
				name != "expectedBuildId" && name != "alphaPassIntegrity")
				throw std::invalid_argument("Unknown vr_alpha_list argument");
		}
		if (args.contains("alphaPassIntegrity")) {
			const auto& settings = args.at("alphaPassIntegrity");
			if (!settings.is_object() || settings.size() != 1 || !settings.contains("enabled") || !settings.at("enabled").is_boolean())
				throw std::invalid_argument("alphaPassIntegrity requires exactly one boolean enabled field");
			const bool requested = settings.at("enabled").get<bool>();
			if (requested && !installed)
				throw std::invalid_argument("Alpha-list protection is unavailable on this runtime or hook owner");
			if (enabled.load(std::memory_order_relaxed) != requested) {
				generation.fetch_add(1, std::memory_order_acq_rel);
				enabled.store(requested, std::memory_order_release);
			}
		}
		return { { "installed", installed }, { "enabled", enabled.load() }, { "active", installed && enabled.load() },
			{ "installationError", installationError }, { "duplicatesSuppressed", duplicates.load() }, { "cacheAllocationFailures", allocationFailures.load() }, { "unverifiableMemberships", unverifiableMemberships.load() },
			{ "maximumCachedGroups", AlphaPassList::MaximumCachedGroups }, { "maximumCachedPasses", AlphaPassList::MaximumCachedPasses },
			{ "maximumWalkNodes", AlphaPassList::MaximumWalkNodes }, { "generation", generation.load() } };
	}
#endif
}
