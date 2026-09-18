#pragma once

#include "Utils/ResourceName.h"
#include <SKSE/Impl/PCH.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace logger = spdlog;
namespace Util
{
	std::optional<REL::Version> GetDllVersion(const std::wstring&);
	std::string GetFormattedVersion(const REL::Version&);
	namespace PathHelpers
	{
		std::filesystem::path GetDataPath();
	}
}
