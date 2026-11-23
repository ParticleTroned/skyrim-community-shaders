// Included by the production-shader harness to reuse its device and storage helpers.
static ComPtr<ID3D11ComputeShader> BaselineShader(ID3D11Device* device, bool optimized)
{
	const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/preserve-source-1-2-0" /
	                  (optimized ? "reconstruct-optimized.dxbc" : "reconstruct-strict.dxbc");
	std::ifstream file(path, std::ios::binary);
	const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
	Require(!bytes.empty(), "pre-slider production DXBC fixture is required");
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytes.data(), bytes.size(), nullptr, &shader));
	return shader;
}
static void CheckLightingPreservation(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* copy, ID3D11Buffer* cb, bool optimized)
{
	const auto oldShader = BaselineShader(device, optimized);
	const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM };
	const std::array flags{ 0u, 0x200u, 0x100u, 0x300u };
	const auto redValues = PackedValues(6), blueValues = PackedValues(5);
	unsigned comparisons = 0;
	for (unsigned formatIndex = 0; formatIndex < formats.size(); ++formatIndex) {
		const auto format = formats[formatIndex];
		UINT support{};
		Check(device->CheckFormatSupport(format, &support));
		Require((support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0, "required storage UAV support");
		for (unsigned domain = 0; domain < 3; ++domain) {
			const auto decode = [domain](float v) { return domain == 2 ? Decode(v) : v; };
			const auto encode = [domain](float v) { return domain == 2 ? Encode(v) : v; };
			const Pixel initial{ encode(0.125f), encode(0.0625f), encode(0.03125f), 1 };
			auto baseline = StorePixels(device, context, copy, cb, std::vector<Pixel>(4096, initial), format);
			const auto original = ReadStored(device, context, copy, cb, baseline);
			// Constant source makes the existing local estimator analytic: the
			// symmetric ramp is unchanged; a checker contributes one third.
			for (std::string_view signal : { "double", "half", "constant", "ramp", "mixed", "identity",
					 "zero", "limit_zero", "bounded", "hidden", "appearance", "transport", "invalid", "near_black", "range",
					 "managed", "legacy", "appearance_partial", "negative", "hdr", "zero_invalid" }) {
				if ((signal == "negative" && formatIndex >= 2) || (signal == "hdr" && formatIndex == 3))
					continue;
				const auto residual = [signal](unsigned x, unsigned y) {
					if (signal == "identity" || signal == "invalid")
						return 0.0f;
					if (signal == "half")
						return -1.0f;
					if (signal == "ramp" || signal == "mixed")
						return 0.25f + float(x) / 128.0f + (signal == "mixed" ? ((x + y) % 2 ? -0.25f : 0.25f) : 0.0f);
					return signal == "constant" ? 0.375f : 1.0f;
				};
				auto source = original;
				if (signal == "near_black")
					source.assign(4096, Pixel{ 0, 0, 0, 1 });
				if (signal == "negative")
					source.assign(4096, Pixel{ -0.125f, 0.25f, 0.0625f, 1 });
				if (signal == "hdr")
					source.assign(4096, Pixel{ encode(2.0f), encode(4.0f), encode(0.5f), 1 });
				auto activeBaseline = StorePixels(device, context, copy, cb, source, format);
				source = ReadStored(device, context, copy, cb, activeBaseline);
				std::vector<Pixel> edited(4096);
				for (unsigned i = 0; i < edited.size(); ++i) {
					const auto gain = std::exp2(residual(i % 64, i / 64));
					edited[i] = { encode(decode(source[i].r) * gain), encode(decode(source[i].g) * gain),
						encode(decode(source[i].b) * gain), 0 };
					if (signal == "invalid" || signal == "zero_invalid")
						edited[i].r = std::numeric_limits<float>::quiet_NaN();
					if (signal == "range")
						edited[i] = { 70000, 70000, 70000, 0 };
				}
				auto neural = MakeTexture(device, edited);
				std::vector<Pixel> fullPreservation;
				for (float preservation : { 1.f, 0.5f, 0.f }) {
					auto constants = FullTextureConstants();
					constants.width = constants.height = 32;
					constants.mode = signal == "legacy" ? 0u : signal == "managed" ? 1u :
					                                                                 2u;
					constants.domain = domain;
					constants.bypass = flags[formatIndex] | (signal == "hidden" ? 2u : signal == "transport" ? 1u :
																											   0u);
					constants.detail = signal == "zero" || signal == "zero_invalid" ? 0.f : signal == "constant" ? 0.75f :
					                                                                                               1.f;
					constants.maximumStops = signal == "limit_zero" ? 0.f : signal == "bounded" ? 0.125f :
					                                                                              2.f;
					constants.appearance = signal == "appearance" ? 1.f : signal == "appearance_partial" ? 0.25f :
					                                                                                       0.f;
					constants.lightingPreservation = preservation;
					auto output = StorePixels(device, context, copy, cb, source, format);
					const std::array inputs{ activeBaseline.srv.Get(), neural.srv.Get(), activeBaseline.srv.Get() };
					Dispatch(context, reconstruct, cb, constants, inputs, output.uav.Get());
					const auto actual = ReadStored(device, context, copy, cb, output);
					if (preservation == 1.f)
						fullPreservation = actual;
					else if (constants.mode != 2)
						Require(std::memcmp(actual.data(), fullPreservation.data(), actual.size() * sizeof(Pixel)) == 0,
							"Managed and Legacy shader outputs are independent of lighting preservation");
					if (preservation == 1.f || (signal == "range" && formatIndex != 0)) {
						auto oldOutput = StorePixels(device, context, copy, cb, source, format);
						Dispatch(context, oldShader.Get(), cb, constants, inputs, oldOutput.uav.Get());
						const auto old = ReadStored(device, context, copy, cb, oldOutput);
						Require(std::memcmp(actual.data(), old.data(), actual.size() * sizeof(Pixel)) == 0,
							"100% must exactly match pre-change production shader for the same compiler flags");
						comparisons += preservation == 1.f;
					}
					for (unsigned y = 0; y < 64; ++y) {
						for (unsigned x = 0; x < 64; ++x) {
							const unsigned i = y * 64 + x;
							const auto a = actual[i], s = source[i];
							const bool outside = x >= 32 || y >= 32;
							const bool exact = outside || signal == "zero" || signal == "zero_invalid" || signal == "hidden" || signal == "near_black" || signal == "negative";
							Require(a.a == s.a, "lighting preservation retains alpha");
							if (exact) {
								if (a.r != s.r || a.g != s.g || a.b != s.b)
									std::fprintf(stderr, "format=%u domain=%u signal=%.*s preservation=%g at %u,%u actual=%g,%g,%g source=%g,%g,%g\n",
										formatIndex, domain, int(signal.size()), signal.data(), preservation, x, y, a.r, a.g, a.b, s.r, s.g, s.b);
								Require(a.r == s.r && a.g == s.g && a.b == s.b, "exact fallback, disabled detail and untouched ROI surroundings");
								continue;
							}
							if (signal == "range") {
								Require(Finite(RGB{ a.r, a.g, a.b }) && Representable(RGB{ a.r, a.g, a.b }, static_cast<Storage>(flags[formatIndex])),
									"lighting changes remain finite and representable");
								continue;
							}
							const bool candidate = signal == "appearance" || signal == "transport" || constants.mode != 2;
							// Interior taps of the linear residual ramp have exactly its centre mean.
							if (!candidate && (signal == "ramp" || signal == "mixed") && (x == 0 || y == 0 || x == 31 || y == 31))
								continue;
							const float c = residual(x, y);
							const float fine = signal == "mixed" ? ((x + y) % 2 ? -0.25f : 0.25f) : 0.f;
							const float low = c - fine * (2.f / 3.f);
							const float edge = std::min(1.f, float(std::min({ x, y, 31 - x, 31 - y })) / 4.f);
							const float stops = std::clamp((c - preservation * low) * constants.detail * edge,
								-constants.maximumStops, constants.maximumStops);
							const float gain = std::exp2(candidate ? c : stops);
							const std::array stored{ a.r, a.g, a.b };
							const std::array input{ s.r, s.g, s.b };
							for (unsigned channel = 0; channel < 3; ++channel) {
								float expected = encode(decode(input[channel]) * gain);
								if (signal == "appearance_partial")
									expected = 0.75f * expected + 0.25f * encode(decode(input[channel]) * std::exp2(c));
								const float unroundedExpected = expected;
								const float expectedWorking = decode(expected);
								// Preserve Source explicitly rounds packed stores; candidate endpoints
								// keep their existing driver conversion, whose error is at most one ULP.
								if (formatIndex == 2 && !candidate)
									expected = NearestPacked(expected, channel == 2 ? blueValues : redValues);
								const float tolerance = 3e-6f + (formatIndex == 0 ? 0.f : formatIndex == 1 ? expected / 1024.f :
																					  formatIndex == 2     ? expected / (channel == 2 ? 32.f : 64.f) :
																											 1.f / 255.f);
								Require(std::abs(stored[channel] - expected) <= tolerance, "analytic uniform/ramp/mixed lighting gain with storage error");
								// HDR sRGB decoding has a steeper slope than its SDR interval.
								const float workingError = signal == "hdr" ?
								                               std::max(std::abs(decode(unroundedExpected + tolerance) - expectedWorking),
																   std::abs(decode(unroundedExpected - tolerance) - expectedWorking)) :
								                           domain == 2 ? 2.5f * tolerance :
								                                         tolerance;
								const bool workingMatches = std::abs(decode(stored[channel]) - expectedWorking) <= workingError + 1e-4f * decode(input[channel]);
								if (!workingMatches)
									std::fprintf(stderr, "working format=%u domain=%u signal=%.*s preservation=%g at %u,%u channel=%u actual=%g expected=%g tolerance=%g stored=%g source=%g\n",
										formatIndex, domain, int(signal.size()), signal.data(), preservation, x, y, channel,
										decode(stored[channel]), expectedWorking, workingError, stored[channel], input[channel]);
								Require(workingMatches,
									"accepted lighting gain and appearance interpolation agree in the selected working domain");
							}
						}
					}
				}
			}
		}
	}
	std::printf("Lighting preservation: %u bit-exact pre-change comparisons; FP32/FP16/R11G11B10/UNORM and all domains\n", comparisons);
}

