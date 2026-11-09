void RunGrassOcclusionMargin(bool reversed)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	std::vector<D3D_SHADER_MACRO> defines;
	if (reversed)
		defines.push_back({ "REVERSE_Z", "1" });
	auto code = Compile(L"tests/shaders/GrassOcclusionTestCS.hlsl", "cs_5_0", defines);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::OcclusionMarginCS");
	struct Case
	{
		std::array<float, 16> projection;
		std::array<float, 4> values;
	};
	std::vector<Case> cases;
	std::vector<bool> mayCull, mustCull;
	constexpr double nearPlane = 5, farPlane = 100000;
	const double a = reversed ? -nearPlane / (farPlane - nearPlane) : farPlane / (farPlane - nearPlane);
	const double b = (reversed ? 1 : -1) * nearPlane * farPlane / (farPlane - nearPlane);
	const auto depth = [&](double distance) { return float(a + b / distance); };
	const auto add = [&](float nearest, float occluder, float tolerance, float slack, bool allowed, bool required) {
		cases.push_back({ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, float(a), float(b), 0, 0, 1, 0 },
			{ nearest, occluder, tolerance, slack } });
		mayCull.push_back(allowed);
		mustCull.push_back(required);
	};
	for (float tolerance : { 0.f, .001f, .05f })
		for (double distance : { 10., 1000., 20000., 90000. })
			for (double ratio : { 1.01, 1., .9999, .9995, .99, .97, .9 }) {
				const bool allowed = distance * ratio < distance * (1 - tolerance);
				add(depth(distance), depth(distance * ratio), tolerance, 0, allowed, ratio == .9);
			}
	for (float tolerance : { 0.f, .001f, .05f }) {
		const float nearest = depth(20000);
		add(nearest, reversed ? 0.f : 1.f, tolerance, 0, false, false);
		add(nearest, nearest, tolerance, 0, false, false);
		add(nearest, std::nextafter(nearest, reversed ? 1.f : 0.f), tolerance, 0, false, false);
		add(reversed ? 1.f : 0.f, depth(10), tolerance, 0, false, false);
		add(nearest, depth(10000), tolerance, .001f, false, false);
		add(nearest, depth(10000), tolerance, 0, true, true);
		auto& transformed = cases.back().projection;
		transformed[2] = -.15f;
		for (UINT axis = 0; axis < 3; ++axis) {
			const float w = std::array<float, 3>{ .3f, -.4f, .8660254f }[axis];
			transformed[8 + axis] = float(a) * w;
			transformed[12 + axis] = w;
		}
		transformed[11] = float(a * 42 + b);
		transformed[15] = 42;
		add(nearest, depth(10000), tolerance, 0, false, false);
		cases.back().projection[8] = .01f;
		add(nearest, depth(10000), tolerance, 0, false, false);
		cases.back().projection[14] = std::numeric_limits<float>::quiet_NaN();
		for (uint32_t invalid = 0; invalid < 4; ++invalid) {
			add(nearest, depth(10000), tolerance, 0, false, false);
			auto& matrix = cases.back().projection;
			if (invalid == 0)
				matrix[10] = std::numeric_limits<float>::infinity();
			else if (invalid == 1) {
				matrix[10] = 1e30f;
				matrix[14] = 1e30f;
			} else {
				matrix[14] = 0;
				matrix[15] = invalid == 2 ? 0.f : -1.f;
			}
		}
		for (float gap : { tolerance * .5f, .1f }) {
			const float direction = reversed ? -1.f : 1.f;
			add(.5f + direction * gap, .5f, tolerance, 0, gap > tolerance, gap == .1f);
			cases.back().projection = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		}
	}
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
	struct ShaderCase
	{
		std::array<float, 4> values;
		float asymptote;
		uint32_t projectionMode;
		std::array<uint32_t, 2> padding{};
	};
	std::vector<ShaderCase> inputs;
	for (const auto& item : cases) {
		const auto projection = GrassOcclusionPolicy::ClassifyProjection(item.projection);
		inputs.push_back({ item.values, projection.asymptote, uint32_t(projection.mode) });
	}
	auto input = makeBuffer(UINT(inputs.size() * sizeof(ShaderCase)), sizeof(ShaderCase), D3D11_BIND_SHADER_RESOURCE, inputs.data(), "GrassTest::OcclusionCases");
	auto output = makeBuffer(UINT(cases.size() * 8), 8, D3D11_BIND_UNORDERED_ACCESS, nullptr, "GrassTest::OcclusionResults");
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
	Util::SetResourceName(srv.Get(), "GrassTest::OcclusionCases SRV");
	Util::SetResourceName(uav.Get(), "GrassTest::OcclusionResults UAV");
	auto source = srv.Get();
	auto target = uav.Get();
	context->CSSetShaderResources(0, 1, &source);
	context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
	context->CSSetShader(shader.Get(), nullptr, 0);
	context->Dispatch(UINT(cases.size()), 1, 1);
	target = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
	D3D11_BUFFER_DESC desc{};
	output->GetDesc(&desc);
	desc.BindFlags = desc.MiscFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Buffer> staging;
	Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::OcclusionReadback");
	context->CopyResource(staging.Get(), output.Get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	std::vector<float> result(cases.size() * 2);
	std::memcpy(result.data(), mapped.pData, result.size() * sizeof(float));
	context->Unmap(staging.Get(), 0);
	for (size_t i = 0; i < cases.size(); ++i) {
		const bool hidden = result[i * 2 + 1] != 0;
		Require(std::isfinite(result[i * 2]) && result[i * 2] >= cases[i].values[3], "Occlusion margin lost independent viewport slack");
		Require(!hidden || mayCull[i], "Occlusion tolerance falsely rejected visible or protected grass");
		Require(hidden || !mustCull[i], "Distance tolerance disabled a well-separated distant occluder");
	}
}
