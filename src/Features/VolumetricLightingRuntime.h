#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace VolumetricLightingRuntime
{
	struct Target
	{
		bool enabled = false;
		bool weatherEnabled = false;
		int32_t quality = 0;
		std::array<int32_t, 3> highQualitySize{};

		bool operator==(const Target&) const = default;
	};

	struct Changes
	{
		bool flags = false;
		bool quality = false;
		bool clearHistory = false;
	};

	/** @brief Render-owned transition history; callers commit only after owning and applying the transaction. */
	class Controller
	{
	public:
		bool BeginFrame(uint32_t frame)
		{
			if (lastFrame == frame)
				return false;
			lastFrame = frame;
			return true;
		}

		Changes Plan(const Target& target, bool reset) const
		{
			const bool flags = reset || !applied || applied->enabled != target.enabled || applied->weatherEnabled != target.weatherEnabled;
			const bool quality = reset || !applied || applied->quality != target.quality || applied->highQualitySize != target.highQualitySize;
			return { flags, quality, (flags || quality) && (!target.enabled || !target.weatherEnabled) };
		}

		void Commit(const Target& target) { applied = target; }

	private:
		std::optional<uint32_t> lastFrame;
		std::optional<Target> applied;
	};
}
