#pragma once

#include <Windows.h>
#include <filesystem>
#include <string_view>
#include <system_error>

/**
 * @file Compatibility.h
 * @brief Plugins that cannot run alongside CSX.
 */
namespace Compatibility
{
	/** @brief A blocked plugin and, where known, why it is blocked. */
	struct IncompatiblePlugin
	{
		const wchar_t* dll;         ///< DLL path relative to Data/SKSE/Plugins.
		std::string_view reason{};  ///< Shown to the user; empty when the rationale is not recorded.
	};

	/** @brief Shadow Fixes shares the receiver hook with CSX's built-in repair. */
	inline constexpr IncompatiblePlugin shadowFixes{
		L"ShadowFixes.dll", "CSX already contains an alternative fix addressing the same sun-shadow receiver issue"
	};

	/** @brief External depth reversal cannot share CSX's depth-convention ownership. */
	inline constexpr IncompatiblePlugin externalReverseZ{
		L"skyrim-runtime-extender/reversed_z.dll", "external Reverse Z conflicts with CSX's depth handling; use CSX's built-in Reverse Z instead"
	};

	/** @brief Installed or loaded matches stop CSX startup before hooks and features initialize.
	 * Feature-local conflicts such as SPS remain with their owning features.
	 */
	inline constexpr IncompatiblePlugin incompatiblePlugins[] = {
		{ L"ShaderTools.dll" },
		{ L"SSEShaderTools.dll" },
		{ L"SkyrimUpscaler.dll" },
		{ L"EVLaS.dll", "superseded by Sky Sync" },
		{ L"AELAS.dll", "superseded by Sky Sync" },
		{ L"SSEReShadeHelper.dll" },
		{ L"TAASharpen.dll" },
		{ L"NVIDIA_Reflex.dll" },
		{ L"MARA.dll" },
		{ L"NativeMeshLightFlickerFix.dll",
			"superseded by Light Limit Fix; both replace the same lighting hooks" },
		{ L"NMN_DeformableTerrain.dll",
			"Dynamic Terrain Deformation is not supported alongside CSX Terrain Blending" },
		shadowFixes,
		externalReverseZ
	};

	/** @brief Checks loaded modules without loading or retaining the other plugin. */
	inline bool IsLoaded(const IncompatiblePlugin& plugin)
	{
		return GetModuleHandleW(std::filesystem::path(plugin.dll).filename().c_str()) != nullptr;
	}

	/** @brief A nonzero error means presence could not be established safely. */
	struct PluginProbe
	{
		bool present{};
		std::error_code error;
	};

	/** @brief Detects loaded or installed plugins without running their DLL entry points. */
	inline PluginProbe ProbePlugin(const IncompatiblePlugin& plugin, const std::filesystem::path& pluginsPath)
	{
		if (IsLoaded(plugin))
			return { true, {} };
		const auto attributes = GetFileAttributesW((pluginsPath / plugin.dll).c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES)
			return { (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0, {} };
		const auto error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
			return {};
		return { false, std::error_code(static_cast<int>(error), std::system_category()) };
	}
}
