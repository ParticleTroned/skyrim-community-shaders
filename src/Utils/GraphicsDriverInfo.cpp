#include "GraphicsDriverInfo.h"

#include <Windows.h>
#include <winternl.h>

#include <algorithm>
#include <cstdlib>
#include <d3dkmthk.h>
#include <format>
#include <iterator>
#include <memory>
#include <type_traits>

namespace
{
	constexpr int kMaximumAmdAdapterCount = 256;

	// ABI layout of AMD ADLVersionsInfoX2 (display-library/include/adl_structures.h).
	struct AmdVersionsInfo
	{
		char driverPackage[256];
		char catalystVersion[256];
		char crimsonVersion[256];
		char webLink[256];
	};
	static_assert(sizeof(AmdVersionsInfo) == 1024);

	// Windows ABI layout of AMD AdapterInfo (display-library/include/adl_structures.h).
	struct AmdAdapterInfo
	{
		int size;
		int index;
		char uniqueId[256];
		int bus;
		int device;
		int function;
		int vendor;
		char name[256];
		char displayName[256];
		int present;
		int exists;
		char driverPath[256];
		char driverPathExt[256];
		char pnpString[256];
		int osDisplayIndex;
	};
	static_assert(sizeof(AmdAdapterInfo) == 1572);

	using AmdAllocate = void*(__stdcall*)(int);
	using AmdCreate = int(__cdecl*)(AmdAllocate, int, void**);
	using AmdDestroy = int(__cdecl*)(void*);
	using AmdGetAdapters = int(__cdecl*)(void*, int, int*, AmdAdapterInfo**);
	using AmdGetVersions = int(__cdecl*)(void*, int, AmdVersionsInfo*);

	void* __stdcall AllocateAmdMemory(int a_bytes)
	{
		return a_bytes > 0 ? std::malloc(static_cast<std::size_t>(a_bytes)) : nullptr;
	}

	std::string ReadAmdRelease(const char (&a_text)[256])
	{
		const auto end = std::find(std::begin(a_text), std::end(a_text), '\0');
		if (end == std::end(a_text))
			return {};
		const std::string_view text(a_text, end);
		if (text.empty() || text.front() == '.' || text.back() == '.' ||
			text.find('.') == std::string_view::npos || text.find("..") != std::string_view::npos ||
			std::any_of(text.begin(), text.end(), [](char a_char) { return a_char != '.' && (a_char < '0' || a_char > '9'); }))
			return {};
		return std::string(text);
	}

	std::optional<D3DKMT_ADAPTERADDRESS> QueryAdapterAddress(const LUID& a_luid, std::string& a_error)
	{
		D3DKMT_OPENADAPTERFROMLUID adapter{ .AdapterLuid = a_luid };
		const auto openStatus = D3DKMTOpenAdapterFromLuid(&adapter);
		if (openStatus != 0) {
			a_error = std::format("adapter LUID lookup failed (0x{:08X})", static_cast<std::uint32_t>(openStatus));
			return std::nullopt;
		}
		D3DKMT_ADAPTERADDRESS address{};
		D3DKMT_QUERYADAPTERINFO query{ adapter.hAdapter, KMTQAITYPE_ADAPTERADDRESS, &address, sizeof(address) };
		const auto queryStatus = D3DKMTQueryAdapterInfo(&query);
		D3DKMT_CLOSEADAPTER close{ adapter.hAdapter };
		const auto closeStatus = D3DKMTCloseAdapter(&close);
		if (queryStatus != 0 || closeStatus != 0) {
			a_error = std::format("adapter address query/close failed (0x{:08X}/0x{:08X})",
				static_cast<std::uint32_t>(queryStatus), static_cast<std::uint32_t>(closeStatus));
			return std::nullopt;
		}
		return address;
	}

