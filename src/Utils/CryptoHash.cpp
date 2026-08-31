#include "Utils/CryptoHash.h"

#include <Windows.h>
#include <bcrypt.h>

#include <format>
#include <iomanip>
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
	}

	Sha256 Sha256Bytes(std::span<const std::byte> a_bytes)
	{
		const auto& provider = Provider();
		std::vector<UCHAR> object(provider.objectBytes);
		BCRYPT_HASH_HANDLE hash = nullptr;
		if (const auto status = BCryptCreateHash(
				provider.algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0);
			status < 0)
			throw std::runtime_error(std::format("BCryptCreateHash failed ({:#x})", static_cast<std::uint32_t>(status)));
		const auto hashStatus = BCryptHashData(
			hash,
			reinterpret_cast<PUCHAR>(const_cast<std::byte*>(a_bytes.data())),
			static_cast<ULONG>(a_bytes.size()),
			0);
		Sha256 digest{};
		const auto finishStatus = hashStatus < 0 ? hashStatus : BCryptFinishHash(
			hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0);
		BCryptDestroyHash(hash);
		if (finishStatus < 0)
			throw std::runtime_error(std::format("SHA-256 hashing failed ({:#x})", static_cast<std::uint32_t>(finishStatus)));
		return digest;
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
