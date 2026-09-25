#include "CullPoolExhaustionFix.h"

#include "Utils/VirtualFunctionHook.h"

#include <array>
#include <cstddef>

namespace
{
	constexpr std::size_t kAppendVirtualSlot = 0x18;
	constexpr std::uintptr_t kFreePoolOffset = 0x20150;
	constexpr std::uintptr_t kPoolHeadOffset = 0x10000;
	constexpr std::uintptr_t kPoolTailOffset = 0x10008;
	constexpr std::uintptr_t kNonAccumFlagOffset = 0x301D5;
	// Workers can claim entries after this observation and before the native allocation.
	constexpr std::uint32_t kFreeEntryMargin = 64;
	constexpr std::array<std::uint8_t, 25> kExpectedPrologue{
		0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
		0x80, 0xB9, 0xD5, 0x01, 0x03, 0x00, 0x00, 0x41, 0x8B, 0xF8
	};

	bool ReadNativeBytes(std::uintptr_t a_address, void* a_output, std::size_t a_size)
	{
		SIZE_T read = 0;
		return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(a_address), a_output, a_size, &read) && read == a_size;
	}
}

bool CullPoolExhaustionFix::TryInstall()
{
	if (original)
		return true;
	const auto baseTable = RE::VTABLE_BSCullingProcess[0].address();
	const auto parabolicTable = RE::VTABLE_BSParabolicCullingProcess[0].address();
	std::uintptr_t baseTarget = 0;
	std::uintptr_t parabolicTarget = 0;
	if (!ReadNativeBytes(baseTable + kAppendVirtualSlot * sizeof(void*), &baseTarget, sizeof(baseTarget)) ||
		!ReadNativeBytes(parabolicTable + kAppendVirtualSlot * sizeof(void*), &parabolicTarget, sizeof(parabolicTarget)) ||
		!baseTarget || baseTarget != parabolicTarget) {
		logger::warn("[Engine Fixes] Culling AppendVirtual targets are unreadable or differ; retaining existing hooks");
		return false;
	}

	const auto text = REL::Module::get().segment(REL::Segment::textx);
	std::array<std::uint8_t, kExpectedPrologue.size()> bytes{};
	if (baseTarget < text.address() || baseTarget - text.address() > text.size() ||
		text.size() - (baseTarget - text.address()) < bytes.size() ||
		!ReadNativeBytes(baseTarget, bytes.data(), bytes.size()) || bytes != kExpectedPrologue) {
		logger::warn("[Engine Fixes] Culling AppendVirtual target is modified or unsupported; retaining existing hooks");
		return false;
	}

	// Both vtables share this engine entry; one checked detour preserves their common call path.
	original = reinterpret_cast<AppendVirtual>(baseTarget);
	const auto result = Util::AttachDetour(reinterpret_cast<void**>(&original), reinterpret_cast<void*>(AppendGuarded));
	if (result.error != NO_ERROR) {
		original = nullptr;
		logger::error("[Engine Fixes] Culling AppendVirtual detour failed ({})", result.error);
		return false;
	}
	return true;
}

void CullPoolExhaustionFix::AppendGuarded(RE::BSCullingProcess* a_process, RE::BSGeometry* a_geometry, std::int32_t a_alphaGroupIndex)
{
	const auto* base = reinterpret_cast<const std::uint8_t*>(a_process);
	if (base[kNonAccumFlagOffset] != 0 || a_alphaGroupIndex != -1) {
		const auto* pool = base + kFreePoolOffset;
		const auto head = std::atomic_ref<const std::uint32_t>(*reinterpret_cast<const std::uint32_t*>(pool + kPoolHeadOffset)).load(std::memory_order_acquire);
		const auto tail = std::atomic_ref<const std::uint32_t>(*reinterpret_cast<const std::uint32_t*>(pool + kPoolTailOffset)).load(std::memory_order_acquire);
		if (tail - head < kFreeEntryMargin) {
			if (droppedAppends.fetch_add(1, std::memory_order_relaxed) == 0)
				logger::warn("[Engine Fixes] Culling pool near exhaustion; dropping pool-consuming appends (logged once)");
			return;
		}
	}
	original(a_process, a_geometry, a_alphaGroupIndex);
}
