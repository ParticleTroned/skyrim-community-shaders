#pragma once

#include "Utils/ContentHash.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// Tasks with identical bytecode inputs share one compile; the first compiles, the rest wait for its blob.
namespace Util::CompileDedupe
{
	/// Everything besides the preprocessed text that changes the compiled bytecode.
	struct KeyInputs
	{
		std::string_view preprocessed;
		std::string_view entryPoint;
		std::string_view profile;
		uint32_t flags = 0;
		std::string_view compilerId;
	};

	/// Hashes every bytecode-affecting input into one key.
	inline ContentHash::Hash128 MakeKey(const KeyInputs& a_in)
	{
		auto h = ContentHash::HashString(a_in.preprocessed);
		h = ContentHash::CombineHashes(h, ContentHash::HashString(a_in.entryPoint));
		h = ContentHash::CombineHashes(h, ContentHash::HashString(a_in.profile));
		const uint64_t flags = a_in.flags;
		h = ContentHash::CombineHashes(h, ContentHash::HashBytes(&flags, sizeof(flags)));
		return ContentHash::CombineHashes(h, ContentHash::HashString(a_in.compilerId));
	}

	/// Removes debug-only `#line` directives and blank lines from non-debug key text.
	inline std::string StripLineDirectives(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		size_t pos = 0;
		while (pos < a_text.size()) {
			const size_t end = a_text.find('\n', pos);
			const size_t next = end == std::string_view::npos ? a_text.size() : end + 1;
			const auto line = a_text.substr(pos, next - pos);
			const bool blank = line.find_first_not_of(" \t\r\n") == std::string_view::npos;
			if (!blank && !line.starts_with("#line"))
				out.append(line);
			pos = next;
		}
		return out;
	}

	/// Upper bound on finished blobs held in memory.
	inline constexpr uint64_t kDefaultMaxRetainedBytes = 1ull << 30;

	class Registry;

	/// Shared state of one in-flight or finished compile.
	struct Entry
	{
		enum class State
		{
			Pending,
			Done,
			Failed
		};
		std::mutex mutex;
		std::condition_variable ready;
		State state = State::Pending;
		std::shared_ptr<const std::vector<char>> blob;
	};

	/// Owner's claim on a key; dropped unpublished, it fails and waiters retry.
	class Ticket
	{
	public:
		Ticket(Registry& a_registry, ContentHash::Hash128 a_key, std::shared_ptr<Entry> a_entry) :
			registry(&a_registry), key(a_key), entry(std::move(a_entry)) {}
		Ticket(Ticket&& a_other) noexcept :
			registry(a_other.registry), key(a_other.key), entry(std::move(a_other.entry)) {}
		Ticket(const Ticket&) = delete;
		Ticket& operator=(const Ticket&) = delete;
		Ticket& operator=(Ticket&&) = delete;
		~Ticket() { Fail(); }

		/// Publishes final bytecode to waiters and relinquishes this claim.
		void Publish(const void* a_data, size_t a_size);
		/// Releases an unpublished claim so another task can retry.
		void Fail();

	private:
		Registry* registry;
		ContentHash::Hash128 key;
		std::shared_ptr<Entry> entry;
	};

	/// Exactly one of `blob` (reuse it) or `ticket` (compile it).
	struct Acquired
	{
		std::shared_ptr<const std::vector<char>> blob;
		std::optional<Ticket> ticket;
	};

	class Registry
	{
	public:
		/// @param a_maxRetainedBytes Cap on blobs kept after publishing; larger ones reach only tasks already waiting.
		explicit Registry(uint64_t a_maxRetainedBytes = kDefaultMaxRetainedBytes) :
			maxRetainedBytes(a_maxRetainedBytes) {}

		/// Returns a finished identical blob (waiting if one is in flight), else a ticket to compile with.
		Acquired Acquire(const ContentHash::Hash128& a_key)
		{
			for (;;) {
				std::shared_ptr<Entry> entry;
				{
					std::scoped_lock lock(mapMutex);
					auto it = map.find(a_key);
					if (it == map.end()) {
						entry = std::make_shared<Entry>();
						map.emplace(a_key, entry);
						return { nullptr, Ticket(*this, a_key, std::move(entry)) };
					}
					entry = it->second;
				}
				std::unique_lock lock(entry->mutex);
				entry->ready.wait(lock, [&] { return entry->state != Entry::State::Pending; });
				if (entry->state == Entry::State::Done)
					return { entry->blob, std::nullopt };
			}
		}

		/// Drops every retained blob; tickets already handed out stay valid.
		void Clear()
		{
			std::scoped_lock lock(mapMutex);
			map.clear();
			retainedBytes = 0;
			droppedBlobs = 0;
		}

		/// Bytes held by registered finished entries.
		uint64_t RetainedBytes() const { return retainedBytes.load(std::memory_order_relaxed); }

		/// Blobs that exceeded the retention cap and were not kept.
		uint64_t DroppedBlobs() const { return droppedBlobs.load(std::memory_order_relaxed); }

	private:
		friend class Ticket;
#ifdef CSX_COMPILE_DEDUPE_TESTING
		friend struct RegistryTestAccess;
#endif

		struct KeyHash
		{
			static constexpr uint64_t kLowMix = 0x9E3779B97F4A7C15ull;
			size_t operator()(const ContentHash::Hash128& a_key) const { return static_cast<size_t>(a_key.high ^ (a_key.low * kLowMix)); }
		};

		void Forget(const ContentHash::Hash128& a_key, const std::shared_ptr<Entry>& a_entry)
		{
			std::scoped_lock lock(mapMutex);
			const auto it = map.find(a_key);
			if (it != map.end() && it->second == a_entry)
				map.erase(it);
		}

		/// Charges a_size against the cap if the entry is still registered; an over-cap entry is unregistered and counted as dropped.
		void Retain(const ContentHash::Hash128& a_key, const std::shared_ptr<Entry>& a_entry, uint64_t a_size)
		{
			std::scoped_lock lock(mapMutex);
			const auto it = map.find(a_key);
			if (it == map.end() || it->second != a_entry)
				return;
			if (a_size > maxRetainedBytes - retainedBytes.load(std::memory_order_relaxed)) {
				droppedBlobs.fetch_add(1, std::memory_order_relaxed);
				map.erase(it);
				return;
			}
			retainedBytes.fetch_add(a_size, std::memory_order_relaxed);
		}

		uint64_t maxRetainedBytes;
		std::atomic<uint64_t> retainedBytes{ 0 };
		std::atomic<uint64_t> droppedBlobs{ 0 };
		std::mutex mapMutex;
		std::unordered_map<ContentHash::Hash128, std::shared_ptr<Entry>, KeyHash> map;
	};

	inline void Ticket::Publish(const void* a_data, size_t a_size)
	{
		if (!entry)
			return;
		auto blob = std::make_shared<const std::vector<char>>(static_cast<const char*>(a_data), static_cast<const char*>(a_data) + a_size);
		registry->Retain(key, entry, a_size);
		{
			std::scoped_lock lock(entry->mutex);
			entry->blob = std::move(blob);
			entry->state = Entry::State::Done;
		}
		entry->ready.notify_all();
		entry.reset();
	}

	inline void Ticket::Fail()
	{
		if (!entry)
			return;
		// Retry admission must be ready before existing waiters can wake.
		registry->Forget(key, entry);
		{
			std::scoped_lock lock(entry->mutex);
			entry->state = Entry::State::Failed;
		}
		entry->ready.notify_all();
		entry.reset();
	}
}
