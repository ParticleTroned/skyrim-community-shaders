void RunGrassCollisionStrength()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"tests/shaders/GrassCollisionStrengthTestCS.hlsl", "cs_5_0");
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::CollisionStrengthCS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	D3D11ShaderTest::ConstantBuffer collision(device.Get(), reflection.Get(), "GrassCollision::GrassCollisionPerFrame");
	Require(collision.slot == 5 && collision.bytes.size() == 48 && collision.Offset("GrassCollision::CollisionStrength") == 36 &&
				collision.Offset("GrassCollision::PreviousCollisionStrength") == 40,
		"Collision strength layout differs from the CPU buffer");
	D3D11ShaderTest::ConstantBuffer geometry(device.Get(), reflection.Get(), "NativeWorld");
	std::array<float, 16> identity{};
	for (size_t i = 0; i < 4; ++i)
		identity[i * 5] = 1;
	geometry.SetVariable("World", identity);
	geometry.Bind(context.Get());
	struct Case
	{
		std::array<float, 4> color{ 1, 1, 1, 1 };
		std::array<float, 4> root{ 0, 0, -60, 1 };
		std::array<float, 4> position{ 0, 0, -30, 1 };
		std::array<float, 4> distance{ 2048, 0, 0, 0 };
	};
	static_assert(sizeof(Case) == 64);
	std::array<Case, 8> cases{};
	cases[1].color[3] = 0;
	cases[2].distance[0] = 0;
	cases[3].position[0] = 4096;
	cases[4].position = cases[4].root;
	cases[5].position[2] = -59.75f;
	cases[6].position[2] = -61;
	cases[7].color[3] = .05f;
	const auto makeBuffer = [&](UINT bytes, UINT stride, UINT bind, const void* initial, const char* name) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.StructureByteStride = stride;
		desc.BindFlags = bind;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		D3D11_SUBRESOURCE_DATA data{ initial, 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&desc, initial ? &data : nullptr, buffer.GetAddressOf()));
		Util::SetResourceName(buffer.Get(), "%s", name);
		return buffer;
	};
	auto input = makeBuffer(sizeof(cases), sizeof(Case), D3D11_BIND_SHADER_RESOURCE, cases.data(), "GrassTest::CollisionStrengthCases");
	auto output = makeBuffer(UINT(cases.size() * 32), 32, D3D11_BIND_UNORDERED_ACCESS, nullptr, "GrassTest::CollisionStrengthResults");
	ComPtr<ID3D11ShaderResourceView> inputs;
	ComPtr<ID3D11UnorderedAccessView> results;
	Check(device->CreateShaderResourceView(input.Get(), nullptr, inputs.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(output.Get(), nullptr, results.GetAddressOf()));
	Util::SetResourceName(inputs.Get(), "GrassTest::CollisionStrengthCases SRV");
	Util::SetResourceName(results.Get(), "GrassTest::CollisionStrengthResults UAV");
	std::vector<DirectX::PackedVector::HALF> pixels(512 * 512 * 4,
		DirectX::PackedVector::XMConvertFloatToHalf(.515625f));
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = textureDesc.Height = 512;
	textureDesc.MipLevels = textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA textureData{ pixels.data(), 512 * 8, 0 };
	ComPtr<ID3D11Texture2D> texture;
	Check(device->CreateTexture2D(&textureDesc, &textureData, texture.GetAddressOf()));
	Util::SetResourceName(texture.Get(), "GrassTest::CollisionStrengthField");
	ComPtr<ID3D11ShaderResourceView> field;
	Check(device->CreateShaderResourceView(texture.Get(), nullptr, field.GetAddressOf()));
	Util::SetResourceName(field.Get(), "GrassTest::CollisionStrengthField SRV");
	D3D11_BUFFER_DESC stagingDesc{};
	output->GetDesc(&stagingDesc);
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = stagingDesc.MiscFlags = stagingDesc.StructureByteStride = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Buffer> staging;
	Check(device->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::CollisionStrengthReadback");
	const auto dispatch = [&](float current, float previous) {
		collision.SetVariable("GrassCollision::CollisionStrength", current);
		collision.SetVariable("GrassCollision::PreviousCollisionStrength", previous);
		collision.Bind(context.Get());
		ID3D11ShaderResourceView* inputView = inputs.Get();
		ID3D11ShaderResourceView* fieldView = field.Get();
		ID3D11UnorderedAccessView* outputView = results.Get();
		context->CSSetShaderResources(0, 1, &inputView);
		context->CSSetShaderResources(100, 1, &fieldView);
		context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(UINT(cases.size()), 1, 1);
		outputView = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<std::array<float, 8>, cases.size()> values{};
		std::memcpy(values.data(), mapped.pData, sizeof(values));
		context->Unmap(staging.Get(), 0);
		return values;
	};
	const auto verifyStrengths = [&] {
		const auto baseline = dispatch(1, 1);
		const std::array<std::array<float, 2>, 7> strengths{ { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 2 }, { 5, 3 }, { 0, 5 }, { 1, 0 } } };
		for (const auto& strength : strengths) {
			const auto values = dispatch(strength[0], strength[1]);
			for (size_t sample = 0; sample < values.size(); ++sample) {
				const float minimumZ = -(std::max)(0.0f, cases[sample].position[2] - cases[sample].root[2]);
				for (size_t axis = 0; axis < 3; ++axis) {
					Require(std::isfinite(values[sample][axis]) && std::isfinite(values[sample][axis + 4]),
						"Collision strength produced nonfinite displacement");
					const float current = baseline[sample][axis] * strength[0];
					const float previous = baseline[sample][axis + 4] * strength[1];
					Require(std::abs(values[sample][axis] - (axis == 2 ? (std::max)(current, minimumZ) : current)) < .001 &&
								std::abs(values[sample][axis + 4] - (axis == 2 ? (std::max)(previous, minimumZ) : previous)) < .001,
						"Current and previous collision displacement do not track their own strength");
				}
				Require(values[sample][2] >= minimumZ - .001f && values[sample][6] >= minimumZ - .001f,
					"Collision strength moved a vertex below its root");
				if ((sample >= 1 && sample <= 4) || sample == 6)
					Require(values[sample][0] == 0 && values[sample][1] == 0 && values[sample][2] == 0 &&
								values[sample][4] == 0 && values[sample][5] == 0 && values[sample][6] == 0,
						"Strength bypassed grass root, alpha, distance or disabled-collision gates");
			}
		}
		return baseline;
	};
	const auto verifyPlaneNormals = [](const std::array<float, 8>& sample, std::array<float, 2> currentSlope, std::array<float, 2> previousSlope) {
		const std::array<std::array<float, 2>, 2> slopes{ currentSlope, previousSlope };
		for (size_t history = 0; history < slopes.size(); ++history) {
			const auto& slope = slopes[history];
			const std::array<float, 3> expected{ slope[0], slope[1], -.1f };
			const float expectedLength = std::hypot(expected[0], expected[1], expected[2]);
			const size_t first = history * 4;
			const float actualLength = std::hypot(sample[first], sample[first + 1], sample[first + 2]);
			Require(std::isfinite(actualLength) && actualLength > .001f, "Planar collision field produced no finite response");
			for (size_t axis = 0; axis < expected.size(); ++axis)
				Require(std::abs(sample[first + axis] / actualLength - expected[axis] / expectedLength) < .001f,
					"Collision normal differs from the analytic field gradient");
		}
	};
	const auto setPlaneFields = [&](std::array<float, 2> currentStep, std::array<float, 2> previousStep) {
		for (UINT y = 0; y < 512; ++y)
			for (UINT x = 0; x < 512; ++x) {
				const float currentHeight = -64 + (float(x) - 256) * currentStep[0] + (float(y) - 256) * currentStep[1];
				const float previousHeight = -72 + (float(x) - 256) * previousStep[0] + (float(y) - 256) * previousStep[1];
				const auto current = DirectX::PackedVector::XMConvertFloatToHalf((2048 - currentHeight) / 4096);
				const auto previous = DirectX::PackedVector::XMConvertFloatToHalf((2048 - previousHeight) / 4096);
				const size_t pixel = (size_t(y) * 512 + x) * 4;
				pixels[pixel] = pixels[pixel + 1] = current;
				pixels[pixel + 2] = pixels[pixel + 3] = previous;
			}
		context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), 512 * 8, 0);
	};
	const auto baseline = verifyStrengths();
	const double expected = -.75 * 30 * (1 - std::pow(30.0 / 2048, 2) * (3 - 2 * 30.0 / 2048));
	Require(std::abs(baseline[0][2] - expected) < .001 && baseline[0][2] == baseline[0][6],
		"1x collision strength differs from the original deformation");
	verifyPlaneNormals(baseline[0], { 0, 0 }, { 0, 0 });
	setPlaneFields({ 4, 0 }, { 0, 2 });
	const auto sloped = verifyStrengths();
	verifyPlaneNormals(sloped[0], { .5f, 0 }, { 0, .25f });
	Require(std::abs(sloped[0][0]) > .01 && std::abs(sloped[0][5]) > .01 &&
				std::abs(sloped[0][0] - sloped[0][4]) > .01,
		"Sloped fields did not exercise distinct lateral collision histories");
	setPlaneFields({ 4, 2 }, { -2, 4 });
	const auto diagonal = verifyStrengths();
	verifyPlaneNormals(diagonal[0], { .5f, .25f }, { -.25f, .5f });
	setPlaneFields({ 4, 0 }, { 0, 2 });
	identity[3] = 32;
	identity[7] = -16;
	identity[11] = 12;
	geometry.SetVariable("World", identity);
	geometry.Bind(context.Get());
	verifyStrengths();
	std::fill(pixels.begin(), pixels.end(), DirectX::PackedVector::HALF(0));
	context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), 512 * 8, 0);
	const auto empty = dispatch(5, 5);
	for (const auto& sample : empty)
		for (float value : sample)
			Require(value == 0, "Strength introduced bending without a collision field");
}
