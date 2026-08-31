#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace CSX::ScreenshotPolicy
{
	inline constexpr std::uint32_t MaximumPendingOperations = 64;
	inline constexpr std::uint32_t MaximumOutputsPerFrame = 4;
	inline constexpr std::uint32_t MaximumSequenceDurationMs = 3'600'000;
	inline constexpr std::uint32_t MaximumSequenceSpanFrames = 216'000;

	inline bool CanAdmitPendingOperations(std::size_t a_pending)
	{
		return a_pending < MaximumPendingOperations;
	}

	inline bool CanAugmentOutputs(std::size_t a_current, std::size_t a_additional)
	{
		return a_current >= 1 &&
		       a_current <= MaximumOutputsPerFrame &&
		       a_additional <= MaximumOutputsPerFrame - a_current;
	}

	inline std::uint32_t ExpectedSequenceArtifacts(bool a_frameManifest)
	{
		return a_frameManifest ? 1u : 0u;
	}

	inline bool CanStartWorker(bool a_accepting, bool a_joinable)
	{
		return a_accepting && !a_joinable;
	}

	inline bool IsContainedPath(
		const std::filesystem::path& a_canonicalRoot,
		const std::filesystem::path& a_canonicalCandidate)
	{
		if (a_canonicalCandidate == a_canonicalRoot)
			return true;
		const auto relative = a_canonicalCandidate.lexically_relative(a_canonicalRoot);
		return !relative.empty() && !relative.is_absolute() &&
		       *relative.begin() != "..";
	}

	inline std::string_view ResolveActualOutputView(
		std::string_view a_requestedView,
		bool a_desktopSource,
		bool a_dominantEyeFallback,
		bool a_dominantEyeRight)
	{
		if (a_desktopSource)
			return a_requestedView == "source_native" ? "source_native" : std::string_view{};
		if (a_dominantEyeFallback && a_requestedView == "framed_combined")
			return a_dominantEyeRight ? "framed_right" : "framed_left";
		return a_requestedView;
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
