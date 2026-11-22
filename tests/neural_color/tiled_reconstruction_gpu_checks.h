static void ApplyAdversarialReconstructionSample(unsigned index, Pixel& baseline, Pixel& prepared, Pixel& neural)
{
	const auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	switch (index % 17) {
	case 0:
		baseline.a = nan;
		break;
	case 1:
		baseline.a = inf;
		break;
	case 2:
		baseline.r = nan;
		break;
	case 3:
		baseline.g = inf;
		break;
	case 4:
		prepared.r = nan;
		break;
	case 5:
		neural.g = inf;
		break;
	case 6:
		baseline.r = baseline.g = baseline.b = 1e-7f;
		break;
	case 7:
		baseline.r = baseline.g = baseline.b = 0.f;
		break;
	case 8:
		neural.b = 70000.f;
		break;
	case 9:
		baseline.r = -.125f;
		break;
	case 10:
		baseline.r = baseline.g = baseline.b = std::numeric_limits<float>::max();
		break;
	case 11:
		baseline.r = baseline.g = baseline.b = std::numeric_limits<float>::denorm_min();
		break;
	case 12:
		baseline.r = baseline.g = baseline.b = -0.f;
		break;
	case 14:
		prepared.g = inf;
		break;
	case 15:
		neural.r = nan;
		break;
	case 16:
		neural.a = inf;
		break;
	default:
		break;
	}
}

