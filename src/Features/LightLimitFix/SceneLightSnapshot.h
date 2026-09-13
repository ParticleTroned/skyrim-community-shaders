#pragma once

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
			for (const auto& light : a_runtime.activeLights)
				Retain(light, a_trackActive);
			for (const auto& light : a_runtime.activeShadowLights)
				Retain(light, a_trackActive);
			for (const auto& light : a_runtime.lightQueueAdd)
				Retain(light, false);
			for (const auto& light : a_runtime.lightQueueRemove)
				Retain(light, false);
			for (const auto& light : a_runtime.unk190)
				Retain(light, false);
		}

		/// Reject uncaptured pass addresses without reading the pointed-to memory.
		[[nodiscard]] Light* Find(Light* a_light) const
		{
			const auto it = owners.find(a_light);
			return it == owners.end() ? nullptr : it->second.owner.get();
		}

		[[nodiscard]] const std::vector<Light*>& ActiveLights() const { return activeLights; }

	private:
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
