#pragma once

static void CheckSharedPreparedInputs(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* prepare, ID3D11ComputeShader* reconstruct, ID3D11Buffer* cb)
{
	const std::vector<Pixel> sentinel(4096, Pixel{ -7, -7, -7, -7 });
	const std::array<std::array<unsigned, 4>, 2> rectangles{ { { 3, 5, 17, 13 }, { 37, 29, 19, 11 } } };
	for (unsigned transform = 0; transform < 3; ++transform) {
		auto shared = MakeTexture(device, sentinel);
		std::array<Texture, 2> privatePrepared, baseline;
		std::array<Constants, 2> constants;
		for (unsigned region = 0; region < 2; ++region) {
			const auto& rect = rectangles[region];
			auto pixels = sentinel;
			for (unsigned y = 0; y < rect[3]; ++y)
				for (unsigned x = 0; x < rect[2]; ++x)
					pixels[y * 64 + x] = { .1f + .003f * x, .2f + .005f * y, .15f + .05f * region, .25f + .5f * region };
			baseline[region] = MakeTexture(device, pixels);
			privatePrepared[region] = MakeTexture(device, sentinel);
			auto& c = constants[region];
			c.x = rect[0];
			c.y = rect[1];
			c.width = rect[2];
			c.height = rect[3];
			c.transform = transform;
			c.exposure = 1.25f;
			Dispatch(context, prepare, cb, c, { baseline[region].srv.Get(), nullptr, nullptr }, privatePrepared[region].uav.Get());
			Dispatch(context, prepare, cb, c, { baseline[region].srv.Get(), nullptr, nullptr }, shared.uav.Get());
		}
		const auto preparedBefore = Read(device, context, shared.texture.Get());
		for (unsigned y = 0; y < 64; ++y)
			for (unsigned x = 0; x < 64; ++x) {
				bool valid = false;
				for (const auto& r : rectangles)
					valid |= x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
				if (!valid)
					Require(preparedBefore[y * 64 + x].r == -7, "shared union must not prepare a gap-spanning hull");
			}
		// A deterministic private neural fixture isolates reconstruction from NVIDIA variability.
		for (unsigned region = 0; region < 2; ++region) {
			auto neuralPixels = preparedBefore;
			const auto& c = constants[region];
			for (unsigned y = c.y; y < c.y + c.height; ++y)
				for (unsigned x = c.x; x < c.x + c.width; ++x) {
					auto& p = neuralPixels[y * 64 + x];
					p.r *= 1.05f;
					p.g *= .95f;
					p.a = 0;
				}
			auto neural = MakeTexture(device, neuralPixels);
			for (unsigned mode = 1; mode < 4; ++mode) {
				auto control = MakeTexture(device, sentinel), candidate = MakeTexture(device, sentinel);
				constants[region].mode = mode;
				Dispatch(context, reconstruct, cb, constants[region], { baseline[region].srv.Get(), neural.srv.Get(), privatePrepared[region].srv.Get() }, control.uav.Get());
				Dispatch(context, reconstruct, cb, constants[region], { baseline[region].srv.Get(), neural.srv.Get(), shared.srv.Get() }, candidate.uav.Get());
				const auto expected = Read(device, context, control.texture.Get()), actual = Read(device, context, candidate.texture.Get());
				Require(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(Pixel)) == 0, "shared P must preserve exact private reconstruction including alpha");
			}
		}
		const auto preparedAfter = Read(device, context, shared.texture.Get());
		Require(std::memcmp(preparedBefore.data(), preparedAfter.data(), preparedBefore.size() * sizeof(Pixel)) == 0, "P remains immutable through every reconstruction");
	}
}
