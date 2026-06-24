#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <filesystem>

// Loads DXVK's d3d11/dxgi DLLs from the Community Shaders mod subfolder
// (Data/SKSE/Plugins/CommunityShaders/dxvk) under unique base names
// (csd3d11.dll / csgi.dll) so DXVK runs from Data/ rather than the game root.
//
// Skyrim statically imports exactly one symbol from each of d3d11.dll and
// dxgi.dll (D3D11CreateDeviceAndSwapChain / CreateDXGIFactory), so the System32
// copies are mapped at process start. The Windows loader keys modules by base
// name, so a second "d3d11.dll"/"dxgi.dll" loaded from a subfolder would merely
// alias the already-mapped System32 module. Giving DXVK's DLLs unique base names
// sidesteps that; CS then points its two IAT hooks at these exports so the game
// renders on DXVK while the inert System32 copies are never called. The d3d11
// DLL's single "dxgi.dll" import is repointed to "csgi.dll" at build time -- see
// tools/stage-dxvk-dlls.ps1.
namespace DxvkLoader
{
	/** @brief Load DXVK's renamed DLLs once (idempotent). Must be called before the
	 *  game creates its D3D11 device. @return true if both DLLs loaded and their
	 *  exports resolved; false otherwise (caller should fall back to system DLLs). */
	bool Load();

	/** @brief Whether Load() has succeeded. */
	bool IsLoaded();

	/** @brief Directory holding the staged native DLLs
	 *  (Data/SKSE/Plugins/CommunityShaders/dxvk), resolved relative to this
	 *  plugin's own module so it is independent of the process CWD or a mod
	 *  manager's virtual file system.
	 *  @return the directory, or an empty path if the module could not be resolved. */
	std::filesystem::path GetDxvkDir();

	/** @brief DXVK's D3D11CreateDeviceAndSwapChain export, or nullptr if not loaded. */
	decltype(&D3D11CreateDeviceAndSwapChain) GetD3D11CreateDeviceAndSwapChain();

	/** @brief DXVK's CreateDXGIFactory export, or nullptr if not loaded. */
	decltype(&CreateDXGIFactory) GetCreateDXGIFactory();
}