static void CheckPreparationReference(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* reference, ID3D11ComputeShader* copy, ID3D11Buffer* cb)
{
	const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM };
	const std::array storageFlags{ 0u, 0x200u, 0x100u, 0x300u };
	for (size_t formatIndex = 0; formatIndex < formats.size(); ++formatIndex) {
		const auto format = formats[formatIndex];
		for (unsigned domain = 0; domain < 3; ++domain) {
			const std::vector<Pixel> source(4096, Pixel{ .25f, .125f, .0625f, .375f });
			auto baseline = StorePixels(device, context, copy, cb, source, format);
			for (unsigned scenario = 0; scenario < 8; ++scenario) {
				auto constants = FullTextureConstants();
				constants.width = constants.height = 32;
				constants.domain = domain;
				constants.mode = scenario == 4 ? 1u : scenario == 6 ? 0u :
				                                                      2u;
				constants.bypass = storageFlags[formatIndex] | (scenario == 5 ? 1u : scenario == 7 ? 2u :
																									 0u);
				constants.detail = scenario == 2 ? 1.f : 0.f;
				constants.appearance = scenario == 3 ? .5f : 0.f;
				constants.lightingPreservation = .5f;
				const float modelRed = scenario == 0 ? std::numeric_limits<float>::quiet_NaN() : .5f;
				auto model = MakeTexture(device, std::vector<Pixel>(4096, Pixel{ modelRed, .25f, .125f, 0 }));
				auto output = StorePixels(device, context, copy, cb, source, format);
				auto oldOutput = StorePixels(device, context, copy, cb, source, format);
				const std::array inputs{ baseline.srv.Get(), model.srv.Get(), baseline.srv.Get() };
				Dispatch(context, reconstruct, cb, constants, inputs, output.uav.Get());
				Dispatch(context, reference, cb, constants, inputs, oldOutput.uav.Get());
				const auto actual = ReadStored(device, context, copy, cb, output);
				const auto expected = ReadStored(device, context, copy, cb, oldOutput);
				Require(std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(Pixel)) == 0,
					"Neural preparation reference changed neutral, bypass or active reconstruction output");
				if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 6)
					Require(actual[16 * 64 + 16].r != source[16 * 64 + 16].r,
						"Reconstruction comparison failed to exercise a nonneutral candidate");
			}
		}
	}
}

