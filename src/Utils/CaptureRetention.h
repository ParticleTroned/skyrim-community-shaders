#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <utility>

namespace Util
{
	inline constexpr std::size_t kCaptureRetentionCapacity = 32;

	/** Bounded, capture-owned CPU snapshots; the publisher serializes Pin/Update. */
	template <class Key, class Value, std::size_t Capacity = kCaptureRetentionCapacity>
	class CaptureRetention
	{
	public:
		static_assert(Capacity > 0);
		struct Entry
		{
			const Key key;

			Entry(const Key& identity, Value initial) : key(identity), value_(std::move(initial)) {}
			/** Copy on consumer threads without touching the publisher or GPU. */
			Value Snapshot() const
			{
				std::scoped_lock lock(mutex_);
				return value_;
			}

		private:
			friend class CaptureRetention;
			mutable std::mutex mutex_;
			Value value_;
		};
		using Lease = std::shared_ptr<const Entry>;

		/** Reuse an exact identity; never evict an outstanding capture. */
		Lease Pin(const Key& key, Value initial)
		{
			std::weak_ptr<Entry>* free = nullptr;
			for (auto& slot : entries_) {
				if (auto entry = slot.lock()) {
					if (entry->key == key)
						return entry;
				} else if (!free)
					free = &slot;
			}
			if (!free)
				return {};
			auto entry = std::make_shared<Entry>(key, std::move(initial));
			*free = entry;
			return entry;
		}

		template <class UpdateValue>
		void Update(const Key& key, UpdateValue&& update)
		{
			UpdateIf([&](const auto& identity) { return identity == key; }, std::forward<UpdateValue>(update));
		}

		template <class Matches, class UpdateValue>
		void UpdateIf(Matches&& matches, UpdateValue&& update)
		{
			for (auto& slot : entries_) {
				if (auto entry = slot.lock(); entry && matches(entry->key)) {
					std::scoped_lock lock(entry->mutex_);
					update(entry->value_);
				}
			}
		}

	private:
		std::array<std::weak_ptr<Entry>, Capacity> entries_{};
	};
}
