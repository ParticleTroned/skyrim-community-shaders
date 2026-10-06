void RunSPDReduction(UINT eyeWidth, UINT height, UINT eyes)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	const std::array levels{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL actual{};
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels.data(), UINT(levels.size()),
		D3D11_SDK_VERSION, device.GetAddressOf(), &actual, context.GetAddressOf()));
	Require(actual >= D3D_FEATURE_LEVEL_11_1, "SPD validation requires a WARP 11.1 device");
	auto code = Compile(L"package/Shaders/Common/DepthPyramidSPD.hlsl", "cs_5_0");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::SPDCS");
	const UINT width = eyeWidth * eyes;
	const UINT mips = UINT((std::min)(std::bit_width(eyeWidth), std::bit_width(height)));
	const UINT spdMips = DepthPyramidPolicy::SinglePassMipCount(width, height, mips);
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = mips;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	ComPtr<ID3D11Texture2D> texture;
	Check(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()));
	Util::SetResourceName(texture.Get(), "GrassTest::SPDTexture");
	std::vector<ComPtr<ID3D11UnorderedAccessView>> views(mips);
	for (UINT mip = 0; mip < mips; ++mip) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = desc.Format;
		view.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipSlice = mip;
		Check(device->CreateUnorderedAccessView(texture.Get(), &view, views[mip].GetAddressOf()));
		Util::SetResourceName(views[mip].Get(), "GrassTest::SPDMip%u UAV", mip);
	}
	D3D11_BUFFER_DESC counterDesc{};
	counterDesc.ByteWidth = 16;
	counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	ComPtr<ID3D11Buffer> counter;
	Check(device->CreateBuffer(&counterDesc, nullptr, counter.GetAddressOf()));
	Util::SetResourceName(counter.Get(), "GrassTest::SPDCounter");
	D3D11_UNORDERED_ACCESS_VIEW_DESC counterView{};
	counterView.Format = DXGI_FORMAT_R32_TYPELESS;
	counterView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	counterView.Buffer.NumElements = 4;
	counterView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	ComPtr<ID3D11UnorderedAccessView> counterUAV;
	Check(device->CreateUnorderedAccessView(counter.Get(), &counterView, counterUAV.GetAddressOf()));
	Util::SetResourceName(counterUAV.Get(), "GrassTest::SPDCounter UAV");
	const UINT groupsX = (width + 63) / 64, groupsY = (height + 63) / 64;
	D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "SpdParams");
	params.SetVariable("SpdSourceSize", std::array<UINT, 2>{ width, height });
	params.SetVariable("SpdMipCount", spdMips);
	params.SetVariable("SpdNumWorkGroups", groupsX * groupsY);
	auto reductionCode = Compile(L"package/Shaders/Common/DepthPyramidBaseCS.hlsl", "cs_5_0");
	ComPtr<ID3D11ShaderReflection> reductionReflection;
	Check(D3DReflect(reductionCode->GetBufferPointer(), reductionCode->GetBufferSize(), IID_PPV_ARGS(reductionReflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> reductionShader;
	Check(device->CreateComputeShader(reductionCode->GetBufferPointer(), reductionCode->GetBufferSize(), nullptr, reductionShader.GetAddressOf()));
	Util::SetResourceName(reductionShader.Get(), "GrassTest::SPDTailCS");
	D3D11ShaderTest::ConstantBuffer reductionParams(device.Get(), reductionReflection.Get(), "Parameters");
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::SPDReadback");
	for (UINT repeat = 0; repeat < 2; ++repeat) {
		params.Bind(context.Get());
		std::vector<float> expected(width * height);
		for (UINT y = 0; y < height; ++y)
			for (UINT x = 0; x < width; ++x)
				expected[y * width + x] = float((x * 17 + y * 31 + repeat * 7) % 127) / 127 * .2f + (x < eyeWidth ? .1f : .6f);
		expected[(height / 2) * width + eyeWidth / 2] = 1;
		context->UpdateSubresource(texture.Get(), 0, nullptr, expected.data(), width * sizeof(float), 0);
		constexpr std::array<UINT, 4> zero{};
		context->ClearUnorderedAccessViewUint(counterUAV.Get(), zero.data());
		std::array<ID3D11UnorderedAccessView*, 14> raw{};
		for (UINT mip = 1; mip <= spdMips; ++mip) raw[mip - 1] = views[mip].Get();
		raw[12] = counterUAV.Get();
		raw[13] = views[0].Get();
		context->CSSetUnorderedAccessViews(0, UINT(raw.size()), raw.data(), nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(groupsX, groupsY, 1);
		raw.fill(nullptr);
		context->CSSetUnorderedAccessViews(0, UINT(raw.size()), raw.data(), nullptr);
		for (UINT mip = spdMips + 1; mip < mips; ++mip) {
			D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};
			sourceView.Format = DXGI_FORMAT_R32_FLOAT;
			sourceView.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sourceView.Texture2D.MostDetailedMip = mip - 1;
			sourceView.Texture2D.MipLevels = 1;
			ComPtr<ID3D11ShaderResourceView> source;
			Check(device->CreateShaderResourceView(texture.Get(), &sourceView, source.GetAddressOf()));
			Util::SetResourceName(source.Get(), "GrassTest::SPDTailMip%u SRV", mip - 1);
			reductionParams.SetVariable("Width", width >> mip);
			reductionParams.SetVariable("Height", height >> mip);
			reductionParams.SetVariable("SourceWidth", width >> (mip - 1));
			reductionParams.SetVariable("SourceHeight", height >> (mip - 1));
			reductionParams.SetVariable("Eyes", eyes);
			reductionParams.SetVariable("FirstLevel", 0u);
			reductionParams.Bind(context.Get());
			auto srv = source.Get();
			auto uav = views[mip].Get();
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetShader(reductionShader.Get(), nullptr, 0);
			context->Dispatch(((width >> mip) + 7) / 8, ((height >> mip) + 7) / 8, 1);
			srv = nullptr;
			uav = nullptr;
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}
		context->CopyResource(staging.Get(), texture.Get());
		UINT previousWidth = width, previousHeight = height;
		for (UINT mip = 1; mip < mips; ++mip) {
			const UINT currentWidth = previousWidth / 2, currentHeight = previousHeight / 2;
			std::vector<float> next(currentWidth * currentHeight);
			for (UINT y = 0; y < currentHeight; ++y)
				for (UINT x = 0; x < currentWidth; ++x)
					next[y * currentWidth + x] = (std::max)({ expected[(y * 2) * previousWidth + x * 2], expected[(y * 2) * previousWidth + x * 2 + 1],
						expected[(y * 2 + 1) * previousWidth + x * 2], expected[(y * 2 + 1) * previousWidth + x * 2 + 1] });
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), mip, D3D11_MAP_READ, 0, &mapped));
			bool matched = true;
			for (UINT y = 0; y < currentHeight; ++y) {
				const auto row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
				for (UINT x = 0; x < currentWidth; ++x)
					matched &= row[x] == next[y * currentWidth + x];
			}
			context->Unmap(staging.Get(), mip);
			Require(matched, "SPD mip differed from conservative reference or crossed the stereo seam");
			expected = std::move(next);
			previousWidth = currentWidth;
			previousHeight = currentHeight;
		}
	}
}
