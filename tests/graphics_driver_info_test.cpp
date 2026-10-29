#include <Windows.h>
#include <winternl.h>

#include <array>
#include <cstring>
#include <d3dkmthk.h>
#include <dxgi.h>
#include <iostream>
#include <stdexcept>
#include <wrl/client.h>

namespace
{
	HMODULE WINAPI DriverTestLoadLibrary(LPCWSTR, HANDLE, DWORD);
	FARPROC WINAPI DriverTestGetProcAddress(HMODULE, LPCSTR);
	BOOL WINAPI DriverTestFreeLibrary(HMODULE);
	NTSTATUS APIENTRY DriverTestOpenAdapter(const D3DKMT_OPENADAPTERFROMLUID*);
	NTSTATUS APIENTRY DriverTestQueryAdapter(const D3DKMT_QUERYADAPTERINFO*);
	NTSTATUS APIENTRY DriverTestCloseAdapter(const D3DKMT_CLOSEADAPTER*);
}

#define LoadLibraryExW DriverTestLoadLibrary
#define GetProcAddress DriverTestGetProcAddress
#define FreeLibrary DriverTestFreeLibrary
#define D3DKMTOpenAdapterFromLuid DriverTestOpenAdapter
#define D3DKMTQueryAdapterInfo DriverTestQueryAdapter
#define D3DKMTCloseAdapter DriverTestCloseAdapter
#include "../src/Utils/GraphicsDriverInfo.cpp"
#undef LoadLibraryExW
#undef GetProcAddress
#undef FreeLibrary
#undef D3DKMTOpenAdapterFromLuid
#undef D3DKMTQueryAdapterInfo
#undef D3DKMTCloseAdapter

namespace
{
	bool installedProbe = false;
	bool moduleAvailable = true;
	bool functionsAvailable = true;
	int createStatus = 0;
	int queryStatus = 0;
	int destroyStatus = 0;
	int loads = 0;
	int frees = 0;
	int destroys = 0;
	int adapterCloses = 0;
	int adapterCount = 2;
	int adaptersStatus = 1;
	NTSTATUS openAdapterStatus = 0;
	NTSTATUS addressStatus = 0;
	NTSTATUS closeAdapterStatus = 0;
	bool throwFromQuery = false;
	const LUID fixtureLuid{ 123, -4 };
	const D3DKMT_ADAPTERADDRESS fixtureAddress{ 4, 2, 0 };
	std::array<AmdAdapterInfo, 2> fixtureAdapters{};
	AmdVersionsInfo fixture{};

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	int __cdecl CreateAmd(AmdAllocate a_allocate, int a_connected, void** a_context)
	{
		Require(a_connected == 1, "ADL must enumerate present adapters");
		auto* memory = a_allocate(8);
		Require(memory != nullptr && a_allocate(-1) == nullptr, "ADL allocation callback");
		std::free(memory);
		*a_context = createStatus == 0 ? &fixture : nullptr;
		return createStatus;
	}

	int __cdecl DestroyAmd(void* a_context)
	{
		Require(a_context == &fixture, "ADL context mismatch");
		++destroys;
		return destroyStatus;
	}

	int __cdecl ReadAdapters(void* a_context, int a_index, int* a_count, AmdAdapterInfo** a_adapters)
	{
		Require(a_context == &fixture && a_index == -1, "ADL adapter enumeration context");
		*a_count = adapterCount;
		*a_adapters = static_cast<AmdAdapterInfo*>(AllocateAmdMemory(sizeof(fixtureAdapters)));
		Require(*a_adapters != nullptr, "ADL test allocation");
		std::memcpy(*a_adapters, fixtureAdapters.data(), sizeof(fixtureAdapters));
		return adaptersStatus;
	}

