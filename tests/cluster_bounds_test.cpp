#include "Features/LightLimitFix/ClusterBoundsCache.h"

#include <bit>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
}

int main()
{
	using namespace LightLimitFixDetail;
	try {
		ClusterBoundsCache cache;
		ClusterBoundsKey key{};
		key.nearPlane = std::bit_cast<std::uint32_t>(1.0f);
		key.farPlane = std::bit_cast<std::uint32_t>(10000.0f);
		key.farDepth = std::bit_cast<std::uint32_t>(1.0f);
		key.eyeCount = 2;
		key.dimensions = { 17, 19, 32 };
		Require(!cache.Matches(key), "An omitted build published a key");
		cache.Publish(key);
		Require(cache.Matches(key), "Exact repeat did not reuse");
		auto changed = key;
		for (auto member : { &ClusterBoundsKey::nearPlane, &ClusterBoundsKey::farPlane,
				 &ClusterBoundsKey::farDepth, &ClusterBoundsKey::eyeCount }) {
			changed = key;
			changed.*member ^= 1;
			Require(!cache.Matches(changed), "Scalar bit change reused old bounds");
		}
		for (unsigned axis = 0; axis < 3; ++axis) {
			changed = key;
			++changed.dimensions[axis];
			Require(!cache.Matches(changed), "Changed rounded dimension reused old bounds");
		}
		for (unsigned eye = 0; eye < 2; ++eye) {
			for (unsigned element = 0; element < 16; ++element) {
				changed = key;
				changed.inverseProjection[eye][element] ^= 1;
				Require(!cache.Matches(changed), "Single projection/jitter bit change reused old bounds");
			}
		}
		changed = key;
		changed.inverseProjection[1][0] = std::bit_cast<std::uint32_t>(-0.0f);
		Require(!cache.Matches(changed), "Signed zero projection difference was ignored");
		Require(cache.Matches(key), "Unsubmitted candidate replaced the previous build");
		cache.Invalidate();
		Require(!cache.Matches(key), "Shader/resource reset retained stale bounds");
		cache.Publish(changed);
		Require(cache.Matches(changed) && !cache.Matches(key), "Recreated bounds retained the old key");
		std::cout << "Exact cluster input, jitter, eye, reset and publication checks passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