	Util::GraphicsDriverVersion QueryAmdAdapterRelease(void* a_context, AmdGetAdapters a_getAdapters,
		AmdGetVersions a_getVersions, const D3DKMT_ADAPTERADDRESS& a_address)
	{
		Util::GraphicsDriverVersion result{ "AMD Software", {}, {} };
		int count = 0;
		AmdAdapterInfo* rawAdapters = nullptr;
		const auto adaptersStatus = a_getAdapters(a_context, -1, &count, &rawAdapters);
		const std::unique_ptr<AmdAdapterInfo, decltype(&std::free)> adapters(rawAdapters, &std::free);
		// Drivers return ADL_OK or the documented boolean success; validate the output.
		if ((adaptersStatus != 0 && adaptersStatus != 1) || !adapters || count <= 0 || count > kMaximumAmdAdapterCount) {
			result.error = std::format("ADL adapter enumeration failed ({}, count={})", adaptersStatus, count);
			return result;
		}
		for (int i = 0; i < count; ++i) {
			const auto& adapter = adapters.get()[i];
			// ADL-allocated records may leave the optional size field unset.
			if ((adapter.size != 0 && adapter.size != sizeof(AmdAdapterInfo)) || adapter.exists != 1 || adapter.index < 0 ||
				static_cast<UINT>(adapter.bus) != a_address.BusNumber ||
				static_cast<UINT>(adapter.device) != a_address.DeviceNumber ||
				static_cast<UINT>(adapter.function) != a_address.FunctionNumber)
				continue;
			AmdVersionsInfo versions{};
			const auto queryStatus = a_getVersions(a_context, adapter.index, &versions);
			// ADL_OK_WARNING permits individual version strings to be unavailable.
			if (queryStatus != 0 && queryStatus != 1) {
				result.error = std::format("ADL adapter version query failed ({})", queryStatus);
				return result;
			}
			result.version = ReadAmdRelease(versions.crimsonVersion);
			if (result.version.empty())
				result.version = ReadAmdRelease(versions.catalystVersion);
			if (result.version.empty())
				result.error = "ADL did not report a public software release";
			return result;
		}
		result.error = "ADL did not identify the rendering adapter";
		return result;
	}

	Util::GraphicsDriverVersion QueryAmdRelease(const LUID& a_luid)
	{
		Util::GraphicsDriverVersion result{ "AMD Software", {}, {} };
		const auto address = QueryAdapterAddress(a_luid, result.error);
		if (!address)
			return result;
		using Module = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;
		Module module(LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), &FreeLibrary);
		if (!module) {
			result.error = std::format("ADL unavailable (Windows error {})", GetLastError());
			return result;
		}
		const auto create = reinterpret_cast<AmdCreate>(GetProcAddress(module.get(), "ADL2_Main_Control_Create"));
		const auto destroy = reinterpret_cast<AmdDestroy>(GetProcAddress(module.get(), "ADL2_Main_Control_Destroy"));
		const auto getAdapters = reinterpret_cast<AmdGetAdapters>(GetProcAddress(module.get(), "ADL2_Adapter_AdapterInfoX3_Get"));
		const auto getVersions = reinterpret_cast<AmdGetVersions>(GetProcAddress(module.get(), "ADL2_Graphics_VersionsX3_Get"));
		if (!create || !destroy || !getAdapters || !getVersions) {
			result.error = "ADL version entry points unavailable";
			return result;
		}

		void* context = nullptr;
		const auto createStatus = create(AllocateAmdMemory, 1, &context);
		if (createStatus != 0 || !context) {
			result.error = std::format("ADL initialization failed ({})", createStatus);
			return result;
		}
		int destroyStatus = 0;
		{
			const auto cleanup = [&](void* a_context) noexcept {
				destroyStatus = destroy(a_context);
				// A context with uncertain lifetime must retain its library reference.
				if (destroyStatus != 0)
					(void)module.release();
			};
			const std::unique_ptr<void, decltype(cleanup)> ownedContext(context, cleanup);
			result = QueryAmdAdapterRelease(context, getAdapters, getVersions, *address);
		}
		if (destroyStatus != 0) {
			result.version.clear();
			result.error = std::format("ADL cleanup failed ({})", destroyStatus);
		}
		return result;
	}
}

namespace Util
{
	GraphicsDriverVersion QueryGraphicsDriverVersion(const DXGI_ADAPTER_DESC& a_adapter, std::optional<std::uint32_t> a_windowsVersionLow)
	{
		if (a_adapter.VendorId == 0x1002)
			return QueryAmdRelease(a_adapter.AdapterLuid);
		if (a_adapter.VendorId != 0x10DE)
			return {};
		if (!a_windowsVersionLow || LOWORD(*a_windowsVersionLow) > 9999)
			return { "NVIDIA", {}, "Windows driver version unavailable or unrecognized" };

		// NVIDIA documents the final five decimal digits as its public release number.
		const auto release = (HIWORD(*a_windowsVersionLow) % 10u) * 10000u + LOWORD(*a_windowsVersionLow);
		if (release == 0)
			return { "NVIDIA", {}, "Windows driver version is zero" };
		return { "NVIDIA", std::format("{}.{:02}", release / 100u, release % 100u), {} };
	}
}
