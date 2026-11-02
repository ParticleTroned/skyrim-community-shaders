#include "Features/Upscaling/NeuralRendering/DirectProvider.h"
#include "Utils/PathValidation.h"

#include <format>
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
	HANDLE noParametersFile = INVALID_HANDLE_VALUE;
	HMODULE fixture = nullptr;
	HMODULE proxy = nullptr;
	try {
		Require(argc == 3, "Missing provider fixtures");
		const auto path = std::filesystem::absolute(argv[1]);
		file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(file != INVALID_HANDLE_VALUE, "Could not lock provider fixture");
		fixture = LoadLibraryW(path.c_str());
		Require(fixture != nullptr, "Could not inspect fixture");
		const auto setFailure = reinterpret_cast<void (*)(unsigned)>(GetProcAddress(fixture, "FixtureSetFailure"));
		const auto parameterCount = reinterpret_cast<unsigned (*)()>(GetProcAddress(fixture, "FixtureParameterCount"));
		Require(setFailure && parameterCount, "Fixture control unavailable");
		Require(Util::IsPathWithinDirectory(path.parent_path(), path) &&
					!Util::IsPathWithinDirectory(path.parent_path(), path.parent_path().parent_path() / L"other-directory" / path.filename()) &&
					!Util::IsPathWithinDirectory(path.parent_path() / L"missing-directory", path),
			"Parameter core path boundary accepted an unrelated or unavailable root");
		auto* device = reinterpret_cast<ID3D12Device*>(1);
		std::string detail;
		const auto noParametersPath = std::filesystem::absolute(argv[2]);
		noParametersFile = CreateFileW(noParametersPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(noParametersFile != INVALID_HANDLE_VALUE, "Could not lock allocator-negative fixture");
		const auto proxyDirectory = std::filesystem::current_path() / "Testing" /
		                            std::format("NrParameterCore-{}-{}", GetCurrentProcessId(), GetTickCount64());
		std::filesystem::create_directories(proxyDirectory);
		const auto proxyPath = proxyDirectory / L"_nvngx.dll";
		std::filesystem::copy_file(path, proxyPath);
		proxy = LoadLibraryW(proxyPath.c_str());
		Require(proxy != nullptr, "Could not load allocator-negative proxy fixture");
		NeuralRendering::DirectProvider untrustedCore;
		const auto untrustedResult = untrustedCore.Initialize(noParametersPath, noParametersFile, device, path.parent_path(), detail);
		CloseHandle(noParametersFile);
		noParametersFile = INVALID_HANDLE_VALUE;
		const auto noParametersModule = GetModuleHandleW(noParametersPath.filename().c_str());
		const auto noParametersCalls = reinterpret_cast<unsigned (*)()>(GetProcAddress(noParametersModule, "FixtureCalls"));
		Require(NVSDK_NGX_FAILED(untrustedResult) && detail.find("DriverStore") != std::string::npos && !untrustedCore.Active() &&
					noParametersCalls && noParametersCalls() == 0 && untrustedCore.Unload(),
			"An untrusted parameter-core proxy reached native provider initialization");
		FreeLibrary(proxy);
		proxy = nullptr;
		std::filesystem::remove(proxyPath);
		std::filesystem::remove(proxyDirectory);
		NeuralRendering::DirectProvider missing;
		Require(NVSDK_NGX_FAILED(missing.Initialize(path.parent_path() / L"missing-provider.dll", file, device, path.parent_path(), detail)) && missing.Unload(),
			"Missing provider was accepted or retained");
		wchar_t executable[MAX_PATH]{};
		Require(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "Could not resolve executable");
		const auto otherFile = CreateFileW(executable, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(otherFile != INVALID_HANDLE_VALUE, "Could not open identity mismatch fixture");
		NeuralRendering::DirectProvider mismatched;
		const auto mismatchResult = mismatched.Initialize(path, otherFile, device, path.parent_path(), detail);
		CloseHandle(otherFile);
		Require(NVSDK_NGX_FAILED(mismatchResult) && detail.find("identity differs") != std::string::npos && mismatched.Unload(),
			"A different loaded file was accepted");
		for (unsigned failure : { 1u, 2u, 3u, 4u, 5u, 6u }) {
			setFailure(failure);
			NeuralRendering::DirectProvider failed;
			const auto initialized = failed.Initialize(path, file, device, path.parent_path(), detail);
			Require(NVSDK_NGX_FAILED(initialized) == (failure == 1) && failed.Active() == (failure != 1), "Native initialization result or ownership was lost");
			NVSDK_NGX_Parameter* parameters = nullptr;
			if (failure != 1) {
				const auto allocated = failed.AllocateParameters(&parameters, detail);
				Require(NVSDK_NGX_FAILED(allocated) == (failure == 3), "Allocator failure was hidden");
				Require((parameters != nullptr) == (failure != 6), "Partial or null allocation was misrepresented");
				if (parameters) {
					Require(!failed.Unload() && NVSDK_NGX_FAILED(failed.ShutdownDevice(device)), "Live parameter maps permitted provider retirement");
					Require(NVSDK_NGX_FAILED(failed.PopulateParameters(parameters)) == (failure == 2), "Provider population result was lost");
					if (failure == 4) {
						Require(NVSDK_NGX_FAILED(failed.DestroyParameters(parameters)) && parameterCount() == 1, "Failed map destruction lost allocator ownership");
						setFailure(0);
					}
					Require(!NVSDK_NGX_FAILED(failed.DestroyParameters(parameters)) && parameterCount() == 0, "Failed startup leaked a parameter map");
				}
				if (failure == 5) {
					Require(NVSDK_NGX_FAILED(failed.ShutdownDevice(device)) && failed.Active() && !failed.Unload(), "Failed native shutdown released the provider");
					setFailure(0);
				}
			}
			Require(!NVSDK_NGX_FAILED(failed.ShutdownDevice(device)) && failed.Unload(), "Failed startup could not retire safely");
		}
		for (unsigned failure : { 7u, 8u }) {
			setFailure(0);
			NeuralRendering::DirectProvider aliased;
			Require(!NVSDK_NGX_FAILED(aliased.Initialize(path, file, device, path.parent_path(), detail)), detail.c_str());
			NVSDK_NGX_Parameter* first = nullptr;
			Require(!NVSDK_NGX_FAILED(aliased.AllocateParameters(&first, detail)) && first, "Alias fixture allocation failed");
			setFailure(failure);
			NVSDK_NGX_Parameter* duplicate = nullptr;
			Require(NVSDK_NGX_FAILED(aliased.AllocateParameters(&duplicate, detail)) && !duplicate && parameterCount() == 1,
				"Repeated parameter pointer transferred duplicate ownership");
			Require(!NVSDK_NGX_FAILED(aliased.DestroyParameters(first)) && parameterCount() == 0 &&
						!NVSDK_NGX_FAILED(aliased.ShutdownDevice(device)) && aliased.Unload(),
				"Aliased allocation prevented exactly-once retirement");
		}
		setFailure(0);
		NeuralRendering::DirectProvider provider;
		Require(!NVSDK_NGX_FAILED(provider.Initialize(path, file, device, path.parent_path(), detail)) && provider.Active(), detail.c_str());
		Require(!provider.Unload(), "Initialized provider was unloaded");
		const auto calls = reinterpret_cast<unsigned (*)()>(GetProcAddress(fixture, "FixtureCalls"));
		Require(calls && calls() == 1, "Native initialization was skipped");
		NVSDK_NGX_Parameter* parameters = nullptr;
		NVSDK_NGX_Parameter* capabilities = nullptr;
		Require(!NVSDK_NGX_FAILED(provider.AllocateParameters(&parameters, detail)) && parameters &&
					!NVSDK_NGX_FAILED(provider.AllocateParameters(&capabilities, detail)) && capabilities && parameterCount() == 2,
			"Legacy allocator did not retain both parameter maps");
		NVSDK_NGX_Parameter* excess = nullptr;
		Require(NVSDK_NGX_FAILED(provider.AllocateParameters(&excess, detail)) && !excess && parameterCount() == 2,
			"Parameter ownership exceeded the capability and feature map slots");
		Require(NVSDK_NGX_FAILED(provider.DestroyParameters(reinterpret_cast<NVSDK_NGX_Parameter*>(4))) &&
					NVSDK_NGX_FAILED(provider.PopulateParameters(reinterpret_cast<NVSDK_NGX_Parameter*>(4))),
			"Foreign parameters reached the retained core");
		Require(!NVSDK_NGX_FAILED(provider.PopulateParameters(capabilities)), "Provider callback population failed");
		std::size_t bytes = 0;
		NVSDK_NGX_Handle* handle = nullptr;
		Require(!NVSDK_NGX_FAILED(provider.scratchSize(static_cast<NVSDK_NGX_Feature>(18), nullptr, &bytes)) && bytes == 123, "Scratch query did not reach provider");
		Require(!NVSDK_NGX_FAILED(provider.create(nullptr, static_cast<NVSDK_NGX_Feature>(18), nullptr, &handle)) && handle, "Feature creation did not reach provider");
		Require(!NVSDK_NGX_FAILED(provider.evaluate(nullptr, handle, nullptr, nullptr)), "Evaluation failed");
		Require(!NVSDK_NGX_FAILED(provider.release(handle)), "Feature release failed");
		Require(!NVSDK_NGX_FAILED(provider.DestroyParameters(parameters)) && !NVSDK_NGX_FAILED(provider.DestroyParameters(capabilities)) &&
					NVSDK_NGX_FAILED(provider.DestroyParameters(parameters)) && parameterCount() == 0,
			"Parameter destruction lost ownership or accepted double destruction");
		Require(!NVSDK_NGX_FAILED(provider.ShutdownDevice(device)) && !provider.Active(), "Provider shutdown failed");
		Require(calls() == 511 && provider.Unload(), "Provider routing, allocator retirement or import restoration failed");
		// A direct fixture call after unload must see the real executable name again.
		const auto scratch = reinterpret_cast<NeuralRendering::DirectProvider::ScratchSize>(GetProcAddress(fixture, "NVSDK_NGX_D3D12_GetScratchBufferSize"));
		Require(scratch(static_cast<NVSDK_NGX_Feature>(18), nullptr, &bytes) == NVSDK_NGX_Result_FAIL_PlatformError,
			"Provider-local import was not restored");
		FreeLibrary(fixture);
		CloseHandle(file);
		std::cout << "Direct provider identity, caller compatibility, dispatch and retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		if (noParametersFile != INVALID_HANDLE_VALUE)
			CloseHandle(noParametersFile);
		if (proxy)
			FreeLibrary(proxy);
		if (fixture)
			FreeLibrary(fixture);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
