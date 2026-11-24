#include "VRAlphaSort.h"

#include "AlphaSortPolicy.h"
#include "EngineFix.h"
#include "Utils/VirtualFunctionHook.h"
#include "VRAlphaPassListFix.h"
#include "VRAlphaSortCode.h"
#include <RE/B/BSGeometry.h>
#include <RE/B/BSRenderPass.h>
#include <RE/N/NiAccumulator.h>
#include <RE/N/NiCamera.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace VRAlphaSort
{
	namespace
	{
		using SortList = void (*)(RE::BSRenderPass**, const RE::NiCamera*, bool, bool);
		using RenderAlphas = void (*)(RE::NiAccumulator*, std::uint32_t);
		SortList originalSort{};
		RenderAlphas originalRender{};
		std::array<void*, 3> originalKeys{};
		std::array<std::unique_ptr<KeyCode>, 3> keyCode;
		thread_local std::optional<AlphaSort::Position> consumingOrigin;
		bool installed{}, attempted{}, engineFixesInstalled{};
		std::string installationError;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic_uint64_t sortedQueues{}, consumeScopes{};
#endif
		AlphaSort::Position Position(const RE::NiPoint3& point) noexcept { return { point.x, point.y, point.z }; }

		std::optional<AlphaSort::Position> Origin(const RE::NiCamera* camera) noexcept
		{
			if (!camera)
				return std::nullopt;
			const auto origin = Position(camera->world.translate);
			return AlphaSort::IsFinite(origin) ? std::optional{ origin } : std::nullopt;
		}

		float DistanceKey(const RE::NiBound& bound, AlphaSort::Position origin, float closest) noexcept
		{
			return AlphaSort::DistanceKey(Position(bound.center), bound.radius, origin, closest);
		}

		float ProjectedKey(const RE::NiBound& bound, const RE::NiPoint3& direction, float closest) noexcept
		{
			return (bound.center.y * direction.y + bound.center.x * direction.x + bound.center.z * direction.z) - closest * bound.radius;
		}

		float GroupKey(const RE::NiBound* bound, const RE::NiCamera* camera, bool closest) noexcept
		{
			if (const auto origin = Origin(camera))
				return DistanceKey(*bound, *origin, closest ? 1.0f : 0.0f);
			const auto& rotation = camera->world.rotate.entry;
			return ProjectedKey(*bound, { rotation[0][0], rotation[1][0], rotation[2][0] }, closest ? 1.0f : 0.0f);
		}

		float QueueKey(const RE::NiBound* bound, const RE::NiPoint3* direction, float closest) noexcept
		{
			return consumingOrigin ? DistanceKey(*bound, *consumingOrigin, closest) : ProjectedKey(*bound, *direction, closest);
		}

		void Sort(RE::BSRenderPass** head, const RE::NiCamera* camera, bool closest, bool reverse)
		{
			if (!head || !*head || !(*head)->passGroupNext)
				return;
			const auto origin = Origin(camera);
			// The shared native helper also sorts opaque batches in the opposite direction.
			if (reverse || !origin) {
				originalSort(head, camera, closest, reverse);
				return;
			}
			const VRAlphaPassListFix::RetirementScope lifetime;
			AlphaSort::Sort(*head, [&](const RE::BSRenderPass* pass) noexcept {
				return pass->geometry ? DistanceKey(pass->geometry->worldBound, *origin, closest ? 1.0f : 0.0f) : 0.0f;
			});
#ifdef DEVBENCH_BRIDGE_ENABLED
			sortedQueues.fetch_add(1, std::memory_order_relaxed);
#endif
		}

		void Render(RE::NiAccumulator* accumulator, std::uint32_t flags)
		{
			struct Scope
			{
				std::optional<AlphaSort::Position> previous = consumingOrigin;
				~Scope() { consumingOrigin = previous; }
			} scope;
			consumingOrigin = Origin(accumulator->camera);
#ifdef DEVBENCH_BRIDGE_ENABLED
			consumeScopes.fetch_add(1, std::memory_order_relaxed);
#endif
			originalRender(accumulator, flags);
		}

		struct Site
		{
			std::uintptr_t rva;
			std::span<const std::uint8_t> expected;
			void** original;
			void* replacement;
		};
	}

	void Install()
	{
		static_assert(offsetof(RE::NiAVObject, world) == 0x7C && offsetof(RE::NiAVObject, worldBound) == 0xE4);
		static_assert(offsetof(RE::NiTransform, translate) == 0x24 && offsetof(RE::NiAccumulator, camera) == 0x10);
		static_assert(offsetof(RE::BSRenderPass, geometry) == 0x10 && offsetof(RE::BSRenderPass, passGroupNext) == 0x30);
		if (attempted || !REL::Module::IsVR())
			return;
		attempted = true;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			installationError = "Distance ordering requires verified Skyrim VR 1.4.15 code";
			logger::warn("[VR alpha sort] {}", installationError);
			return;
		}
		if (EngineFix::IsInstalledByEngineFixes("AlphaDistanceSort")) {
			engineFixesInstalled = true;
			logger::info("[VR alpha sort] Skipped CSX distance ordering (already installed by Engine Fixes)");
			return;
		}
		const auto base = REL::Module::get().base();
		constexpr std::array<std::uint8_t, 14> sortBytes{ 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
		constexpr std::array<std::uint8_t, 15> renderBytes{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
		constexpr std::array<std::uint8_t, 53> groupBytes{
			0xF3, 0x0F, 0x10, 0x47, 0x7C, 0xF3, 0x0F, 0x10, 0x97, 0x88, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x8F, 0x94, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x06, 0xF3, 0x0F, 0x59, 0x56, 0x04, 0xF3, 0x0F, 0x59, 0x4E, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0x45, 0x84, 0xFF, 0x74, 0x05, 0xF3, 0x0F, 0x5C, 0x56, 0x0C
		};
		constexpr std::array<std::uint8_t, 64> peekBytes{
			0xF3, 0x0F, 0x10, 0x90, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x52, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x98, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x02, 0xF3, 0x0F, 0x59, 0x80, 0xE4, 0x00, 0x00, 0x00, 0x8B, 0x05, 0xD5, 0xBB, 0x18, 0x02, 0xF3, 0x0F, 0x59, 0x4A, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0xF3, 0x0F, 0x5C, 0xD3
		};
		constexpr std::array<std::uint8_t, 65> consumeBytes{
			0xF3, 0x0F, 0x10, 0x80, 0xE4, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x07, 0xF3, 0x0F, 0x10, 0xB0, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x77, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x4F, 0x08, 0xF3, 0x0F, 0x58, 0xF0, 0x41, 0x0F, 0x28, 0xC0, 0xF3, 0x0F, 0x59, 0x80, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0xF1, 0xF3, 0x0F, 0x5C, 0xF0
		};
		// Squared keys are compatible with a drain sentinel, not a projected-depth cutoff.
		constexpr std::array<std::uint8_t, 40> drainBytes{
			0xF3, 0x0F, 0x10, 0x35, 0x9B, 0x3C, 0x2A, 0x00, 0x0F, 0x2F, 0xC6, 0x48, 0x8B, 0x8B, 0x58, 0x01, 0x00, 0x00, 0x76, 0x2A, 0x0F, 0x28, 0xDE, 0x89, 0x7C, 0x24, 0x20, 0x0F, 0x28, 0xD7, 0x48, 0x8D, 0x54, 0x24, 0x30, 0xE8, 0x6F, 0x03, 0x04, 0x00
		};
		constexpr std::array<std::uint8_t, 4> emptyBytes{ 0xFF, 0xFF, 0x7F, 0xFF };
		std::array sites{
			Site{ 0x133FF00, sortBytes, reinterpret_cast<void**>(&originalSort), reinterpret_cast<void*>(&Sort) },
			Site{ 0x130A240, renderBytes, reinterpret_cast<void**>(&originalRender), reinterpret_cast<void*>(&Render) },
			Site{ 0x134A552, groupBytes, &originalKeys[0], nullptr },
			Site{ 0x134A64C, peekBytes, &originalKeys[1], nullptr },
			Site{ 0x134A79B, consumeBytes, &originalKeys[2], nullptr },
			Site{ 0x130A359, drainBytes, nullptr, nullptr },
			Site{ 0x15ADFFC, emptyBytes, nullptr, nullptr }
		};
		for (const auto& site : sites) {
			std::array<std::uint8_t, consumeBytes.size()> actual{};
			SIZE_T read{};
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(base + site.rva), actual.data(), site.expected.size(), &read) ||
				read != site.expected.size() || !std::equal(site.expected.begin(), site.expected.end(), actual.begin())) {
				installationError = std::format("Unsupported or modified alpha-sort code at RVA {:X}; retaining existing ordering", site.rva);
				logger::warn("[VR alpha sort] {}", installationError);
				return;
			}
			if (site.original)
				*site.original = reinterpret_cast<void*>(base + site.rva);
		}
		try {
			for (std::size_t i = 0; i < keyCode.size(); ++i) {
				auto& site = sites[i + 2];
				const auto helper = i == 0 ? reinterpret_cast<std::uintptr_t>(&GroupKey) : reinterpret_cast<std::uintptr_t>(&QueueKey);
				keyCode[i] = std::make_unique<KeyCode>(static_cast<KeySite>(i), helper, base + site.rva + site.expected.size(), base + 0x34D6250);
				site.replacement = const_cast<std::uint8_t*>(keyCode[i]->getCode());
			}
		} catch (const std::exception& error) {
			installationError = std::format("Unable to prepare alpha-sort code: {}", error.what());
			logger::error("[VR alpha sort] {}", installationError);
			return;
		}
		struct Operations
		{
			std::span<const Site> sites;
			long Begin() const { return DetourTransactionBegin(); }
			long UpdateThread() const { return DetourUpdateThread(GetCurrentThread()); }
			long Attach(void**, void*) const
			{
				for (const auto& site : sites) {
					if (!site.original)
						continue;
					const auto result = DetourAttach(site.original, site.replacement);
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
			installationError = std::format("Atomic alpha-sort installation failed ({})", result.error);
			logger::error("[VR alpha sort] {}", installationError);
			return;
		}
		installed = true;
		logger::info("[VR alpha sort] Installed distance ordering for alpha lists, groups, peek and consumption");
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json GetDiagnostics()
	{
		return { { "installed", installed }, { "active", installed || engineFixesInstalled },
			{ "provider", engineFixesInstalled ? "engineFixes" : installed ? "csx" :
																			 "none" },
			{ "candidate", true }, { "installationError", installationError },
			{ "metric", "signedSquaredCameraDistance" }, { "sortedQueues", sortedQueues.load() },
			{ "consumeScopes", consumeScopes.load() }, { "liveToggleSupported", false } };
	}
#endif
}
