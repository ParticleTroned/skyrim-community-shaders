#ifdef NDEBUG
#	undef NDEBUG
#endif
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace RE
{
	struct NiLight
	{};
	struct LightHandle
	{
		NiLight value;
		bool fault = false;
		unsigned reads = 0;
		NiLight* get()
		{
			++reads;
			if (fault)
				RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
			return &value;
		}
	};
	struct BSLight
	{
		LightHandle light;
		uint32_t flags = (1u << 11) | (1u << 13);
		bool rttiFault = false;
	};
	struct BSRenderPass
	{
		BSLight** sceneLights;
		uint32_t numLights;
	};
}
namespace PointLightFlags
{
	enum class Flags : uint32_t
	{
		Linear = 1u << 11
	};
	constexpr uint32_t ToMask(Flags value) { return static_cast<uint32_t>(value); }
	uint32_t GetVanillaPointLightFlags(RE::BSLight* light, RE::NiLight*)
	{
		if (light->rttiFault)
			throw std::runtime_error("injected RTTI failure");
		return light->flags;
	}
}
namespace logger
{
	unsigned warnings = 0;
	void warn(const char*) { ++warnings; }
}
struct ID3D11Buffer
{};
struct ConstantBuffer;
#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(alignof(Type) == 16)
struct AdaptiveBrightness
{
#include "adaptive_balance_point_light_members.h"
	void UpdateVanillaPointLightData(RE::BSRenderPass*, uint32_t, uint32_t);
};
struct ConstantBuffer
{
	AdaptiveBrightness::VanillaPointLightData uploaded{};
	ID3D11Buffer resource;
	unsigned updates = 0;
	void Update(const AdaptiveBrightness::VanillaPointLightData& data)
	{
		uploaded = data;
		++updates;
	}
	ID3D11Buffer* CB() { return &resource; }
};
namespace globals
{
	namespace d3d
	{
		struct Context
		{
			unsigned bindings = 0;
			uint32_t lastRegister = 0;
			ID3D11Buffer* lastBuffer = nullptr;
			void PSSetConstantBuffers(uint32_t slot, uint32_t count, ID3D11Buffer** buffers)
			{
				assert(count == 1);
				++bindings;
				lastRegister = slot;
				lastBuffer = *buffers;
			}
		} contextValue;
		auto* context = &contextValue;
	}
	namespace features
	{
		struct InverseSquareLighting
		{
			bool enabled = true;
			bool IsEnabled() const { return enabled; }
		} inverseSquareLighting;
	}
}

#include "adaptive_balance_point_lights_under_test.h"

int main()
{
	ConstantBuffer buffer;
	AdaptiveBrightness balance;
	balance.vanillaPointLightCB = &buffer;
	std::array<RE::BSLight, 10> lights;
	std::array<RE::BSLight*, 10> pointers;
	for (size_t i = 0; i < lights.size(); ++i)
		pointers[i] = &lights[i];
	RE::BSRenderPass pass{ pointers.data(), static_cast<uint32_t>(pointers.size()) };
	const auto resetReads = [&]() {
		for (auto& light : lights)
			light.light.reads = 0;
	};
	const auto requireNeutral = [&]() {
		for (auto flags : buffer.uploaded.pointLightFlags)
			assert(flags == 0);
	};
	for (auto slot : { AdaptiveBrightness::kLightingPointLightCBRegister, AdaptiveBrightness::kWaterPointLightCBRegister }) {
		for (bool linear : { false, true }) {
			globals::features::inverseSquareLighting.enabled = linear;
			resetReads();
			balance.UpdateVanillaPointLightData(&pass, UINT32_MAX, slot);
			for (uint32_t i = 0; i < kMaxVanillaPointLights; ++i)
				assert(buffer.uploaded.pointLightFlags[i] == ((1u << 13) | (linear ? 1u << 11 : 0)));
			assert(buffer.uploaded.pointLightFlags[kMaxVanillaPointLights] == 0);
			assert(lights[0].light.reads == 0 && lights[kMaxVanillaPointLights + 1].light.reads == 0);
			assert(globals::d3d::contextValue.lastRegister == slot && globals::d3d::contextValue.lastBuffer == buffer.CB());
		}
		for (bool rtti : { false, true }) {
			resetReads();
			lights[2].rttiFault = rtti;
			lights[2].light.fault = !rtti;
			const auto updates = buffer.updates;
			const auto bindings = globals::d3d::contextValue.bindings;
			balance.UpdateVanillaPointLightData(&pass, 3, slot);
			requireNeutral();
			assert(lights[1].light.reads == 1 && lights[2].light.reads == 1 && lights[3].light.reads == 0);
			assert(buffer.updates == updates + 1 && globals::d3d::contextValue.bindings == bindings + 1);
			assert(globals::d3d::contextValue.lastRegister == slot);
			lights[2].rttiFault = lights[2].light.fault = false;
			balance.UpdateVanillaPointLightData(&pass, 3, slot);
			assert(buffer.uploaded.pointLightFlags[0] != 0 && buffer.uploaded.pointLightFlags[2] != 0);
		}
		resetReads();
		pass.numLights = 2;
		balance.UpdateVanillaPointLightData(&pass, UINT32_MAX, slot);
		assert(lights[1].light.reads == 1 && lights[2].light.reads == 0 && buffer.uploaded.pointLightFlags[1] == 0);
		pointers[1] = nullptr;
		balance.UpdateVanillaPointLightData(&pass, 1, slot);
		requireNeutral();
		pointers[1] = &lights[1];
		pass.numLights = 0;
		balance.UpdateVanillaPointLightData(&pass, 3, slot);
		requireNeutral();
		pass.numLights = static_cast<uint32_t>(pointers.size());
	}
	assert(logger::warnings == 1);
	const auto updates = buffer.updates;
	balance.UpdateVanillaPointLightData(nullptr, 3, 3);
	pass.sceneLights = nullptr;
	balance.UpdateVanillaPointLightData(&pass, 3, 3);
	assert(buffer.updates == updates);
	std::cout << "Adaptive Balance point-light recovery and binding checks passed.\n";
}
