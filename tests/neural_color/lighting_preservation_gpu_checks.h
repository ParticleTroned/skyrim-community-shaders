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
					 "zero", "limit_zero", "bounded", "hidden", "appearance", "transport", "invalid", "near_black", "range" }) {
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
				auto activeBaseline = StorePixels(device, context, copy, cb, source, format);
				std::vector<Pixel> edited(4096);
				for (unsigned i = 0; i < edited.size(); ++i) {
					const auto gain = std::exp2(residual(i % 64, i / 64));
					edited[i] = { encode(decode(source[i].r) * gain), encode(decode(source[i].g) * gain),
						encode(decode(source[i].b) * gain), 0 };
					if (signal == "invalid")
						edited[i].r = std::numeric_limits<float>::quiet_NaN();
					if (signal == "range")
						edited[i] = { 70000, 70000, 70000, 0 };
				}
				auto neural = MakeTexture(device, edited);
				for (float preservation : { 1.f, 0.5f, 0.f }) {
					auto constants = FullTextureConstants();
					constants.width = constants.height = 32;
					constants.mode = 2;
					constants.domain = domain;
					constants.bypass = flags[formatIndex] | (signal == "hidden" ? 2u : signal == "transport" ? 1u :
																											   0u);
					constants.detail = signal == "zero" ? 0.f : signal == "constant" ? 0.75f :
					                                                                   1.f;
					constants.maximumStops = signal == "limit_zero" ? 0.f : signal == "bounded" ? 0.125f :
					                                                                              2.f;
					constants.appearance = signal == "appearance" ? 1.f : 0.f;
					constants.lightingPreservation = preservation;
					auto output = StorePixels(device, context, copy, cb, source, format);
					const std::array inputs{ activeBaseline.srv.Get(), neural.srv.Get(), activeBaseline.srv.Get() };
					Dispatch(context, reconstruct, cb, constants, inputs, output.uav.Get());
					const auto actual = ReadStored(device, context, copy, cb, output);
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
							const bool exact = outside || signal == "zero" || signal == "hidden" || signal == "near_black";
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
							const bool candidate = signal == "appearance" || signal == "transport";
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
								// Preserve Source explicitly rounds packed stores; candidate endpoints
								// keep their existing driver conversion, whose error is at most one ULP.
								if (formatIndex == 2 && !candidate)
									expected = NearestPacked(expected, channel == 2 ? blueValues : redValues);
								const float tolerance = 3e-6f + (formatIndex == 0 ? 0.f : formatIndex == 1 ? expected / 1024.f :
																					  formatIndex == 2     ? expected / (channel == 2 ? 32.f : 64.f) :
																											 1.f / 255.f);
								Require(std::abs(stored[channel] - expected) <= tolerance, "analytic uniform/ramp/mixed lighting gain with storage error");
								const float workingError = domain == 2 ? 2.5f * tolerance : tolerance;
								Require(std::abs(decode(stored[channel]) / decode(input[channel]) - gain) <= workingError / decode(input[channel]) + 1e-4f,
									"common RGB gain is retained in the selected working domain");
							}
						}
					}
				}
			}
		}
	}
	std::printf("Lighting preservation: %u bit-exact pre-change comparisons; FP32/FP16/R11G11B10/UNORM and all domains\n", comparisons);
}
