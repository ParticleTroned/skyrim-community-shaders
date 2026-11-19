bool ReadFileAt(HANDLE file, std::uint32_t offset, void* destination, DWORD bytes) noexcept
{
	LARGE_INTEGER position{};
	position.QuadPart = offset;
	DWORD read{};
	return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, destination, bytes, &read, nullptr) && read == bytes;
}

struct CodeRange
{
	std::uint32_t rva{}, bytes{};
	std::array<std::byte, 512> expected{};
};

bool ReadExpectedCode(HANDLE file, const IMAGE_NT_HEADERS64& image,
	std::span<const IMAGE_SECTION_HEADER> sections, CodeRange& code) noexcept
{
	const auto rva = code.rva, bytes = code.bytes;
	if (!bytes || bytes > code.expected.size() || rva >= image.OptionalHeader.SizeOfImage || bytes > image.OptionalHeader.SizeOfImage - rva)
		return false;
	for (const auto& section : sections) {
		if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || rva < section.VirtualAddress)
			continue;
		const auto offset = rva - section.VirtualAddress;
		if (offset > section.SizeOfRawData || bytes > section.SizeOfRawData - offset || offset > UINT32_MAX - section.PointerToRawData)
			continue;
		return ReadFileAt(file, section.PointerToRawData + offset, code.expected.data(), bytes);
	}
	return false;
}

bool MatchesCode(std::uintptr_t base, std::span<const CodeRange> ranges) noexcept
{
	std::array<std::byte, 512> live{};
	for (const auto& code : ranges) {
		if (!code.bytes || code.bytes > live.size() || code.rva > UINTPTR_MAX - base ||
			!Copy(base + code.rva, live.data(), code.bytes) || std::memcmp(code.expected.data(), live.data(), code.bytes) != 0)
			return false;
	}
	return !ranges.empty();
}

long Install()
{
	auto& state = Get();
	HMODULE module{};
	if (!GetModuleHandleExW(0, L"nvwgf2umx.dll", &module))
		return GetLastError();
	// Keep the image stable while hashing; unknown binaries retain no permanent reference.
	const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> reference(module, &FreeLibrary);
	std::array<wchar_t, 32768> path{};
	const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
	if (!length || length >= path.size())
		return ERROR_INVALID_NAME;
	winrt::file_handle file(CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return GetLastError();
	const auto profile = FindDriverProfile(CryptoHash::ToHex(CryptoHash::Sha256File(file.get())));
	if (!profile)
		return ERROR_REVISION_MISMATCH;
	const auto base = reinterpret_cast<std::uintptr_t>(module);
	IMAGE_DOS_HEADER dos{};
	IMAGE_NT_HEADERS64 image{};
	if (!ReadFileAt(file.get(), 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 1024 * 1024 ||
		!ReadFileAt(file.get(), static_cast<std::uint32_t>(dos.e_lfanew), &image, sizeof(image)) || image.Signature != IMAGE_NT_SIGNATURE ||
		image.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || image.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		!image.OptionalHeader.SizeOfImage || base > UINTPTR_MAX - image.OptionalHeader.SizeOfImage ||
		image.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) || !image.FileHeader.NumberOfSections || image.FileHeader.NumberOfSections > 96)
		return ERROR_BAD_EXE_FORMAT;
	std::array<IMAGE_SECTION_HEADER, 96> headers{};
	const auto headerBytes = static_cast<DWORD>(image.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));
	if (!ReadFileAt(file.get(), static_cast<std::uint32_t>(dos.e_lfanew + sizeof(image)), headers.data(), headerBytes))
		return ERROR_BAD_EXE_FORMAT;
	const auto sections = std::span(headers.data(), image.FileHeader.NumberOfSections);
	std::array code{ CodeRange{ profile->firstRva, 0x18a }, CodeRange{ profile->secondRva, 0x1d0 }, CodeRange{ profile->emitterRva, 0x100 } };
	for (auto& range : code)
		if (!ReadExpectedCode(file.get(), image, sections, range))
			return ERROR_INVALID_DATA;
	// Once threads are suspended, revalidation must neither allocate nor perform file I/O.
	const auto verify = [&] { return MatchesCode(base, code); };
	if (!verify())
		return ERROR_INVALID_DATA;
	HMODULE pinned{};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<const wchar_t*>(base), &pinned) || pinned != module)
		return ERROR_MOD_NOT_FOUND;

	// Enlist existing threads before changing code they may already be executing.
	const auto processId = GetCurrentProcessId(), currentThreadId = GetCurrentThreadId();
	std::vector<winrt::handle> threads;
	const auto raw = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (raw == INVALID_HANDLE_VALUE)
		return GetLastError();
	winrt::handle snapshot(raw);
	THREADENTRY32 entry{ sizeof(entry) };
	if (!Thread32First(snapshot.get(), &entry))
		return GetLastError();
	do {
		if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == currentThreadId)
			continue;
		if (threads.size() == 4096)
			return ERROR_INSUFFICIENT_BUFFER;
		winrt::handle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID));
		if (!thread) {
			const auto error = GetLastError();
			if (error == ERROR_INVALID_PARAMETER)
				continue;
			return error;
		}
		const auto owner = GetProcessIdOfThread(thread.get());
		if (!owner)
			return GetLastError();
		// A thread ID can be recycled between enumeration and opening its handle.
		if (owner != processId)
			continue;
		threads.push_back(std::move(thread));
	} while (Thread32Next(snapshot.get(), &entry));
	if (GetLastError() != ERROR_NO_MORE_FILES)
		return GetLastError();
	state.profile = profile;
	state.first = reinterpret_cast<NativeGrowth>(base + profile->firstRva);
	state.second = reinterpret_cast<NativeGrowth>(base + profile->secondRva);
	struct Operations
	{
		const std::vector<winrt::handle>& threads;
		const decltype(verify)& verify;
		long Begin() const { return DetourTransactionBegin(); }
		long UpdateThread() const
		{
			for (const auto& thread : threads) {
				if (const auto result = DetourUpdateThread(thread.get()); result != NO_ERROR)
					return result;
			}
			return DetourUpdateThread(GetCurrentThread());
		}
		long Attach(void** original, void* hook) const { return verify() ? DetourAttach(original, hook) : ERROR_INVALID_DATA; }
		long Abort() const { return DetourTransactionAbort(); }
		long Commit() const { return DetourTransactionCommit(); }
	};
	return detail::AttachDetourTransaction(reinterpret_cast<void**>(&state.first), reinterpret_cast<void*>(&Grow), Operations{ threads, verify }).error;
}
