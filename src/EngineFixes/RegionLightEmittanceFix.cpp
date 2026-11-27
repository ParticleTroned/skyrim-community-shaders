#include "RegionLightEmittanceFix.h"

#include <REL/ID.h>
#include <REL/Module.h>
#include <REL/Pattern.h>
#include <REL/Relocation.h>
#include <SKSE/Version.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
	constexpr auto kWeatherInstructions = REL::make_pattern<"45 33 C9 4D 8B C6 48 8D 55 E7 48 8B CF E8 ?? ?? ?? ??">();
	constexpr std::size_t kArgumentsSize = 13;
	constexpr std::size_t kCallSize = 5;
	// Both weather inputs must use the resolved regional weather in r14.
	constexpr std::array<std::byte, 3> kPreviousWeatherArgument{ std::byte{ 0x4D }, std::byte{ 0x8B }, std::byte{ 0xCE } };
}

bool RegionLightEmittanceFix::TryInstall()
{
	if (installed)
		return true;
	const bool vr = REL::Module::IsVR();
	if (vr && REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::warn("[Region Light Emittance Fix] Unsupported VR runtime; retaining native emittance");
		return false;
	}

	const auto updateManagedNodes = REL::VariantID(18464, 18895, 0x272820).address();
	const std::uintptr_t argumentOffset = REL::Module::IsAE() ? 0x3CA : 0x3DC;
	const auto text = REL::Module::get().segment(REL::Segment::textx);
	const auto containsCode = [&text](std::uintptr_t a_address, std::size_t a_size) {
		return a_address >= text.address() && a_address - text.address() <= text.size() &&
		       a_size <= text.size() - (a_address - text.address());
	};
	if (!containsCode(updateManagedNodes, argumentOffset + kArgumentsSize + kCallSize)) {
		logger::warn("[Region Light Emittance Fix] Weather arguments are outside executable code; retaining existing code");
		return false;
	}

	const auto previousWeatherArgument = updateManagedNodes + argumentOffset;
	using Instructions = std::array<std::byte, kArgumentsSize + kCallSize>;
	const auto readInstructions = [previousWeatherArgument](Instructions& a_instructions) {
		SIZE_T read = 0;
		return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(previousWeatherArgument),
				   a_instructions.data(), a_instructions.size(), &read) &&
		       read == a_instructions.size();
	};
	Instructions original{};
	if (!readInstructions(original) || !kWeatherInstructions.match(original)) {
		logger::warn("[Region Light Emittance Fix] Weather arguments are modified or unsupported; retaining existing code");
		return false;
	}
	std::int32_t displacement = 0;
	std::memcpy(&displacement, original.data() + kArgumentsSize + 1, sizeof(displacement));
	const auto colourHelper = previousWeatherArgument + original.size() + static_cast<std::intptr_t>(displacement);
	if (!containsCode(colourHelper, 1) || (vr && colourHelper != REL::Module::get().base() + 0x3C5DF0)) {
		logger::warn("[Region Light Emittance Fix] Weather colour call target differs; retaining existing code");
		return false;
	}

	MEMORY_BASIC_INFORMATION originalPage{};
	if (VirtualQuery(reinterpret_cast<const void*>(previousWeatherArgument), &originalPage, sizeof(originalPage)) != sizeof(originalPage)) {
		logger::warn("[Region Light Emittance Fix] Weather argument page unavailable; retaining existing code");
		return false;
	}
	if (!REL::safe_write(previousWeatherArgument, kPreviousWeatherArgument.data(), kPreviousWeatherArgument.size(), kWeatherInstructions)) {
		logger::warn("[Region Light Emittance Fix] Weather arguments changed before installation; retaining existing code");
		return false;
	}
	Instructions expected = original;
	std::copy(kPreviousWeatherArgument.begin(), kPreviousWeatherArgument.end(), expected.begin());
	Instructions applied{};
	if (!readInstructions(applied) || applied != expected) {
		if (applied == original) {
			logger::error("[Region Light Emittance Fix] Weather argument write did not apply");
			return false;
		}
		stl::report_and_fail("The regional light emittance patch is inconsistent. See CSX.log.");
	}
	MEMORY_BASIC_INFORMATION appliedPage{};
	if (VirtualQuery(reinterpret_cast<const void*>(previousWeatherArgument), &appliedPage, sizeof(appliedPage)) != sizeof(appliedPage) ||
		appliedPage.Protect != originalPage.Protect ||
		!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(previousWeatherArgument), kPreviousWeatherArgument.size())) {
		stl::report_and_fail("The regional light emittance patch could not be finalized safely. See CSX.log.");
	}
	installed = true;
	return true;
}
