#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
	using uint = unsigned;
	using uint32_t = std::uint32_t;

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Strict-light upload assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	struct float3
	{
		float x, y, z;
	};
	struct uint128_t
	{
		std::array<std::uint32_t, 4> words{};
		uint128_t(std::uint32_t a_value = 0) : words{ a_value, 0, 0, 0 } {}
		bool operator==(const uint128_t&) const = default;
	};
	namespace PointLightFlags
	{
		enum class Flags : std::uint32_t
		{
		};
	}
	namespace stl
	{
		template <class T>
		struct enumeration
		{
			T value{};
			auto underlying() const { return std::to_underlying(value); }
		};
	}
	namespace RE
	{
		struct BSRenderPass
		{};
		struct ShaderManagerState
		{};
		struct Accumulator
		{};
	}
	struct ID3D11Buffer
	{};
	struct RecordingContext
	{
		unsigned bindings = 0;
		ID3D11Buffer* lastBuffer = nullptr;
		void PSSetConstantBuffers(uint a_slot, uint a_count, ID3D11Buffer* const* a_buffers)
		{
			Require(a_slot == 3 && a_count == 1 && a_buffers && *a_buffers);
			++bindings;
			lastBuffer = *a_buffers;
		}
	};
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	};
	struct RecordingState
	{
		uint32_t frameCount = 0;
	};
	namespace globals
	{
		RecordingState stateStorage;
		RecordingState* state = &stateStorage;
		ShaderCache shaderStorage;
		ShaderCache* shaderCache = &shaderStorage;
		namespace d3d
		{
			RecordingContext contextStorage;
			RecordingContext* context = &contextStorage;
		}
		namespace game
		{
			RE::ShaderManagerState manager;
			RE::ShaderManagerState* smState = &manager;
			RE::Accumulator accumulator;
			struct AccumulatorReference
			{
				RE::Accumulator* value = &accumulator;
				RE::Accumulator** get() { return &value; }
			} currentAccumulator;
		}
	}
	namespace Util
	{
#include "strict_light_frame_checker_under_test.h"

#include "strict_light_frame_read_under_test.h"
	}

	struct ConstantBuffer;
	struct LightLimitFix
	{
#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(alignof(Type) == 16)
#include "strict_light_data_under_test.h"

#include "strict_light_state_under_test.h"
#undef STATIC_ASSERT_ALIGNAS_16

		ConstantBuffer* strictLightDataCB = nullptr;
		Util::FrameChecker frameChecker;
		void BSLightingShader_SetupGeometry_After(RE::BSRenderPass*);
		void Retire();
		void Create();
	};
	static_assert(sizeof(LightLimitFix::StrictLightDataCB) == 1456);

	struct ConstantBuffer
	{
		static inline unsigned creations = 0, destructions = 0, creationAttempts = 0;
		static inline bool failNextConstruction = false;
		ConstantBuffer(std::size_t a_bytes, std::string_view a_name)
		{
			Require(a_bytes == sizeof(LightLimitFix::StrictLightDataCB) && a_name == "LightLimitFix::StrictLightData");
			++creationAttempts;
			if (std::exchange(failNextConstruction, false))
				throw std::bad_alloc();
			++creations;
		}
		~ConstantBuffer() { ++destructions; }
		unsigned attempts = 0, updates = 0;
		bool failNext = false;
		ID3D11Buffer buffer;
		LightLimitFix::StrictLightDataCB uploaded{};
		void Update(const LightLimitFix::StrictLightDataCB& a_data)
		{
			++attempts;
			if (std::exchange(failNext, false))
				throw std::runtime_error("Injected upload failure");
			uploaded = a_data;
			++updates;
		}
		ID3D11Buffer* CB() { return &buffer; }
	};

	template <class T>
	std::size_t ConstantBufferDesc()
	{
		return sizeof(T);
	}

	void LightLimitFix::Retire()
	{
#include "strict_light_retire_under_test.h"
	}

	void LightLimitFix::Create()
	{
#include "strict_light_create_under_test.h"
	}

