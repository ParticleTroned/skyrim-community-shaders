#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace CSX::RenderMap
{
	/** Bounded command epochs; these do not assert that a shader changed pixels. */
	class TransferVersions
	{
	public:
		static constexpr std::size_t kCapacity = 256;
		struct Version
		{
			std::uint64_t resource = 0;
			std::uint64_t observation = 0;
			std::uint64_t command = 0;
			std::uint64_t frame = 0;
		};

		void Reset(std::uint64_t a_generation) noexcept
		{
			generation = a_generation;
			entries = {};
			count = 0;
		}

		Version Read(std::uint64_t a_generation, std::uint64_t a_resource,
			std::uint64_t a_frame) const noexcept
		{
			if (!a_generation || a_generation != generation || !a_resource ||
				a_frame == (std::numeric_limits<std::uint64_t>::max)())
				return {};
			for (std::size_t index = 0; index < count; ++index)
				if (entries[index].resource == a_resource && entries[index].frame == a_frame && entries[index].observation)
					return entries[index];
			return {};
		}

		bool Write(std::uint64_t a_generation, Version a_version) noexcept
		{
			if (!a_generation || !a_version.resource || !a_version.observation || !a_version.command ||
				a_version.frame == (std::numeric_limits<std::uint64_t>::max)())
				return false;
			if (a_generation != generation)
				Reset(a_generation);
			for (std::size_t index = 0; index < count; ++index) {
				if (entries[index].resource == a_version.resource) {
					entries[index] = a_version;
					return true;
				}
			}
			if (count == entries.size())
				return false;
			entries[count++] = a_version;
			return true;
		}

		bool CanWrite(std::uint64_t a_generation, std::uint64_t a_resource) const noexcept
		{
			if (!a_generation || !a_resource)
				return false;
			if (a_generation != generation || count < entries.size())
				return true;
			for (std::size_t index = 0; index < count; ++index)
				if (entries[index].resource == a_resource)
					return true;
			return false;
		}

		void Invalidate() noexcept
		{
			for (std::size_t index = 0; index < count; ++index)
				entries[index].observation = 0;
		}

	private:
		std::uint64_t generation = 0;
		std::array<Version, kCapacity> entries{};
		std::size_t count = 0;
	};
}
