#include "Features/MeshBlendingPolicy.h"
#include "Utils/BoundedTextRead.h"

#include <sstream>
#include <string>

namespace
{
	bool TestCacheReusePolicy()
	{
		using CSX::MeshBlendingPolicy::CachedClassification;
		using CSX::MeshBlendingPolicy::CanReuseCacheHit;

		return CanReuseCacheHit(CachedClassification::kRejected, true, false) &&
		       CanReuseCacheHit(CachedClassification::kAllowedByRule, false, false) &&
		       !CanReuseCacheHit(CachedClassification::kAllowedByRule, true, true) &&
		       CanReuseCacheHit(CachedClassification::kAutomatic, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, false, false) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, true, true);
	}

	bool TestLandscapeSelectorPolicy()
	{
		using CSX::MeshBlendingPolicy::HasLandscapeSelector;
		using CSX::MeshBlendingPolicy::TrimAsciiSpaces;

		return !HasLandscapeSelector({}, {}, {}) &&
		       !HasLandscapeSelector("   ", " ", "  ") &&
		       HasLandscapeSelector(" 0x1~Plugin.esp ", {}, {}) &&
		       HasLandscapeSelector({}, " EditorID ", {}) &&
		       HasLandscapeSelector({}, {}, " textures/example.dds ") &&
		       TrimAsciiSpaces("  value  ") == "value" &&
		       TrimAsciiSpaces("   ").empty();
	}

	bool TestBoundedTextRead()
	{
		using Util::BoundedTextRead::Read;
		using Util::BoundedTextRead::Result;

		std::string output;
		std::istringstream exact("1234");
		if (Read(exact, 4u, output) != Result::Success || output != "1234")
			return false;

		std::istringstream oversized("12345");
		if (Read(oversized, 4u, output) != Result::LimitExceeded)
			return false;

		std::istringstream empty("");
		if (Read(empty, 0u, output) != Result::Success || !output.empty())
			return false;

		const std::string multiChunk(8192u, 'x');
		std::istringstream exactMultiChunk(multiChunk);
		if (Read(exactMultiChunk, multiChunk.size(), output) != Result::Success ||
			output != multiChunk)
			return false;

		std::istringstream broken("content");
		broken.setstate(std::ios::badbit);
		return Read(broken, 32u, output) == Result::ReadError;
	}
}

int main()
{
	return TestCacheReusePolicy() &&
	               TestLandscapeSelectorPolicy() &&
	               TestBoundedTextRead() ?
	           0 :
	           1;
}
