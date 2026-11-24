#include "PathValidation.h"

#include <cwchar>

namespace Util
{
	namespace
	{
		template <class Canonical>
		std::filesystem::path ResolvePathWithMissingTail(const std::filesystem::path& path, std::error_code& error, Canonical&& canonical)
		{
			auto existing = path;
			std::filesystem::path tail;
			for (;;) {
				auto resolved = canonical(existing, error);
				if (!error)
					return tail.empty() ? resolved : (resolved / tail).lexically_normal();
				if (error != std::errc::no_such_file_or_directory)
					return {};
				const auto parent = existing.parent_path();
				if (parent == existing || parent.empty())
					return {};
				tail = tail.empty() ? existing.filename() : existing.filename() / tail;
				existing = parent;
			}
		}
	}

	bool IsPathWithinDirectory(const std::filesystem::path& basePath, const std::filesystem::path& testPath)
	{
		std::error_code error;
		const auto canonicalBase = std::filesystem::canonical(basePath, error);
		if (error)
			return false;

		const auto absoluteTest = std::filesystem::absolute(testPath, error);
		if (error)
			return false;
		// Resolve the longest existing virtual path before appending its missing tail.
		// Resolving prefixes first can switch to a different MO2 directory mapping.
		const auto canonicalTest = ResolvePathWithMissingTail(absoluteTest, error, [](const auto& path, auto& pathError) {
			return std::filesystem::canonical(path, pathError);
		});
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
