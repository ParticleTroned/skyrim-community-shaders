#pragma once

#include <cstdint>
#include <d3dcompiler.h>
#include <filesystem>
#include <memory>

namespace Util::ShaderInclude
{
	struct File
	{
		std::unique_ptr<char[]> data;
		UINT size = 0;
	};

	struct ReadError
	{
		const char* operation = "";
		DWORD code = ERROR_SUCCESS;
		uint64_t expectedBytes = 0;
		uint64_t readBytes = 0;
	};

	/** Reads a complete UINT-sized include without using the CRT file tables. */
	bool Read(const std::filesystem::path& path, File& contents, ReadError& error) noexcept;
	/** Emits bounded, deduplicated diagnostics without throwing into the compiler. */
	void Report(const std::filesystem::path& source, const std::filesystem::path& attemptedPath, const ReadError& error) noexcept;
}

namespace Util
{
	/** Resolves runtime includes from the shader root, independently of the including file. */
	struct CustomInclude : public ID3DInclude
	{
		explicit CustomInclude(const std::filesystem::path& root = L"Data\\Shaders", const std::filesystem::path& source = {}) :
			includeRoot(root), sourcePath(source) {}

		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR filename, LPCVOID parent, LPCVOID* data, UINT* size) noexcept override;
		HRESULT Close(LPCVOID data) noexcept override;

	private:
		const std::filesystem::path includeRoot;
		const std::filesystem::path sourcePath;
	};
}
