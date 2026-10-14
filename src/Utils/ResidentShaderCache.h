#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace Util
{
	/** Bounded positive lookups that cannot retain retired shader resources. */
	template <class Key, class Shader, std::size_t Capacity = 4>
	class ResidentShaderCache
	{
		static_assert(Capacity > 0);

	public:
		/** Acquire an exact hit; the caller rechecks its resident revision afterward. */
		template <class Owner>
		[[nodiscard]] std::shared_ptr<Shader> Find(const std::shared_ptr<Owner>& a_ownerToken,
			const void* a_device, std::uint64_t a_revision, const Key& a_key)
		{
			UpdateContext(a_ownerToken, a_device, a_revision);
			for (auto& entry : entries) {
				if (entry.key && *entry.key == a_key) {
					auto shader = entry.shader.lock();
					if (!shader) {
						entry.key.reset();
						entry.shader.reset();
					}
					return shader;
				}
			}
			return {};
		}

		/** Store a positive result with the revision captured under its stage lock. */
		template <class Owner>
		void Store(const std::shared_ptr<Owner>& a_ownerToken, const void* a_device,
			std::uint64_t a_revision, const Key& a_key, const std::shared_ptr<Shader>& a_shader)
		{
			UpdateContext(a_ownerToken, a_device, a_revision);
			if (!a_shader)
				return;
			for (auto& entry : entries) {
				if (entry.key && *entry.key == a_key) {
					entry.shader = a_shader;
					return;
				}
			}
			auto& entry = entries[nextEntry];
			entry.key.emplace(a_key);
			entry.shader = a_shader;
			nextEntry = (nextEntry + 1) % Capacity;
		}

	private:
		struct Entry
		{
			std::optional<Key> key;
			std::weak_ptr<Shader> shader;
		};

		template <class Owner>
		void UpdateContext(const std::shared_ptr<Owner>& a_ownerToken, const void* a_device,
			std::uint64_t a_revision)
		{
			if (!ownerToken.owner_before(a_ownerToken) && !a_ownerToken.owner_before(ownerToken) &&
				device == a_device && revision == a_revision)
				return;
			for (auto& entry : entries) {
				entry.key.reset();
				entry.shader.reset();
			}
			ownerToken = a_ownerToken;
			device = a_device;
			revision = a_revision;
			nextEntry = 0;
		}

		std::array<Entry, Capacity> entries{};
		std::weak_ptr<const void> ownerToken;
		const void* device = nullptr;
		std::uint64_t revision = 0;
		std::size_t nextEntry = 0;
	};
}
