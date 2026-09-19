#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

// Engine stand-ins exercise production bounds and dispatch policy without D3D.
struct float2
{
	float x, y;
};
constexpr int D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION = 16384;
struct State
{
	float2 screenSize{ 2000, 1000 };
};
namespace globals
{
	inline State storage;
	inline State* state = &storage;
	namespace game
	{
		inline bool isVR = false;
		inline int graphicsStorage = 0;
		inline int* graphicsState = &graphicsStorage;
	}
}
namespace Util
{
	inline float2 renderSize{ 1000, 500 };
	float2 ConvertToDynamic(float2) { return renderSize; }
}
struct ConstantBuffer
{
	int updates = 0;
	template <class T>
	void Update(const T&)
	{
		++updates;
	}
};
struct VolumetricLighting
{
	struct VLData
	{
		int32_t screenX, screenY, screenXMin1, screenYMin1, eyeWidth;
		uint32_t horizontalGroupsPerEye, pad[2];
	} vlData{};
	ConstantBuffer* vlDataCB = nullptr;
	bool blurDimensionsValid = false;
	int32_t fullScreenX = 0, fullScreenY = 0;
	static constexpr int32_t BlurThreadGroupSizeX = 256;
	static constexpr int32_t BlurThreadGroupSizeY = 256;
	static constexpr int32_t BlurWindow = 12;
	int* blurHCS = nullptr;
	int* blurVCS = nullptr;
	void UpdateBlurDimensions();
	void SetGroupCountsHCS(uint32_t&, uint32_t&) const;
	void SetGroupCountsVCS(uint32_t&, uint32_t&) const;
};
#include "volumetric_blur_under_test.h"

#define CHECK(condition)                                                 \
	do {                                                                 \
		if (!(condition)) {                                              \
			std::fprintf(stderr, "Line %d: %s\n", __LINE__, #condition); \
			return 1;                                                    \
		}                                                                \
	} while (false)

int main()
{
	ConstantBuffer buffer;
	VolumetricLighting lighting;
	lighting.vlDataCB = &buffer;
	struct Case
	{
		bool vr;
		int width, height;
		uint32_t horizontalGroups, verticalGroups;
	};
	for (const auto& test : std::array{
			 Case{ false, 1, 1, 1, 1 },
			 Case{ false, 232, 232, 1, 1 },
			 Case{ false, 233, 233, 2, 2 },
			 Case{ true, 2, 1, 2, 1 },
			 Case{ true, 464, 232, 2, 1 },
			 Case{ true, 465, 233, 4, 2 },
			 Case{ true, 1001, 697, 6, 4 } }) {
		globals::game::isVR = test.vr;
		Util::renderSize = { float(test.width), float(test.height) };
		lighting.UpdateBlurDimensions();
		CHECK(lighting.blurDimensionsValid);
		CHECK(lighting.vlData.screenXMin1 == test.width - 1);
		CHECK(lighting.vlData.screenYMin1 == test.height - 1);
		CHECK(lighting.vlData.eyeWidth == (test.vr ? test.width / 2 : test.width));
		uint32_t x = 999, y = 999;
		lighting.SetGroupCountsHCS(x, y);
		CHECK(x == test.horizontalGroups && y == uint32_t(test.height));
		lighting.SetGroupCountsVCS(x, y);
		CHECK(x == uint32_t(test.width) && y == test.verticalGroups);
	}

	int shaderSentinel = 1;
	lighting.blurHCS = lighting.blurVCS = &shaderSentinel;
	Util::renderSize = { 500.9f, 250.9f };
	lighting.UpdateBlurDimensions();
	CHECK(lighting.vlData.screenX == 500 && lighting.vlData.screenY == 250);
	CHECK(lighting.blurHCS == &shaderSentinel && lighting.blurVCS == &shaderSentinel);
	globals::storage.screenSize.x = 1999;
	lighting.UpdateBlurDimensions();
	CHECK(!lighting.blurHCS && !lighting.blurVCS);

	const auto valid = lighting.vlData;
	const int updates = buffer.updates;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();
	for (float invalid : { nan, infinity, -infinity, -1.0f, 0.0f, 0.5f, 1.0f, 2000.0f }) {
		Util::renderSize = { invalid, 250 };
		lighting.UpdateBlurDimensions();
		CHECK(!lighting.blurDimensionsValid && buffer.updates == updates);
		CHECK(lighting.vlData.screenX == valid.screenX && lighting.vlData.screenY == valid.screenY);
	}
	for (float invalid : { nan, infinity, -1.0f, 0.0f, 0.5f, 1001.0f }) {
		Util::renderSize = { 500, invalid };
		lighting.UpdateBlurDimensions();
		CHECK(!lighting.blurDimensionsValid && buffer.updates == updates);
	}
	Util::renderSize = { 500, 250 };
	for (float invalid : { nan, infinity, -1.0f, 0.0f, 1.0f, 16385.0f }) {
		globals::storage.screenSize.x = invalid;
		lighting.UpdateBlurDimensions();
		CHECK(!lighting.blurDimensionsValid && buffer.updates == updates);
	}
	globals::storage.screenSize = { 16384, 16384 };
	Util::renderSize = globals::storage.screenSize;
	lighting.UpdateBlurDimensions();
	CHECK(lighting.blurDimensionsValid);
	uint32_t x = 0, y = 0;
	lighting.SetGroupCountsHCS(x, y);
	CHECK(x == 72 && y == 16384);
	lighting.SetGroupCountsVCS(x, y);
	CHECK(x == 16384 && y == 71);
	lighting.vlDataCB = nullptr;
	lighting.UpdateBlurDimensions();
	CHECK(!lighting.blurDimensionsValid);
	lighting.vlDataCB = &buffer;
	globals::game::graphicsState = nullptr;
	lighting.UpdateBlurDimensions();
	CHECK(!lighting.blurDimensionsValid);
	globals::game::graphicsState = &globals::game::graphicsStorage;
	globals::state = nullptr;
	lighting.UpdateBlurDimensions();
	CHECK(!lighting.blurDimensionsValid);
	globals::state = &globals::storage;
	lighting.UpdateBlurDimensions();
	CHECK(lighting.blurDimensionsValid);
	std::puts("Volumetric blur bounds, dispatch and cache regression cases passed.");
}
