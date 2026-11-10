#include "Features/Upscaling/NeuralRendering/DirectProvider.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <latch>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template <class T>
	class FixturePatch
	{
	public:
		FixturePatch(T& target, T replacement) : target_(target), original_(target)
		{
			Require(Write(replacement), "Could not mutate synthetic fixture metadata");
		}
		~FixturePatch()
		{
			if (!Write(original_))
				std::terminate();
		}
		FixturePatch(const FixturePatch&) = delete;
		FixturePatch& operator=(const FixturePatch&) = delete;

	private:
		bool Write(const T& value)
		{
			DWORD protection = 0;
			if (!VirtualProtect(&target_, sizeof(T), PAGE_READWRITE, &protection))
				return false;
			std::memcpy(&target_, &value, sizeof(T));
			DWORD ignored = 0;
			return VirtualProtect(&target_, sizeof(T), protection, &ignored) != FALSE;
		}
		T& target_;
		T original_;
	};
}

void TestDirectProviderContract(const std::filesystem::path& path, HANDLE file, HMODULE fixture, ID3D12Device* device)
{
	const auto reset = reinterpret_cast<void (*)(unsigned)>(GetProcAddress(fixture, "FixtureSetFailure"));
	const auto calls = reinterpret_cast<unsigned (*)()>(GetProcAddress(fixture, "FixtureCalls"));
	const auto metadataCalls = reinterpret_cast<unsigned (*)()>(GetProcAddress(fixture, "FixtureMetadataCalls"));
	Require(reset && calls && metadataCalls, "Fixture contract controls are missing");
	auto* base = reinterpret_cast<std::byte*>(fixture);
	auto& dos = *reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	auto& nt = *reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos.e_lfanew);
	auto& exports = *reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	const auto exportSlot = [&](const char* name) -> DWORD& {
		const auto* names = reinterpret_cast<const DWORD*>(base + exports.AddressOfNames);
		const auto* ordinals = reinterpret_cast<const WORD*>(base + exports.AddressOfNameOrdinals);
		auto* functions = reinterpret_cast<DWORD*>(base + exports.AddressOfFunctions);
		for (DWORD i = 0; i < exports.NumberOfNames; ++i)
			if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), name) == 0)
				return functions[ordinals[i]];
		throw std::runtime_error("Synthetic export is missing");
	};
	const auto rejects = [&](auto& field, auto replacement, std::string_view reason) {
		reset(0);
		NeuralRendering::DirectProvider provider;
		std::string detail;
		NVSDK_NGX_Result result;
		{
			FixturePatch patch(field, replacement);
			result = provider.Initialize(path, file, device, path.parent_path(), detail);
		}
		Require(NVSDK_NGX_FAILED(result) && detail.find(reason) != std::string::npos, detail.c_str());
		Require(calls() == 0 && metadataCalls() == 0 && !provider.Active() && !provider.InitializationAttempted(), "Incompatible metadata reached a native entry point");
		Require(!provider.scratchSize && !provider.create && !provider.evaluate && !provider.release, "Failed binding published a feature dispatch pointer");
		Require(provider.Unload(), "Rejected provider could not release its module and allocator");
	};

	for (const auto* name : { "NVSDK_NGX_GetAPIVersion", "NVSDK_NGX_GetApplicationId", "NVSDK_NGX_D3D12_Init_Ext",
			 "NVSDK_NGX_D3D12_PopulateParameters_Impl", "NVSDK_NGX_D3D12_GetScratchBufferSize", "NVSDK_NGX_D3D12_CreateFeature",
			 "NVSDK_NGX_D3D12_EvaluateFeature", "NVSDK_NGX_D3D12_ReleaseFeature", "NVSDK_NGX_D3D12_Shutdown1" }) {
		rejects(exportSlot(name), DWORD{ 0 }, name);
		rejects(exportSlot(name), exportSlot("FixtureDataExport"), "executable code");
		rejects(exportSlot(name), exportSlot("FixtureForwarded"), "owning module");
	}
	for (const auto* name : { "NVSDK_NGX_D3D12_AllocateParameters", "NVSDK_NGX_D3D12_DestroyParameters" }) {
		rejects(exportSlot(name), exportSlot("FixtureDataExport"), "executable code");
		rejects(exportSlot(name), exportSlot("FixtureForwarded"), "owning module");
	}
	rejects(dos.e_lfanew, LONG{ -1 }, "AMD64 PE32+");
	rejects(dos.e_lfanew, std::numeric_limits<LONG>::max() - 3, "AMD64 PE32+");
	rejects(nt.FileHeader.Machine, WORD{ IMAGE_FILE_MACHINE_I386 }, "AMD64 PE32+");
	rejects(nt.FileHeader.SizeOfOptionalHeader, WORD{ 0 }, "AMD64 PE32+");
	rejects(nt.OptionalHeader.Magic, WORD{ IMAGE_NT_OPTIONAL_HDR32_MAGIC }, "AMD64 PE32+");
	rejects(nt.OptionalHeader.NumberOfRvaAndSizes, DWORD{ 0 }, "AMD64 PE32+");
	auto& imports = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	rejects(imports.Size, std::numeric_limits<DWORD>::max(), "compatibility import");
	rejects(imports.VirtualAddress, nt.OptionalHeader.SizeOfImage - 1, "compatibility import");
	bool foundImport = false;
	for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); descriptor->Name; ++descriptor) {
		if (!descriptor->OriginalFirstThunk)
			continue;
		for (auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk); thunk->u1.AddressOfData; ++thunk) {
			if (IMAGE_SNAP_BY_ORDINAL64(thunk->u1.Ordinal))
				continue;
			const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
			if (std::strcmp(name->Name, "GetModuleFileNameW") != 0)
				continue;
			foundImport = true;
			auto* functions = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
			const auto index = thunk - reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
			rejects(functions[index].u1.Function, reinterpret_cast<ULONGLONG>(base + exportSlot("FixtureDataExport")), "compatibility import");
			rejects(functions[index].u1.Function, ULONGLONG{ 0 }, "compatibility import");
			rejects(descriptor->FirstThunk, descriptor->FirstThunk + 1, "compatibility import");
			rejects(descriptor->OriginalFirstThunk, nt.OptionalHeader.SizeOfImage - 1, "compatibility import");
			rejects(thunk->u1.AddressOfData, ULONGLONG{ nt.OptionalHeader.SizeOfImage - sizeof(WORD) }, "compatibility import");
			rejects(thunk->u1.AddressOfData, ULONGLONG{ 0x100000000 }, "compatibility import");
			break;
		}
	}
	Require(foundImport, "Synthetic fixture has no caller import to validate");
	reset(0);
}

void TestParameterCoreDiscovery(const std::filesystem::path& path, HANDLE file, const std::filesystem::path& corePath, ID3D12Device* device)
{
	std::atomic<bool> loadFailed = false;
	std::latch started{ 1 };
	std::jthread loader([&](std::stop_token stop) {
		bool first = true;
		do {
			const auto module = LoadLibraryW(corePath.c_str());
			if (!module || !FreeLibrary(module))
				loadFailed = true;
			if (first) {
				started.count_down();
				first = false;
			}
		} while (!stop.stop_requested() && !loadFailed.load());
	});
	started.wait();
	for (unsigned attempt = 0; attempt < 64; ++attempt) {
		NeuralRendering::DirectProvider provider;
		std::string detail;
		const auto result = provider.Initialize(path, file, device, path.parent_path(), detail);
		Require(NVSDK_NGX_FAILED(result) && !provider.Active() && provider.Unload(), "Concurrent untrusted core discovery initialized or retained the provider");
	}
	loader.request_stop();
	loader.join();
	Require(!loadFailed, "Concurrent fixture load or unload failed");
	Require(!GetModuleHandleW(corePath.filename().c_str()), "Concurrent allocator discovery retained a module reference");
}
