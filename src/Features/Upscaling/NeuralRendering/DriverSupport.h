#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace NeuralRendering
{
	/** Windows driver baseline from the RTX Remix revision in NeuralRenderingSDKPin.cmake. */
	inline constexpr std::uint32_t kMinimumNvidiaDriverRelease = 61692;

	/** Rendering-adapter driver detection, independent of NGX initialization and capability queries. */
	struct DriverSupport
	{
		std::optional<std::uint32_t> installedRelease;
		std::uint32_t queryResult = 0;

		[[nodiscard]] bool RequiresUpdate() const noexcept
		{
			return installedRelease && *installedRelease < kMinimumNvidiaDriverRelease;
		}

		/** Explain an outdated or undetected driver; supported versions have no warning. */
		[[nodiscard]] std::string Message() const
		{
			if (installedRelease && !RequiresUpdate())
				return {};
			const auto requirement = std::format("Neural Rendering requires NVIDIA driver {}.{:02} or newer.",
				kMinimumNvidiaDriverRelease / 100u, kMinimumNvidiaDriverRelease % 100u);
			if (installedRelease)
				return std::format("{} Installed driver: {}.{:02}. Update your driver using the NVIDIA app or nvidia.com/drivers, then restart the game.",
					requirement, *installedRelease / 100u, *installedRelease % 100u);
			return requirement + " Your installed driver version could not be detected. Check it in the NVIDIA app and update if needed, then restart the game.";
		}
	};
}
