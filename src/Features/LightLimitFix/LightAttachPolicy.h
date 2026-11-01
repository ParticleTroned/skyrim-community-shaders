#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace LightLimitFixDetail::LightAttach
{
	inline constexpr int MinimumRefreshFrames = 1;
	inline constexpr int MaximumRefreshFrames = 600;
	inline constexpr int DefaultRefreshFrames = 60;
	inline constexpr std::size_t MaximumTrackedLights = 4096;
	inline constexpr std::uint64_t MaximumFrameAgeMilliseconds = 250;

	/** Light and scene inputs only; geometry membership can still change between refreshes. */
	struct Signature
	{
		const void* scene = nullptr;
		const void* source = nullptr;
		const void* object = nullptr;
		const void* lightPortalGraph = nullptr;
		const void* scenePortalGraph = nullptr;
		std::array<float, 3> position{}, radius{}, cachedPosition{};
		float fade = 0;
		std::uint32_t flags = 0, lightFlags = 0, culling = 0;
		bool operator==(const Signature&) const = default;
	};

	/** Fixed storage avoids allocation in the engine hook; colliding lights keep native handling. */
	template <class Owner, std::size_t Capacity = MaximumTrackedLights>
	class Tracker
	{
	public:
		static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);

		/** Observe only eligible calls; gaps and regressed clocks require a fresh native walk. */
		bool ShouldWalk(const void* a_key, const Signature& a_signature, std::uint32_t a_frame, std::uint32_t a_interval)
		{
			const auto slot = Slot(a_key);
			auto& entry = entries[slot];
			if (a_interval <= 1 || a_interval > MaximumRefreshFrames || !a_key || entry.owner.get() != a_key || !entry.valid)
				return true;
			const bool consecutive = a_frame >= entry.lastSeen && a_frame - entry.lastSeen <= 1;
			entry.lastSeen = a_frame;
			const bool walk = !consecutive || entry.signature != a_signature || a_frame < entry.lastWalk ||
			                  a_frame - entry.lastWalk >= a_interval || (std::uint64_t(a_frame) + slot) % a_interval == 0;
			if (walk)
				entry.valid = false;
			return walk;
		}

		/** Publish only after a successful native walk; never release another light inside the hook. */
		bool RecordWalk(const Owner& a_owner, const Signature& a_signature, std::uint32_t a_frame)
		{
			const auto* key = a_owner.get();
			if (!key)
				return false;
			auto& entry = entries[Slot(key)];
			if (entry.owner.get() && entry.owner.get() != key)
				return false;
			if (!entry.owner.get()) {
				entry.owner = a_owner;
				++size;
			} else if (entry.signature.source != a_signature.source || entry.signature.scene != a_signature.scene || entry.signature.object != a_signature.object) {
				// A replaced source or scene must pass through retirement before reusing the slot.
				entry.valid = false;
				return false;
			}
			entry.signature = a_signature;
			entry.lastSeen = entry.lastWalk = a_frame;
			entry.valid = true;
			return true;
		}

		/** Converted, strict and non-world calls invalidate any previous skip decision. */
		void Forget(const void* a_key)
		{
			auto& entry = entries[Slot(a_key)];
			if (entry.owner.get() == a_key)
				entry.valid = false;
		}

		/** Move retired references into empty caller storage, to be released after unlocking. */
		std::size_t Retire(std::uint32_t a_frame, bool a_all, std::span<Owner, Capacity> a_retired)
		{
			std::size_t count = 0;
			for (auto& entry : entries) {
				if (entry.owner.get() && (a_all || !entry.valid || a_frame < entry.lastSeen || a_frame - entry.lastSeen > 1)) {
					a_retired[count++] = std::move(entry.owner);
					entry.valid = false;
					--size;
				}
			}
			return count;
		}

		/** Number of retained identities, including invalid entries awaiting safe retirement. */
		std::size_t Size() const { return size; }

	private:
		static std::size_t Slot(const void* a_key)
		{
			auto key = reinterpret_cast<std::uintptr_t>(a_key) >> 4;
			key ^= key >> 17;
			key *= std::uintptr_t{ 0x9E3779B1u };
			return key & (Capacity - 1);
		}
		struct Entry
		{
			Owner owner;
			Signature signature;
			std::uint32_t lastSeen = 0, lastWalk = 0;
			bool valid = false;
		};
		std::array<Entry, Capacity> entries{};
		std::size_t size = 0;
	};
}
