#include "DxvkLoader.h"

#include <filesystem>

namespace DxvkLoader
{
	namespace
	{
		bool g_attempted = false;
		bool g_loaded = false;
		decltype(&D3D11CreateDeviceAndSwapChain) g_d3d11Create = nullptr;
		decltype(&CreateDXGIFactory) g_createFactory = nullptr;

		// Directory holding the staged DXVK DLLs, resolved relative to this
		// plugin's own module so it works regardless of the process CWD or a mod
		// manager's virtual file system. CommunityShaders.dll lives in
		// .../SKSE/Plugins, so the DLLs sit in .../SKSE/Plugins/CommunityShaders/dxvk.
		std::filesystem::path GetDxvkDir()
		{
			HMODULE self = nullptr;
			if (!::GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&GetDxvkDir),
					&self)) {
				return {};
			}
			wchar_t buf[MAX_PATH]{};
			const DWORD n = ::GetModuleFileNameW(self, buf, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) {
				return {};
			}
			return std::filesystem::path(buf).parent_path() / L"CommunityShaders" / L"dxvk";
		}
	}

	bool Load()
	{
		if (g_attempted) {
			return g_loaded;
		}
		g_attempted = true;

		const auto dir = GetDxvkDir();
		if (dir.empty()) {
			logger::error("[DXVK] Could not resolve plugin directory for DXVK DLLs");
			return false;
		}

		const auto dxgiPath = (dir / L"csgi.dll").wstring();
		const auto d3d11Path = (dir / L"csd3d11.dll").wstring();

		// Load dxgi first: csd3d11.dll imports it (the original "dxgi.dll" import
		// is repointed to "csgi.dll"), and the loader binds that import to the
		// already-mapped module by base name.
		const HMODULE dxgiMod = ::LoadLibraryExW(dxgiPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!dxgiMod) {
			const DWORD err = ::GetLastError();
			logger::error("[DXVK] Failed to load csgi.dll from '{}' (error {})", dir.string(), err);
			return false;
		}
		const HMODULE d3d11Mod = ::LoadLibraryExW(d3d11Path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!d3d11Mod) {
			const DWORD err = ::GetLastError();
			logger::error("[DXVK] Failed to load csd3d11.dll from '{}' (error {})", dir.string(), err);
			return false;
		}

		g_d3d11Create = reinterpret_cast<decltype(g_d3d11Create)>(::GetProcAddress(d3d11Mod, "D3D11CreateDeviceAndSwapChain"));
		g_createFactory = reinterpret_cast<decltype(g_createFactory)>(::GetProcAddress(dxgiMod, "CreateDXGIFactory"));

		if (!g_d3d11Create || !g_createFactory) {
			logger::error("[DXVK] Resolved DXVK DLLs but missing exports (d3d11create={}, createfactory={})",
				g_d3d11Create != nullptr, g_createFactory != nullptr);
			return false;
		}

		logger::info("[DXVK] Loaded DXVK from '{}' (csd3d11.dll + csgi.dll)", dir.string());
		g_loaded = true;
		return true;
	}

	bool IsLoaded() { return g_loaded; }
	decltype(&D3D11CreateDeviceAndSwapChain) GetD3D11CreateDeviceAndSwapChain() { return g_d3d11Create; }
	decltype(&CreateDXGIFactory) GetCreateDXGIFactory() { return g_createFactory; }
}
