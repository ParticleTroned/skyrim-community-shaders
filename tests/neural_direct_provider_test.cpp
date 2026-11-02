#include "Features/Upscaling/NeuralRendering/DirectProvider.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
}

int main(int argc, char** argv)
{
	HANDLE file = INVALID_HANDLE_VALUE;
	HMODULE fixture = nullptr;
	try {
		Require(argc == 2, "Missing provider fixture");
		const auto path = std::filesystem::absolute(argv[1]);
		file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(file != INVALID_HANDLE_VALUE, "Could not lock provider fixture");
		fixture = LoadLibraryW(path.c_str());
		Require(fixture != nullptr, "Could not inspect fixture");
		const auto setFailure = reinterpret_cast<void (*)(unsigned)>(GetProcAddress(fixture, "FixtureSetFailure"));
		Require(setFailure != nullptr, "Fixture control unavailable");
		auto* device = reinterpret_cast<ID3D12Device*>(1);
		std::string detail;
		NeuralRendering::DirectProvider missing;
		Require(NVSDK_NGX_FAILED(missing.Initialize(path.parent_path() / L"missing-provider.dll", file, device, path.parent_path(), nullptr, detail)) && missing.Unload(),
			"Missing provider was accepted or retained");
		wchar_t executable[MAX_PATH]{};
		Require(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "Could not resolve executable");
		const auto otherFile = CreateFileW(executable, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(otherFile != INVALID_HANDLE_VALUE, "Could not open identity mismatch fixture");
		NeuralRendering::DirectProvider mismatched;
		const auto mismatchResult = mismatched.Initialize(path, otherFile, device, path.parent_path(), nullptr, detail);
		CloseHandle(otherFile);
		Require(NVSDK_NGX_FAILED(mismatchResult) && detail.find("identity differs") != std::string::npos && mismatched.Unload(),
			"A different loaded file was accepted");
		for (unsigned failure : { 1u, 2u }) {
			setFailure(failure);
			NeuralRendering::DirectProvider failed;
			Require(NVSDK_NGX_FAILED(failed.Initialize(path, file, device, path.parent_path(), nullptr, detail)), "Provider failure was hidden");
			Require(failed.Active() == (failure == 2), "Partial initialization ownership was lost");
			Require(!NVSDK_NGX_FAILED(failed.ShutdownDevice(device)) && failed.Unload(), "Failed startup could not retire safely");
		}
		setFailure(0);
		NeuralRendering::DirectProvider provider;
		Require(!NVSDK_NGX_FAILED(provider.Initialize(path, file, device, path.parent_path(), nullptr, detail)) && provider.Active(), detail.c_str());
		Require(!provider.Unload(), "Initialized provider was unloaded");
		const auto calls = reinterpret_cast<unsigned (*)()>(GetProcAddress(fixture, "FixtureCalls"));
		Require(calls && calls() == 3, "Provider initialization or callback registration was skipped");
		std::size_t bytes = 0;
		NVSDK_NGX_Handle* handle = nullptr;
		Require(!NVSDK_NGX_FAILED(provider.scratchSize(static_cast<NVSDK_NGX_Feature>(18), nullptr, &bytes)) && bytes == 123, "Scratch query did not reach provider");
		Require(!NVSDK_NGX_FAILED(provider.create(nullptr, static_cast<NVSDK_NGX_Feature>(18), nullptr, &handle)) && handle, "Feature creation did not reach provider");
		Require(!NVSDK_NGX_FAILED(provider.evaluate(nullptr, handle, nullptr, nullptr)), "Evaluation failed");
		Require(!NVSDK_NGX_FAILED(provider.release(handle)), "Feature release failed");
		Require(!NVSDK_NGX_FAILED(provider.ShutdownDevice(device)) && !provider.Active(), "Provider shutdown failed");
		Require(calls() == 127 && provider.Unload(), "Provider routing or import restoration failed");
		// A direct fixture call after unload must see the real executable name again.
		const auto scratch = reinterpret_cast<NeuralRendering::DirectProvider::ScratchSize>(GetProcAddress(fixture, "NVSDK_NGX_D3D12_GetScratchBufferSize"));
		Require(scratch(static_cast<NVSDK_NGX_Feature>(18), nullptr, &bytes) == NVSDK_NGX_Result_FAIL_PlatformError,
			"Provider-local import was not restored");
		FreeLibrary(fixture);
		CloseHandle(file);
		std::cout << "Direct provider identity, caller compatibility, dispatch and retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		if (fixture)
			FreeLibrary(fixture);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