#include "strict_light_comparison_under_test.h"

#include "strict_light_after_under_test.h"

	struct Fixture
	{
		LightLimitFix feature;
		ConstantBuffer& buffer;
		static ConstantBuffer& CreateBuffer(LightLimitFix& a_feature)
		{
			a_feature.Create();
			return *a_feature.strictLightDataCB;
		}
		Fixture() : buffer(CreateBuffer(feature))
		{
			globals::stateStorage = {};
			globals::shaderStorage.enabled = true;
			globals::d3d::contextStorage = {};
			globals::d3d::context = &globals::d3d::contextStorage;
			globals::game::smState = &globals::game::manager;
			globals::game::currentAccumulator.value = &globals::game::accumulator;
			feature.strictLightDataCB = &buffer;
		}
		~Fixture() { feature.Retire(); }
		void Draw() { feature.BSLightingShader_SetupGeometry_After(nullptr); }
	};

	LightLimitFix::LightData MakeLight(float a_seed = 1.0f)
	{
		LightLimitFix::LightData light{};
		light.color = { a_seed, a_seed + 1, a_seed + 2 };
		light.fade = a_seed + 3;
		light.radius = a_seed + 4;
		light.invRadius = a_seed + 5;
		light.fadeZone = a_seed + 6;
		light.sizeBias = a_seed + 7;
		light.positionWS[0].data = { a_seed + 8, a_seed + 9, a_seed + 10 };
		light.positionWS[1].data = { a_seed + 11, a_seed + 12, a_seed + 13 };
		light.roomFlags.words = { 1, 2, 3, 4 };
		light.lightFlags.value = static_cast<PointLightFlags::Flags>(0x4001);
		light.shadowMaskIndex = 14;
		return light;
	}

	std::array<float*, 14> FloatFields(LightLimitFix::LightData& a_light)
	{
		return {
			&a_light.color.x, &a_light.color.y, &a_light.color.z, &a_light.fade,
			&a_light.radius, &a_light.invRadius, &a_light.fadeZone, &a_light.sizeBias,
			&a_light.positionWS[0].data.x, &a_light.positionWS[0].data.y, &a_light.positionWS[0].data.z,
			&a_light.positionWS[1].data.x, &a_light.positionWS[1].data.y, &a_light.positionWS[1].data.z
		};
	}

	void TestFirstUploadAndBinding()
	{
		Fixture fixture;
		fixture.buffer.uploaded.NumStrictLights = 1;
		Require(!fixture.feature.hasUploadedStrictLightData && fixture.feature.strictLightBindingPending);
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && fixture.buffer.uploaded.NumStrictLights == 0);
		Require(fixture.feature.hasUploadedStrictLightData && !fixture.feature.strictLightBindingPending);
		Require(globals::d3d::contextStorage.bindings == 1);
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && globals::d3d::contextStorage.bindings == 1);
		++globals::stateStorage.frameCount;
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && globals::d3d::contextStorage.bindings == 2);
		fixture.feature.strictLightDataTemp.RoomIndex = -1;
		fixture.Draw();
		Require(fixture.buffer.updates == 2 && fixture.buffer.uploaded.RoomIndex == -1);
		fixture.feature.strictLightDataTemp.ShadowBitMask = 0x80000000;
		fixture.Draw();
		Require(fixture.buffer.updates == 3 && fixture.buffer.uploaded.ShadowBitMask == 0x80000000);
	}

	void TestEveryActiveField()
	{
		for (std::size_t field = 0; field < 20; ++field) {
			Fixture fixture;
			auto& data = fixture.feature.strictLightDataTemp;
			data.NumStrictLights = 1;
			data.StrictLights[0] = MakeLight();
			fixture.Draw();
			auto& light = data.StrictLights[0];
			if (field < 14)
				*FloatFields(light)[field] += 1;
			else if (field < 18)
				light.roomFlags.words[field - 14] ^= 0x80000000;
			else if (field == 18)
				light.lightFlags.value = static_cast<PointLightFlags::Flags>(0x2001);
			else
				++light.shadowMaskIndex;
			fixture.Draw();
			Require(fixture.buffer.updates == 2);
			Require(SameStrictLightData(data, fixture.buffer.uploaded));
			fixture.Draw();
			Require(fixture.buffer.updates == 2 && globals::d3d::contextStorage.bindings == 1);
		}
	}

	void TestFloatRepresentations()
	{
		constexpr std::array<std::uint32_t, 7> patterns{
			0x00000000, 0x80000000, 0x7fc00001, 0x7fc00002, 0x7f800000, 0xff800000, 0x7fa00003
		};
		for (std::size_t field = 0; field < 14; ++field) {
			for (std::size_t pattern = 0; pattern < patterns.size(); ++pattern) {
				Fixture fixture;
				auto& data = fixture.feature.strictLightDataTemp;
				data.NumStrictLights = 1;
				data.StrictLights[0] = MakeLight();
				*FloatFields(data.StrictLights[0])[field] = std::bit_cast<float>(patterns[pattern]);
				fixture.Draw();
				fixture.Draw();
				Require(fixture.buffer.updates == 1);
				*FloatFields(data.StrictLights[0])[field] = std::bit_cast<float>(patterns[(pattern + 1) % patterns.size()]);
				fixture.Draw();
				Require(fixture.buffer.updates == 2);
				fixture.Draw();
				Require(fixture.buffer.updates == 2);
			}
		}
	}

	void TestPaddingAndInactiveLights()
	{
		Fixture fixture;
		auto& data = fixture.feature.strictLightDataTemp;
		data.NumStrictLights = 1;
		data.StrictLights[0] = MakeLight();
		fixture.Draw();
		data.pad0 = 0xffffffff;
		auto& light = data.StrictLights[0];
		light.pad0 = light.pad1 = 0xffffffff;
		for (auto& position : light.positionWS)
			position.pad0 = 0xffffffff;
		for (std::size_t i = 1; i < std::size(data.StrictLights); ++i)
			data.StrictLights[i] = MakeLight(static_cast<float>(i + 100));
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && SameStrictLightData(data, fixture.buffer.uploaded));
		data.NumStrictLights = 0;
		fixture.Draw();
		Require(fixture.buffer.updates == 2);
		light = MakeLight(200);
		fixture.Draw();
		Require(fixture.buffer.updates == 2);
	}

	void TestCountAndOrder()
	{
		Fixture fixture;
		auto& data = fixture.feature.strictLightDataTemp;
		data.StrictLights[0] = MakeLight();
		data.StrictLights[1] = MakeLight(20);
		data.NumStrictLights = 1;
		fixture.Draw();
		data.NumStrictLights = 2;
		fixture.Draw();
		Require(fixture.buffer.updates == 2);
		std::swap(data.StrictLights[0], data.StrictLights[1]);
		fixture.Draw();
		Require(fixture.buffer.updates == 3);
		data.NumStrictLights = 1;
		fixture.Draw();
		Require(fixture.buffer.updates == 4);
		data.NumStrictLights = 0;
		fixture.Draw();
		Require(fixture.buffer.updates == 5);
		data.NumStrictLights = 15;
		for (std::size_t i = 0; i < std::size(data.StrictLights); ++i)
			data.StrictLights[i] = MakeLight(static_cast<float>(i));
		fixture.Draw();
		++data.StrictLights[14].color.z;
		fixture.Draw();
		Require(fixture.buffer.updates == 7);
		auto invalid = data;
		invalid.NumStrictLights = 16;
		Require(!SameStrictLightData(invalid, invalid));
		Require(!SameStrictLightData(data, invalid) && !SameStrictLightData(invalid, data));
	}

	void TestGuardsAndFailureRetry()
	{
		Fixture fixture;
		globals::shaderStorage.enabled = false;
		fixture.Draw();
		globals::shaderStorage.enabled = true;
		globals::game::smState = nullptr;
		fixture.Draw();
		globals::game::smState = &globals::game::manager;
		fixture.feature.strictLightDataCB = nullptr;
		fixture.Draw();
		fixture.feature.strictLightDataCB = &fixture.buffer;
		globals::game::currentAccumulator.value = nullptr;
		fixture.Draw();
		globals::game::currentAccumulator.value = &globals::game::accumulator;
		Require(fixture.buffer.attempts == 0 && !fixture.feature.hasUploadedStrictLightData);
		Require(globals::d3d::contextStorage.bindings == 0 && fixture.feature.strictLightBindingPending);
		fixture.buffer.failNext = true;
		try {
			fixture.Draw();
			Require(false);
		} catch (const std::runtime_error&) {
		}
		Require(fixture.buffer.attempts == 1 && !fixture.feature.hasUploadedStrictLightData);
		Require(globals::d3d::contextStorage.bindings == 0 && fixture.feature.strictLightBindingPending);
		fixture.Draw();
		Require(fixture.buffer.attempts == 2 && fixture.buffer.updates == 1);
		auto previous = fixture.feature.strictLightDataUploaded;
		fixture.feature.strictLightDataTemp.RoomIndex = 42;
		fixture.buffer.failNext = true;
		try {
			fixture.Draw();
			Require(false);
		} catch (const std::runtime_error&) {
		}
		Require(fixture.feature.hasUploadedStrictLightData && SameStrictLightData(previous, fixture.feature.strictLightDataUploaded));
		fixture.Draw();
		Require(fixture.buffer.attempts == 4 && fixture.buffer.updates == 2 && fixture.buffer.uploaded.RoomIndex == 42);
	}

	void TestResourceBindingAndFrameWrap()
	{
		Fixture fixture;
		globals::stateStorage.frameCount = UINT32_MAX;
		fixture.Draw();
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && globals::d3d::contextStorage.bindings == 1);
		globals::stateStorage.frameCount = 0;
		fixture.Draw();
		fixture.Draw();
		Require(fixture.buffer.updates == 1 && globals::d3d::contextStorage.bindings == 2);
		const auto creations = ConstantBuffer::creations;
		fixture.feature.Create();
		Require(ConstantBuffer::creations == creations && fixture.feature.hasUploadedStrictLightData);
		Require(!fixture.feature.strictLightBindingPending);
		const auto destructions = ConstantBuffer::destructions;
		fixture.feature.Retire();
		Require(!fixture.feature.strictLightDataCB && !fixture.feature.hasUploadedStrictLightData);
		Require(ConstantBuffer::destructions == destructions + 1);
		ConstantBuffer::failNextConstruction = true;
		try {
			fixture.feature.Create();
			Require(false);
		} catch (const std::bad_alloc&) {
		}
		Require(!fixture.feature.strictLightDataCB && !fixture.feature.hasUploadedStrictLightData);
		fixture.Draw();
		Require(globals::d3d::contextStorage.bindings == 2);
		fixture.feature.Create();
		auto& replacement = *fixture.feature.strictLightDataCB;
		Require(!fixture.feature.hasUploadedStrictLightData && fixture.feature.strictLightBindingPending);
		fixture.Draw();
		Require(replacement.updates == 1 && globals::d3d::contextStorage.bindings == 3);
		Require(globals::d3d::contextStorage.lastBuffer == &replacement.buffer);
		fixture.Draw();
		Require(replacement.updates == 1 && globals::d3d::contextStorage.bindings == 3);
		++globals::stateStorage.frameCount;
		fixture.Draw();
		Require(replacement.updates == 1 && globals::d3d::contextStorage.bindings == 4);
	}
}

int main()
{
	TestFirstUploadAndBinding();
	TestEveryActiveField();
	TestFloatRepresentations();
	TestPaddingAndInactiveLights();
	TestCountAndOrder();
	TestGuardsAndFailureRetry();
	TestResourceBindingAndFrameWrap();
	std::cout << "Strict-light upload tests passed\n";
}
