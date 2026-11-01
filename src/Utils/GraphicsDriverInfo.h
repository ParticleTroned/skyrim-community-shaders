#pragma once

#include <dxgi.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Util
{
	struct GraphicsDriverVersion
	{
		std::string_view vendor;
		std::string version;
		std::string error;
	};

	/// Query the selected adapter's public release; unsupported vendors return an empty vendor.
	[[nodiscard]] GraphicsDriverVersion QueryGraphicsDriverVersion(const DXGI_ADAPTER_DESC& a_adapter, std::optional<std::uint32_t> a_windowsVersionLow);
}
