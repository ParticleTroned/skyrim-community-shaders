#include "Features/ScreenshotCaptureVideo.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			std::abort();
	}

	std::uint32_t ReadU32(const std::vector<std::uint8_t>& a_bytes, std::size_t a_offset)
	{
		Require(a_offset + 4 <= a_bytes.size());
		return static_cast<std::uint32_t>(a_bytes[a_offset]) |
		       (static_cast<std::uint32_t>(a_bytes[a_offset + 1]) << 8u) |
		       (static_cast<std::uint32_t>(a_bytes[a_offset + 2]) << 16u) |
		       (static_cast<std::uint32_t>(a_bytes[a_offset + 3]) << 24u);
	}

	bool MatchesFourCC(
		const std::vector<std::uint8_t>& a_bytes,
		std::size_t a_offset,
		std::string_view a_fourCC)
	{
		return a_fourCC.size() == 4 && a_offset + 4 <= a_bytes.size() &&
		       std::equal(a_fourCC.begin(), a_fourCC.end(), a_bytes.begin() + a_offset);
	}

	std::size_t FindFourCC(
		const std::vector<std::uint8_t>& a_bytes,
		std::string_view a_fourCC,
		std::size_t a_start = 0)
	{
		for (auto offset = a_start; offset + 4 <= a_bytes.size(); ++offset) {
			if (MatchesFourCC(a_bytes, offset, a_fourCC))
				return offset;
		}
		return a_bytes.size();
	}
}

int main()
{
	const auto outputPath = std::filesystem::temp_directory_path() / "csx_screenshot_capture_video_test.avi";
	std::error_code ignored;
	std::filesystem::remove(outputPath, ignored);

	ScreenshotCaptureVideo::MjpegAviWriter writer;
	Require(writer.Open(outputPath, 640, 360, 15));
	const std::array<std::uint8_t, 5> oddFrame{ 0xFF, 0xD8, 1, 0xFF, 0xD9 };
	const std::array<std::uint8_t, 6> evenFrame{ 0xFF, 0xD8, 2, 3, 0xFF, 0xD9 };
	Require(writer.AddFrame(oddFrame));
	Require(writer.AddFrame(evenFrame));
	Require(writer.Finalize());

	std::ifstream input(outputPath, std::ios::binary);
	const std::vector<std::uint8_t> bytes{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()
	};
	Require(bytes.size() > 128);
	Require(MatchesFourCC(bytes, 0, "RIFF"));
	Require(ReadU32(bytes, 4) == bytes.size() - 8);
	Require(MatchesFourCC(bytes, 8, "AVI "));

	const auto avih = FindFourCC(bytes, "avih");
	Require(avih < bytes.size());
	Require(ReadU32(bytes, avih + 24) == 2);
	Require(ReadU32(bytes, avih + 40) == 640);
	Require(ReadU32(bytes, avih + 44) == 360);

	const auto movi = FindFourCC(bytes, "movi");
	const auto firstFrame = FindFourCC(bytes, "00dc", movi + 4);
	const auto secondFrame = FindFourCC(bytes, "00dc", firstFrame + 8 + oddFrame.size() + 1);
	const auto index = FindFourCC(bytes, "idx1", secondFrame + 8 + evenFrame.size());
	Require(movi < firstFrame && firstFrame < secondFrame && secondFrame < index);
	Require(ReadU32(bytes, firstFrame + 4) == oddFrame.size());
	Require(ReadU32(bytes, secondFrame + 4) == evenFrame.size());
	Require(ReadU32(bytes, index + 4) == 32);
	Require(MatchesFourCC(bytes, index + 8, "00dc"));
	Require(ReadU32(bytes, index + 16) == firstFrame - movi);
	Require(ReadU32(bytes, index + 20) == oddFrame.size());
	Require(MatchesFourCC(bytes, index + 24, "00dc"));
	Require(ReadU32(bytes, index + 32) == secondFrame - movi);
	Require(ReadU32(bytes, index + 36) == evenFrame.size());

	std::filesystem::remove(outputPath, ignored);
}
