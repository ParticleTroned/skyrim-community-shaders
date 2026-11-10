void RunGrassBounds(bool reversed = false)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	std::vector<D3D_SHADER_MACRO> defines;
	if (reversed)
		defines.push_back({ "REVERSE_Z", "1" });
	auto code = Compile(L"tests/shaders/GrassBoundsTestCS.hlsl", "cs_5_0", defines);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::BoundsCS");
	struct Case
	{
		std::array<float, 16> matrix;
		std::array<float, 4> sphere;
	};
	std::vector<Case> cases;
	for (float skew : { -0.25f, 0.0f, 0.4f })
		for (float depth : { 2.f, 10.f, 1000.f })
			for (float radius : { .01f, .4f, .99f }) {
				Case item{};
				item.matrix = { 1.2f, .1f, skew, 0, 0, 1.5f, -.15f, 0, 0, 0, 1.0001f, -.1f, 0, 0, 1, 0 };
				item.sphere = { skew * depth, depth * .2f, depth, radius };
				if (reversed) {
					item.matrix[10] = -0.1f / (1000.0f - 0.1f);
					item.matrix[11] = 100.0f / (1000.0f - 0.1f);
				}
				cases.push_back(item);
			}
	Case nearPlane = cases.front();
	nearPlane.sphere = { 0, 0, .05f, .1f };
	cases.push_back(nearPlane);
	Case behind = cases.front();
	behind.sphere = { 0, 0, -1, .1f };
	cases.push_back(behind);
	Case malformed = cases.front();
	malformed.sphere[3] = NAN;
	cases.push_back(malformed);
	const auto buffer = [&](UINT bytes, UINT stride, UINT bind, const void* initial, const char* name) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.StructureByteStride = stride;
		desc.BindFlags = bind;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		D3D11_SUBRESOURCE_DATA data{ initial, 0, 0 };
		ComPtr<ID3D11Buffer> result;
		Check(device->CreateBuffer(&desc, initial ? &data : nullptr, result.GetAddressOf()));
		Util::SetResourceName(result.Get(), "%s", name);
		return result;
	};
	auto input = buffer(UINT(cases.size() * sizeof(Case)), sizeof(Case), D3D11_BIND_SHADER_RESOURCE, cases.data(), "GrassTest::BoundsCases");
	auto output = buffer(UINT(cases.size() * 2 * 16), 16, D3D11_BIND_UNORDERED_ACCESS, nullptr, "GrassTest::BoundsResults");
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
	Util::SetResourceName(srv.Get(), "GrassTest::BoundsCases SRV");
	Util::SetResourceName(uav.Get(), "GrassTest::BoundsResults UAV");
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
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Buffer> staging;
	Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::BoundsReadback");
	context->CopyResource(staging.Get(), output.Get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	std::vector<float> result(cases.size() * 8);
	std::memcpy(result.data(), mapped.pData, result.size() * sizeof(float));
	context->Unmap(staging.Get(), 0);
	for (size_t index = 0; index < cases.size(); ++index) {
		const auto& item = cases[index];
		const float* bound = result.data() + index * 8;
		if (index >= cases.size() - 3) {
			Require(bound[5] == 0, "Near-plane or invalid sphere did not fail open");
			continue;
		}
		Require(bound[5] == 1, "Usable projected sphere was rejected");
		for (int latitude = -16; latitude <= 16; ++latitude)
			for (int longitude = 0; longitude < 64; ++longitude) {
				const double z = latitude / 16.0, radial = std::sqrt(1 - z * z), angle = longitude * 6.283185307179586 / 64;
				const std::array<double, 4> position{ item.sphere[0] + item.sphere[3] * radial * std::cos(angle),
					item.sphere[1] + item.sphere[3] * radial * std::sin(angle), item.sphere[2] + item.sphere[3] * z, 1 };
				std::array<double, 4> clip{};
				for (size_t row = 0; row < 4; ++row)
					for (size_t column = 0; column < 4; ++column) clip[row] += item.matrix[row * 4 + column] * position[column];
				Require(clip[0] / clip[3] >= bound[0] && clip[0] / clip[3] <= bound[2] &&
							clip[1] / clip[3] >= bound[1] && clip[1] / clip[3] <= bound[3] && (reversed ? clip[2] / clip[3] <= bound[4] : clip[2] / clip[3] >= bound[4]),
					"Analytic grass bounds exclude a sphere surface sample");
			}
	}
}
