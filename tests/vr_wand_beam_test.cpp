#include "Features/VR/WandBeamGeometry.h"
#include "Features/VR/WandBeamShader.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d3dcompiler.h>
#include <limits>

namespace
{
	bool Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			std::printf("FAIL: %s\n", a_message);
		return a_condition;
	}

	bool RunGeometry()
	{
		using namespace WandBeamGeometry;
		ScreenBeam beam;
		const Viewport full{ 0, 0, 1280, 720 };
		bool passed = Check(Project({ -0.5f, 0, 0.5f, 1 }, { 0.5f, 0, 0.5f, 1 }, full, 1280, 720, 1.5f, beam),
			"a visible beam must project");
		passed &= Check(beam.endpoints[0] == 320 && beam.endpoints[2] == 960 && beam.endpoints[1] == 360,
			"projected beam endpoints must preserve the selected cursor point");
		passed &= Check(beam.origin[0] + beam.size[0] <= 1280 && beam.origin[1] + beam.size[1] <= 720,
			"draw bounds must remain inside the target");
		passed &= Check(Project({ 0, 0, -0.25f, -0.1f }, { 0.5f, 0, 0.5f, 1 }, full, 1280, 720, 1.5f, beam),
			"a controller behind the eye must clip to the visible segment");
		passed &= Check(std::isfinite(beam.endpoints[0]) && beam.endpoints[2] == 960 && beam.size[0] <= 1280,
			"near-eye clipping must retain the final cursor and bound dispatch");
		passed &= Check(!Project({ 0, 0, -1, -1 }, { 0.5f, 0, -1, -1 }, full, 1280, 720, 1.5f, beam),
			"a fully hidden beam must not draw");
		passed &= Check(!Project({ 2, 0, 0.5f, 1 }, { 3, 0, 0.5f, 1 }, full, 1280, 720, 1.5f, beam),
			"a beam outside the eye must not draw");
		passed &= Check(Project({ -4, 0, 0.5f, 1 }, { 4, 0, 0.5f, 1 }, { 640, 0, 640, 720 }, 1280, 720, 1.5f, beam),
			"a crossing segment must clip within the selected stereo eye");
		passed &= Check(beam.origin[0] >= 640 && beam.origin[0] + beam.size[0] <= 1280 &&
							beam.endpoints[0] == 640 && beam.endpoints[2] == 1280,
			"a right-eye beam must never write into the left eye");
		passed &= Check(Project({ 0, 0, 0.5f, 1 }, { 0, 0, 0.5f, 1 }, full, 1280, 720, 1.5f, beam),
			"an eye looking along the beam must retain a bounded point");
		passed &= Check(beam.size[0] <= 6 && beam.size[1] <= 6,
			"a zero-length projected beam must have a small finite footprint");
		passed &= Check(!Project({ std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 }, {}, full, 1280, 720, 1.5f, beam),
			"invalid tracking coordinates must suppress rendering");
		passed &= Check(!Project({}, {}, { 0.25f, 0.25f, 0.25f, 0.25f }, 1280, 720, 1.5f, beam),
			"a viewport with no complete pixels must not dispatch");
		passed &= Check(!Project({}, {}, full, 0, 720, 1.5f, beam) &&
							!Project({}, {}, full, 20000, 720, 1.5f, beam) &&
							!Project({}, {}, full, 1280, 720, std::numeric_limits<float>::infinity(), beam),
			"invalid target dimensions and beam radius must be rejected");
		return passed;
	}

	bool RunShaders()
	{
		bool passed = true;
		for (const auto& entry : std::array{
				 std::array{ "VSMain", "vs_5_0" }, std::array{ "PSMain", "ps_5_0" }, std::array{ "CSMain", "cs_5_0" } }) {
			ID3DBlob* bytecode = nullptr;
			ID3DBlob* errors = nullptr;
			const HRESULT result = D3DCompile(WandBeamRenderer::ShaderSource, sizeof(WandBeamRenderer::ShaderSource) - 1,
				"VRWandBeam", nullptr, nullptr, entry[0], entry[1],
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &bytecode, &errors);
			if (FAILED(result) && errors)
				std::printf("%s: %s\n", entry[0], static_cast<const char*>(errors->GetBufferPointer()));
			passed &= Check(SUCCEEDED(result) && bytecode && bytecode->GetBufferSize() > 0, entry[0]);
			if (bytecode)
				bytecode->Release();
			if (errors)
				errors->Release();
		}
		return passed;
	}
}

int main()
{
	if (!RunGeometry() || !RunShaders())
		return EXIT_FAILURE;
	std::puts("Wand beam clipping, stereo bounds, invalid-pose rejection, and all three shader stages passed.");
	return EXIT_SUCCESS;
}
