// Uses the production shader and texture fixtures from nr_model_resolution_shader_test.cpp.
inline constexpr std::array<std::uint32_t, 8> kNonFiniteBits{
	0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u,
	0x7F800001u, 0xFF800001u, 0x7FFFFFFFu, 0xFFFFFFFFu
};

void CheckFiniteClassification(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
{
	using Bits = std::array<std::uint32_t, 4>;
	constexpr std::array finiteValues{
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x007FFFFFu, 0x807FFFFFu, 0x00800000u, 0x80800000u,
		0x7F7FFFFFu, 0xFF7FFFFFu, 0x477FE000u, 0xC77FE000u
	};
	std::array<std::uint32_t, finiteValues.size() + kNonFiniteBits.size()> values{};
	std::ranges::copy(finiteValues, values.begin());
	std::ranges::copy(kNonFiniteBits, values.begin() + finiteValues.size());
	constexpr Size source{ UINT(values.size()), 1 }, target{ source[0], 4 };
	std::vector<Bits> inputs(source[0]);
	for (std::size_t x = 0; x < inputs.size(); ++x)
		for (std::size_t c = 0; c < 4; ++c)
			inputs[x][c] = values[(x + c * 3) % values.size()];
	Texture input(device, source, DXGI_FORMAT_R32G32B32A32_UINT, inputs);
	Texture result(device, target, DXGI_FORMAT_R32G32B32A32_UINT, std::vector<Bits>(target[0] * target[1]));
	Shader shader(device, L"tests/shaders/NeuralRenderingFiniteCS.hlsl", vr);
	auto* srv = input.srv.Get();
	auto* uav = result.uav.Get();
	context->CSSetShader(shader.shader.Get(), nullptr, 0);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->Dispatch((source[0] + 7) / 8, 1, 1);
	context->ClearState();
	const auto actual = result.Read<Bits>(device, context);
	for (UINT row = 0; row < target[1]; ++row)
		for (UINT x = 0; x < source[0]; ++x)
			for (UINT c = 0; c < 4; ++c) {
				const auto value = std::bit_cast<float>(inputs[x][c]);
				Require(actual[row * target[0] + x][c] == UINT(std::isfinite(value)),
					"Optimized scalar/vector finite classification rejected a boundary or accepted a non-finite value");
			}
}

void CheckRejectedReconstruction(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader,
	const Pixel& source, const Pixel& prepared, const Pixel& neural, bool selected = false, bool compressed = false)
{
	constexpr Size size{ 2, 2 };
	Texture input(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, source));
	Texture before(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, prepared));
	Texture after(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, neural));
	Texture result(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, sentinel));
	ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
	Configure(constants, size, size, { {}, size }, { {}, size }, false, { 1, 1 });
	std::optional<Texture> mask;
	if (selected) {
		mask.emplace(device, size, DXGI_FORMAT_R32_FLOAT, std::vector(4, 0.5f));
		constants.SetVariable("SelectedOutput", 1u);
		constants.SetVariable("SelectionMode", 1u);
		constants.SetVariable("OutputStorageFormat", UINT(DXGI_FORMAT_R32G32B32A32_FLOAT));
		constants.SetVariable("SelectionBounds", Pixel{ 0.0f, 0.0f, 1.0f, 1.0f });
	}
	std::optional<ConstantBuffer> peripheral;
	if (compressed) {
		peripheral.emplace(device, shader.reflection.Get(), "PeripheralMappingConstants");
		peripheral->SetVariable("PeripheralCenter", Motion{ 0.5f, 0.5f });
		peripheral->SetVariable("PeripheralRadii", Motion{ 0.5f, 0.5f });
		peripheral->SetVariable("PeripheralOrigin", Motion{});
		peripheral->SetVariable("PeripheralWork", Motion{ 0.75f, 0.75f });
		peripheral->SetVariable("PeripheralProtected", 0.5f);
		peripheral->SetVariable("PeripheralBoundaryScale", 0.75f);
		peripheral->SetVariable("PeripheralEdgeSlope", 0.25f);
	}
	const std::array inputs{ input.srv.Get(), before.srv.Get(), after.srv.Get(), static_cast<ID3D11ShaderResourceView*>(nullptr), mask ? mask->srv.Get() : nullptr };
	const std::array outputs{ result.uav.Get() };
	for (UINT format : { 0u, 1u, 2u, 3u }) {
		constants.SetVariable("OutputFormat", format);
		if (peripheral)
			peripheral->Bind(context);
		Dispatch(context, shader, constants, size, inputs, outputs);
		for (const auto& pixel : result.Read<Pixel>(device, context))
			Require(std::bit_cast<std::array<std::uint32_t, 4>>(pixel) == std::bit_cast<std::array<std::uint32_t, 4>>(source),
				"Invalid source RGBA, model RGB or arithmetic overflow changed the source under an optimized storage clamp");
	}
}