// Exercise the production kernel against untiled execution on actual small resources.
static void CheckTiledReconstruction(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* prepare, ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* reference,
	ID3D11ComputeShader* copy, ID3D11Buffer* cb, bool optimized, bool nvidiaReference = false)
{
	const auto immutable = BaselineShader(device, optimized);
	const std::array sizes{ std::array{ 1u, 1u }, std::array{ 1u, 19u }, std::array{ 17u, 1u },
		std::array{ 7u, 7u }, std::array{ 8u, 8u }, std::array{ 9u, 9u }, std::array{ 17u, 19u } };
	const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM };
	const std::array storageFlags{ 0u, 0x200u, 0x100u, 0x300u };
	unsigned comparisons = 0, immutableComparisons = 0;
	for (const auto size : sizes) {
		const auto [width, height] = size;
		if (nvidiaReference && size != sizes.back())
			continue;
		for (unsigned formatIndex = 0; formatIndex < formats.size(); ++formatIndex) {
			const auto format = formats[formatIndex];
			for (unsigned domain = 0; domain < 3; ++domain) {
				for (unsigned scenario = 0; scenario < (size == sizes.back() ? 7u : 5u); ++scenario) {
					Constants c;
					c.width = width;
					c.height = height;
					c.mode = 2;
					c.domain = domain;
					c.bypass = storageFlags[formatIndex];
					c.transform = nvidiaReference ? 3u : scenario < 2 ? 0u :
					                                 scenario == 2    ? 1u :
					                                                    2u;
					c.exposure = scenario == 2 ? 2.f : 0.5f;
					Texture exposure;
					if (scenario >= 5) {
						c.bypass |= 4u;
						exposure = MakeTexture(device, { Pixel{ 1, 2, 2, scenario == 5 ? 1.f : 0.f } },
							DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1);
						auto* snapshot = exposure.srv.Get();
						context->CSSetShaderResources(3, 1, &snapshot);
					}
					// The ROI ends at the real transport edges; every group halo must stay local.
					const unsigned transportWidth = c.x + width, transportHeight = c.y + height;
					std::vector<Pixel> source(size_t(width) * height);
					for (unsigned y = 0; y < height; ++y)
						for (unsigned x = 0; x < width; ++x)
							source[y * width + x] = { .0625f + .007f * x, .125f + .003f * y,
								.03125f + .001f * ((x + y) % 5), .375f };
					auto baseline = StorePixels(device, context, copy, cb, source, format, width, height);
					source = ReadStored(device, context, copy, cb, baseline);
					auto prepared = MakeTexture(device, std::vector<Pixel>(size_t(transportWidth) * transportHeight),
						DXGI_FORMAT_R32G32B32A32_FLOAT, transportWidth, transportHeight);
					Dispatch(context, prepare, cb, c, { baseline.srv.Get() }, prepared.uav.Get());
					auto preparation = Read(device, context, prepared.texture.Get()), edits = preparation;
					for (unsigned y = 0; y < height; ++y) {
						for (unsigned x = 0; x < width; ++x) {
							const auto i = y * width + x, p = (y + c.y) * transportWidth + x + c.x;
							const float gain = (x + y) % 2 ? 1.17f : 0.91f;
							edits[p] = { preparation[p].r * gain, preparation[p].g * gain, preparation[p].b * gain, 0 };
							if (scenario == 1)
								ApplyAdversarialReconstructionSample(i, source[i], preparation[p], edits[p]);
						}
					}
					if (scenario == 1) {
						baseline = StorePixels(device, context, copy, cb, source, format, width, height);
						source = ReadStored(device, context, copy, cb, baseline);
						prepared = MakeTexture(device, preparation, DXGI_FORMAT_R32G32B32A32_FLOAT, transportWidth, transportHeight);
					}
					if (scenario == 2 && width > 1) {
						c.regionCount = 2;
						c.regions[0][2] = width / 2;
						c.regions[0][3] = height;
						c.regions[1][0] = width / 2;
						c.regions[1][2] = width - width / 2;
						c.regions[1][3] = height;
					} else if (scenario == 3) {
						c.regionCount = 2;
						c.regions[0][2] = c.regions[0][3] = 1;
						c.regions[1][0] = width / 2;
						c.regions[1][1] = height / 2;
						c.regions[1][2] = width - width / 2;
						c.regions[1][3] = height - height / 2;
					} else if (scenario == 4) {
						c.regionCount = 2;
						c.regions[0][2] = std::max(1u, width - 1);
						c.regions[0][3] = std::max(1u, height - 1);
						c.regions[1][2] = width;
						c.regions[1][3] = height;
					}
					const auto neural = MakeTexture(device, edits, DXGI_FORMAT_R32G32B32A32_FLOAT, transportWidth, transportHeight);
					const std::array inputs{ baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() };
					for (float preservation : { 0.f, .5f, 1.f }) {
						for (float appearance : { 0.f, .25f, 1.f }) {
							// Sweep endpoints on dense/sparse partial groups; other cases stress the active tiled filter.
							const bool sweepEndpoints = size == sizes.back() && (scenario == 0 || scenario == 3);
							if (!sweepEndpoints && (preservation != 1.f || appearance != 0.f))
								continue;
							c.lightingPreservation = preservation;
							c.appearance = appearance;
							auto result = StorePixels(device, context, copy, cb, source, format, width, height);
							auto expectedResult = StorePixels(device, context, copy, cb, source, format, width, height);
							Dispatch(context, reconstruct, cb, c, inputs, result.uav.Get());
							Dispatch(context, reference, cb, c, inputs, expectedResult.uav.Get());
							const auto actual = ReadStored(device, context, copy, cb, result);
							const auto expected = ReadStored(device, context, copy, cb, expectedResult);
							const bool matches = std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(Pixel)) == 0;
							if (!matches) {
								std::fprintf(stderr, "Tile comparison %ux%u format=%u domain=%u scenario=%u preservation=%g appearance=%g optimized=%d reference=%d\n",
									width, height, formatIndex, domain, scenario, preservation, appearance, optimized, nvidiaReference);
								PrintFirstPixelDifference(actual, expected);
							}
							Require(matches, "tiled reconstruction must exactly match untiled production arithmetic");
							++comparisons;
							// Older DXBC predates crop, proxy and nonfinite-input changes; finite raw inputs still match.
							if (preservation == 1.f && scenario == 0 && !nvidiaReference) {
								Dispatch(context, immutable.Get(), cb, c, inputs, expectedResult.uav.Get());
								const auto old = ReadStored(device, context, copy, cb, expectedResult);
								const bool immutableMatches = std::memcmp(actual.data(), old.data(), actual.size() * sizeof(Pixel)) == 0;
								if (!immutableMatches) {
									std::fprintf(stderr, "Immutable comparison %ux%u format=%u domain=%u scenario=%u appearance=%g optimized=%d\n",
										width, height, formatIndex, domain, scenario, appearance, optimized);
									PrintFirstPixelDifference(actual, old);
								}
								Require(immutableMatches,
									"small textures must exactly match compatible immutable production DXBC");
								++immutableComparisons;
							}
							for (size_t i = 0; i < actual.size(); ++i) {
								const auto b = source[i];
								if (std::isfinite(b.r) && std::isfinite(b.g) && std::isfinite(b.b) && std::isfinite(b.a))
									Require(actual[i].a == b.a, "tiled outputs must retain baseline alpha");
								else
									Require(actual[i].r == 0 && actual[i].g == 0 && actual[i].b == 0,
										"invalid centre RGBA must store zero after group synchronization");
							}
						}
					}
					ID3D11ShaderResourceView* emptyExposure = nullptr;
					context->CSSetShaderResources(3, 1, &emptyExposure);
				}
			}
		}
	}
	std::printf("Tiled reconstruction: %u exact untiled comparisons, %u immutable DXBC comparisons (NVIDIA reference=%d)\n",
		comparisons, immutableComparisons, nvidiaReference);
}
