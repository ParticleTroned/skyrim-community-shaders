#include "PathValidation.h"

#include <cwchar>

namespace Util
{
	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath)
	{
		std::error_code error;
		const auto canonicalBase = std::filesystem::canonical(basePath, error);
		if (error)
			return false;

		const auto canonicalTest = std::filesystem::weakly_canonical(testPath, error);
		if (error)
			return false;

		auto testPart = canonicalTest.begin();
		for (auto basePart = canonicalBase.begin(); basePart != canonicalBase.end(); ++basePart, ++testPart) {
			if (testPart == canonicalTest.end() || _wcsicmp(basePart->c_str(), testPart->c_str()) != 0)
				return false;
		}
		return true;
	}
}