	int __cdecl ReadAmd(void* a_context, int a_index, AmdVersionsInfo* a_versions)
	{
		Require(a_context == &fixture, "ADL query context mismatch");
		Require(a_index == 7, "AMD release must belong to the rendering GPU, not the first adapter");
		if (throwFromQuery)
			throw std::runtime_error("injected query exception");
		*a_versions = fixture;
		return queryStatus;
	}

	HMODULE WINAPI DriverTestLoadLibrary(LPCWSTR a_path, HANDLE a_file, DWORD a_flags)
	{
		if (installedProbe)
			return ::LoadLibraryExW(a_path, a_file, a_flags);
		++loads;
		Require(std::wstring_view(a_path) == L"atiadlxx.dll" && a_flags == LOAD_LIBRARY_SEARCH_SYSTEM32, "ADL must load from System32");
		::SetLastError(ERROR_MOD_NOT_FOUND);
		return moduleAvailable ? reinterpret_cast<HMODULE>(&fixture) : nullptr;
	}

	FARPROC WINAPI DriverTestGetProcAddress(HMODULE a_module, LPCSTR a_name)
	{
		if (installedProbe)
			return ::GetProcAddress(a_module, a_name);
		if (!functionsAvailable)
			return nullptr;
		const std::string_view name(a_name);
		if (name == "ADL2_Main_Control_Create")
			return reinterpret_cast<FARPROC>(&CreateAmd);
		if (name == "ADL2_Main_Control_Destroy")
			return reinterpret_cast<FARPROC>(&DestroyAmd);
		if (name == "ADL2_Adapter_AdapterInfoX3_Get")
			return reinterpret_cast<FARPROC>(&ReadAdapters);
		if (name == "ADL2_Graphics_VersionsX3_Get")
			return reinterpret_cast<FARPROC>(&ReadAmd);
		return nullptr;
	}

	BOOL WINAPI DriverTestFreeLibrary(HMODULE a_module)
	{
		if (installedProbe)
			return ::FreeLibrary(a_module);
		++frees;
		return TRUE;
	}

	NTSTATUS APIENTRY DriverTestOpenAdapter(const D3DKMT_OPENADAPTERFROMLUID* a_adapter)
	{
		if (installedProbe)
			return ::D3DKMTOpenAdapterFromLuid(a_adapter);
		Require(a_adapter->AdapterLuid.LowPart == fixtureLuid.LowPart && a_adapter->AdapterLuid.HighPart == fixtureLuid.HighPart,
			"The exact DXGI rendering LUID must be used");
		const_cast<D3DKMT_OPENADAPTERFROMLUID*>(a_adapter)->hAdapter = 23;
		return openAdapterStatus;
	}

	NTSTATUS APIENTRY DriverTestQueryAdapter(const D3DKMT_QUERYADAPTERINFO* a_query)
	{
		if (installedProbe)
			return ::D3DKMTQueryAdapterInfo(a_query);
		Require(a_query->hAdapter == 23 && a_query->Type == KMTQAITYPE_ADAPTERADDRESS &&
					a_query->PrivateDriverDataSize == sizeof(fixtureAddress),
			"Kernel adapter address query");
		*static_cast<D3DKMT_ADAPTERADDRESS*>(a_query->pPrivateDriverData) = fixtureAddress;
		return addressStatus;
	}

	NTSTATUS APIENTRY DriverTestCloseAdapter(const D3DKMT_CLOSEADAPTER* a_adapter)
	{
		if (installedProbe)
			return ::D3DKMTCloseAdapter(a_adapter);
		Require(a_adapter->hAdapter == 23, "Kernel adapter close identity");
		++adapterCloses;
		return closeAdapterStatus;
	}

