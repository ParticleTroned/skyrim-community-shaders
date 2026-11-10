#pragma once

#include <string>

namespace NeuralRendering
{
	/** NGX discovery accepts ordinary drive/UNC paths, not extended-length Windows paths. */
	inline std::wstring NormalizeNgxFeaturePath(std::wstring path)
	{
		if (!path.starts_with(L"\\\\?\\"))
			return path;
		if (path.size() >= 8 && (path[4] == L'U' || path[4] == L'u') &&
			(path[5] == L'N' || path[5] == L'n') && (path[6] == L'C' || path[6] == L'c') && path[7] == L'\\')
			return L"\\\\" + path.substr(8);
		if (path.size() >= 7 && ((path[4] >= L'A' && path[4] <= L'Z') || (path[4] >= L'a' && path[4] <= L'z')) &&
			path[5] == L':' && (path[6] == L'\\' || path[6] == L'/'))
			return path.substr(4);
		return {};
	}
}
