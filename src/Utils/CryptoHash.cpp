#include "Utils/CryptoHash.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Util::CryptoHash
{
	namespace
	{
		struct Sha256Provider
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			DWORD objectBytes = 0;

			Sha256Provider()
			{
				if (const auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0); status < 0)
					throw std::runtime_error(std::format("BCryptOpenAlgorithmProvider failed ({:#x})", static_cast<std::uint32_t>(status)));
				DWORD copied = 0;
				if (const auto status = BCryptGetProperty(
						algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &copied, 0);
					status < 0) {
					BCryptCloseAlgorithmProvider(algorithm, 0);
					algorithm = nullptr;
					throw std::runtime_error(std::format("BCryptGetProperty failed ({:#x})", static_cast<std::uint32_t>(status)));
				}
			}

			~Sha256Provider()
			{
				if (algorithm)
					BCryptCloseAlgorithmProvider(algorithm, 0);
			}
		};

		const Sha256Provider& Provider()
		{
			static const Sha256Provider provider;
			return provider;
		}

		class HashContext
		{
		public:
			HashContext() : object_(Provider().objectBytes)
			{
				const auto status = BCryptCreateHash(Provider().algorithm, &hash_, object_.data(), static_cast<ULONG>(object_.size()), nullptr, 0, 0);
				if (status < 0)
					throw std::runtime_error(std::format("BCryptCreateHash failed ({:#x})", static_cast<std::uint32_t>(status)));
			}
			~HashContext() { BCryptDestroyHash(hash_); }
			HashContext(const HashContext&) = delete;
			HashContext& operator=(const HashContext&) = delete;

			void Add(std::span<const std::byte> bytes)
			{
				while (!bytes.empty()) {
					const auto count = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), (std::numeric_limits<ULONG>::max)()));
					const auto status = BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), count, 0);
					if (status < 0)
						throw std::runtime_error(std::format("BCryptHashData failed ({:#x})", static_cast<std::uint32_t>(status)));
					bytes = bytes.subspan(count);
				}
			}
			Sha256 Finish()
			{
				Sha256 digest{};
				const auto status = BCryptFinishHash(hash_, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0);
				if (status < 0)
					throw std::runtime_error(std::format("BCryptFinishHash failed ({:#x})", static_cast<std::uint32_t>(status)));
				return digest;
			}

		private:
			std::vector<UCHAR> object_;
			BCRYPT_HASH_HANDLE hash_ = nullptr;
		};
	}

	Sha256 Sha256Bytes(std::span<const std::byte> a_bytes)
	{
		HashContext hash;
		hash.Add(a_bytes);
		return hash.Finish();
	}

	Sha256 Sha256File(void* a_file)
	{
		LARGE_INTEGER size{}, beginning{};
		if (!GetFileSizeEx(a_file, &size) || size.QuadPart < 0 || !SetFilePointerEx(a_file, beginning, nullptr, FILE_BEGIN))
			throw std::runtime_error(std::format("Could not size or seek file for SHA-256: {}", GetLastError()));
		HashContext hash;
		std::vector<std::byte> buffer(1024 * 1024);
		auto remaining = static_cast<std::uint64_t>(size.QuadPart);
		// Virtual file providers need not report EOF reliably; consume the locked file's exact length.
		while (remaining) {
			const auto count = static_cast<DWORD>(std::min<std::uint64_t>(remaining, buffer.size()));
			DWORD read = 0;
			if (!ReadFile(a_file, buffer.data(), count, &read, nullptr))
				throw std::runtime_error(std::format("ReadFile for SHA-256 failed: {}", GetLastError()));
			if (!read || read > count)
				throw std::runtime_error("File length changed while hashing");
			hash.Add(std::span(buffer).first(read));
			remaining -= read;
		}
		return hash.Finish();
	}

	std::string ToHex(const Sha256& a_digest)
	{
		std::ostringstream value;
		value << std::hex << std::setfill('0');
		for (const auto byte : a_digest)
			value << std::setw(2) << std::to_integer<unsigned>(byte);
		return value.str();
	}
}