	void ProbeInstalled()
	{
		installedProbe = true;
		Microsoft::WRL::ComPtr<IDXGIFactory> factory;
		Require(SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(factory.GetAddressOf()))), "DXGI factory");
		int supportedAdapters = 0;
		for (UINT index = 0;; ++index) {
			Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
			const auto enumerationStatus = factory->EnumAdapters(index, adapter.GetAddressOf());
			if (enumerationStatus == DXGI_ERROR_NOT_FOUND)
				break;
			Require(SUCCEEDED(enumerationStatus), "DXGI adapter enumeration failed");
			DXGI_ADAPTER_DESC description{};
			LARGE_INTEGER version{};
			const auto descriptionStatus = adapter->GetDesc(&description);
			const auto versionStatus = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
			Require(SUCCEEDED(descriptionStatus), "DXGI adapter description");
			const auto result = Util::QueryGraphicsDriverVersion(description,
				SUCCEEDED(versionStatus) ? std::optional<std::uint32_t>(version.LowPart) : std::nullopt);
			if (!result.vendor.empty()) {
				++supportedAdapters;
				Require(!result.version.empty(), result.error.c_str());
				std::cout << "[GPU] " << result.vendor << " driver version: " << result.version << result.error << '\n';
				std::cout << "Windows driver version: " << HIWORD(version.HighPart) << '.' << LOWORD(version.HighPart)
						  << '.' << HIWORD(version.LowPart) << '.' << LOWORD(version.LowPart) << '\n';
			}
		}
		Require(supportedAdapters != 0, "No supported graphics adapter was probed");
	}
}

