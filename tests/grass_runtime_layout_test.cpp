#include <RE/Skyrim.h>

#include "Features/GrassOptimizations/GrassRuntime.h"

#include <array>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace
{
	template <class T, std::size_t Count>
	void BorrowArray(RE::BSTArray<T>& destination, std::array<T, Count>& source)
	{
		const auto pointer = source.data();
		const auto count = static_cast<std::uint32_t>(Count);
		auto bytes = reinterpret_cast<std::byte*>(&destination);
		std::memcpy(bytes, &pointer, sizeof(pointer));
		std::memcpy(bytes + 0x08, &count, sizeof(count));
		std::memcpy(bytes + 0x10, &count, sizeof(count));
	}

	void CheckLayout(bool vr, std::size_t nativeOffset)
	{
		alignas(GrassRuntime::PropertyData) std::array<std::byte, 0x1E8> bytes{};
		auto* native = ::new (bytes.data() + nativeOffset) GrassRuntime::PropertyData{};
		std::array<float, 3> fades{ 0.25f, 0.5f, 1.0f };
		const std::uint32_t count = static_cast<std::uint32_t>(fades.size());
		BorrowArray(native->fadeAlphas, fades);
		native->wavePeriod = 220.0f;
		native->windTimer = 0x13579;
		native->lightData.activeLightMask = 0x24;
		const auto* property = reinterpret_cast<const RE::BSGrassShaderProperty*>(bytes.data());
		const auto& observed = GrassRuntime::GetPropertyData(property, vr);
		if (&observed != native || observed.fadeAlphas.size() != count || observed.fadeAlphas[1] != fades[1] ||
			observed.wavePeriod != 220.0f || observed.windTimer != 0x13579 || observed.lightData.activeLightMask != 0x24)
			throw std::runtime_error("Native grass tail was read at the wrong runtime offset");
		std::array<int, 3> lightIdentities{};
		std::array<RE::BSLight*, 3> lights{};
		for (std::size_t i = 0; i < lights.size(); ++i)
			lights[i] = reinterpret_cast<RE::BSLight*>(&lightIdentities[i]);
		BorrowArray(native->lightData.lights, lights);
		const auto cachedLights = lights;
		const auto cachedWavePeriod = observed.wavePeriod;
		const auto cachedLightMask = observed.lightData.activeLightMask;
		const auto matches = [&] {
			return GrassRuntime::MatchesPersistentProperty(observed, cachedWavePeriod, cachedLightMask, cachedLights);
		};
		for (std::uint32_t frame = 0; frame < 230; ++frame) {
			++native->windTimer;
			fades[1] = static_cast<float>(frame) / 230.0f;
			if (!matches())
				throw std::runtime_error("Animated wind or fade invalidated persistent grass property state");
		}
		native->wavePeriod += 1.0f;
		if (matches())
			throw std::runtime_error("Changed grass wave period retained an incompatible property snapshot");
		native->wavePeriod = cachedWavePeriod;
		native->lightData.activeLightMask ^= 1u;
		if (matches())
			throw std::runtime_error("Changed grass light mask retained an incompatible property snapshot");
		native->lightData.activeLightMask = cachedLightMask;
		std::swap(lights[0], lights[1]);
		if (matches())
			throw std::runtime_error("Changed grass light ordering retained an incompatible property snapshot");
		std::swap(lights[0], lights[1]);
		std::array<RE::BSLight*, 2> shorterLights{ lights[0], lights[1] };
		BorrowArray(native->lightData.lights, shorterLights);
		if (matches())
			throw std::runtime_error("Changed grass light count retained an incompatible property snapshot");
		BorrowArray(native->lightData.lights, lights);
		if (!matches())
			throw std::runtime_error("Restored grass property state failed to match");
		// The fixture borrows stack fades and must not invoke the game's allocator.
		std::memset(bytes.data() + nativeOffset, 0, sizeof(RE::BSTArray<float>));
		std::memset(&native->lightData.lights, 0, sizeof(RE::BSTArray<RE::BSLight*>));
	}
}

int main()
{
	try {
		CheckLayout(false, 0x160);
		CheckLayout(true, 0x178);
		std::cout << "SE/AE and VR grass layouts, animated cache reuse and property invalidation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
