#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>

namespace FSRDispatchInputTelemetry
{
	inline constexpr std::uint32_t kSchemaVersion = 1;

	/** Retain submitted SDK inputs; availability does not establish freshness. */
	struct Snapshot
	{
		bool available = false;
		bool reset = false;
		float jitterOffsetX = 0.0f;
		float jitterOffsetY = 0.0f;
		float frameTimeDeltaMilliseconds = 0.0f;
	};

	/** Reject nonfinite input evidence without changing the SDK dispatch. */
	inline Snapshot Capture(bool a_reset, float a_jitterX, float a_jitterY,
		float a_frameTimeDeltaMilliseconds) noexcept
	{
		if (!std::isfinite(a_jitterX) || !std::isfinite(a_jitterY) ||
			!std::isfinite(a_frameTimeDeltaMilliseconds) || a_frameTimeDeltaMilliseconds < 0.0f)
			return {};
		return { true, a_reset, a_jitterX, a_jitterY, a_frameTimeDeltaMilliseconds };
	}

	/** Emit a versioned nullable contract only for qualified successful evidence. */
	inline nlohmann::json ToJson(const Snapshot& a_snapshot, bool a_qualified)
	{
		using json = nlohmann::json;
		const bool available = a_qualified && a_snapshot.available &&
		                       Capture(a_snapshot.reset, a_snapshot.jitterOffsetX, a_snapshot.jitterOffsetY,
								   a_snapshot.frameTimeDeltaMilliseconds)
		                           .available;
		return {
			{ "schemaVersion", kSchemaVersion },
			{ "available", available },
			{ "reset", available ? json(a_snapshot.reset) : json(nullptr) },
			{ "jitterOffsetPixels", available ? json::array({ a_snapshot.jitterOffsetX, a_snapshot.jitterOffsetY }) : json(nullptr) },
			{ "frameTimeDeltaMilliseconds", available ? json(a_snapshot.frameTimeDeltaMilliseconds) : json(nullptr) },
		};
	}
}
