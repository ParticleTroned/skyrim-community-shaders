#pragma once

#include "TextureData.h"
#include <algorithm>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>

namespace StreamingTextures
{
	using TexturePixels = std::shared_ptr<const DirectX::ScratchImage>;

	/** Bounded immutable restoration pixels; resource serials prevent reuse across texture reloads. */
	class RestorationCache
	{
	public:
		struct Snapshot
		{
			std::uint64_t limitBytes = 0, bytes = 0, entries = 0, generation = 1;
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::uint64_t hits = 0, misses = 0, evictions = 0, storeFailures = 0;
#endif
		};

		explicit RestorationCache(std::uint64_t limit = TextureStreamingPolicy::RestorationCacheBytes)
		{
			status.limitBytes = std::min(limit, TextureStreamingPolicy::RestorationCacheBytes);
		}

		[[nodiscard]] Snapshot GetStatus() const
		{
			std::scoped_lock lock(mutex);
			auto result = status;
			result.entries = entries.size();
			return result;
		}

		/** Returns shared immutable pixels without duplicating an active upload's memory. */
		TexturePixels Find(std::uint64_t serial, std::string_view path, const D3D11_TEXTURE2D_DESC& full, std::uint64_t generation)
		{
			std::scoped_lock lock(mutex);
			const auto found = index.find(serial);
			if (generation != status.generation || found == index.end() || found->second->path != path || !MatchesDDS(*found->second->pixels, full)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				++status.misses;
#endif
				return {};
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			++status.hits;
#endif
			entries.splice(entries.begin(), entries, found->second);
			return found->second->pixels;
		}

		/** Stale worker generations cannot repopulate a cleared cache. */
		bool Store(std::uint64_t serial, std::string_view path, const D3D11_TEXTURE2D_DESC& full, TexturePixels pixels, std::uint64_t generation)
		{
			if (!serial || path.empty() || path.size() > 260 || !pixels || !MatchesDDS(*pixels, full))
				return false;
			const auto bytes = pixels->GetPixelsSize();
			std::scoped_lock lock(mutex);
			if (generation != status.generation || !bytes || bytes > status.limitBytes)
				return false;
			try {
				if (const auto found = index.find(serial); found != index.end())
					Erase(found->second);
				while (!entries.empty() && (status.bytes > status.limitBytes - bytes || entries.size() >= maximumEntries)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					++status.evictions;
#endif
					Erase(std::prev(entries.end()));
				}
				entries.push_front({ serial, std::string(path), std::move(pixels) });
				try {
					index.emplace(serial, entries.begin());
				} catch (...) {
					entries.pop_front();
					throw;
				}
				status.bytes += bytes;
				return true;
			} catch (const std::bad_alloc&) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				++status.storeFailures;
#endif
				ClearUnlocked();
				return false;
			}
		}

		/** Invalidate in-flight stores while releasing retained CPU pixels. */
		void Clear(bool onlyIfPopulated = false)
		{
			std::scoped_lock lock(mutex);
			if (onlyIfPopulated && entries.empty())
				return;
			ClearUnlocked();
		}

	private:
		struct Entry
		{
			std::uint64_t serial;
			std::string path;
			TexturePixels pixels;
		};
		using Entries = std::list<Entry>;
		static constexpr std::size_t maximumEntries = 1024;
		mutable std::mutex mutex;
		Snapshot status;
		Entries entries;
		std::unordered_map<std::uint64_t, Entries::iterator> index;

		void ClearUnlocked()
		{
			++status.generation;
			entries.clear();
			index.clear();
			status.bytes = 0;
		}

		void Erase(Entries::iterator entry)
		{
			status.bytes -= entry->pixels->GetPixelsSize();
			index.erase(entry->serial);
			entries.erase(entry);
		}
	};
}
