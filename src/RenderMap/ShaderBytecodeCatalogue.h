#pragma once

#include "RenderMap/Collector.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace CSX::RenderMap
{
	inline constexpr std::size_t kMaximumPersistentStageShaders = 65536;
	inline constexpr std::size_t kMaximumRetainedShaderDumpBytes = 64 * 1024 * 1024;

	namespace detail
	{
		inline std::atomic_uint64_t shaderMetadataFailures{ 0 };
	}

	/** Count unavailable diagnostic observations without allocating or logging. */
	inline void RecordShaderMetadataFailure() noexcept
	{
		detail::shaderMetadataFailures.fetch_add(1, std::memory_order_relaxed);
	}

	/** Return process-local unavailable-operation evidence without an engine snapshot claim. */
	inline std::uint64_t ShaderMetadataFailureCount() noexcept
	{
		return detail::shaderMetadataFailures.load(std::memory_order_relaxed);
	}

	/** Bound metadata and optional dumps independently of live capture storage. */
	class ShaderBytecodeCatalogue
	{
	public:
		struct Record
		{
			std::vector<std::uint8_t> bytes;
			std::uint64_t bytecodeSize{ 0 };
			std::array<char, kSha256HexLength + 1> sha256{};
			bool hashAvailable{ false };
		};

		enum class Admission
		{
			kStored,
			kDumpUnavailable,
			kIdentityLimit
		};

		explicit ShaderBytecodeCatalogue(
			std::size_t a_maxIdentities = kMaximumPersistentStageShaders,
			std::size_t a_maxDumpBytes = kMaximumRetainedShaderDumpBytes) noexcept :
			maxIdentities(std::min(a_maxIdentities, kMaximumPersistentStageShaders)),
			maxDumpBytes(std::min(a_maxDumpBytes, kMaximumRetainedShaderDumpBytes))
		{}

		/** Publish accounting only after insertion succeeds; failures retain prior data. */
		Admission Store(void* a_shader, Record a_record, const void* a_bytecode,
			std::size_t a_bytes, bool a_retainDump)
		{
			a_record.bytecodeSize = a_bytes;
			std::unique_lock lock(mutex);
			const auto old = records.find(a_shader);
			if (!a_shader || (old == records.end() && records.size() >= maxIdentities))
				return Admission::kIdentityLimit;
			const auto oldBytes = old == records.end() ? 0 : old->second.bytes.size();
			const auto remaining = maxDumpBytes - (retainedDumpBytes - oldBytes);
			a_record.bytes = std::vector<std::uint8_t>{};
			auto admission = Admission::kStored;
			if (a_retainDump) {
				if (a_bytes <= remaining && (a_bytecode || a_bytes == 0)) {
					a_record.bytes.resize(a_bytes);
					if (a_bytes != 0)
						std::memcpy(a_record.bytes.data(), a_bytecode, a_bytes);
				} else {
					admission = Admission::kDumpUnavailable;
				}
			}
			const auto newBytes = a_record.bytes.size();
			records.insert_or_assign(a_shader, std::move(a_record));
			retainedDumpBytes = retainedDumpBytes - oldBytes + newBytes;
			return admission;
		}

		bool ReadIdentity(void* a_shader, std::uint64_t& a_size,
			std::array<char, kSha256HexLength + 1>& a_sha256) const
		{
			a_size = 0;
			a_sha256 = {};
			std::shared_lock lock(mutex);
			const auto found = records.find(a_shader);
			if (found == records.end())
				return false;
			a_size = found->second.bytecodeSize;
			a_sha256 = found->second.hashAvailable ? found->second.sha256 :
			                                         std::array<char, kSha256HexLength + 1>{};
			return true;
		}

		std::vector<std::uint8_t> ReadBytes(void* a_shader) const
		{
			std::shared_lock lock(mutex);
			const auto found = records.find(a_shader);
			return found == records.end() ? std::vector<std::uint8_t>{} : found->second.bytes;
		}

		void Retire(void* a_shader)
		{
			std::unique_lock lock(mutex);
			const auto found = records.find(a_shader);
			if (found != records.end()) {
				retainedDumpBytes -= found->second.bytes.size();
				records.erase(found);
			}
		}

	private:
		const std::size_t maxIdentities;
		const std::size_t maxDumpBytes;
		mutable std::shared_mutex mutex;
		std::unordered_map<void*, Record> records;
		std::size_t retainedDumpBytes{ 0 };
	};
}