void CheckNonFiniteReconstruction(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader)
{
	constexpr Pixel original{ 0.25f, 0.5f, 0.75f, 0.375f }, proxy{ 0.125f, 0.25f, 0.5f, 1.0f };
	constexpr Pixel edited{ 0.25f, 0.375f, 0.625f, 1.0f };
	for (const auto bits : kNonFiniteBits)
		for (unsigned lane = 0; lane < 3; ++lane)
			for (unsigned channel = 0; channel < (lane == 0 ? 4u : 3u); ++channel) {
				auto source = original, prepared = proxy, neural = edited;
				auto* selected = lane == 0 ? &source : lane == 1 ? &prepared :
				                                                   &neural;
				(*selected)[channel] = std::bit_cast<float>(bits);
				CheckRejectedReconstruction(device, context, shader, source, prepared, neural);
			}
	for (const auto sign : { -1.0f, 1.0f })
		for (unsigned channel = 0; channel < 3; ++channel) {
			const float maximum = sign * std::numeric_limits<float>::max();
			auto source = original, prepared = proxy, neural = edited;
			prepared[channel] = -maximum;
			neural[channel] = maximum;
			CheckRejectedReconstruction(device, context, shader, source, prepared, neural);
			source[channel] = maximum;
			prepared[channel] = 0.0f;
			CheckRejectedReconstruction(device, context, shader, source, prepared, neural);
		}
}

void CheckHalfReconstructionBoundaries(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader)
{
	constexpr Size size{ 2, 2 };
	constexpr Pixel source{ 0.0f, 0.0f, 0.0f, 0.375f };
	const std::array values{ 65504.0f, std::nextafter(65504.0f, 0.0f), std::nextafter(65504.0f, 70000.0f),
		65520.0f, 1e20f, std::numeric_limits<float>::max() };
	for (const auto magnitude : values)
		for (const float sign : { -1.0f, 1.0f }) {
			const auto value = sign * magnitude;
			Texture input(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, source));
			Texture before(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, Pixel{}));
			Texture after(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, Pixel{ value, value, value, 1.0f }));
			Texture result(device, size, DXGI_FORMAT_R16G16B16A16_FLOAT, std::vector<std::array<std::uint16_t, 4>>(4));
			ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
			Configure(constants, size, size, { {}, size }, { {}, size }, false, { 1, 1 });
			constants.SetVariable("OutputFormat", 1u);
			const std::array inputs{ input.srv.Get(), before.srv.Get(), after.srv.Get() };
			const std::array outputs{ result.uav.Get() };
			Dispatch(context, shader, constants, size, inputs, outputs);
			const float expected = std::clamp(value, -65504.0f, 65504.0f);
			const auto nearest = DirectX::PackedVector::XMConvertFloatToHalf(expected);
			const auto truncated = std::uint16_t(nearest - (std::abs(DirectX::PackedVector::XMConvertHalfToFloat(nearest)) > std::abs(expected) ? 1u : 0u));
			for (const auto& pixel : result.Read<std::array<std::uint16_t, 4>>(device, context)) {
				for (unsigned channel = 0; channel < 3; ++channel) {
					// Accept only the two adjacent encodings allowed by nearest/truncating stores.
					Require(pixel[channel] == nearest || pixel[channel] == truncated,
						"Finite half-storage boundary overflowed or was rejected instead of saturating");
				}
				Require(DirectX::PackedVector::XMConvertHalfToFloat(pixel[3]) == source[3], "Half reconstruction changed source alpha");
			}
			Texture candidate(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, sentinel));
			const std::array candidateOutput{ candidate.uav.Get() };
			Dispatch(context, shader, constants, size, inputs, candidateOutput);
			for (const auto& pixel : candidate.Read<Pixel>(device, context))
				Require(pixel == Pixel{ expected, expected, expected, source[3] }, "Reconstruction changed the finite clamp before storage conversion");
		}
}

