#include "Features/LightLimitFix/ActorShadowHistory.h"
#include "Features/LightLimitFix/ShadowPassPolicy.h"
#include "Features/TextureStreaming/Policy.h"
#include "Utils/DepthPyramidPolicy.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
{
	try {
		using namespace LightLimitFixDetail;
		using namespace ShadowCulling;
		constexpr auto shadowTechnique = 0x2Bu + (1u << 14);
		Require(ShadowPassPolicy::IsIsolatedShadowPass(true, shadowTechnique, 0, 0), "Native lightless shadow pass was rejected");
		Require(!ShadowPassPolicy::IsIsolatedShadowPass(false, shadowTechnique, 0, 0), "Ordinary color pass entered shadow culling");
		Require(!ShadowPassPolicy::IsIsolatedShadowPass(true, shadowTechnique, 1, 0), "Lit pass entered shadow culling");
		Require(!ShadowPassPolicy::IsIsolatedShadowPass(true, shadowTechnique, 0, 1), "Shadow-lit pass entered shadow culling");
		Require(!ShadowPassPolicy::IsIsolatedShadowPass(true, 0x2A, 0, 0), "Invalid utility descriptor entered shadow culling");

		const std::array<std::array<double, 3>, 1> camera{};
		ActorShadowHistory history({ 1000, 800 });
		const auto position = [](double distance) { return std::array<double, 3>{ distance + EyeDistanceMargin, 0, 0 }; };
		Require(!history.Beyond(1, position(999), camera, 1, 1, 1), "Actor culled before cutoff");
		Require(history.Beyond(1, position(1001), camera, 1, 2, 2), "Distant actor never entered hysteresis");
		Require(history.Beyond(1, position(900), camera, 1, 3, 3), "Actor returned before the return threshold");
		Require(!history.Beyond(1, position(799), camera, 1, 4, 4), "Nearby actor retained a stale culling decision");
		Require(!history.Beyond(1, position(2000), camera, 1, 3, 3), "Delayed frame overrode newer actor visibility");
		Require(!history.Beyond(1, position(900), camera, 1, 300, 6), "Stale history retained a return band");
		Require(!history.Beyond(2, position(2000), camera, 2, 301, 7), "A second view entered the single-view policy");

		using namespace TextureStreamingPolicy;
		const std::array<Eye, 1> view{ Eye{ 1920, 1080, 1920, 1080, 1, 1, 1000 } };
		const double ordinary = RequiredEdge(view, 1, 256, 0);
		auto widerFov = view;
		widerFov[0].projectionX = widerFov[0].projectionY = 0.5;
		Require(RequiredEdge(widerFov, 1, 256, 0) == ordinary * 0.5, "Texture demand ignored the current projection");
		Require(!std::isfinite(RequiredEdge(view, 2, 256, 0)), "Texture demand accepted a second view");
		auto halfRender = view;
		halfRender[0].renderWidth *= 0.5;
		halfRender[0].renderHeight *= 0.5;
		Require(RequiredEdge(halfRender, 1, 256, 0) == ordinary, "Render scaling lost final-output detail");
		Require(RequiredEdge(halfRender, 1, 256, -2) == ordinary * 2, "Upscaler mip bias lost requested detail");

		Require(DepthPyramidPolicy::SinglePassMipCount(4096, 4096, 16) == 12, "SPD tail dispatch exceeded its local coverage");
		Require(DepthPyramidPolicy::SinglePassMipCount(8192, 2160, 16) == 6, "Wide output dispatched an invalid SPD tail");
		Require(DepthPyramidPolicy::SinglePassMipCount(1, 1, 1) == 0, "Single-mip depth emitted redundant dispatches");
		std::cout << "PASS: SE shadow admission, actor hysteresis, projection/render-scale demand and SPD extent bounds\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