int main(int a_argc, char** a_argv)
{
	try {
		if (a_argc == 2 && std::string_view(a_argv[1]) == "--probe-installed") {
			ProbeInstalled();
			return 0;
		}
		const auto QueryGraphicsDriverVersion = [](UINT a_vendorId, std::optional<std::uint32_t> a_version) {
			DXGI_ADAPTER_DESC adapter{};
			adapter.VendorId = a_vendorId;
			adapter.AdapterLuid = fixtureLuid;
			return Util::QueryGraphicsDriverVersion(adapter, a_version);
		};
		Require(QueryGraphicsDriverVersion(0x10DE, MAKELONG(1656, 16)).version == "616.56", "RTX 4090 supplied driver");
		Require(QueryGraphicsDriverVersion(0x10DE, MAKELONG(2101, 16)).version == "621.01", "RTX 5080 supplied driver");
		Require(QueryGraphicsDriverVersion(0x10DE, MAKELONG(0, 16)).version == "600.00", "NVIDIA zero padding");
		Require(!QueryGraphicsDriverVersion(0x10DE, std::nullopt).error.empty(), "Missing Windows version");
		Require(!QueryGraphicsDriverVersion(0x10DE, MAKELONG(10000, 16)).error.empty(), "Invalid NVIDIA encoding");
		Require(QueryGraphicsDriverVersion(0x8086, 0).vendor.empty() && loads == 0, "Other vendors must not query ADL");

		fixtureAdapters[0].size = fixtureAdapters[1].size = sizeof(AmdAdapterInfo);
		fixtureAdapters[0].exists = fixtureAdapters[1].exists = 1;
		fixtureAdapters[0].index = 3;
		fixtureAdapters[0].bus = 8;
		fixtureAdapters[1].index = 7;
		fixtureAdapters[1].bus = fixtureAddress.BusNumber;
		fixtureAdapters[1].device = fixtureAddress.DeviceNumber;
		fixtureAdapters[1].function = fixtureAddress.FunctionNumber;
		strcpy_s(fixture.driverPackage, "internal-package-version");
		strcpy_s(fixture.catalystVersion, "15.8");
		strcpy_s(fixture.crimsonVersion, "26.3.1");
		Require(QueryGraphicsDriverVersion(0x1002, std::nullopt).version == "26.3.1", "AMD public release must take priority");
		Require(destroys == 1 && frees == 1, "ADL context and module cleanup");
		queryStatus = 1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version == "26.3.1", "ADL warning may accompany a valid public release");
		queryStatus = 0;
		fixture.crimsonVersion[0] = '\0';
		Require(QueryGraphicsDriverVersion(0x1002, 0).version == "15.8", "Legacy AMD public release");
		fixture.catalystVersion[0] = '\0';
		std::memset(fixture.crimsonVersion, '9', sizeof(fixture.crimsonVersion));
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject unterminated AMD string");
		strcpy_s(fixture.crimsonVersion, "26.3.1\ninjected");
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject control characters");
		strcpy_s(fixture.crimsonVersion, "package-1.branch");
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject internal packaging text as a public version");
		strcpy_s(fixture.crimsonVersion, "26..1");
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject empty numeric components");
		fixtureAdapters[1].bus = 9;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Never substitute another GPU's driver");
		fixtureAdapters[1].bus = fixtureAddress.BusNumber;
		adapterCount = 257;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject invalid adapter counts before access");
		adapterCount = 2;
		adaptersStatus = 0;
		strcpy_s(fixture.crimsonVersion, "26.3.1");
		Require(QueryGraphicsDriverVersion(0x1002, 0).version == "26.3.1", "ADL_OK enumeration from current drivers");
		fixtureAdapters[1].size = 0;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version == "26.3.1", "ADL allocated records may omit their size field");
		fixtureAdapters[1].size = 1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject incompatible ADL record sizes");
		fixtureAdapters[1].size = sizeof(AmdAdapterInfo);
		fixtureAdapters[1].exists = 0;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Never use a removed adapter's version");
		fixtureAdapters[1].exists = 1;
		fixtureAdapters[1].index = -1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Never allow a global ADL index fallback");
		fixtureAdapters[1].index = 7;
		adapterCount = 0;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Reject empty ADL output even with success status");
		adapterCount = 2;
		adaptersStatus = -1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Respect ADL adapter enumeration failure");
		adaptersStatus = 1;
		throwFromQuery = true;
		const auto destroysBeforeThrow = destroys;
		bool queryThrew = false;
		try {
			(void)QueryGraphicsDriverVersion(0x1002, 0);
		} catch (const std::runtime_error&) {
			queryThrew = true;
		}
		Require(queryThrew && destroys == destroysBeforeThrow + 1, "ADL context must be released during unwinding");
		throwFromQuery = false;
		queryStatus = -1;
		Require(!QueryGraphicsDriverVersion(0x1002, 0).error.empty(), "ADL query failure");
		Require(destroys == frees, "Failed queries must release context and module");
		createStatus = -1;
		const auto oldDestroys = destroys;
		Require(!QueryGraphicsDriverVersion(0x1002, 0).error.empty() && destroys == oldDestroys, "Failed ADL initialization");
		functionsAvailable = false;
		Require(!QueryGraphicsDriverVersion(0x1002, 0).error.empty(), "Missing ADL entry points");
		moduleAvailable = false;
		Require(!QueryGraphicsDriverVersion(0x1002, 0).error.empty(), "Missing ADL library");
		moduleAvailable = functionsAvailable = true;
		createStatus = queryStatus = 0;
		destroyStatus = -1;
		const auto oldFrees = frees;
		Require(!QueryGraphicsDriverVersion(0x1002, 0).error.empty() && frees == oldFrees, "Retain library if context destruction fails");
		openAdapterStatus = -1;
		const auto oldCloses = adapterCloses;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty() && adapterCloses == oldCloses, "Failed LUID lookup must not close an unopened handle");
		openAdapterStatus = 0;
		addressStatus = -1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty() && adapterCloses == oldCloses + 1, "Failed address query must close its handle");
		addressStatus = 0;
		closeAdapterStatus = -1;
		Require(QueryGraphicsDriverVersion(0x1002, 0).version.empty(), "Kernel adapter close failure must remain explicit");
		std::cout << "Graphics driver information tests passed\n";
		return 0;
	} catch (const std::exception& a_error) {
		std::cerr << a_error.what() << '\n';
		return 1;
	}
}