void CheckPreparationOverflow(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader)
{
	constexpr Size source{ 4, 4 }, model{ 2, 2 };
	const float maximum = std::numeric_limits<float>::max();
	Texture color(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(16, Pixel{ maximum, maximum, maximum, 1.0f }));
	Texture depth(device, source, DXGI_FORMAT_R32_FLOAT, std::vector(16, 0.5f));
	Texture motion(device, source, DXGI_FORMAT_R32G32_FLOAT, std::vector(16, Motion{ maximum, -maximum }));
	Texture prepared(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(4, sentinel));
	Texture preparedDepth(device, model, DXGI_FORMAT_R32_FLOAT, std::vector(4, -1.0f));
	Texture preparedMotion(device, model, DXGI_FORMAT_R32G32_FLOAT, std::vector(4, Motion{ -1.0f, -1.0f }));
	ConstantBuffer constants(device, shader.reflection.Get(), "ModelResolutionConstants");
	Configure(constants, source, model, { {}, source }, { {}, model }, false, { 2.0f, 3.0f });
	const std::array inputs{ color.srv.Get(), depth.srv.Get(), motion.srv.Get() };
	const std::array outputs{ prepared.uav.Get(), preparedDepth.uav.Get(), preparedMotion.uav.Get() };
	Dispatch(context, shader, constants, model, inputs, outputs);
	for (const auto& pixel : prepared.Read<Pixel>(device, context))
		Require(pixel == Pixel{}, "Finite preparation inputs overflowed the area accumulator without a safe fallback");
	for (const auto& pixel : preparedMotion.Read<Motion>(device, context))
		Require(pixel == Motion{}, "Finite source motion overflowed native normalization without a safe fallback");
	for (const auto pixel : preparedDepth.Read<float>(device, context))
		Require(pixel == 0.5f, "Motion overflow changed the selected depth surface");
	using HalfMotion = std::array<std::uint16_t, 2>;
	Texture halfMotion(device, source, DXGI_FORMAT_R16G16_FLOAT, std::vector(16, HalfMotion{ 0x7BFFu, 0xFBFFu }));
	Texture preparedHalfMotion(device, model, DXGI_FORMAT_R16G16_FLOAT, std::vector<HalfMotion>(4));
	const std::array halfInputs{ color.srv.Get(), depth.srv.Get(), halfMotion.srv.Get() };
	const std::array halfOutputs{ prepared.uav.Get(), preparedDepth.uav.Get(), preparedHalfMotion.uav.Get() };
	Dispatch(context, shader, constants, model, halfInputs, halfOutputs);
	for (const auto& pixel : preparedHalfMotion.Read<HalfMotion>(device, context))
		Require(pixel == HalfMotion{}, "Finite normalized motion overflowed its physical half-float carrier");
}

void CheckFusedNonFiniteReconstruction(ID3D11Device* device, ID3D11DeviceContext* context, bool vr)
{
	constexpr Pixel source{ 0.25f, 0.5f, 0.75f, 0.375f }, prepared{ 0.125f, 0.25f, 0.5f, 1.0f };
	constexpr Pixel neural{ 0.25f, 0.375f, 0.625f, 1.0f };
	for (const bool compressed : { false, true }) {
		std::vector<D3D_SHADER_MACRO> defines{ { "MODEL_OUTPUT_SELECTION", "1" } };
		if (compressed)
			defines.push_back({ "NR_PERIPHERAL_COMPRESSION", "1" });
		Shader shader(device, L"features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl",
			vr, "features/Neural Rendering/Shaders", false, false, defines);
		for (const auto bits : kNonFiniteBits) {
			auto invalidSource = source, invalidNeural = neural;
			invalidSource[3] = invalidNeural[0] = std::bit_cast<float>(bits);
			CheckRejectedReconstruction(device, context, shader, invalidSource, prepared, neural, true, compressed);
			CheckRejectedReconstruction(device, context, shader, source, prepared, invalidNeural, true, compressed);
		}
	}
}