static void CheckPackedLightingPreservation(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* reconstruct, ID3D11ComputeShader* copy, ID3D11Buffer* cb)
{
	const std::vector<Pixel> empty(4096, Pixel{ 0.01f, 0.02f, 0.03f, 0.375f });
	// Touching tiles, empty padding and nonzero transport origins must match isolated ROIs.
	for (const auto format : { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
			 DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM }) {
		for (unsigned gap : { 0u, 7u }) {
			const std::array<std::array<unsigned, 4>, 2> regions{ { { 2, 3, 13, 17 }, { 15 + gap, 3, 11, 19 } } };
			auto source = empty;
			std::array<std::vector<Pixel>, 2> privateSource{ empty, empty }, privatePrepared{ empty, empty }, privateNeural{ empty, empty };
			auto edits = empty, preparation = empty;
			for (unsigned tile = 0; tile < regions.size(); ++tile) {
				const auto& r = regions[tile];
				for (unsigned y = 0; y < r[3]; ++y) {
					for (unsigned x = 0; x < r[2]; ++x) {
						const Pixel b{ 0.125f + 0.002f * x, 0.0625f + 0.001f * y, 0.03125f, 0.375f };
						// Opposite broad relighting, plus real detail, tests both isolation and preservation.
						const auto gain = std::exp2((tile ? -1.f : 1.f) + ((x + y) % 2 ? -0.2f : 0.2f));
						const Pixel n{ b.r * gain, b.g * gain, b.b * gain, 0 };
						preparation[(y + r[1] + 7) * 64 + x + r[0] + 5] = privatePrepared[tile][(y + 7) * 64 + x + 5] = b;
						source[(y + r[1]) * 64 + x + r[0]] = privateSource[tile][y * 64 + x] = b;
						edits[(y + r[1] + 7) * 64 + x + r[0] + 5] = privateNeural[tile][(y + 7) * 64 + x + 5] = n;
					}
				}
			}
			auto baseline = StorePixels(device, context, copy, cb, source, format);
			auto neural = MakeTexture(device, edits);
			auto prepared = MakeTexture(device, preparation);
			for (float preservation : { 0.f, 0.5f, 1.f }) {
				for (float appearance : { 0.f, 0.25f, 1.f }) {
					Constants c;
					c.width = c.height = 40;
					c.mode = 2;
					c.bypass = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 0x200u :
					           format == DXGI_FORMAT_R11G11B10_FLOAT    ? 0x100u :
					           format == DXGI_FORMAT_R8G8B8A8_UNORM     ? 0x300u :
					                                                      0u;
					c.lightingPreservation = preservation;
					c.appearance = appearance;
					c.regionCount = 2;
					for (unsigned i = 0; i < 2; ++i)
						std::copy(regions[i].begin(), regions[i].end(), c.regions[i]);
					auto output = StorePixels(device, context, copy, cb, empty, format);
					Dispatch(context, reconstruct, cb, c, { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, output.uav.Get());
					const auto atlas = ReadStored(device, context, copy, cb, output);
					Require(std::abs(atlas[11 * 64 + 8].r - source[11 * 64 + 8].r) > 0.001f,
						"Packed fixture must retain a measurable neural edit");
					for (unsigned tile = 0; tile < 2; ++tile) {
						const auto& r = regions[tile];
						c.width = r[2];
						c.height = r[3];
						c.regionCount = 0;
						auto b = StorePixels(device, context, copy, cb, privateSource[tile], format);
						auto n = MakeTexture(device, privateNeural[tile]);
						auto p = MakeTexture(device, privatePrepared[tile]);
						auto result = StorePixels(device, context, copy, cb, empty, format);
						Dispatch(context, reconstruct, cb, c, { b.srv.Get(), n.srv.Get(), p.srv.Get() }, result.uav.Get());
						const auto isolated = ReadStored(device, context, copy, cb, result);
						for (unsigned y = 0; y < r[3]; ++y)
							for (unsigned x = 0; x < r[2]; ++x) {
								const auto& actual = atlas[(y + r[1]) * 64 + x + r[0]];
								const auto& expected = isolated[y * 64 + x];
								Require(std::memcmp(&actual, &expected, sizeof(Pixel)) == 0,
									"Packed lighting reconstruction differs from its isolated crop");
							}
					}
					if (appearance < 1.f) {
						const auto stored = ReadStored(device, context, copy, cb, baseline);
						Require(std::memcmp(&atlas[0], &stored[0], sizeof(Pixel)) == 0,
							"Unowned atlas padding changed during lighting preservation");
					}
				}
			}
		}
	}
}
