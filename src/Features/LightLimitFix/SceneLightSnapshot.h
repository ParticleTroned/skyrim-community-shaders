#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LightLimitFixDetail
{
	/// Owns captured lights; pass pointers are lookup keys, never ownership sources.
	template <class LightPointer>
	class SceneLightSnapshot
	{
	public:
		using Light = std::remove_pointer_t<decltype(std::declval<LightPointer>().get())>;

		/// Non-owning slot hints are built outside the queue lock and validated before retention.
		class OwnerIndex
		{
		public:
			/// Copy raw slot hints under the queue lock only after the capacity check succeeds.
			template <class RuntimeData>
			bool TryCaptureScene(const RuntimeData& a_runtime, std::size_t& a_required)
			{
				a_required = CountSceneSlots(a_runtime);
				if (a_required > locations.capacity())
					return false;
				locations.clear();
				VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t a_source) {
					for (decltype(a_list.size()) index = 0; index < a_list.size(); ++index)
						if (auto* key = a_list[index].get())
							locations.push_back({ key, a_source, index });
					return false;
				});
				return true;
			}

			/// Grow scratch storage only after releasing the engine queue lock.
			void ReserveCapture(std::size_t a_required) { ReserveGrowing(locations, a_required); }
			/// Sort captured raw hints after releasing the queue lock.
			void FinishCapture()
			{
				std::sort(locations.begin(), locations.end(), [](const Location& a_left, const Location& a_right) {
					if (a_left.key != a_right.key)
						return std::less<Light*>{}(a_left.key, a_right.key);
					return a_left.source != a_right.source ? a_left.source < a_right.source : a_left.index < a_right.index;
				});
			}

			/// A moved or newly added owner falls back to live lookup, never to a saved reference.
			template <class RuntimeData>
			[[nodiscard]] LightPointer Retain(const RuntimeData& a_runtime, Light* a_key) const
			{
				LightPointer owner;
				const auto it = std::lower_bound(locations.begin(), locations.end(), a_key, [](const Location& a_location, Light* a_value) {
					return std::less<Light*>{}(a_location.key, a_value);
				});
				if (it != locations.end() && it->key == a_key) {
					VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t a_source) {
						if (a_source != it->source || it->index >= a_list.size())
							return false;
						const auto index = static_cast<decltype(a_list.size())>(it->index);
						if (a_list[index].get() != a_key)
							return false;
						owner = a_list[index];
						return true;
					});
				}
				return owner ? std::move(owner) : RetainSceneOwner(a_runtime, a_key);
			}

		private:
			struct Location
			{
				Light* key;
				std::size_t source;
				std::size_t index;
			};
			std::vector<Location> locations;
		};

		/// Reuse raw hints per thread without sharing mutable scratch with nested dispatch.
		class OwnerIndexLease
		{
		public:
			OwnerIndexLease()
			{
				static thread_local OwnerIndex reusable;
				static thread_local bool borrowed = false;
				if (!borrowed) {
					borrowed = true;
					borrowFlag = &borrowed;
					index = &reusable;
				} else {
					index = &fallback;
				}
			}
			~OwnerIndexLease()
			{
				if (borrowFlag)
					*borrowFlag = false;
			}
			OwnerIndexLease(const OwnerIndexLease&) = delete;
			OwnerIndexLease& operator=(const OwnerIndexLease&) = delete;
			OwnerIndex& Get() { return *index; }

		private:
			OwnerIndex fallback;
			OwnerIndex* index = nullptr;
			bool* borrowFlag = nullptr;
		};

		/// Retain an owned reference; engine-source copies require the queue lock.
		template <class Pointer>
		void Retain(const Pointer& a_owner, bool a_active)
		{
			auto* light = a_owner.get();
			if (!light)
				return;
			auto [it, inserted] = owners.try_emplace(light, a_owner);
			Activate(it->second, a_active);
		}

		/// Capture active and pending owners while the scene's light queue lock is held.
		template <class RuntimeData>
		void RetainScene(const RuntimeData& a_runtime, bool a_trackActive = true)
		{
			VisitSceneOwners(a_runtime, [&](const auto& a_owner, bool a_active) {
				Retain(a_owner, a_trackActive && a_active);
				return false;
			});
		}

		/// Check all slots before copying any owner into preallocated storage under the queue lock.
		template <class RuntimeData>
		bool TryCaptureScene(const RuntimeData& a_runtime, std::size_t& a_required)
		{
			assert(captured.empty());
			a_required = CountSceneSlots(a_runtime);
			if (a_required > captured.capacity())
				return false;
			VisitSceneOwners(a_runtime, [&](const auto& a_owner, bool a_active) {
				if (a_owner)
					captured.push_back({ a_owner, a_active });
				return false;
			});
			return true;
		}

		/// Capacity survives frame resets; allocation never occurs in the locked capture.
		void ReserveCapture(std::size_t a_required) { ReserveGrowing(captured, a_required); }
		/// Deduplicate and transfer retained owners after releasing the queue lock.
		void FinishCapture()
		{
			for (auto& entry : captured) {
				auto* key = entry.owner.get();
				auto [it, inserted] = owners.try_emplace(key, std::in_place, std::move(entry.owner));
				Activate(it->second, entry.active);
			}
			captured.clear();
		}
		/// Release all engine leases outside the queue lock while retaining container capacity.
		void Clear()
		{
			owners.clear();
			activeLights.clear();
			captured.clear();
		}

		/// Acquire one current owning-list reference under the queue lock; never dereference the key.
		template <class RuntimeData>
		[[nodiscard]] static LightPointer RetainSceneOwner(const RuntimeData& a_runtime, Light* a_key)
		{
			LightPointer owner;
			if (a_key) {
				VisitSceneOwners(a_runtime, [&](const auto& a_candidate, bool) {
					if (a_candidate.get() != a_key)
						return false;
					owner = a_candidate;
					return true;
				});
			}
			return owner;
		}

		/// Reject uncaptured pass addresses without reading the pointed-to memory.
		[[nodiscard]] Light* Find(Light* a_light) const
		{
			const auto it = owners.find(a_light);
			return it == owners.end() ? nullptr : it->second.owner.get();
		}

		[[nodiscard]] const std::vector<Light*>& ActiveLights() const { return activeLights; }

	private:
		template <class Value>
		static void ReserveGrowing(std::vector<Value>& a_buffer, std::size_t a_required)
		{
			const auto capacity = a_buffer.capacity();
			const auto maximum = a_buffer.max_size();
			const auto grown = capacity > maximum / 2 ? maximum : std::max<std::size_t>(1, capacity * 2);
			a_buffer.reserve(std::max(a_required, grown));
		}

		template <class RuntimeData>
		static std::size_t CountSceneSlots(const RuntimeData& a_runtime)
		{
			std::size_t count = 0;
			VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t) {
				count += static_cast<std::size_t>(a_list.size());
				return false;
			});
			return count;
		}

		template <class RuntimeData, class Visitor>
		static bool VisitSceneOwners(const RuntimeData& a_runtime, Visitor a_visit)
		{
			return VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool a_active, std::size_t) {
				for (const auto& owner : a_list) {
					if (a_visit(owner, a_active))
						return true;
				}
				return false;
			});
		}

		template <class RuntimeData, class Visitor>
		static bool VisitSceneOwnerLists(const RuntimeData& a_runtime, Visitor a_visit)
		{
			return a_visit(a_runtime.activeLights, true, 0) ||
			       a_visit(a_runtime.activeShadowLights, true, 1) ||
			       a_visit(a_runtime.lightQueueAdd, false, 2) ||
			       a_visit(a_runtime.lightQueueRemove, false, 3) ||
			       a_visit(a_runtime.unk190, false, 4);
		}

		struct Entry
		{
			explicit Entry(const LightPointer& a_owner) : owner(a_owner) {}
			Entry(std::in_place_t, LightPointer&& a_owner) : owner(std::move(a_owner)) {}
			LightPointer owner;
			bool active = false;
		};
		void Activate(Entry& a_entry, bool a_active)
		{
			if (a_active && !a_entry.active) {
				activeLights.push_back(a_entry.owner.get());
				a_entry.active = true;
			}
		}

		struct CapturedOwner
		{
			LightPointer owner;
			bool active;
		};
		std::vector<CapturedOwner> captured;
		std::unordered_map<Light*, Entry> owners;
		std::vector<Light*> activeLights;
	};

	/// Recycle snapshot storage without retaining stale scene addresses or engine leases.
	template <class Scene, class Snapshot>
	class SceneLightSnapshotCache
	{
	public:
		SceneLightSnapshotCache() : nextSpare(storage.end()) {}
		SceneLightSnapshotCache(const SceneLightSnapshotCache&) = delete;
		SceneLightSnapshotCache& operator=(const SceneLightSnapshotCache&) = delete;
		SceneLightSnapshotCache(SceneLightSnapshotCache&&) = delete;
		SceneLightSnapshotCache& operator=(SceneLightSnapshotCache&&) = delete;
		/// Return the published frame capture; reset invalidates its engine leases.
		[[nodiscard]] Snapshot* Find(Scene* a_scene) const
		{
			const auto it = active.find(a_scene);
			return it == active.end() ? nullptr : it->second;
		}
		/// Prepare an unpublished slot outside the queue lock; clear its leases on failure.
		Snapshot& Spare()
		{
			if (nextSpare == storage.end()) {
				storage.emplace_back();
				nextSpare = std::prev(storage.end());
			}
			return *nextSpare;
		}
		/// Publish the spare outside the queue lock, preserving stable snapshot addresses.
		Snapshot* Publish(Scene* a_scene)
		{
			assert(nextSpare != storage.end());
			const auto [it, inserted] = active.try_emplace(a_scene, &*nextSpare);
			if (inserted)
				++nextSpare;
			else
				nextSpare->Clear();
			return it->second;
		}
		/// Release frame owners and scene keys outside the queue lock while recycling storage.
		void clear()
		{
			for (auto& snapshot : storage)
				snapshot.Clear();
			active.clear();
			nextSpare = storage.begin();
		}
		[[nodiscard]] bool empty() const { return active.empty(); }

	private:
		std::list<Snapshot> storage;
		std::unordered_map<Scene*, Snapshot*> active;
		typename std::list<Snapshot>::iterator nextSpare;
	};

}
