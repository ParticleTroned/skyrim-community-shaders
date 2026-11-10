#include "SunOcclusionPollFix.h"

#include <REL/IDDB.h>
#include <REL/Module.h>
#include <REL/Pattern.h>
#include <REL/Relocation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
	// r9b controls flushing; r8d remains zero so pending queries never block.
	constexpr auto kPollInstructions = REL::make_pattern<"41 B1 01 48 8D 0D ?? ?? ?? ?? 45 33 C0 E8 ?? ?? ?? ??">();
	constexpr std::size_t kPollArgumentsSize = 13;
	constexpr std::size_t kCallSize = 5;
	constexpr std::size_t kFlushFlagOffset = 2;
}

bool SunOcclusionPollFix::TryInstall()
{
	if (installed)
		return true;
#ifdef ENABLE_SKYRIM_VR
	if (REL::Module::IsVR() && !REL::IDDB::get().IsVRAddressLibraryAtLeastVersion("0.281.0", false)) {
		logger::warn("[Sun Occlusion Poll Fix] VR Address Library 0.281.0 or newer is required; retaining native polling");
		return false;
	}
#endif

	const auto updateSunOcclusion = REL::RelocationID(99967, 106605).address();
	const auto pollOcclusionQuery = REL::RelocationID(75569, 77370).address();
	const std::uintptr_t callOffset = REL::Module::IsAE() ? 0x85 : 0x42;
	const auto text = REL::Module::get().segment(REL::Segment::textx);
	const auto containsCode = [&text](std::uintptr_t a_address, std::size_t a_size) {
		return a_address >= text.address() && a_address - text.address() <= text.size() &&
		       a_size <= text.size() - (a_address - text.address());
	};
	if (!containsCode(updateSunOcclusion, callOffset + kCallSize) || !containsCode(pollOcclusionQuery, 1)) {
		logger::warn("[Sun Occlusion Poll Fix] Poll addresses are outside executable code; retaining native polling");
		return false;
	}

	const auto pollCall = updateSunOcclusion + callOffset;
	const auto pollArguments = pollCall - kPollArgumentsSize;
	std::array<std::byte, kPollArgumentsSize + kCallSize> instructions{};
	SIZE_T read = 0;
	if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(pollArguments),
			instructions.data(), instructions.size(), &read) ||
		read != instructions.size() || !kPollInstructions.match(instructions)) {
		logger::warn("[Sun Occlusion Poll Fix] Poll instructions are modified or unsupported; retaining existing code");
		return false;
	}

	std::int32_t displacement = 0;
	std::memcpy(&displacement, instructions.data() + kPollArgumentsSize + 1, sizeof(displacement));
	if (pollCall + kCallSize + static_cast<std::intptr_t>(displacement) != pollOcclusionQuery) {
		logger::warn("[Sun Occlusion Poll Fix] Poll call target differs; retaining existing code");
		return false;
	}

	const auto flushFlag = pollArguments + kFlushFlagOffset;
	constexpr std::array<std::uint8_t, 1> expectedFlushFlag{ 1 };
	if (!REL::safe_write(flushFlag, std::uint8_t{ 0 }, expectedFlushFlag)) {
		logger::warn("[Sun Occlusion Poll Fix] Flush argument changed before installation; retaining existing code");
		return false;
	}
	std::uint8_t appliedFlag = 1;
	if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(flushFlag), &appliedFlag, sizeof(appliedFlag), &read) ||
		read != sizeof(appliedFlag) || appliedFlag != 0) {
		logger::error("[Sun Occlusion Poll Fix] Could not confirm the flush argument write");
		return false;
	}
	installed = true;
	return true;
}
