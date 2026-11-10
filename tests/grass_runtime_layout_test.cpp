#include <RE/Skyrim.h>

#include "Features/GrassOptimizations/GrassRuntime.h"

#include <array>
#include <bit>
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

	struct NativeSetupFixture
	{
		static inline bool vr = false;
		static inline float current = 10.f, previous = 0.f;
		static inline uint32_t calls = 0;

		static void Setup(RE::BSShader*, RE::BSRenderPass* pass, uint32_t flags)
		{
			auto& data = GrassRuntime::GetPropertyData(static_cast<RE::BSGrassShaderProperty*>(pass->shaderProperty), vr);
			previous = flags & 0x10 ? current : std::bit_cast<float>(data.windTimer);
			if (!(flags & 0x10))
				data.windTimer = std::bit_cast<uint32_t>(current);
			++calls;
			if (flags & 0x20)
				throw std::runtime_error("Synthetic native setup failure");
		}
	};

	void CheckWindPreservation(RE::BSGrassShaderProperty* property, bool vr)
	{
		NativeSetupFixture::vr = vr;
		RE::BSRenderPass pass{};
		pass.shaderProperty = property;
		auto& data = GrassRuntime::GetPropertyData(property, vr);
		const uint32_t saved = std::bit_cast<uint32_t>(9.f);
		for (uint32_t flags : { 0u, 0x10u, 0x20u }) {
			data.windTimer = saved;
			const auto calls = NativeSetupFixture::calls;
			bool threw = false;
			try {
				GrassRuntime::PrepareGeometryWithoutAdvancingWind(nullptr, &pass, flags, NativeSetupFixture::Setup, vr);
			} catch (const std::runtime_error&) {
				threw = true;
			}
			if (data.windTimer != saved || NativeSetupFixture::calls != calls + 1 || threw != bool(flags & 0x20) ||
				NativeSetupFixture::previous != (flags & 0x10 ? 10.f : 9.f))
				throw std::runtime_error("Speculative setup consumed native wind history or suppressed setup");
		}
		data.windTimer = saved;
		NativeSetupFixture::Setup(nullptr, &pass, 0);
		for (int repeat = 0; repeat < 2; ++repeat)
			GrassRuntime::PrepareGeometryWithoutAdvancingWind(nullptr, &pass, 0, NativeSetupFixture::Setup, vr);
		if (data.windTimer != std::bit_cast<uint32_t>(10.f))
			throw std::runtime_error("Speculative setup undid the real draw's native history update");
		for (float previous : { 0.f, 9.f })
			for (uint32_t flags : { 0u, 0x10u, 0x20u }) {
				bool threw = false;
				try {
					GrassRuntime::PrepareGeometryWithoutAdvancingWind(nullptr, &pass, flags, NativeSetupFixture::Setup, vr, std::bit_cast<uint32_t>(previous));
				} catch (const std::runtime_error&) {
					threw = true;
				}
				if (data.windTimer != std::bit_cast<uint32_t>(10.f) || threw != bool(flags & 0x20) ||
					NativeSetupFixture::previous != (flags & 0x10 ? 10.f : previous))
					throw std::runtime_error("Restoring the original draw lost its previous wind or advanced native history");
			}
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
		CheckWindPreservation(reinterpret_cast<RE::BSGrassShaderProperty*>(bytes.data()), vr);
		// The fixture borrows stack fades and must not invoke the game's allocator.
		std::memset(bytes.data() + nativeOffset, 0, sizeof(RE::BSTArray<float>));
		std::memset(&native->lightData.lights, 0, sizeof(RE::BSTArray<RE::BSLight*>));
	}

	void CheckGroupLayout()
	{
		using Runtime = RE::BSMultiStreamInstanceTriShape::MULTISTREAM_TRISHAPE_RUNTIME_DATA;
		alignas(Runtime) std::array<std::byte, sizeof(Runtime)> storage{};
		// Placement storage avoids invoking the game's allocator for the borrowed array.
		auto& native = *::new (storage.data()) Runtime{};
		std::array<RE::BSMultiStreamInstanceTriShape::InstanceGroup*, 2> groups{};
		BorrowArray(native.instanceGroups, groups);
		native.activeGroupCount = 1;
		const auto captured = GrassRuntime::CaptureGroupLayout(native);
		const auto matches = [&]() { return captured == GrassRuntime::CaptureGroupLayout(native); };
		// Cached-file loading need not maintain the generated-grass instance total.
		for (uint32_t total : { 0u, 64u, 4096u }) {
			native.instanceCount = total;
			if (!matches())
				throw std::runtime_error("Generated-grass totals invalidated cached group layout");
		}
		++native.activeGroupCount;
		if (matches())
			throw std::runtime_error("Attachment into a vacant slot retained a stale snapshot");
		--native.activeGroupCount;
		std::array<RE::BSMultiStreamInstanceTriShape::InstanceGroup*, 2> replacement{};
		BorrowArray(native.instanceGroups, replacement);
		if (matches())
			throw std::runtime_error("Reallocated group storage retained a stale snapshot");
		BorrowArray(native.instanceGroups, groups);
		auto* count = reinterpret_cast<std::byte*>(&native.instanceGroups) + 0x10;
		const uint32_t one = 1;
		std::memcpy(count, &one, sizeof(one));
		if (matches())
			throw std::runtime_error("Changed native slot count retained a stale snapshot");
		BorrowArray(native.instanceGroups, groups);
		if (!matches())
			throw std::runtime_error("Restored group layout did not match");
	}

	void CheckInstanceDataBounds()
	{
		std::array<std::byte, 32> data{};
		RE::BSGraphics::VertexBuffer buffer{};
		for (uint64_t upper : { 0ull, 0xdeadbeef00000000ull })
			for (uint32_t bytes : { 0u, 31u, 32u, 33u }) {
				buffer.byteWidth = upper | bytes;
				for (bool present : { false, true }) {
					buffer.m_data = present ? data.data() : nullptr;
					if (GrassRuntime::HasInstanceData(buffer, data.size()) != (present && bytes >= data.size()))
						throw std::runtime_error("Native header padding authorized an invalid CPU instance read");
				}
			}
	}

	void CheckInstanceLayout()
	{
		RE::BSGraphics::VertexBuffer buffer{};
		for (uint64_t padding : { 0ull, 0xdeadbeef00000000ull })
			for (uint32_t instances : { 0u, 1u, 2u, 64u })
				for (uint32_t stride : { 0u, 16u, 24u, 32u, 64u }) {
					buffer.byteWidth = padding | (instances * stride);
					if (GrassRuntime::MatchesInstanceBytes(buffer, uint64_t(instances) * 32) != (instances && stride == 32))
						throw std::runtime_error("Invalid native grass record width was admitted");
				}
		buffer.byteWidth = 0xdeadbeef00000020ull;
		for (uint64_t required : { 0ull, 31ull, 33ull, 0x100000020ull, UINT64_MAX })
			if (GrassRuntime::MatchesInstanceBytes(buffer, required))
				throw std::runtime_error("Native width padding or overflow authorized an invalid instance range");
	}
}

int main()
{
	try {
		CheckLayout(false, 0x160);
		CheckLayout(true, 0x178);
		CheckGroupLayout();
		CheckInstanceDataBounds();
		CheckInstanceLayout();
		std::cout << "SE/AE and VR grass layouts, native wind preservation, animated cache reuse and property invalidation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
