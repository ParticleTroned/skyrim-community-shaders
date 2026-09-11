#include "OCUEffectFoveationAPI.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace
{
	ocu_effect_foveation::Snapshot mockSnapshot{};
	HMODULE mockLoadedModule = nullptr;
	HMODULE mockSystemModule = nullptr;
	std::uint32_t queryResult = 0;
	int queryCalls = 0;
	int moduleChecks = 0;
	bool exportAvailable = true;
	bool systemExportAvailable = false;
	int lastQueryProvider = 0;
	std::int64_t mockNow = 1000000;
	std::uint32_t __cdecl Query(std::uint32_t version, std::uint32_t bytes, ocu_effect_foveation::Snapshot* output)
	{
		++queryCalls;
		lastQueryProvider = 1;
		if (version != 1 || bytes != sizeof(mockSnapshot) || !output)
			throw std::runtime_error("Client query ABI mismatch");
		if (queryResult == 0)
			*output = mockSnapshot;
		return queryResult;
	}
	std::uint32_t __cdecl SystemQuery(std::uint32_t version, std::uint32_t bytes, ocu_effect_foveation::Snapshot* output)
	{
		const auto result = Query(version, bytes, output);
		lastQueryProvider = 2;
		return result;
	}
	HMODULE MockGetModuleHandleW(LPCWSTR name)
	{
		++moduleChecks;
		if (std::wcscmp(name, L"openvr_api.dll") == 0)
			return mockLoadedModule;
		if (std::wcscmp(name, L"vrclient_x64.dll") == 0)
			return mockSystemModule;
		throw std::runtime_error("Wrong runtime module");
	}
	FARPROC MockGetProcAddress(HMODULE candidateModule, LPCSTR name)
	{
		if (std::strcmp(name, "OCU_GetEffectFoveationV1") != 0)
			throw std::runtime_error("Wrong export");
		if (candidateModule == mockLoadedModule)
			return exportAvailable ? reinterpret_cast<FARPROC>(&Query) : nullptr;
		if (candidateModule == mockSystemModule)
			return systemExportAvailable ? reinterpret_cast<FARPROC>(&SystemQuery) : nullptr;
		throw std::runtime_error("Unexpected module queried");
	}
	BOOL MockQueryPerformanceCounter(LARGE_INTEGER* value)
	{
		value->QuadPart = mockNow;
		return TRUE;
	}
	BOOL MockQueryPerformanceFrequency(LARGE_INTEGER* value)
	{
		value->QuadPart = 1000000;
		return TRUE;
	}
}

// Substitute only the OS boundary; exercise the production client unchanged.
#define GetModuleHandleW MockGetModuleHandleW
#define GetProcAddress MockGetProcAddress
#define QueryPerformanceCounter MockQueryPerformanceCounter
#define QueryPerformanceFrequency MockQueryPerformanceFrequency
#include "Features/OCUEffectFoveationClient.h"
#undef GetModuleHandleW
#undef GetProcAddress
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency

