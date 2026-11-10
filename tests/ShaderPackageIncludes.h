#pragma once

#include <array>
#include <cstdint>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

/// Resolve shader includes from the installed Shaders root, as Util::CompileShader does.
class PackageIncludes : public ID3DInclude
{
public:
	explicit PackageIncludes(std::filesystem::path root, std::filesystem::path featureRoot = {}, std::filesystem::path secondFeatureRoot = {}) :
		roots_{ std::move(root), std::move(featureRoot), std::move(secondFeatureRoot) } {}
	HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID,
		LPCVOID* data, UINT* bytes) override
	{
		try {
			std::ifstream file;
			for (const auto& root : roots_) {
				if (root.empty())
					continue;
				file.clear();
				file.open(root / name, std::ios::binary | std::ios::ate);
				if (file)
					break;
			}
			if (!file)
				return E_FAIL;
			const auto length = file.tellg();
			if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<UINT>::max())
				return E_FAIL;
			auto contents = std::make_unique<char[]>(static_cast<std::size_t>(length) + 1u);
			file.seekg(0);
			file.read(contents.get(), length);
			if (!file)
				return E_FAIL;
			*bytes = static_cast<UINT>(length);
			*data = contents.release();
			return S_OK;
		} catch (...) {
			return E_FAIL;
		}
	}
	HRESULT __stdcall Close(LPCVOID data) override
	{
		delete[] static_cast<const char*>(data);
		return S_OK;
	}

private:
	std::array<std::filesystem::path, 3> roots_;
};
