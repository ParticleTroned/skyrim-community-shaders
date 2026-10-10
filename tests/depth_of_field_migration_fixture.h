#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

using uint = unsigned int;
using json = nlohmann::json;
struct float3
{
	float x, y, z;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(float3, x, y, z)
#include "Features/Bloom.h"
namespace SKSE
{
	namespace logger
	{
		template <class... Args>
		void info(const char*, Args&&...)
		{}
		template <class... Args>
		void warn(const char*, Args&&...)
		{}
	}
}
namespace Fixture
{
	std::filesystem::path root;
	std::string failedWrite;
}
namespace Util
{
	namespace PathHelpers
	{
		std::filesystem::path GetOverridesPath() { return Fixture::root / "Overrides"; }
		std::filesystem::path GetUserOverridesPath() { return GetOverridesPath() / "User"; }
		std::filesystem::path GetAppliedOverridesPath() { return Fixture::root / "tracking.json"; }
	}
	namespace FileHelpers
	{
		enum class JsonFileReadResult
		{
			Success,
			NotFound,
			Error
		};
		JsonFileReadResult ReadJsonFile(const std::filesystem::path&, json&, std::string&);
		bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& data, std::string& error)
		{
			if (path.filename().string() == Fixture::failedWrite) {
				error = "Injected write failure";
				return false;
			}
			std::ofstream output(path);
			output << data;
			return output.good();
		}
	}
}
