#pragma once

#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>

namespace Util
{
	/** Resolves runtime includes from the shader root, independently of the including file. */
	struct CustomInclude : public ID3DInclude
	{
		explicit CustomInclude(const std::filesystem::path& root = L"Data\\Shaders") : includeRoot(root) {}

		HRESULT Open([[maybe_unused]] D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, [[maybe_unused]] LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
		{
			const std::filesystem::path filePath = includeRoot / pFileName;

			std::ifstream file(filePath, std::ios::binary);
			if (!file.is_open()) {
				*ppData = NULL;
				*pBytes = 0;
				return E_FAIL;
			}

			file.seekg(0, std::ios::end);
			UINT size = static_cast<UINT>(file.tellg());
			file.seekg(0, std::ios::beg);

			char* data = new char[size];
			file.read(data, size);
			*ppData = data;
			*pBytes = size;
			return S_OK;
		}

		HRESULT Close(LPCVOID pData) override
		{
			if (pData)
				delete[] pData;
			return S_OK;
		}

	private:
		const std::filesystem::path includeRoot;
	};
}
