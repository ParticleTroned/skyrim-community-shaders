#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <string_view>
#	include <cstddef>
#	include <cstdint>

namespace CSX::Diagnostics::Stutters
{
	inline constexpr std::size_t kAssetPathBytes = 512;
	enum class AssetKind : std::uint8_t
	{
		None,
		Texture,
		Mesh
	};
	/** Copied loader evidence; unknown values remain distinct from zero or failure. */
	struct AssetRecord
	{
		AssetKind kind = AssetKind::None;
		std::string_view path;
		std::uint64_t operationId = 0, bytes = 0;
		std::int64_t resultCode = 0;
		std::uint32_t width = 0, height = 0, mipLevels = 0, format = 0;
		bool pathTruncated = false, resultKnown = false, resultCodeKnown = false, success = false, bytesKnown = false, dimensionsKnown = false, cacheKnown = false, cacheHit = false;
	};
}
#endif
