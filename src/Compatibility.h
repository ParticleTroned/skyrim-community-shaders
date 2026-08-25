#pragma once

/**
 * @file Compatibility.h
 * @brief Plugins that cannot run alongside CSX.
 */
namespace Compatibility
{
	/** @brief A blocked plugin and, where known, why it is blocked. */
	struct IncompatiblePlugin
	{
		const wchar_t* dll;         ///< Path relative to the game root.
		std::string_view reason{};  ///< Shown to the user; empty when the rationale is not recorded.
	};

	/** @brief Probed with LoadLibrary at startup; any hit disables all hooks and features. */
	inline constexpr IncompatiblePlugin incompatiblePlugins[] = {
		{ L"Data/SKSE/Plugins/ShaderTools.dll" },
		{ L"Data/SKSE/Plugins/SSEShaderTools.dll" },
		{ L"Data/SKSE/Plugins/SkyrimUpscaler.dll" },
		{ L"Data/SKSE/Plugins/EVLaS.dll", "superseded by Sky Sync" },
		{ L"Data/SKSE/Plugins/AELAS.dll", "superseded by Sky Sync" },
		{ L"Data/SKSE/Plugins/SSEReShadeHelper.dll" },
		{ L"Data/SKSE/Plugins/TAASharpen.dll" },
		{ L"Data/SKSE/Plugins/NVIDIA_Reflex.dll" },
		{ L"Data/SKSE/Plugins/MARA.dll" }
	};
}
