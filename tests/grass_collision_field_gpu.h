void RunBasicGrassCollisionField()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"features/Grass Collision/Shaders/GrassCollision/BasicCollisionUpdateCS.hlsl", "cs_5_0");
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::BasicCollisionUpdateCS");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	D3D11ShaderTest::ConstantBuffer constants(device.Get(), reflection.Get(), "PerFrameCB");
	Require(constants.bytes.size() == 48 && constants.Offset("CameraHeightDelta") == 32, "Basic collision constants changed layout");
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = desc.Height = 8;
	desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	ComPtr<ID3D11Texture2D> field;
	ComPtr<ID3D11UnorderedAccessView> output;
	Check(device->CreateTexture2D(&desc, nullptr, field.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(field.Get(), nullptr, output.GetAddressOf()));
	Util::SetResourceName(field.Get(), "GrassTest::BasicField");
	Util::SetResourceName(output.Get(), "GrassTest::BasicField UAV");
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::BasicFieldReadback");
	const float initial[4]{ .5f, .25f, 0, 0 };
	const auto dispatch = [&] {
		context->ClearUnorderedAccessViewFloat(output.Get(), initial);
		constants.Bind(context.Get());
		auto* view = output.Get();
		context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		view = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
		context->CopyResource(staging.Get(), field.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float, 4> values{};
		memcpy(values.data(), mapped.pData, sizeof(values));
		context->Unmap(staging.Get(), 0);
		return values;
	};
	constants.SetVariable("TimeDelta", .02f);
	const auto recovered = dispatch();
	Require(std::abs(recovered[0] - (.5f - .02f / 4096)) < 1e-6f &&
				std::abs(recovered[1] - (.25f - 2.0f / 4096)) < 1e-6f && recovered[2] == .5f && recovered[3] == .25f,
		"Basic decay or previous height retention changed");
	constants.SetVariable("TimeDelta", 0.0f);
	const auto paused = dispatch();
	Require(paused[0] == .5f && paused[1] == .25f && paused[2] == .5f && paused[3] == .25f, "Paused Basic field changed");
	constants.SetVariable("CameraHeightDelta", 32.0f);
	const auto shifted = dispatch();
	Require(shifted[0] == .5f - 32.0f / 4096 && shifted[2] == shifted[0], "Basic camera-height history did not move with the camera");
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 512, 512 });
	for (const float value : dispatch())
		Require(value == 0, "Invalid Basic clipmap cells retained history");
	// A physical texel before the ring origin wraps to logical cell one, which is newly exposed.
	constants.SetVariable("ArrayOrigin", std::array<unsigned, 2>{ 511, 0 });
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 2, 0 });
	for (const float value : dispatch())
		Require(value == 0, "Basic clipmap wrapping retained a newly exposed cell");
	const auto makeInput = [&](const void* data, UINT stride, const char* name) {
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = bufferDesc.StructureByteStride = stride;
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		D3D11_SUBRESOURCE_DATA initialData{ data, 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		ComPtr<ID3D11ShaderResourceView> view;
		Check(device->CreateBuffer(&bufferDesc, &initialData, buffer.GetAddressOf()));
		Check(device->CreateShaderResourceView(buffer.Get(), nullptr, view.GetAddressOf()));
		Util::SetResourceName(buffer.Get(), "%s", name);
		Util::SetResourceName(view.Get(), "%s SRV", name);
		return view;
	};
	struct Box
	{
		std::array<float, 2> minimum{ -16, -16 }, maximum{ 16, 16 };
		uint32_t first = 0, end = 1;
		std::array<float, 2> padding{};
	} box;
	const std::array<float, 4> sphere{ 0, 0, 0, 16 };
	auto boxes = makeInput(&box, sizeof(box), "GrassTest::BasicBoxes");
	auto spheres = makeInput(sphere.data(), sizeof(sphere), "GrassTest::BasicSpheres");
	ID3D11ShaderResourceView* inputs[]{ boxes.Get(), spheres.Get() };
	context->CSSetShaderResources(0, 2, inputs);
	constants.SetVariable("ArrayOrigin", std::array<unsigned, 2>{ 0, 0 });
	constants.SetVariable("PosOffset", std::array<float, 2>{ 2044, 2044 });
	constants.SetVariable("CameraHeightDelta", 0.0f);
	constants.SetVariable("BoundingBoxCount", 1u);
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 512, 512 });
	const auto contact = dispatch();
	Require(contact[0] == .50390625f && contact[1] == .50390625f && contact[2] == 0 && contact[3] == 0,
		"Basic actor sphere did not seed a new field cell with the expected surface height");
}

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
	Require(constants.bytes.size() == 64 && constants.Offset("MaximumBend") == 48 && constants.Offset("HeightOffset") == 60,
		"Collision update constants changed layout");
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
	auto velocity = makeField(DXGI_FORMAT_R32G32B32A32_FLOAT, "GrassTest::VelocityPrevious");
	auto output = makeField(DXGI_FORMAT_R32G32B32A32_FLOAT, "GrassTest::FieldOutput");
	auto outputVelocity = makeField(DXGI_FORMAT_R32G32B32A32_FLOAT, "GrassTest::VelocityOutput");
	const float initial[4]{ .25f, -.125f, .5f, 1 }, initialMotion[4]{ 0, 0, -16, 16 };
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), initial);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), initialMotion);
	D3D11_TEXTURE2D_DESC desc{};
	output.texture->GetDesc(&desc);
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::FieldReadback");
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	ComPtr<ID3D11Texture2D> halfStaging;
	Check(device->CreateTexture2D(&desc, nullptr, halfStaging.GetAddressOf()));
	Util::SetResourceName(halfStaging.Get(), "GrassTest::HalfFieldReadback");
	const auto read = [&](const Field& field) {
		D3D11_TEXTURE2D_DESC fieldDesc{};
		field.texture->GetDesc(&fieldDesc);
		const bool half = fieldDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
		auto* target = half ? halfStaging.Get() : staging.Get();
		context->CopyResource(target, field.texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(target, 0, D3D11_MAP_READ, 0, &mapped));
		std::array<float, 4> result{};
		if (half)
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = DirectX::PackedVector::XMConvertHalfToFloat(static_cast<const DirectX::PackedVector::HALF*>(mapped.pData)[i]);
		else
			std::memcpy(result.data(), mapped.pData, sizeof(result));
		context->Unmap(target, 0);
		return result;
	};
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
		ID3D11ShaderResourceView* emptyInputs[4]{};
		context->CSSetShaderResources(0, 4, emptyInputs);
		return read(output);
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
	const auto structured = [&](const auto& value, const char* name, UINT stride = 0) {
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = sizeof(value);
		bufferDesc.StructureByteStride = stride ? stride : sizeof(value);
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
	const auto groundMotion = read(outputVelocity);
	Require(groundMotion[2] == -16 && groundMotion[3] == 16 && impact[3] == 1,
		"Ground contact did not retain its finite vertical bounds");
	auto verticalSweep = capsule;
	verticalSweep[8] = verticalSweep[12] = verticalSweep[0];
	verticalSweep[2] = verticalSweep[6] = -100;
	verticalSweep[10] = verticalSweep[14] = 100;
	capsules = structured(verticalSweep, "GrassTest::VerticalSweep");
	Require(dispatch()[3] == 1 && read(outputVelocity)[2] == -116 && read(outputVelocity)[3] == 116,
		"A zero-XY vertical sweep lost the heights crossed between frames");
	auto diagonalSweep = verticalSweep;
	diagonalSweep[0] = diagonalSweep[4] = diagonalSweep[8] + 400;
	capsules = structured(diagonalSweep, "GrassTest::DiagonalSweep");
	dispatch();
	const auto diagonalMotion = read(outputVelocity);
	Require(diagonalMotion[2] <= 76 && diagonalMotion[2] >= 75.9375f && diagonalMotion[3] == 116 && diagonalMotion[2] > 30,
		"A diagonal sweep invented ground contact outside its XY contact interval");
	auto rotatingSweep = diagonalSweep;
	rotatingSweep[0] = rotatingSweep[4] = -3892;
	rotatingSweep[8] = rotatingSweep[12] = -4292;
	rotatingSweep[2] = rotatingSweep[14] = 100;
	rotatingSweep[6] = rotatingSweep[10] = 0;
	capsules = structured(rotatingSweep, "GrassTest::RotatingSweep");
	dispatch();
	const auto rotatingMotion = read(outputVelocity);
	Require(rotatingMotion[2] <= 30 && rotatingMotion[2] >= 29.984375f && rotatingMotion[3] >= 70 && rotatingMotion[3] <= 70.0625f,
		"Crossing capsule endpoints used unrelated endpoint extrema inside the contact interval");
	for (const float centre : { 8179.0f, 8237.0f, -8179.0f, -8237.0f }) {
		auto highContact = capsule;
		for (size_t offset : { 2u, 6u, 10u, 14u })
			highContact[offset] = centre;
		capsules = structured(highContact, "GrassTest::ConservativeHighContact");
		dispatch();
		const auto highMotion = read(outputVelocity);
		Require(highMotion[2] <= centre - 16 && highMotion[2] >= centre - 24 &&
					highMotion[3] >= centre + 16 && highMotion[3] <= centre + 24,
			"Half-float contact storage rounded a high root or marginal tip contact inward");
	}
	auto elevatedCapsule = capsule;
	for (size_t offset : { 2u, 6u, 10u, 14u })
		elevatedCapsule[offset] += 256;
	capsules = structured(elevatedCapsule, "GrassTest::ElevatedCapsule");
	Require(dispatch() == impact, "Height changed the impulse within its own contact layer");
	const auto elevatedMotion = read(outputVelocity);
	Require(elevatedMotion[2] == 240 && elevatedMotion[3] == 272, "Same-XY elevated contact lost its height");
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 0, 0 });
	const auto preservedGround = dispatch();
	Require(std::abs(preservedGround[0] - initial[0] * retention) < 1e-6 && read(outputVelocity)[2] == -16,
		"A disjoint elevated impact erased or moved the recovering ground layer");
	const auto retain = [&] {
		context->CopyResource(previous.texture.Get(), output.texture.Get());
		context->CopyResource(velocity.texture.Get(), outputVelocity.texture.Get());
	};
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 1024, 1024 });
	dispatch();
	retain();
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 0, 0 });
	constants.SetVariable("BoundingBoxCount", 0u);
	const auto persisted = dispatch();
	const auto persistedMotion = read(outputVelocity);
	Require(persisted[2] > 0 && persisted[3] > 0 && persistedMotion[2] == 240 && persistedMotion[3] == 272,
		"Recovery lost the height of a departed elevated collider");
	retain();
	constants.SetVariable("HeightOffset", 64.0f);
	constants.SetVariable("TimeDelta", 0.0f);
	Require(dispatch() == persisted, "Camera movement changed paused deformation");
	const auto shiftedMotion = read(outputVelocity);
	Require(shiftedMotion[2] == 240 && shiftedMotion[3] == 272 && read(velocity)[2] == 240,
		"Camera movement changed the fixed contact-height origin");
	constants.SetVariable("HeightOffset", 0.0f);
	constants.SetVariable("TimeDelta", dt);
	constants.SetVariable("BoundingBoxCount", 1u);
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 1024, 1024 });
	Box twoLayers;
	twoLayers.end = 2;
	boxes = structured(twoLayers, "GrassTest::LayeredBox");
	for (const auto& layers : { std::array{ elevatedCapsule, capsule }, std::array{ capsule, elevatedCapsule } }) {
		capsules = structured(layers, "GrassTest::LayeredCapsules", sizeof(capsule));
		Require(dispatch() == impact, "Disjoint bridge and ground impulses were combined");
		const auto layeredMotion = read(outputVelocity);
		Require(layeredMotion[2] == -16 && layeredMotion[3] == 16, "Disjoint layers expanded contact into the gap between floors");
	}
	Box threeLayers;
	threeLayers.end = 3;
	boxes = structured(threeLayers, "GrassTest::DominantContactBox");
	std::array<std::array<float, 16>, 3> chain{ capsule, capsule, capsule };
	const std::array<float, 3> centres{ 25, 5, 15 }, radii{ 5, 5, 10 };
	for (size_t i = 0; i < chain.size(); ++i) {
		chain[i][3] = radii[i];
		for (size_t offset : { 2u, 6u, 10u, 14u })
			chain[i][offset] = centres[i];
	}
	std::array<unsigned, 3> order{ 0, 1, 2 };
	do {
		const std::array ordered{ chain[order[0]], chain[order[1]], chain[order[2]] };
		capsules = structured(ordered, "GrassTest::ContactChain", sizeof(capsule));
		Require(dispatch() == impact && read(outputVelocity)[2] == 0 && read(outputVelocity)[3] == 10,
			"Overlapping contact chains changed the dominant lowest contact with input order");
	} while (std::next_permutation(order.begin(), order.end()));
	std::array ties{ capsule, capsule, capsule };
	ties[1][0] = ties[1][4] = ties[1][8] = ties[1][12] = -4084;
	ties[2][8] = ties[2][12] = -4084;
	do {
		const std::array ordered{ ties[order[0]], ties[order[1]], ties[order[2]] };
		capsules = structured(ordered, "GrassTest::ContactTies", sizeof(capsule));
		const auto tiedImpact = dispatch();
		Require(tiedImpact[0] == impact[0] && tiedImpact[1] == -impact[1] && tiedImpact[2] == impact[2],
			"Contact penetration or direction ties changed with input order");
	} while (std::next_permutation(order.begin(), order.end()));
	boxes = structured(twoLayers, "GrassTest::LayeredBox");
	capsules = structured(std::array{ elevatedCapsule, capsule }, "GrassTest::LayeredCapsules", sizeof(capsule));
	constants.SetVariable("MaximumBend", .001f);
	Require(std::abs(dispatch()[1] - .001f) < 1e-6, "Maximum bend was exceeded");
	constants.SetVariable("TimeDelta", 0.0f);
	Require(dispatch() == std::array<float, 4>{}, "Paused collision injected an impulse or compression");
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), initial);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), initialMotion);
	constants.SetVariable("MaximumBend", 1.5f);
	constants.SetVariable("MaximumCompression", 1.0f);
	constants.SetVariable("TimeDelta", dt);
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 0, 0 });
	boxes = structured(Box{}, "GrassTest::RisingActorBox");
	for (unsigned step = 1; step <= 8; ++step) {
		auto rising = capsule;
		for (size_t offset : { 2u, 6u, 10u, 14u })
			rising[offset] += step * 8.0f;
		capsules = structured(rising, "GrassTest::RisingCapsule");
		dispatch();
		const auto motion = read(outputVelocity);
		Require(motion[2] == -16 && motion[3] == 16, "Progressively higher impacts moved a recovering ground contact upward");
		retain();
	}
	velocity = makeField(DXGI_FORMAT_R16G16B16A16_FLOAT, "GrassTest::HalfContactPrevious");
	outputVelocity = makeField(DXGI_FORMAT_R16G16B16A16_FLOAT, "GrassTest::HalfContactOutput");
	const float distantContact[4]{ 0, 0, -2064, -2032 };
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), initial);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), distantContact);
	constants.SetVariable("MaximumBend", 1.5f);
	constants.SetVariable("MaximumCompression", 1.0f);
	constants.SetVariable("TimeDelta", 0.0f);
	constants.SetVariable("BoundingBoxCount", 0u);
	constants.SetVariable("ValidMargin", std::array<int, 2>{ 0, 0 });
	for (unsigned frame = 1; frame <= 100; ++frame) {
		constants.SetVariable("HeightOffset", frame * .25f);
		Require(dispatch() == std::array<float, 4>{ initial[0], initial[1], initial[2], initial[3] },
			"Sub-texel camera motion changed paused field history");
		const auto motion = read(outputVelocity);
		Require(motion[2] == distantContact[2] && motion[3] == distantContact[3], "Fractional camera movement accumulated half-float height drift");
		retain();
	}
	previous = makeField(DXGI_FORMAT_R16G16B16A16_FLOAT, "GrassTest::HalfFieldPrevious");
	output = makeField(DXGI_FORMAT_R16G16B16A16_FLOAT, "GrassTest::HalfFieldOutput");
	const float halfResidual[4]{ 0, 0, std::ldexp(1.0f, -24), 1 };
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), halfResidual);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), initialMotion);
	constants.SetVariable("HeightOffset", 0.0f);
	Require(dispatch()[2] == halfResidual[2], "Paused half-float recovery discarded its residual history");
	retain();
	constants.SetVariable("TimeDelta", dt);
	Require(dispatch() == std::array<float, 4>{} && read(outputVelocity) == std::array<float, 4>{},
		"Half-float recovery never retired its contact layer");
	retain();
	capsules = structured(elevatedCapsule, "GrassTest::RecoveredElevatedCapsule");
	constants.SetVariable("BoundingBoxCount", 1u);
	Require(dispatch()[3] == 1 && read(outputVelocity)[2] == 240 && read(outputVelocity)[3] == 272,
		"A recovered lower layer blocked a new elevated contact");
	const float agingState[4]{ .25f, -.125f, .5f, 2047 };
	context->ClearUnorderedAccessViewFloat(previous.uav.Get(), agingState);
	context->ClearUnorderedAccessViewFloat(velocity.uav.Get(), initialMotion);
	constants.SetVariable("SpringStrength", .05f);
	constants.SetVariable("Damping", 20.0f);
	Require(dispatch()[0] > .2f && read(outputVelocity)[2] == -16 && read(output)[3] == 2048,
		"A higher contact replaced recent visible low-spring history");
	retain();
	constants.SetVariable("BoundingBoxCount", 0u);
	Require(dispatch()[0] > .2f && read(outputVelocity)[2] == -16,
		"Idle expiry discarded visible recovery without a replacement contact");
	retain();
	constants.SetVariable("BoundingBoxCount", 1u);
	constants.SetVariable("TimeDelta", 0.0f);
	const auto agingPaused = dispatch();
	Require(agingPaused == read(previous) && read(outputVelocity)[2] == -16,
		"Paused updates aged or replaced visible contact history");
	constants.SetVariable("TimeDelta", dt);
	Require(dispatch()[3] == 1 && read(outputVelocity)[2] == 240 && read(outputVelocity)[3] == 272,
		"Bounded idle history permanently blocked a new disjoint contact");
}
