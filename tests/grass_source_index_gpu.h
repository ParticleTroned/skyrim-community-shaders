// Included after the shader compiler helpers in grass_batch_shader_test.cpp.
void RunGrassSourceIndices()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"tests/shaders/grass_source_index_vs.hlsl", "vs_5_0");
	ComPtr<ID3D11VertexShader> shader;
	Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::SourceIndexVS");
	const std::array<D3D11_SO_DECLARATION_ENTRY, 4> declaration{ { { 0, "TEXCOORD", 0, 0, 4, 0 }, { 0, "TEXCOORD", 1, 0, 4, 0 },
		{ 0, "TEXCOORD", 2, 0, 4, 0 }, { 0, "TEXCOORD", 3, 0, 4, 0 } } };
	const UINT outputStride = 16 * sizeof(float);
	ComPtr<ID3D11GeometryShader> stream;
	Check(device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(), code->GetBufferSize(),
		declaration.data(), UINT(declaration.size()), &outputStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, stream.GetAddressOf()));
	Util::SetResourceName(stream.Get(), "GrassTest::SourceIndexStream");
	const auto makeBuffer = [&](UINT bytes, UINT bind, const void* initial, bool raw, const char* name) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.BindFlags = bind;
		desc.MiscFlags = raw ? D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS : 0;
		D3D11_SUBRESOURCE_DATA data{ initial, 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&desc, initial ? &data : nullptr, buffer.GetAddressOf()));
		Util::SetResourceName(buffer.Get(), "%s", name);
		return buffer;
	};
	using Record = std::array<uint16_t, 16>;
	std::vector<Record> records(GrassPolicy::kMaxBatchInstances);
	for (UINT i = 0; i < records.size(); ++i)
		for (UINT component = 0; component < 16; ++component)
			records[i][component] = uint16_t(((i * 17 + component * 997) % 0x7c00) | ((component & 1) << 15));
	records[0] = { 0, 0x8000, 1, 0x8001, 0x3ff, 0x83ff, 0x400, 0x8400,
		0x3555, 0xb555, 0x3c00, 0xbc00, 0x7bff, 0xfbff, 0x4000, 0xc000 };
	const std::array<UINT, 14> indices{ 0, 1, 31, 63, 64, 65, 127, 128, 129, 255, 256, 65535, 65536, GrassPolicy::kMaxBatchInstances - 1 };
	std::array<float, indices.size()> encoded{};
	std::array<Record, indices.size()> native{};
	for (size_t i = 0; i < indices.size(); ++i) {
		encoded[i] = float(indices[i]);
		native[i] = records[indices[i]];
	}
	auto source = makeBuffer(UINT(records.size() * sizeof(Record)), D3D11_BIND_SHADER_RESOURCE, records.data(), true, "GrassTest::ExactSource");
	auto compacted = makeBuffer(sizeof(encoded), D3D11_BIND_VERTEX_BUFFER, encoded.data(), false, "GrassTest::SourceIndices");
	auto fallback = makeBuffer(sizeof(native), D3D11_BIND_VERTEX_BUFFER, native.data(), false, "GrassTest::NativeRecords");
	auto output = makeBuffer(UINT(indices.size() * outputStride), D3D11_BIND_STREAM_OUTPUT, nullptr, false, "GrassTest::DecodedRecords");
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = DXGI_FORMAT_R32_TYPELESS;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	view.BufferEx.NumElements = UINT(records.size() * sizeof(Record) / 4);
	view.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	ComPtr<ID3D11ShaderResourceView> srv;
	Check(device->CreateShaderResourceView(source.Get(), &view, srv.GetAddressOf()));
	Util::SetResourceName(srv.Get(), "GrassTest::ExactSource SRV");
	D3D11_BUFFER_DESC stagingDesc{};
	output->GetDesc(&stagingDesc);
	stagingDesc.BindFlags = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Buffer> staging;
	Check(device->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::DecodedReadback");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "GrassBatch");
	context->VSSetShader(shader.Get(), nullptr, 0);
	context->GSSetShader(stream.Get(), nullptr, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
	for (bool indexed : { false, true }) {
		params.SetVariable("GrassBatchEnabled", UINT(indexed));
		context->UpdateSubresource(params.buffer.Get(), 0, nullptr, params.bytes.data(), 0, 0);
		auto* constants = params.buffer.Get();
		context->VSSetConstantBuffers(params.slot, 1, &constants);
		auto* resource = indexed ? srv.Get() : nullptr;
		context->VSSetShaderResources(3, 1, &resource);
		std::array<D3D11_INPUT_ELEMENT_DESC, 4> elements{};
		for (UINT i = 0; i < elements.size(); ++i)
			elements[i] = { "TEXCOORD", 4 + i, indexed ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT,
				1, indexed ? 0u : i * 8, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
		ComPtr<ID3D11InputLayout> layout;
		Check(device->CreateInputLayout(elements.data(), UINT(elements.size()), code->GetBufferPointer(), code->GetBufferSize(), layout.GetAddressOf()));
		Util::SetResourceName(layout.Get(), "GrassTest::SourceIndexLayout");
		context->IASetInputLayout(layout.Get());
		auto* input = indexed ? compacted.Get() : fallback.Get();
		const UINT stride = indexed ? sizeof(float) : sizeof(Record), offset = 0;
		context->IASetVertexBuffers(1, 1, &input, &stride, &offset);
		auto* target = output.Get();
		context->SOSetTargets(1, &target, &offset);
		context->DrawInstanced(1, UINT(indices.size()), 0, 0);
		target = nullptr;
		context->SOSetTargets(1, &target, &offset);
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		const auto* actual = static_cast<const uint32_t*>(mapped.pData);
		bool matches = true;
		for (size_t i = 0; i < indices.size(); ++i)
			for (size_t component = 0; component < 16; ++component)
				matches &= actual[i * 16 + component] == std::bit_cast<uint32_t>(DirectX::PackedVector::XMConvertHalfToFloat(native[i][component]));
		context->Unmap(staging.Get(), 0);
		Require(matches, indexed ? "Source-index IA/VS path changed native half values" : "Native IA fallback changed half values");
	}
}
