void RunGrassCollisionField()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"features/Grass Collision/Shaders/GrassCollision/CollisionUpdateCS.hlsl", "cs_5_0");
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::CollisionUpdateCS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	D3D11ShaderTest::ConstantBuffer constants(device.Get(), reflection.Get(), "PerFrameCB");
	Require(constants.bytes.size() == 64 && constants.Offset("MaximumBend") == 48, "Collision update constants changed layout");
	struct Field
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};
	const auto makeField = [&](DXGI_FORMAT format, const char* name) {
		Field field;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 8;
		desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Format = format;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		Check(device->CreateTexture2D(&desc, nullptr, field.texture.GetAddressOf()));
		Check(device->CreateShaderResourceView(field.texture.Get(), nullptr, field.srv.GetAddressOf()));
		Check(device->CreateUnorderedAccessView(field.texture.Get(), nullptr, field.uav.GetAddressOf()));
		Util::SetResourceName(field.texture.Get(), "%s", name);
		Util::SetResourceName(field.srv.Get(), "%s SRV", name);
		Util::SetResourceName(field.uav.Get(), "%s UAV", name);
		return field;
	};
	auto previous = makeField(DXGI_FORMAT_R32G32B32A32_FLOAT, "GrassTest::FieldPrevious");
	auto velocity = makeField(DXGI_FORMAT_R32G32_FLOAT, "GrassTest::VelocityPrevious");
	auto output = makeField(DXGI_FORMAT_R32G32B32A32_FLOAT, "GrassTest::FieldOutput");
	auto outputVelocity = makeField(DXGI_FORMAT_R32G32_FLOAT, "GrassTest::VelocityOutput");
	const float initial[4]{ .25f, -.125f, .5f, 0 }, zero[4]{};
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), initial);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), zero);
	D3D11_TEXTURE2D_DESC desc{};
	output.texture->GetDesc(&desc);
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::FieldReadback");
	const float dt = 1.0f / 60;
	constants.SetVariable("TimeDelta", dt);
	constants.SetVariable("SpringStrength", 5.0f);
	constants.SetVariable("Damping", 7.5f);
	constants.SetVariable("MaximumBend", 1.5f);
	constants.SetVariable("MaximumCompression", 1.0f);
	constants.SetVariable("CompressionRecovery", .25f);
	ComPtr<ID3D11ShaderResourceView> boxes, capsules;
	const auto dispatch = [&] {
		constants.Bind(context.Get());
		ID3D11ShaderResourceView* inputs[]{ boxes.Get(), capsules.Get(), previous.srv.Get(), velocity.srv.Get() };
		context->CSSetShaderResources(0, 4, inputs);
		ID3D11UnorderedAccessView* outputs[]{ output.uav.Get(), outputVelocity.uav.Get() };
		context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		ID3D11UnorderedAccessView* empty[2]{};
		context->CSSetUnorderedAccessViews(0, 2, empty, nullptr);
		context->CopyResource(staging.Get(), output.texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float, 4> result{};
		std::memcpy(result.data(), mapped.pData, sizeof(result));
		context->Unmap(staging.Get(), 0);
		return result;
	};
	const auto recovered = dispatch();
	const float retention = 1 - 5 * dt * dt / (1 + 7.5f * dt + 5 * dt * dt);
	Require(std::abs(recovered[0] - initial[0] * retention) < 1e-6 &&
				std::abs(recovered[1] - initial[1] * retention) < 1e-6 &&
				std::abs(recovered[2] - initial[2] * std::exp(-.25f * dt)) < 1e-6,
		"Spring or compression recovery is incorrect");
	constants.SetVariable("TimeDelta", 0.0f);
	const auto paused = dispatch();
	Require(paused[0] == initial[0] && paused[1] == initial[1] && paused[2] == initial[2], "Paused field changed");
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 1, 0 });
	const auto entered = dispatch();
	Require(entered == std::array<float, 4>{}, "New clipmap cells retained old deformation");
	constants.SetVariable("ArrayOrigin", std::array<unsigned, 2>{ 1023, 0 });
	const auto wrapped = dispatch();
	Require(wrapped[0] == initial[0], "Clipmap wrapping discarded a retained cell");
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 1024, 1024 });
	Require(dispatch() == std::array<float, 4>{}, "Teleport reset retained deformation");
	const auto structured = [&](const auto& value, const char* name) {
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = bufferDesc.StructureByteStride = sizeof(value);
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		D3D11_SUBRESOURCE_DATA initialData{ &value, 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&bufferDesc, &initialData, buffer.GetAddressOf()));
		Util::SetResourceName(buffer.Get(), "%s", name);
		ComPtr<ID3D11ShaderResourceView> srv;
		Check(device->CreateShaderResourceView(buffer.Get(), nullptr, srv.GetAddressOf()));
		Util::SetResourceName(srv.Get(), "%s SRV", name);
		return srv;
	};
	struct Box
	{
		std::array<float, 2> minimum{ -4200, -4200 }, maximum{ -4000, -4000 };
		uint32_t begin = 0, end = 1;
		std::array<float, 2> padding{};
	};
	boxes = structured(Box{}, "GrassTest::CollisionBox");
	const std::array<float, 16> capsule{ -4092, -4092, 0, 16, -4092, -4092, 0, 0,
		-4100, -4092, 0, 0, -4100, -4092, 0, 0 };
	capsules = structured(capsule, "GrassTest::CollisionCapsule");
	constants.SetVariable("BoundingBoxCount", 1u);
	constants.SetVariable("CollisionStrength", 1.0f);
	constants.SetVariable("ArrayOrigin", std::array<unsigned, 2>{ 0, 0 });
	constants.SetVariable("TimeDelta", dt);
	constants.SetVariable("MaximumCompression", .4f);
	const auto impact = dispatch();
	Require(std::abs(impact[0]) < 1e-6 && std::abs(impact[1] - 50 * dt * dt / (1 + 7.5f * dt + 5 * dt * dt)) < 1e-6 &&
				std::abs(impact[2] - .4f) < 1e-6,
		"Swept capsule impact or compression is incorrect");
	constants.SetVariable("MaximumBend", .001f);
	Require(std::abs(dispatch()[1] - .001f) < 1e-6, "Maximum bend was exceeded");
	constants.SetVariable("TimeDelta", 0.0f);
	Require(dispatch() == std::array<float, 4>{}, "Paused collision injected an impulse or compression");
}
