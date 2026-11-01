#include "Utils/CryptoHash.h"

#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct File
	{
		HANDLE handle = INVALID_HANDLE_VALUE;
		~File()
		{
			if (handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}
	};
}

int main()
{
	using namespace Util::CryptoHash;
	Require(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		"Shared SHA-256 no longer matches the known digest");
	const auto path = std::filesystem::temp_directory_path() /
	                  (std::wstring(L"csx-hash-") + std::to_wstring(GetCurrentProcessId()) + L".tmp");
	File file{ CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr) };
	Require(file.handle != INVALID_HANDLE_VALUE, "Could not create owned hash fixture");
	Require(Sha256File(file.handle) == Sha256Bytes(std::string_view{}), "Empty file hash differs");
	const std::string content(1024 * 1024 + 17, 'n');
	DWORD written = 0;
	Require(WriteFile(file.handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) && written == content.size(),
		"Could not write multi-chunk hash fixture");
	Require(Sha256File(file.handle) == Sha256Bytes(content), "File hashing did not seek and consume all chunks");
	Require(Sha256File(file.handle) == Sha256Bytes(content), "Borrowed handle could not be reused");
	bool rejected = false;
	try {
		(void)Sha256File(INVALID_HANDLE_VALUE);
	} catch (const std::exception&) {
		rejected = true;
	}
	Require(rejected, "Invalid file handle was accepted");
}
