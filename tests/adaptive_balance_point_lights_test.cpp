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
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

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
// The extracted feature owns the same aligned payload as production.
#pragma warning(push)
#pragma warning(disable: 4324)
struct AdaptiveBrightness
{
#include "adaptive_balance_point_light_members.h"
	void SetupResources();
	void UpdateVanillaPointLightData(RE::BSRenderPass*, uint32_t, uint32_t);
};
#pragma warning(pop)
template <typename T>
constexpr size_t ConstantBufferDesc()
{
	return sizeof(T);
}
struct ConstantBuffer
{
	static inline bool failNextCreation = false;
	ConstantBuffer() = default;
	ConstantBuffer(size_t size, const char* name)
	{
		assert(size == 32 && std::strcmp(name, "AdaptiveBalance::VanillaPointLightData") == 0);
		if (std::exchange(failNextCreation, false))
			throw std::runtime_error("injected creation failure");
	}
	AdaptiveBrightness::VanillaPointLightData uploaded{};
	ID3D11Buffer resource;
	unsigned attempts = 0, updates = 0;
	bool failNextUpdate = false;
	void Update(const AdaptiveBrightness::VanillaPointLightData& data)
	{
		++attempts;
		if (std::exchange(failNextUpdate, false)) {
			// A failed upload cannot promise that the previous GPU contents survived.
			uploaded = {};
			throw std::runtime_error("injected upload failure");
		}
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

struct PointLightFixture
{
	ConstantBuffer buffer;
	AdaptiveBrightness balance;
	RE::BSLight light;
	std::array<RE::BSLight*, 2> pointers{ nullptr, &light };
	RE::BSRenderPass pass{ pointers.data(), static_cast<uint32_t>(pointers.size()) };
	PointLightFixture()
	{
		balance.vanillaPointLightCB = &buffer;
		globals::features::inverseSquareLighting.enabled = true;
	}
	void Draw(uint32_t slot = AdaptiveBrightness::kLightingPointLightCBRegister)
	{
		const auto bindings = globals::d3d::contextValue.bindings;
		balance.UpdateVanillaPointLightData(&pass, 1, slot);
		assert(globals::d3d::contextValue.bindings == bindings + 1);
		assert(globals::d3d::contextValue.lastRegister == slot);
		assert(globals::d3d::contextValue.lastBuffer == balance.vanillaPointLightCB->CB());
	}
};

void CheckUploadReuse()
{
	PointLightFixture fixture;
	fixture.light.flags = 0;
	fixture.Draw();
	assert(fixture.buffer.updates == 1 && fixture.balance.vanillaPointLightUploadValid);
	fixture.Draw();
	assert(fixture.buffer.updates == 1);

	const auto updates = fixture.buffer.updates;
	const auto bindings = globals::d3d::contextValue.bindings;
	const auto reads = fixture.light.light.reads;
	for (auto flags : { 1u << 13, 1u << 13, 1u << 14, 1u << 14, 1u << 13 }) {
		fixture.light.flags = flags;
		fixture.Draw();
		assert(fixture.buffer.uploaded.pointLightFlags[0] == flags);
	}
	assert(fixture.buffer.updates == updates + 3 && globals::d3d::contextValue.bindings == bindings + 5);
	assert(fixture.light.light.reads == reads + 5);

	const auto beforeWater = fixture.buffer.updates;
	fixture.Draw(AdaptiveBrightness::kWaterPointLightCBRegister);
	assert(fixture.buffer.updates == beforeWater);
	fixture.light.flags = 1u << 14;
	fixture.Draw(AdaptiveBrightness::kWaterPointLightCBRegister);
	fixture.light.flags = 1u << 13;
	fixture.Draw();
	assert(fixture.buffer.updates == beforeWater + 2);

	fixture.light.flags |= PointLightFlags::ToMask(PointLightFlags::Flags::Linear);
	fixture.Draw();
	const auto withLinear = fixture.buffer.updates;
	globals::features::inverseSquareLighting.enabled = false;
	fixture.Draw();
	assert(fixture.buffer.updates == withLinear + 1 && fixture.buffer.uploaded.pointLightFlags[0] == (1u << 13));
	globals::features::inverseSquareLighting.enabled = true;
	fixture.Draw();
	assert(fixture.buffer.updates == withLinear + 2 && fixture.buffer.uploaded.pointLightFlags[0] == fixture.light.flags);
}

void CheckResourceLifetimeAndFailure()
{
	PointLightFixture fixture;
	fixture.Draw();
	fixture.balance.SetupResources();
	std::unique_ptr<ConstantBuffer> replacement(fixture.balance.vanillaPointLightCB);
	assert(!fixture.balance.vanillaPointLightUploadValid);
	fixture.Draw();
	assert(replacement->updates == 1);
	ConstantBuffer::failNextCreation = true;
	bool failed = false;
	try {
		fixture.balance.SetupResources();
	} catch (const std::runtime_error&) {
		failed = true;
	}
	assert(failed && !fixture.balance.vanillaPointLightUploadValid && fixture.balance.vanillaPointLightCB == replacement.get());
	fixture.Draw();
	assert(replacement->updates == 2);

	const auto originalFlags = fixture.light.flags;
	for (bool retryPrevious : { true, false }) {
		fixture.light.flags = 1u << 14;
		replacement->failNextUpdate = true;
		const auto attempts = replacement->attempts, updates = replacement->updates;
		const auto bindings = globals::d3d::contextValue.bindings;
		failed = false;
		try {
			fixture.Draw();
		} catch (const std::runtime_error&) {
			failed = true;
		}
		assert(failed && !fixture.balance.vanillaPointLightUploadValid);
		assert(replacement->attempts == attempts + 1 && replacement->updates == updates);
		assert(globals::d3d::contextValue.bindings == bindings);
		assert(fixture.balance.lastUploadedVanillaPointLightData.pointLightFlags[0] == originalFlags);
		if (retryPrevious)
			fixture.light.flags = originalFlags;
		fixture.Draw();
		assert(replacement->attempts == attempts + 2 && replacement->updates == updates + 1);
		assert(fixture.balance.vanillaPointLightUploadValid && replacement->uploaded.pointLightFlags[0] == fixture.light.flags);
	}
}

void CheckEveryLightEntry()
{
	ConstantBuffer buffer;
	AdaptiveBrightness balance;
	balance.vanillaPointLightCB = &buffer;
	globals::features::inverseSquareLighting.enabled = true;
	std::array<RE::BSLight, 8> lights;
	std::array<RE::BSLight*, 8> pointers;
	for (size_t index = 0; index < lights.size(); ++index) {
		lights[index].flags = 0;
		pointers[index] = &lights[index];
	}
	RE::BSRenderPass pass{ pointers.data(), static_cast<uint32_t>(pointers.size()) };
	balance.UpdateVanillaPointLightData(&pass, kMaxVanillaPointLights, AdaptiveBrightness::kLightingPointLightCBRegister);
	assert(buffer.updates == 1);
	for (uint32_t index = 0; index < kMaxVanillaPointLights; ++index) {
		for (auto flags : { 1u << 11, 1u << 13, 1u << 14, 0u }) {
			lights[index + 1].flags = flags;
			const auto updates = buffer.updates;
			const auto bindings = globals::d3d::contextValue.bindings;
			for (auto slot : { AdaptiveBrightness::kLightingPointLightCBRegister, AdaptiveBrightness::kWaterPointLightCBRegister }) {
				balance.UpdateVanillaPointLightData(&pass, kMaxVanillaPointLights, slot);
				assert(buffer.updates == updates + 1);
				assert(globals::d3d::contextValue.lastRegister == slot && globals::d3d::contextValue.lastBuffer == buffer.CB());
				for (uint32_t entry = 0; entry < std::size(buffer.uploaded.pointLightFlags); ++entry)
					assert(buffer.uploaded.pointLightFlags[entry] == (entry == index ? flags : 0));
			}
			assert(globals::d3d::contextValue.bindings == bindings + 2);
		}
	}
}

void CheckEarlyReturns()
{
	PointLightFixture fixture;
	const auto bindings = globals::d3d::contextValue.bindings;
	for (bool warm : { false, true }) {
		if (warm)
			fixture.Draw();
		fixture.balance.UpdateVanillaPointLightData(nullptr, 1, 3);
		fixture.pass.sceneLights = nullptr;
		fixture.balance.UpdateVanillaPointLightData(&fixture.pass, 1, 3);
		fixture.pass.sceneLights = fixture.pointers.data();
		globals::d3d::context = nullptr;
		fixture.balance.UpdateVanillaPointLightData(&fixture.pass, 1, 3);
		globals::d3d::context = &globals::d3d::contextValue;
		fixture.balance.vanillaPointLightCB = nullptr;
		fixture.balance.UpdateVanillaPointLightData(&fixture.pass, 1, 3);
		fixture.balance.vanillaPointLightCB = &fixture.buffer;
		assert(fixture.buffer.attempts == static_cast<unsigned>(warm));
		assert(globals::d3d::contextValue.bindings == bindings + static_cast<unsigned>(warm));
		assert(fixture.balance.vanillaPointLightUploadValid == warm);
	}
}

void CheckRecoveryAndBinding()
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
			balance.UpdateVanillaPointLightData(&pass, 3, slot);
			requireNeutral();
			assert(buffer.updates == updates + 1 && globals::d3d::contextValue.bindings == bindings + 2);
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
}

int main()
{
	CheckUploadReuse();
	CheckResourceLifetimeAndFailure();
	CheckEveryLightEntry();
	CheckEarlyReturns();
	CheckRecoveryAndBinding();
	std::cout << "Adaptive Balance point-light upload, recovery and binding checks passed.\n";
}
