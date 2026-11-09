void RunActiveDepthReduction(bool reversed)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	std::vector<D3D_SHADER_MACRO> defines{ { "BASE_REDUCTION", "4" } };
	if (reversed)
		defines.push_back({ "REVERSE_Z", "1" });
	auto code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassDepthCS.hlsl", "cs_5_0", defines);
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::ActiveDepthCS");
	constexpr UINT sourceWidth = 120, sourceHeight = 72, width = 16, height = 16, eyes = 2;
	const float farDepth = reversed ? 0.f : 1.f, occluder = reversed ? .8f : .2f;
	const auto texture = [&](UINT textureWidth, UINT textureHeight, UINT bind, const char* name) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = textureWidth;
		desc.Height = textureHeight;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.BindFlags = bind;
		ComPtr<ID3D11Texture2D> result;
		Check(device->CreateTexture2D(&desc, nullptr, result.GetAddressOf()));
		Util::SetResourceName(result.Get(), "%s", name);
		return result;
	};
	auto input = texture(sourceWidth, sourceHeight, D3D11_BIND_SHADER_RESOURCE, "GrassTest::ActiveDepthSource");
	auto output = texture(width, height, D3D11_BIND_UNORDERED_ACCESS, "GrassTest::ActiveDepthReduced");
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
	Util::SetResourceName(srv.Get(), "GrassTest::ActiveDepthSource SRV");
	Util::SetResourceName(uav.Get(), "GrassTest::ActiveDepthReduced UAV");
	D3D11_TEXTURE2D_DESC stagingDesc{};
	output->GetDesc(&stagingDesc);
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::ActiveDepthReadback");
	D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "Parameters");
	params.SetVariable("Width", width);
	params.SetVariable("Height", height);
	params.SetVariable("SourceWidth", sourceWidth);
	params.SetVariable("SourceHeight", sourceHeight);
	params.SetVariable("Eyes", eyes);
	params.SetVariable("FirstLevel", 1u);
	const float staleDepth[4]{ occluder, occluder, occluder, occluder };
	context->ClearUnorderedAccessViewFloat(uav.Get(), staleDepth);
	for (const auto extent : { std::array<UINT, 2>{ 60, 36 }, std::array<UINT, 2>{ 28, 20 } }) {
		// Packed active eyes move with resolution while backing storage retains its pitch.
		std::vector<float> pixels(sourceWidth * sourceHeight, occluder);
		for (UINT y = 0; y < extent[1]; ++y)
			for (UINT x = extent[0] / eyes; x < extent[0]; ++x)
				pixels[y * sourceWidth + x] = farDepth;
		context->UpdateSubresource(input.Get(), 0, nullptr, pixels.data(), sourceWidth * sizeof(float), 0);
		params.SetVariable("ActiveWidth", extent[0]);
		params.SetVariable("ActiveHeight", extent[1]);
		params.Bind(context.Get());
		auto source = srv.Get();
		auto target = uav.Get();
		context->CSSetShaderResources(0, 1, &source);
		context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(width / 8, height / 8, 1);
		target = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		bool correct = true;
		for (UINT y = 0; y < height; ++y) {
			const auto row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
			for (UINT x = 0; x < width; ++x) {
				const bool covered = x < width / eyes && (x + 1) * 4 <= extent[0] / eyes && (y + 1) * 4 <= extent[1];
				correct &= row[x] == (covered ? occluder : farDepth);
			}
		}
		context->Unmap(staging.Get(), 0);
		Require(correct, "Active depth reduction retained stale padding or crossed the packed stereo seam");
	}
}
