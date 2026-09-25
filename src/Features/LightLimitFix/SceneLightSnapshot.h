#pragma once

#include <cstddef>
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

		/// Build and use under the queue lock; non-owning slot hints require live validation before retention.
		class OwnerIndex
		{
		public:
			template <class RuntimeData>
			explicit OwnerIndex(const RuntimeData& a_runtime)
			{
				std::size_t capacity = 0;
				VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t) {
					capacity += a_list.size();
					return false;
				});
				locations.reserve(capacity);
				VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t a_source) {
					for (decltype(a_list.size()) index = 0; index < a_list.size(); ++index) {
						if (auto* key = a_list[index].get())
							locations.try_emplace(key, a_source, index);
					}
					return false;
				});
			}

			/// A moved or newly added owner falls back to live lookup, never to a saved reference.
			template <class RuntimeData>
			[[nodiscard]] LightPointer Retain(const RuntimeData& a_runtime, Light* a_key) const
			{
				LightPointer owner;
				if (const auto it = locations.find(a_key); it != locations.end()) {
					const auto& location = it->second;
					VisitSceneOwnerLists(a_runtime, [&](const auto& a_list, bool, std::size_t a_source) {
						if (a_source != location.source || location.index >= a_list.size())
							return false;
						const auto index = static_cast<decltype(a_list.size())>(location.index);
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
				std::size_t source;
				std::size_t index;
			};
			std::unordered_map<Light*, Location> locations;
		};

		/// Copy an engine-owned reference while its list's mutation lock is held.
		template <class Pointer>
		void Retain(const Pointer& a_owner, bool a_active)
		{
			auto* light = a_owner.get();
			if (!light)
				return;
			auto [it, inserted] = owners.try_emplace(light, a_owner);
			if (a_active && !it->second.active) {
				activeLights.push_back(light);
				it->second.active = true;
			}
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
			LightPointer owner;
			bool active = false;
		};
		std::unordered_map<Light*, Entry> owners;
		std::vector<Light*> activeLights;
	};
}
