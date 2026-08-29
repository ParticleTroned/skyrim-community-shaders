#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace CSX::ScreenshotPolicy
{
	inline constexpr std::uint32_t MaximumPendingOperations = 64;
	inline constexpr std::uint32_t MaximumSequenceDurationMs = 3'600'000;
	inline constexpr std::uint32_t MaximumSequenceSpanFrames = 216'000;

	inline bool CanAdmitPendingOperations(std::size_t a_pending)
	{
		return a_pending < MaximumPendingOperations;
	}

	inline bool IsSafeWindowsFilenameSegment(std::string_view a_value)
	{
		if (a_value.empty() || a_value == "." || a_value == ".." ||
			a_value.back() == ' ' || a_value.back() == '.')
			return false;
		for (const unsigned char value : a_value) {
			// Version 1 deliberately admits canonical printable ASCII only. This
			// avoids case-folding and Unicode-normalization aliases on Windows.
			if (value < 0x20 || value > 0x7e || std::string_view("<>:\"/\\|?*").find(value) != std::string_view::npos)
				return false;
		}
		auto deviceStem = std::string(a_value.substr(0, a_value.find('.')));
		std::ranges::transform(deviceStem, deviceStem.begin(), [](unsigned char value) {
			return static_cast<char>(std::toupper(value));
		});
		if (deviceStem == "CON" || deviceStem == "PRN" || deviceStem == "AUX" || deviceStem == "NUL")
			return false;
		if (deviceStem.size() == 4 &&
			(deviceStem.starts_with("COM") || deviceStem.starts_with("LPT")) &&
			deviceStem[3] >= '1' && deviceStem[3] <= '9')
			return false;
		return true;
	}

	inline std::string FilenameCollisionKey(std::string_view a_value)
	{
		std::string result(a_value);
		std::ranges::transform(result, result.begin(), [](unsigned char value) {
			return static_cast<char>(std::tolower(value));
		});
		return result;
	}

	inline bool IsWallClockScheduleWithinLimit(
		std::uint32_t a_startDelayMs,
		std::uint32_t a_intervalMs,
		std::uint32_t a_frameCount)
	{
		if (a_frameCount == 0 || a_intervalMs == 0)
			return false;
		const auto span = static_cast<std::uint64_t>(a_startDelayMs) +
		                  static_cast<std::uint64_t>(a_intervalMs) * (a_frameCount - 1u);
		return span <= MaximumSequenceDurationMs;
	}

	inline bool IsGameFrameScheduleWithinLimit(
		std::uint32_t a_startDelayFrames,
		std::uint32_t a_intervalFrames,
		std::uint32_t a_frameCount)
	{
		if (a_frameCount == 0 || a_intervalFrames == 0)
			return false;
		const auto span = static_cast<std::uint64_t>(a_startDelayFrames) +
		                  static_cast<std::uint64_t>(a_intervalFrames) * (a_frameCount - 1u);
		return span <= MaximumSequenceSpanFrames;
	}
}
