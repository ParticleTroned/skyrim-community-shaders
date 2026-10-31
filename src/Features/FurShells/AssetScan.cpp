#include "AssetScan.h"

#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/StringUtils.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace FurShellAssets
{
	namespace
	{
		constexpr std::string_view Suffix = "_shell.dds";

		bool EndsWithShell(std::string_view a_name)
		{
			return a_name.size() >= Suffix.size() && Util::IEquals(a_name.substr(a_name.size() - Suffix.size()), Suffix);
		}

		/** @return True when a shell may exist or the tree could not be read in full; false only for a missing textures folder or a complete walk with none. */
		bool LooseShellMayExist(const std::filesystem::path& a_dataDir, std::stop_token a_stop)
		{
			std::error_code ec;
			std::filesystem::recursive_directory_iterator it(a_dataDir / "textures", ec);
			if (ec == std::errc::no_such_file_or_directory)
				return false;
			for (const std::filesystem::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
				if (a_stop.stop_requested())
					return true;
				const bool regular = it->is_regular_file(ec);
				if (!ec && regular && EndsWithShell(Util::PathToUtf8(it->path().filename())))
					return true;
			}
			return static_cast<bool>(ec);
		}

		struct Header
		{
			char magic[4];
			uint32_t version;
			uint32_t folderRecordOffset;
			uint32_t archiveFlags;
			uint32_t folderCount;
			uint32_t fileCount;
			uint32_t totalFolderNameLength;
			uint32_t totalFileNameLength;
			uint32_t fileFlags;
		};
		static_assert(sizeof(Header) == 36);

		constexpr uint32_t IncludeDirectoryNames = 0x1;
		constexpr uint32_t IncludeFileNames = 0x2;
		constexpr uint32_t FileRecordSize = 16;

		/** @return True when the archive holds a shell or its file name table cannot be read. */
		bool ArchiveMayHoldShell(const std::filesystem::path& a_path)
		{
			std::ifstream file(a_path, std::ios::binary);
			Header header{};
			if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) || std::string_view(header.magic, 4) != std::string_view("BSA\0", 4))
				return true;
			if ((header.archiveFlags & IncludeFileNames) == 0 || (header.version != 104 && header.version != 105))
				return true;

			if (header.folderRecordOffset < sizeof(Header))
				return true;
			const uint64_t folderRecordSize = header.version == 105 ? 24 : 16;
			uint64_t nameTable = header.folderRecordOffset + header.folderCount * folderRecordSize + static_cast<uint64_t>(header.fileCount) * FileRecordSize;
			if (header.archiveFlags & IncludeDirectoryNames)
				nameTable += static_cast<uint64_t>(header.totalFolderNameLength) + header.folderCount;  // each folder name carries a length byte

			file.seekg(0, std::ios::end);
			const auto fileSize = file.tellg();
			if (fileSize < 0 || header.totalFileNameLength > 64u * 1024u * 1024u ||
				nameTable > static_cast<uint64_t>(fileSize) ||
				header.totalFileNameLength > static_cast<uint64_t>(fileSize) - nameTable ||
				(header.fileCount != 0 && header.totalFileNameLength == 0))
				return true;
			std::string names(header.totalFileNameLength, '\0');
			file.seekg(static_cast<std::streamoff>(nameTable));
			if (!file.read(names.data(), static_cast<std::streamsize>(names.size())))
				return true;

			size_t nameCount = 0;
			for (size_t begin = 0; begin < names.size();) {
				const size_t end = names.find('\0', begin);
				if (end == std::string::npos || EndsWithShell(std::string_view(names).substr(begin, end - begin)))
					return true;
				begin = end + 1;
				++nameCount;
			}
			return nameCount != header.fileCount;
		}
	}

	bool AnyShellTextures(const std::filesystem::path& a_dataDir, std::stop_token a_stop)
	{
		try {
			if (a_stop.stop_requested() || LooseShellMayExist(a_dataDir, a_stop))
				return true;

			std::error_code ec;
			for (std::filesystem::directory_iterator it(a_dataDir, ec), end; !ec && it != end; it.increment(ec)) {
				if (a_stop.stop_requested())
					return true;
				const auto& path = it->path();
				if (Util::IEquals(Util::PathToUtf8(path.extension()), ".bsa") && ArchiveMayHoldShell(path))
					return true;
			}
			return static_cast<bool>(ec);
		} catch (const std::exception& error) {
			logger::warn("[Fur Shells] Asset scan incomplete; retaining texture lookup: {}", error.what());
			return true;
		}
	}
}