int main()
{
	int assertions = 0;
	auto require = [&](bool condition, const char* label) {
		++assertions;
		if (!condition)
			throw std::runtime_error(label);
	};
	using OCUEffectFoveation::Client;
	using namespace ocu_effect_foveation;
	try {
		Client client;
		require(!client.ReadForFrame(1, false).Active() && moduleChecks == 0, "Default-off must not query runtime");
		require(!client.ReadForFrame(1, true).Active() && queryCalls == 0 && client.GetStatus() == Client::Status::ProviderUnavailable,
			"Missing loaded runtime must use native quality");
		mockLoadedModule = reinterpret_cast<HMODULE>(1);
		mockSnapshot.structSize = sizeof(mockSnapshot);
		mockSnapshot.version = 1;
		mockSnapshot.mode = Mode::EyeTracked;
		mockSnapshot.shape = Shape::UVRadialHalfExtent;
		mockSnapshot.frameId = 1;
		mockSnapshot.publicationQpc = mockNow;
		mockSnapshot.qpcFrequency = 1000000;
		mockSnapshot.centerUV[0][0] = .3f;
		mockSnapshot.centerUV[0][1] = .5f;
		mockSnapshot.centerUV[1][0] = .6f;
		mockSnapshot.centerUV[1][1] = .4f;
		mockSnapshot.innerRadius = .6f;
		mockSnapshot.midRadius = .8f;
		require(client.ReadForFrame(2, true).Active() && queryCalls == 1, "Current provider activates on next render frame");
		mockSnapshot.centerUV[0][0] = .4f;
		require(client.ReadForFrame(2, true).centers[0] == .3f && queryCalls == 1, "Both eyes/passes must use one frame mockSnapshot");
		require(!client.ReadForFrame(3, true).Active() && queryCalls == 2, "Old publication must not survive into new game frame");
		mockSnapshot.frameId = 400;
		require(client.ReadForFrame(4, true).Active(), "Nonconsecutive provider frame must recover immediately");
		require(!client.ReadForFrame(4, false).Active(), "Disable must clear cached active state in same renderer frame");
		mockSnapshot.frameId = 401;
		mockSnapshot.mode = Mode::Disabled;
		require(!client.ReadForFrame(5, true).Active() && client.GetStatus() == Client::Status::ProfileDisabled, "Disabled publication restores native");
		mockSnapshot.frameId = 402;
		mockSnapshot.mode = Mode::Fixed;
		require(client.ReadForFrame(6, true).Active(), "Disabled-to-fixed recovery has no cooldown");
		queryResult = 1;
		require(!client.ReadForFrame(7, true).Active() && client.GetStatus() == Client::Status::QueryUnsupported, "Failed query cannot reuse prior gaze");
		queryResult = 0;
		mockSnapshot.frameId = 403;
		mockNow += 100001;
		require(!client.ReadForFrame(8, true).Active(), "Stale wall-clock publication cannot be used");
		mockSnapshot.frameId = 404;
		mockSnapshot.publicationQpc = mockNow;
		require(client.ReadForFrame(9, true).Active(), "Fresh publication immediately recovers");
		mockSnapshot.frameId = 405;
		mockSnapshot.version = 99;
		mockSnapshot.mode = Mode::Disabled;
		require(!client.ReadForFrame(10, true).Active() && client.GetStatus() == Client::Status::QueryUnsupported,
			"Even disabled responses must validate the advertised ABI");
		mockLoadedModule = reinterpret_cast<HMODULE>(2);
		exportAvailable = false;
		require(!client.ReadForFrame(11, true).Active(), "Loaded older runtime missing export is normal native fallback");

		Client globalClient;
		mockSnapshot.version = 1;
		mockSnapshot.mode = Mode::EyeTracked;
		mockSnapshot.frameId = 1000;
		mockSystemModule = reinterpret_cast<HMODULE>(10);
		systemExportAvailable = false;
		const auto queriesBeforeValve = queryCalls;
		require(!globalClient.ReadForFrame(20, true).Active() && queryCalls == queriesBeforeValve,
			"Valve-only loader and vrclient without exact export must remain native");
		mockSystemModule = reinterpret_cast<HMODULE>(11);
		systemExportAvailable = true;
		mockSnapshot.centerUV[0][0] = .25f;
		require(globalClient.ReadForFrame(20, true).Active() && lastQueryProvider == 2,
			"Late system-wide OCU behind Valve stub must be discovered even in same renderer frame");
		const auto queriesBeforeCache = queryCalls;
		require(globalClient.ReadForFrame(20, true).centers[0] == .25f && queryCalls == queriesBeforeCache,
			"Unchanged selected provider still queried only once per renderer frame");
		mockLoadedModule = reinterpret_cast<HMODULE>(3);
		exportAvailable = true;
		mockSnapshot.frameId = 1;
		mockSnapshot.centerUV[0][0] = .75f;
		require(globalClient.ReadForFrame(20, true).centers[0] == .75f && lastQueryProvider == 1,
			"App-local OCU takes priority and invalidates old same-frame cached profile/freshness");
		require(!globalClient.ReadForFrame(21, true).Active(),
			"App-local preference must not fall through to another runtime merely because its publication is stale");
		mockLoadedModule = reinterpret_cast<HMODULE>(4);
		exportAvailable = false;
		mockSnapshot.frameId = 2;
		mockSnapshot.centerUV[0][0] = .35f;
		require(globalClient.ReadForFrame(21, true).centers[0] == .35f && lastQueryProvider == 2,
			"Switch back to system-wide OCU resets provider freshness and cached frame");
		mockSystemModule = nullptr;
		require(!globalClient.ReadForFrame(21, true).Active() && globalClient.GetStatus() == Client::Status::ProviderUnavailable,
			"Loss of selected provider must clear active cache in same renderer frame");
		mockLoadedModule = nullptr;
		mockSystemModule = reinterpret_cast<HMODULE>(12);
		mockSnapshot.frameId = 1;
		require(globalClient.ReadForFrame(21, true).Active() && lastQueryProvider == 2,
			"Already-loaded system-wide OCU works without app-local loader and without recovery delay");
		std::cout << "PASS: " << assertions << " production client/cache/fallback assertions\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
