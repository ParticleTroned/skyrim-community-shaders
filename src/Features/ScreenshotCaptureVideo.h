#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

namespace ScreenshotCaptureVideo
{
	class MjpegAviWriter
	{
	public:
		bool Open(
			const std::filesystem::path& a_path,
			std::uint32_t a_width,
			std::uint32_t a_height,
			std::uint32_t a_framesPerSecond)
		{
			if (a_width == 0 || a_height == 0 || a_framesPerSecond == 0 ||
				a_width > INT16_MAX || a_height > INT16_MAX) {
				return false;
			}

			path = a_path;
			width = a_width;
			height = a_height;
			framesPerSecond = a_framesPerSecond;
			if (!path.parent_path().empty())
				std::filesystem::create_directories(path.parent_path());
			output.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
			if (!output)
				return false;

			WriteFourCC("RIFF");
			riffSizePosition = output.tellp();
			WriteU32(0);
			WriteFourCC("AVI ");

			WriteFourCC("LIST");
			const auto headerListSizePosition = output.tellp();
			WriteU32(0);
			const auto headerListStart = output.tellp();
			WriteFourCC("hdrl");

			WriteFourCC("avih");
			WriteU32(56);
			WriteU32(std::max(1u, 1000000u / framesPerSecond));
			WriteU32(0);
			WriteU32(0);
			WriteU32(0x10u);
			mainHeaderFrameCountPosition = output.tellp();
			WriteU32(0);
			WriteU32(0);
			WriteU32(1);
			mainHeaderSuggestedBufferPosition = output.tellp();
			WriteU32(0);
			WriteU32(width);
			WriteU32(height);
			for (int i = 0; i < 4; ++i)
				WriteU32(0);

			WriteFourCC("LIST");
			const auto streamListSizePosition = output.tellp();
			WriteU32(0);
			const auto streamListStart = output.tellp();
			WriteFourCC("strl");

			WriteFourCC("strh");
			WriteU32(56);
			WriteFourCC("vids");
			WriteFourCC("MJPG");
			WriteU32(0);
			WriteU16(0);
			WriteU16(0);
			WriteU32(0);
			WriteU32(1);
			WriteU32(framesPerSecond);
			WriteU32(0);
			streamHeaderFrameCountPosition = output.tellp();
			WriteU32(0);
			streamHeaderSuggestedBufferPosition = output.tellp();
			WriteU32(0);
			WriteU32(UINT32_MAX);
			WriteU32(0);
			WriteI16(0);
			WriteI16(0);
			WriteI16(static_cast<std::int16_t>(width));
			WriteI16(static_cast<std::int16_t>(height));

			WriteFourCC("strf");
			WriteU32(40);
			WriteU32(40);
			WriteU32(width);
			WriteU32(height);
			WriteU16(1);
			WriteU16(24);
			WriteFourCC("MJPG");
			WriteU32(width * height * 3u);
			WriteU32(0);
			WriteU32(0);
			WriteU32(0);
			WriteU32(0);

			PatchSize(streamListSizePosition, streamListStart);
			PatchSize(headerListSizePosition, headerListStart);

			WriteFourCC("LIST");
			moviListSizePosition = output.tellp();
			WriteU32(0);
			moviListStart = output.tellp();
			WriteFourCC("movi");
			return static_cast<bool>(output);
		}

		bool AddFrame(std::span<const std::uint8_t> a_jpeg)
		{
			if (!output || a_jpeg.empty() || a_jpeg.size() > UINT32_MAX)
				return false;
			const auto chunkPosition = output.tellp();
			// AVI 1.0 idx1 offsets are relative to the start of the movi list data,
			// including its leading "movi" list type. The first frame is offset 4.
			const auto relativeOffset = chunkPosition - moviListStart;
			if (relativeOffset < 0 || static_cast<std::uint64_t>(relativeOffset) > UINT32_MAX)
				return false;
			const auto size = static_cast<std::uint32_t>(a_jpeg.size());
			WriteFourCC("00dc");
			WriteU32(size);
			output.write(
				reinterpret_cast<const char*>(a_jpeg.data()),
				static_cast<std::streamsize>(size));
			if ((size & 1u) != 0)
				output.put('\0');
			index.push_back({ static_cast<std::uint32_t>(relativeOffset), size });
			maxFrameSize = std::max(maxFrameSize, size);
			return static_cast<bool>(output);
		}

		bool Finalize()
		{
			if (!output || index.empty())
				return false;
			const auto indexChunkPosition = output.tellp();
			PatchValue(moviListSizePosition, static_cast<std::uint32_t>(indexChunkPosition - moviListStart));
			WriteFourCC("idx1");
			WriteU32(static_cast<std::uint32_t>(index.size() * 16u));
			for (const auto& entry : index) {
				WriteFourCC("00dc");
				WriteU32(0x10u);
				WriteU32(entry.offset);
				WriteU32(entry.size);
			}
			const auto end = output.tellp();
			if (end < 8 || static_cast<std::uint64_t>(end) - 8u > UINT32_MAX)
				return false;
			PatchValue(riffSizePosition, static_cast<std::uint32_t>(end) - 8u);
			PatchValue(mainHeaderFrameCountPosition, static_cast<std::uint32_t>(index.size()));
			PatchValue(streamHeaderFrameCountPosition, static_cast<std::uint32_t>(index.size()));
			PatchValue(mainHeaderSuggestedBufferPosition, maxFrameSize);
			PatchValue(streamHeaderSuggestedBufferPosition, maxFrameSize);
			output.seekp(end);
			output.flush();
			const bool succeeded = static_cast<bool>(output);
			output.close();
			return succeeded;
		}

	private:
		struct IndexEntry
		{
			std::uint32_t offset = 0;
			std::uint32_t size = 0;
		};

		void WriteFourCC(const char (&a_value)[5]) { output.write(a_value, 4); }
		void WriteU16(std::uint16_t a_value) { output.write(reinterpret_cast<const char*>(&a_value), sizeof(a_value)); }
		void WriteI16(std::int16_t a_value) { output.write(reinterpret_cast<const char*>(&a_value), sizeof(a_value)); }
		void WriteU32(std::uint32_t a_value) { output.write(reinterpret_cast<const char*>(&a_value), sizeof(a_value)); }
		void PatchValue(std::streampos a_position, std::uint32_t a_value)
		{
			const auto current = output.tellp();
			output.seekp(a_position);
			WriteU32(a_value);
			output.seekp(current);
		}
		void PatchSize(std::streampos a_sizePosition, std::streampos a_dataStart)
		{
			const auto end = output.tellp();
			PatchValue(a_sizePosition, static_cast<std::uint32_t>(end - a_dataStart));
		}

		std::filesystem::path path;
		std::fstream output;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t framesPerSecond = 0;
		std::uint32_t maxFrameSize = 0;
		std::streampos riffSizePosition{};
		std::streampos mainHeaderFrameCountPosition{};
		std::streampos mainHeaderSuggestedBufferPosition{};
		std::streampos streamHeaderFrameCountPosition{};
		std::streampos streamHeaderSuggestedBufferPosition{};
		std::streampos moviListSizePosition{};
		std::streampos moviListStart{};
		std::vector<IndexEntry> index;
	};
}
