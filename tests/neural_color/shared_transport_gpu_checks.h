#pragma once

static constexpr std::array identityBaselineFormats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM };

static void CheckSharedPreparedInputs(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* prepare, ID3D11ComputeShader* reconstruct, ID3D11Buffer* cb)
{
	const std::vector<Pixel> sentinel(4096, Pixel{ -7, -7, -7, -7 });
	const std::array<std::array<unsigned, 4>, 2> rectangles{ { { 3, 5, 17, 13 }, { 37, 29, 19, 11 } } };
	for (unsigned transform = 0; transform < 3; ++transform) {
		auto shared = MakeTexture(device, sentinel);
		std::array<::Texture, 2> privatePrepared, baseline;
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

static std::vector<std::byte> ReadTextureBytes(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture)
{
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	const unsigned pixelBytes = desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT                                                    ? 16u :
	                            (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM) ? 8u :
	                                                                                                                               4u;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	Util::SetResourceName(staging.Get(), "NRColorTest::StoredReadback");
	context->CopyResource(staging.Get(), texture);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	const size_t rowBytes = size_t(desc.Width) * pixelBytes;
	std::vector<std::byte> bytes(rowBytes * desc.Height);
	for (unsigned y = 0; y < desc.Height; ++y)
		std::memcpy(bytes.data() + y * rowBytes, static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
	context->Unmap(staging.Get(), 0);
	return bytes;
}

static void CheckIdentityPreparedBaseline(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* reference, ID3D11ComputeShader* copy,
	ID3D11Buffer* cb, bool optimized)
{
	const auto immutable = BaselineShader(device, optimized);
	const std::array storageFlags{ 0u, 0x200u, 0x100u, 0x300u, 0x300u, 0x300u };
	unsigned comparisons = 0;
	for (const auto origin : { std::array{ 3u, 5u }, std::array{ 37u, 29u } }) {
		for (const auto size : { std::array{ 1u, 1u }, std::array{ 7u, 9u }, std::array{ 17u, 19u } }) {
			const auto [width, height] = size;
			const unsigned transportWidth = origin[0] + width, transportHeight = origin[1] + height;
			for (unsigned formatIndex = 0; formatIndex < identityBaselineFormats.size(); ++formatIndex) {
				const auto format = identityBaselineFormats[formatIndex];
				for (unsigned scenario = 0; scenario < 3; ++scenario) {
					std::vector<Pixel> pixels(size_t(transportWidth) * transportHeight, Pixel{ -7, -7, -7, -7 });
					for (unsigned y = 0; y < height; ++y) {
						for (unsigned x = 0; x < width; ++x) {
							auto& p = pixels[(y + origin[1]) * transportWidth + x + origin[0]];
							p = { .0625f + .007f * x, .125f + .003f * y, .03125f + .001f * ((x + y) % 5), .375f };
							Pixel ignored{}, neural{};
							if (scenario == 1)
								ApplyAdversarialReconstructionSample(y * width + x, p, ignored, neural);
							if (scenario == 2 && width > 1 && x == width / 2)
								p = { std::numeric_limits<float>::quiet_NaN(), -7, -7, -7 };
						}
					}
					const auto original = StorePixels(device, context, copy, cb, pixels, format, transportWidth, transportHeight);
					auto prepared = StorePixels(device, context, copy, cb,
						std::vector<Pixel>(pixels.size(), Pixel{ -7, -7, -7, -7 }), format, transportWidth, transportHeight);
					auto baseline = MakeTexture(device, std::vector<Pixel>(size_t(width) * height), format, width, height);
					// Identity preparation copies stored bits, including invalid RGB and alpha.
					const D3D11_BOX box{ origin[0], origin[1], 0, transportWidth, transportHeight, 1 };
					context->CopySubresourceRegion(baseline.texture.Get(), 0, 0, 0, 0, original.texture.Get(), 0, &box);
					context->CopySubresourceRegion(prepared.texture.Get(), 0, origin[0], origin[1], 0, original.texture.Get(), 0, &box);
					const auto before = ReadTextureBytes(device, context, prepared.texture.Get());
					auto neuralPixels = ReadStored(device, context, copy, cb, prepared);
					for (auto& p : neuralPixels) {
						p.r *= 1.17f;
						p.g *= .91f;
						p.a = 0;
					}
					const auto neural = StorePixels(device, context, copy, cb, neuralPixels, format, transportWidth, transportHeight);
					for (unsigned setting = 0; setting < 7; ++setting) {
						Constants c;
						c.x = origin[0];
						c.y = origin[1];
						c.width = width;
						c.height = height;
						Settings settings;
						settings.mode = setting == 1 ? Mode::NeuralLighting : Mode::PreserveSource;
						settings = ResolveReconstructionSettings(settings);
						c.mode = static_cast<unsigned>(settings.mode);
						c.lightingPreservation = settings.lightingPreservation;
						c.domain = scenario;
						c.detail = setting == 2 ? 0.f : 1.f;
						c.appearance = setting == 3 ? .25f : setting == 4 ? 1.f :
						                                                    0.f;
						c.bypass = storageFlags[formatIndex] | (setting == 5 ? 1u : setting == 6 ? 2u :
																								   0u);
						if (scenario == 2 && width > 1) {
							c.regionCount = 2;
							c.regions[0][2] = width / 2;
							c.regions[0][3] = height;
							c.regions[1][0] = width / 2 + 1;
							c.regions[1][2] = width - width / 2 - 1;
							c.regions[1][3] = height;
						}
						auto expected = MakeTexture(device, std::vector<Pixel>(size_t(width) * height), format, width, height);
						auto actual = MakeTexture(device, std::vector<Pixel>(size_t(width) * height), format, width, height);
						Dispatch(context, reconstruct, cb, c, { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, expected.uav.Get());
						const auto control = ReadTextureBytes(device, context, expected.texture.Get());
						if (scenario == 0 && setting == 0) {
							Dispatch(context, immutable.Get(), cb, c, { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, actual.uav.Get());
							Require(ReadTextureBytes(device, context, actual.texture.Get()) == control, "compact control must match compatible immutable DXBC");
						}
						c.baselineOffset[0] = c.x;
						c.baselineOffset[1] = c.y;
						for (auto* shader : { reconstruct, reference }) {
							Dispatch(context, shader, cb, c, { prepared.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, actual.uav.Get());
							Require(ReadTextureBytes(device, context, actual.texture.Get()) == control, "prepared baseline must preserve exact stored output bytes");
							++comparisons;
						}
					}
					Require(ReadTextureBytes(device, context, prepared.texture.Get()) == before, "reused prepared baseline must remain immutable");
				}
			}
		}
	}
	std::printf("Identity prepared baseline: %u exact stored-byte comparisons, optimized=%d\n", comparisons, optimized);
}
