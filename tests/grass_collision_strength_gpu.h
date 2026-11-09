void RunGrassCollisionStrength(bool basic = false)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	std::vector<D3D_SHADER_MACRO> defines;
	if (basic)
		defines.push_back({ "GRASS_COLLISION_BASIC", "1" });
	auto code = Compile(L"tests/shaders/GrassCollisionStrengthTestCS.hlsl", "cs_5_0", defines);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::CollisionStrengthCS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	D3D11ShaderTest::ConstantBuffer collision(device.Get(), reflection.Get(), "GrassCollision::GrassCollisionPerFrame");
	Require(collision.slot == 5 && collision.bytes.size() == 80 && collision.Offset("GrassCollision::CollisionStrength") == 36 &&
				collision.Offset("GrassCollision::PreviousCollisionStrength") == 40 && collision.Offset("GrassCollision::CurrentHeightOffset") == 64,
		"Collision strength layout differs from the CPU buffer");
	D3D11ShaderTest::ConstantBuffer geometry(device.Get(), reflection.Get(), "NativeWorld");
	std::array<float, 16> identity{};
	for (size_t i = 0; i < 4; ++i)
		identity[i * 5] = 1;
	geometry.SetVariable("World", identity);
	geometry.SetVariable("PreviousWorld", identity);
	geometry.Bind(context.Get());
	struct Case
	{
		std::array<float, 4> color{ 1, 1, 1, 1 };
		std::array<float, 4> root{ 0, 0, 0, 1 };
		std::array<float, 4> position{ 0, 0, 30, 1 };
		std::array<float, 4> distance{ 2048, 0, 0, 1 };
	};
	static_assert(sizeof(Case) == 64);
	std::array<Case, 11> cases{};
	cases[1].color[3] = 0;
	cases[2].distance[0] = 0;
	cases[3].root[0] = cases[3].position[0] = 8192;
	cases[4].position = cases[4].root;
	cases[5].position[2] = .25f;
	cases[6].position[2] = -1;
	cases[7].color[3] = .05f;
	cases[8].root[2] = 256;
	cases[8].position[2] = 286;
	cases[9].root[2] = -256;
	cases[9].position[2] = -226;
	cases[10].distance[1] = std::sqrt(.75f);
	cases[10].distance[3] = .5f;
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
	const auto fieldTexture = [&](std::array<float, 4> value, const char* name, bool splitHeights = false) {
		const UINT fieldSize = basic ? 512 : 1024;
		std::vector<DirectX::PackedVector::HALF> pixels(size_t(fieldSize) * fieldSize * 4);
		for (size_t i = 0; i < pixels.size(); ++i) {
			const auto channel = i % 4;
			const float component = splitHeights && channel >= 2 ?
			                            ((i / 4) % fieldSize < fieldSize / 2 ? -256.0f : 256.0f) + (channel == 2 ? -16.0f : 16.0f) :
			                            value[channel];
			pixels[i] = DirectX::PackedVector::XMConvertFloatToHalf(component);
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = fieldSize;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initial{ pixels.data(), fieldSize * 8, 0 };
		ComPtr<ID3D11Texture2D> texture;
		Check(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()));
		Util::SetResourceName(texture.Get(), "%s", name);
		ComPtr<ID3D11ShaderResourceView> view;
		Check(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
		Util::SetResourceName(view.Get(), "%s SRV", name);
		return view;
	};
	auto currentField = fieldTexture(basic ? std::array<float, 4>{ .5f, .5f, .49609375f, .49609375f } : std::array<float, 4>{ .25f, 0, .25f, 1 }, "GrassTest::CurrentField");
	auto previousField = fieldTexture({ 0, .125f, .125f, 1 }, "GrassTest::PreviousField");
	auto currentMotion = fieldTexture({ 0, 0, -16, 16 }, "GrassTest::CurrentContact");
	auto previousMotion = currentMotion;
	collision.SetVariable("GrassCollision::CompressionHeight", .1f);
	collision.SetVariable("GrassCollision::PreviousCompressionHeight", .1f);
	collision.SetVariable("GrassCollision::MaximumCompressibleGrassHeight", 40.0f);
	collision.SetVariable("GrassCollision::PreviousMaximumCompressibleGrassHeight", 40.0f);
	collision.SetVariable("GrassCollision::MaximumBend", 1.0f);
	collision.SetVariable("GrassCollision::PreviousMaximumBend", 1.0f);

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
		ID3D11ShaderResourceView* fields[]{ currentField.Get(), previousField.Get(), currentMotion.Get(), previousMotion.Get() };
		ID3D11UnorderedAccessView* outputView = results.Get();
		context->CSSetShaderResources(0, 1, &inputView);
		context->CSSetShaderResources(100, 4, fields);
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
	for (const auto strengths : { std::array<float, 2>{ 0, 1 }, { 1, 0 }, { 1, 1 }, { 2, 3 }, { 5, 5 } }) {
		const auto values = dispatch(strengths[0], strengths[1]);
		for (size_t i = 0; i < values.size(); ++i) {
			for (const float value : values[i])
				Require(std::isfinite(value), "Nonfinite collision vertex output");
			Require(values[i][2] >= -std::max(0.0f, cases[i].position[2] - cases[i].root[2]) - .001f &&
						values[i][6] >= -std::max(0.0f, cases[i].position[2] - cases[i].root[2]) - .001f,
				"Collision crossed its root");
			if ((i >= 1 && i <= 4) || i == 6 || (!basic && (i == 8 || i == 9)))
				for (size_t axis : { 0u, 1u, 2u, 4u, 5u, 6u })
					Require(values[i][axis] == 0, "Root, alpha or distance gate failed");
		}
		if (basic) {
			const float fadePosition = 1 - 30.0f / 2048;
			const float fade = fadePosition * fadePosition * (3 - 2 * fadePosition);
			Require(std::abs(values[0][2] - std::max(-30.0f, -30 * .75f * fade * strengths[0])) < .001f &&
						std::abs(values[0][6] - std::max(-30.0f, -14 * .75f * fade * strengths[1])) < .001f,
				"Basic height response, previous field or strength changed");
			Require(values[0][0] == 0 && values[0][1] == 0 && values[0][3] == 1, "Uniform Basic field changed lateral position or normal");
			continue;
		}
		// Uniform orthogonal fields give independent analytic current/previous rotations.
		const float angle = std::min(.25f * strengths[0], 1.0f);
		const float previousAngle = std::min(.125f * strengths[1], 1.0f);
		const float scale = 1 - std::min(.25f * strengths[0], 1.0f);
		const float previousScale = 1 - std::min(.125f * strengths[1], 1.0f);
		Require(std::abs(values[0][0] - 30 * std::sin(angle)) < .001f && std::abs(values[0][1]) < .001f &&
					std::abs(values[0][2] - (30 * std::cos(angle) * scale - 30)) < .001f,
			"Current bend/compression differs from analytic rotation");
		Require(std::abs(values[0][5] - 30 * std::sin(previousAngle)) < .001f && std::abs(values[0][4]) < .001f &&
					std::abs(values[0][6] - (30 * std::cos(previousAngle) * previousScale - 30)) < .001f,
			"Previous field or strength was not preserved");
		const float normalZ = std::cos(angle) / std::max(scale, 1e-4f);
		Require(std::abs(values[0][3] - normalZ / std::hypot(std::sin(angle), normalZ)) < .001f, "Deformed normal did not follow rotation/compression");
	}
	if (!basic) {
		currentMotion = fieldTexture({ 0, 0, 240, 272 }, "GrassTest::BridgeContact");
		const auto bridge = dispatch(1, 1);
		Require(bridge[0][0] == 0 && bridge[0][2] == 0 && bridge[0][5] > 0 &&
					bridge[8][0] > 0 && bridge[8][5] == 0 && bridge[9][0] == 0 && bridge[7][0] == 0 && bridge[7][2] == 0,
			"Current and previous contacts affected grass on another floor");
		currentMotion = fieldTexture({ 0, 0, 16, 48 }, "GrassTest::TipContact");
		const auto tips = dispatch(1, 1);
		Require(tips[0][0] > 0 && tips[10][0] == 0, "Contact did not follow the actual vertical reach of a tilted blade");
		currentMotion = fieldTexture({ 0, 0, -32, 0 }, "GrassTest::FootContact");
		Require(dispatch(1, 1)[0][0] > 0, "A foot surface touching the grass root lost contact");
		currentMotion = fieldTexture({ 0, 0, 8160, 8200 }, "GrassTest::HighRootContact");
		collision.SetVariable("GrassCollision::CurrentHeightOffset", 8195.0f);
		Require(dispatch(1, 1)[0][0] > 0, "Half-float contact storage rejected a nonrepresentable high root");
		currentMotion = fieldTexture({ 0, 0, 8216, 8256 }, "GrassTest::HighTipContact");
		collision.SetVariable("GrassCollision::CurrentHeightOffset", 8191.5f);
		Require(dispatch(1, 1)[0][0] > 0, "Half-float contact storage rejected a marginal high tip contact");
		collision.SetVariable("GrassCollision::CurrentHeightOffset", 0.0f);
		currentMotion = fieldTexture({}, "GrassTest::AdjacentHeightContacts", true);
		previousMotion = currentMotion;
		const auto adjacent = dispatch(1, 1);
		Require(adjacent[0][0] == 0 && adjacent[0][2] == 0 && adjacent[0][5] == 0 && adjacent[0][6] == 0 &&
					adjacent[8][0] > 0 && adjacent[9][0] > 0,
			"Filtering adjacent floors invented a contact in the vertical gap");
		auto currentWorld = identity, previousWorld = identity;
		currentWorld[11] = -64;
		previousWorld[11] = -32;
		geometry.SetVariable("World", currentWorld);
		geometry.SetVariable("PreviousWorld", previousWorld);
		geometry.Bind(context.Get());
		currentMotion = fieldTexture({ 0, 0, -16, 16 }, "GrassTest::AnchoredContact");
		previousMotion = currentMotion;
		collision.SetVariable("GrassCollision::CurrentHeightOffset", 64.0f);
		collision.SetVariable("GrassCollision::PreviousHeightOffset", 32.0f);
		const auto reprojected = dispatch(1, 1);
		const float currentFadePosition = 1 - 64.0f / 2048, previousFadePosition = 1 - 32.0f / 2048;
		const float currentFade = currentFadePosition * currentFadePosition * (3 - 2 * currentFadePosition);
		const float previousFade = previousFadePosition * previousFadePosition * (3 - 2 * previousFadePosition);
		Require(std::abs(reprojected[0][0] - 30 * std::sin(.25f * currentFade)) < .001f &&
					std::abs(reprojected[0][5] - 30 * std::sin(.125f * previousFade)) < .001f,
			"Vertical camera movement used the wrong contact history for a motion-vector endpoint");
		cases[0].distance[0] = 4096;
		context->UpdateSubresource(input.Get(), 0, nullptr, cases.data(), 0, 0);
		for (unsigned frame = 1; frame <= 100; ++frame) {
			const float height = 2048 + frame * .25f, previousHeight = height - .25f;
			currentWorld[11] = -height;
			previousWorld[11] = -previousHeight;
			geometry.SetVariable("World", currentWorld);
			geometry.SetVariable("PreviousWorld", previousWorld);
			geometry.Bind(context.Get());
			collision.SetVariable("GrassCollision::CurrentHeightOffset", height);
			collision.SetVariable("GrassCollision::PreviousHeightOffset", previousHeight);
			const auto fractional = dispatch(1, 1);
			const float factor = 1 - height / 4096, previousFactor = 1 - previousHeight / 4096;
			const float fade = factor * factor * (3 - 2 * factor), oldFade = previousFactor * previousFactor * (3 - 2 * previousFactor);
			Require(std::abs(fractional[0][0] - 30 * std::sin(.25f * fade)) < .001f &&
						std::abs(fractional[0][5] - 30 * std::sin(.125f * oldFade)) < .001f,
				"Repeated fractional camera motion drifted away from a stationary contact layer");
		}
	}
	currentField = fieldTexture({ 0, 0, 0, 0 }, "GrassTest::EmptyField");
	previousField = currentField;
	const auto empty = dispatch(5, 5);
	for (const auto& sample : empty)
		for (size_t axis : { 0u, 1u, 2u, 4u, 5u, 6u })
			Require(sample[axis] == 0, "Empty collision field moved vertices");
}
