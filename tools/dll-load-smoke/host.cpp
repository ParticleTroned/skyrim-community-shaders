#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

#ifndef PAD_BYTES
#	define PAD_BYTES (80 * 1024 * 1024)
#endif

static_assert(PAD_BYTES >= 16 * 1024 * 1024 && PAD_BYTES <= 256 * 1024 * 1024);

// Relocated engine calls land in a surrogate image; this does not model engine state.
#pragma section(".pad", read, write)
__declspec(allocate(".pad")) static unsigned char pad[PAD_BYTES];

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
		return 2;

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	std::memset(pad, 0xC3, sizeof(pad));
	DWORD previousProtection = 0;
	if (!VirtualProtect(pad, sizeof(pad), PAGE_EXECUTE_READ, &previousProtection) ||
		!FlushInstructionCache(GetCurrentProcess(), pad, sizeof(pad))) {
		std::fwprintf(stderr, L"Host image setup failed: %lu\n", GetLastError());
		return 2;
	}
	if (!SetCurrentDirectoryW(argv[2])) {
		std::fwprintf(stderr, L"Game root unavailable: %lu\n", GetLastError());
		return 2;
	}

	const HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	const DWORD error = module ? ERROR_SUCCESS : GetLastError();
	if (module)
		std::puts("CSX_DLL_LOAD_OK");
	else
		std::fwprintf(stderr, L"LOAD FAILED %lu\n", error);
	std::fflush(stdout);
	std::fflush(stderr);

	// Teardown belongs to real-game validation, outside this loader-only diagnostic.
	if (!TerminateProcess(GetCurrentProcess(), module ? 0 : 1))
		std::fwprintf(stderr, L"Host termination failed: %lu\n", GetLastError());
	return 2;
}
