// Included after the shader compiler helpers in grass_batch_shader_test.cpp.
void RunCullingShader(bool vr)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	std::vector<D3D_SHADER_MACRO> defines{ { "GRASS_DIAGNOSTICS", "1" } };
	if (vr)
		defines.push_back({ "VR", "1" });
	auto code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl", "cs_5_0", defines);
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::CullCS");
	const UINT eyes = vr ? 2 : 1, capacity = 64;
	using Record = std::array<uint32_t, 8>;
	const std::array<Record, 3> records{ Record{ 0, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 },
		Record{ 0x4400, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 },
		Record{ 0xb800, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 } };
	struct Slice
	{
		UINT first, count, end, padding;
		std::array<float, 4> origin;
	};
	const std::array<Slice, 2> slices{ Slice{ 0, 1, 1, 0, { 0, 0, 0, .125f } }, Slice{ 1, 2, 3, 0, { 0, 0, 0, .875f } } };
	const auto makeBuffer = [&](UINT bytes, UINT bind, UINT stride, UINT misc, const void* initial, const char* name) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.BindFlags = bind;
		desc.StructureByteStride = stride;
		desc.MiscFlags = misc;
		D3D11_SUBRESOURCE_DATA data{ initial, 0, 0 };
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&desc, initial ? &data : nullptr, buffer.GetAddressOf()));
		Util::SetResourceName(buffer.Get(), "%s", name);
		return buffer;
	};
	auto input = makeBuffer(sizeof(records), D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, records.data(), "GrassTest::Input");
	auto table = makeBuffer(sizeof(slices), D3D11_BIND_SHADER_RESOURCE, sizeof(Slice), D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, slices.data(), "GrassTest::Slices");
	std::vector<Record> sentinel(capacity * eyes * 3);
	for (auto& record : sentinel) record.fill(0xdeadbeef);
	auto output = makeBuffer(UINT(sentinel.size() * sizeof(Record)), D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, sentinel.data(), "GrassTest::Output");
	auto extras = makeBuffer(capacity * eyes * 3 * 24, D3D11_BIND_UNORDERED_ACCESS, 24, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, nullptr, "GrassTest::Extras");
	auto args = makeBuffer(192, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, nullptr, "GrassTest::Args");
	auto counters = makeBuffer(19 * sizeof(uint32_t), D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, nullptr, "GrassTest::Counters");
	ComPtr<ID3D11ShaderResourceView> inputSRV, tableSRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC inputView{};
	inputView.Format = DXGI_FORMAT_R32_TYPELESS;
	inputView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	inputView.BufferEx.NumElements = sizeof(records) / 4;
	inputView.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	Check(device->CreateShaderResourceView(input.Get(), &inputView, inputSRV.GetAddressOf()));
	Check(device->CreateShaderResourceView(table.Get(), nullptr, tableSRV.GetAddressOf()));
	Util::SetResourceName(inputSRV.Get(), "GrassTest::Input SRV");
	Util::SetResourceName(tableSRV.Get(), "GrassTest::Slices SRV");
	std::array<ComPtr<ID3D11UnorderedAccessView>, 4> views;
	std::array<ID3D11Buffer*, 4> buffers{ output.Get(), extras.Get(), args.Get(), counters.Get() };
	for (UINT i = 0; i < views.size(); ++i) {
		D3D11_BUFFER_DESC desc{};
		buffers[i]->GetDesc(&desc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		view.Format = i == 1 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
		view.Buffer.NumElements = desc.ByteWidth / (i == 1 ? 24 : 4);
		view.Buffer.Flags = i == 1 ? 0 : D3D11_BUFFER_UAV_FLAG_RAW;
		Check(device->CreateUnorderedAccessView(buffers[i], &view, views[i].GetAddressOf()));
		Util::SetResourceName(views[i].Get(), "GrassTest::Output%u UAV", i);
	}
	D3D11_TEXTURE2D_DESC depthDesc{};
	depthDesc.Width = eyes * 4;
	depthDesc.Height = 4;
	depthDesc.ArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	std::vector<float> depth(depthDesc.Width * 4, .2f);
	if (vr)
		for (UINT y = 0; y < 4; ++y)
			for (UINT x = 4; x < 8; ++x) depth[y * 8 + x] = 1;
	D3D11_SUBRESOURCE_DATA depthData{ depth.data(), depthDesc.Width * 4, 0 };
	ComPtr<ID3D11Texture2D> depthTexture;
	ComPtr<ID3D11ShaderResourceView> depthSRV;
	Check(device->CreateTexture2D(&depthDesc, &depthData, depthTexture.GetAddressOf()));
	Check(device->CreateShaderResourceView(depthTexture.Get(), nullptr, depthSRV.GetAddressOf()));
	Util::SetResourceName(depthTexture.Get(), "GrassTest::Depth");
	Util::SetResourceName(depthSRV.Get(), "GrassTest::Depth SRV");
	D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "Parameters");
	D3D11ShaderTest::ConstantBuffer native(device.Get(), reflection.Get(), "NativeGeometry");
	std::array<float, 32> matrices{};
	for (UINT eye = 0; eye < eyes; ++eye)
		for (UINT axis = 0; axis < 4; ++axis) matrices[eye * 16 + axis * 5] = 1;
	if (vr) {
		native.SetVariable("WorldViewProj", matrices);
		native.SetVariable("World", matrices);
	} else {
		std::array<float, 16> single{};
		std::copy_n(matrices.begin(), 16, single.begin());
		native.SetVariable("WorldViewProj", single);
		native.SetVariable("World", single);
	}
	std::unique_ptr<D3D11ShaderTest::ConstantBuffer> stereo;
	if (vr) {
		stereo = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), "StereoParameters");
		stereo->SetVariable("StereoEnabled", 1.0f);
		stereo->SetVariable("EyeOffsetScale", std::array<float, 2>{ -.5f, .5f });
		stereo->Bind(context.Get());
	}
	params.SetVariable("InstanceCount", 3u);
	params.SetVariable("SliceCount", 2u);
	params.SetVariable("Capacity", capacity);
	params.SetVariable("EyeCount", eyes);
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .05f });
	params.SetVariable("QualityRadius", .05f);
	params.SetVariable("MeshWeight", 1.0f);
	params.SetVariable("ViewHeight", 100.0f);
	params.SetVariable("MinPixels", 2.0f);
	params.SetVariable("FullPixels", 16.0f);
	params.SetVariable("MinDensity", 0.0f);
	params.SetVariable("BandPixels", 1.0f);
	params.SetVariable("MidPixels", 128.0f);
	params.SetVariable("FarPixels", 64.0f);
	params.SetVariable("DepthWidth", depthDesc.Width);
	params.SetVariable("DepthHeight", 4u);
	params.SetVariable("DepthBias", .001f);
	params.SetVariable("DepthUVScale", std::array<float, 4>{ 1, 1, 0, 0 });
	native.SetVariable("WindTimer", 4.25f);
	native.SetVariable("PreviousWindTimer", 3.75f);
	native.Bind(context.Get());
	const auto read = [&](ID3D11Buffer* source) {
		D3D11_BUFFER_DESC desc{};
		source->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Buffer> staging;
		Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
		Util::SetResourceName(staging.Get(), "GrassTest::Readback");
		context->CopyResource(staging.Get(), source);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<uint32_t> result(desc.ByteWidth / 4);
		std::memcpy(result.data(), mapped.pData, desc.ByteWidth);
		context->Unmap(staging.Get(), 0);
		return result;
	};
	const auto dispatch = [&](bool frustum, bool density, bool hiZ, bool lod) {
		params.SetVariable("FrustumEnabled", UINT(frustum));
		params.SetVariable("DensityEnabled", UINT(density));
		params.SetVariable("DepthMips", hiZ ? 1u : 0u);
		params.SetVariable("MidEnabled", UINT(lod));
		params.SetVariable("FarEnabled", UINT(lod));
		params.Bind(context.Get());
		const UINT zero[4]{};
		context->ClearUnorderedAccessViewUint(views[2].Get(), zero);
		context->ClearUnorderedAccessViewUint(views[3].Get(), zero);
		std::array<ID3D11ShaderResourceView*, 3> srvs{ inputSRV.Get(), tableSRV.Get(), depthSRV.Get() };
		std::array<ID3D11UnorderedAccessView*, 4> uavs{ views[0].Get(), views[1].Get(), views[2].Get(), views[3].Get() };
		context->CSSetShaderResources(0, 3, srvs.data());
		context->CSSetUnorderedAccessViews(0, 4, uavs.data(), nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		uavs.fill(nullptr);
		context->CSSetUnorderedAccessViews(0, 4, uavs.data(), nullptr);
		return read(args.Get());
	};
	auto values = dispatch(false, false, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 3, "Full-density mode dropped an instance");
	auto counts = read(counters.Get());
	Require(counts[10] == 3 * eyes && counts[11] == 3 * eyes, "Missing-pyramid diagnostics do not reconcile");
	auto compacted = read(output.Get());
	auto fades = read(extras.Get());
	for (UINT eye = 0; eye < eyes; ++eye) {
		for (UINT instance = 0; instance < 3; ++instance) {
			const auto start = (eye * capacity + instance) * 8;
			Record record{};
			std::copy_n(compacted.begin() + start, 8, record.begin());
			auto original = std::find(records.begin(), records.end(), record);
			Require(original != records.end(), "Packed instance record changed");
			const auto fade = std::bit_cast<float>(fades[(eye * capacity + instance) * 6 + 3]);
			Require(fade == (original == records.begin() ? .125f : .875f), "Sparse source fade changed");
			const float x = original == records.begin() ? 0.0f : original == records.begin() + 1 ? 4.0f :
			                                                                                       -.5f;
			for (UINT time = 0; time < 2; ++time) {
				const float angle = .4f * (x * -.0078125f + (time == 0 ? 4.25f : 3.75f));
				const float scalar = (std::sin(3.14159265358979323846f * std::sin(angle)) + std::sin(6.28318530717958647692f * std::sin(angle))) * .3f + .2f * std::cos(3.14159265358979323846f * std::cos(angle));
				Require(std::abs(std::bit_cast<float>(fades[(eye * capacity + instance) * 6 + 4 + time]) - scalar) < 1e-5f, "Precomputed wind differs from the native formula");
			}
		}
		Require(compacted[(eye * capacity + 3) * 8] == 0xdeadbeef, "Dispatch wrote beyond survivor count");
	}
	values = dispatch(true, false, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 2, "Frustum culling retained an outside instance or lost a visible one");
	counts = read(counters.Get());
	Require(counts[0] == 3 * eyes && counts[1] == eyes, "Frustum diagnostics do not reconcile");
	values = dispatch(true, false, true, false);
	Require(values[4] == 0, "Occluded grass survived");
	if (vr)
		Require(values[12] == 2, "Grass Hi-Z crossed the eye seam");
	counts = read(counters.Get());
	Require(counts[3] == 2, "Hi-Z diagnostics do not reconcile");
	Require(counts[10] == 2 * eyes && counts[11] == 0 && counts[18] > 0, "Hi-Z eligible and depth-cell diagnostics do not reconcile");
	if (vr)
		Require(counts[16] == 2, "Uncovered VR eye depth was not classified");
	values = dispatch(true, false, false, true);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[(2 * eyes + eye) * 8 + 4] == 2 && values[eye * 8 + 4] == 0, "Far LOD selection lost or duplicated instances");
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .7f });
	params.SetVariable("CollisionEnabled", 1u);
	for (float distance : { 0.0f, 1.0f, 2048.0f }) {
		params.SetVariable("CollisionDistance", distance);
		values = dispatch(true, false, false, false);
		for (UINT eye = 0; eye < eyes; ++eye)
			Require(values[eye * 8 + 4] == (distance == 2048 ? 3u : 2u), "Collision distance did not tighten only unreachable collision bounds");
	}
	params.SetVariable("CollisionEnabled", 0u);
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .05f });
	params.SetVariable("MinPixels", 64.0f);
	params.SetVariable("FullPixels", 128.0f);
	values = dispatch(true, true, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 0, "Density cutoff ignored projected size");
	counts = read(counters.Get());
	Require(counts[2] == 2 * eyes, "Density diagnostics do not reconcile");
	params.SetVariable("RenderDistance", 1.0f);
	params.SetVariable("OverrideDistance", 1u);
	params.SetVariable("EdgeFadeStart", .85f);
	values = dispatch(false, false, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 2, "Distance override lost resident near grass or retained distant grass");
	counts = read(counters.Get());
	Require(counts[8] == eyes, "Distance diagnostics do not reconcile");
	params.SetVariable("RenderDistance", 0.0f);
	params.SetVariable("InvisibleFadeCull", .2f);
	values = dispatch(false, false, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 2, "Fade cutoff ignored the native sparse group fade");
	counts = read(counters.Get());
	Require(counts[9] == eyes, "Fade diagnostics do not reconcile");
	params.SetVariable("InvisibleFadeCull", 0.0f);
	params.SetVariable("SimpleShadingPixelSize", 32.0f);
	values = dispatch(false, false, false, false);
	fades = read(extras.Get());
	for (UINT eye = 0; eye < eyes; ++eye)
		for (UINT instance = 0; instance < 3; ++instance)
			Require(std::bit_cast<float>(fades[(eye * capacity + instance) * 6 + 3]) < 0, "Simple shading flag lost fade magnitude");
	params.SetVariable("SimpleShadingPixelSize", 0.0f);
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, 1 });
	values = dispatch(false, false, true, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 3, "Near-plane-intersecting bounds were falsely occluded");
	counts = read(counters.Get());
	Require(counts[12] == 3 * eyes, "Near-plane projection failures were not classified");
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, std::numeric_limits<float>::quiet_NaN() });
	values = dispatch(true, false, true, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 3, "Invalid bounds failed to retain grass");
	counts = read(counters.Get());
	Require(counts[7] == 3 * eyes, "Invalid-bound diagnostics do not reconcile");
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .05f });
	D3D11ShaderTest::ConstantBuffer frustum(device.Get(), reflection.Get(), "FrustumParameters");
	std::array<std::array<float, 4>, 12> sharedPlanes{};
	for (auto& plane : sharedPlanes) plane = { 0, 0, 0, 1 };
	sharedPlanes[0] = { 1, 0, 0, .1f };
	sharedPlanes[6] = { -1, 0, 0, 1 };
	frustum.SetVariable("CullingPlanes", sharedPlanes);
	frustum.SetVariable("CullingPlanesValid", 1u);
	frustum.Bind(context.Get());
	values = dispatch(true, false, false, false);
	Require(values[4] == 2 && (!vr || values[12] == 2), "Shared per-eye planes changed visibility");
	compacted = read(output.Get());
	const auto contains = [&](UINT eye, const Record& record) {
		for (UINT instance = 0; instance < values[eye * 8 + 4]; ++instance)
			if (std::equal(record.begin(), record.end(), compacted.begin() + (eye * capacity + instance) * 8))
				return true;
		return false;
	};
	Require(contains(0, records[1]) && !contains(0, records[2]), "Left eye did not use its shared frustum");
	if (vr)
		Require(!contains(1, records[1]) && contains(1, records[2]), "Right eye used the left frustum");
	frustum.SetVariable("CullingPlanesValid", 0u);
	frustum.Bind(context.Get());
	std::array<Slice, 2> sparse{ Slice{ 2, 1, 1, 0, { 0, 0, 0, .625f } }, Slice{} };
	context->UpdateSubresource(table.Get(), 0, nullptr, sparse.data(), 0, 0);
	params.SetVariable("SliceCount", 1u);
	params.SetVariable("InstanceCount", 1u);
	values = dispatch(false, false, false, false);
	compacted = read(output.Get());
	fades = read(extras.Get());
	for (UINT eye = 0; eye < eyes; ++eye) {
		Require(values[eye * 8 + 4] == 1 && std::equal(records[2].begin(), records[2].end(), compacted.begin() + eye * capacity * 8), "Filtered slices lost their raw record offset");
		Require(std::bit_cast<float>(fades[eye * capacity * 6 + 3]) == .625f, "Filtered slice lost its fade");
	}
	context->UpdateSubresource(table.Get(), 0, nullptr, slices.data(), 0, 0);
	params.SetVariable("SliceCount", 2u);
	params.SetVariable("InstanceCount", 3u);
	if (vr) {
		params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .05f });
		stereo->SetVariable("StereoEnabled", 0.0f);
		stereo->Bind(context.Get());
		values = dispatch(false, false, false, false);
		Require(values[4] == 3 && values[12] == 0, "VR mono pass submitted duplicate eye geometry");
	}
}

void RunDepthReduction(bool vr)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassDepthCS.hlsl", "cs_5_0");
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::DepthCS");
	const UINT eyes = vr ? 2 : 1;
	std::vector<float> pixels(3 * eyes * 3, .2f);
	if (vr)
		for (UINT y = 0; y < 3; ++y)
			for (UINT x = 3; x < 6; ++x) pixels[y * 6 + x] = .7f;
	const auto texture = [&](UINT width, UINT height, UINT bind, const void* data, UINT pitch, const char* name) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.BindFlags = bind;
		D3D11_SUBRESOURCE_DATA initial{ data, pitch, 0 };
		ComPtr<ID3D11Texture2D> result;
		Check(device->CreateTexture2D(&desc, data ? &initial : nullptr, result.GetAddressOf()));
		Util::SetResourceName(result.Get(), "%s", name);
		return result;
	};
	auto input = texture(3 * eyes, 3, D3D11_BIND_SHADER_RESOURCE, pixels.data(), 3 * eyes * 4, "GrassTest::DepthSource");
	auto output = texture(2 * eyes, 2, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, 0, "GrassTest::DepthReduced");
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateShaderResourceView(input.Get(), nullptr, srv.GetAddressOf()));
	Check(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
	Util::SetResourceName(srv.Get(), "GrassTest::DepthSource SRV");
	Util::SetResourceName(uav.Get(), "GrassTest::DepthReduced UAV");
	D3D11ShaderTest::ConstantBuffer params(device.Get(), reflection.Get(), "Parameters");
	params.SetVariable("Width", 2 * eyes);
	params.SetVariable("Height", 2u);
	params.SetVariable("SourceWidth", 3 * eyes);
	params.SetVariable("SourceHeight", 3u);
	params.SetVariable("Eyes", eyes);
	params.SetVariable("FirstLevel", 1u);
	params.SetVariable("ActiveWidth", 3 * eyes);
	params.SetVariable("ActiveHeight", 3u);
	params.Bind(context.Get());
	auto source = srv.Get();
	auto target = uav.Get();
	context->CSSetShaderResources(0, 1, &source);
	context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
	context->CSSetShader(shader.Get(), nullptr, 0);
	context->Dispatch(1, 1, 1);
	target = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
	D3D11_TEXTURE2D_DESC desc{};
	output->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
	Util::SetResourceName(staging.Get(), "GrassTest::DepthReadback");
	context->CopyResource(staging.Get(), output.Get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
	bool correct = true;
	for (UINT y = 0; y < 2; ++y) {
		const auto row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
		for (UINT eye = 0; eye < eyes; ++eye)
			for (UINT x = 0; x < 2; ++x) {
				const float expected = x == 0 && y == 0 ? (eye == 0 ? .2f : .7f) : 1;
				correct &= row[eye * 2 + x] == expected;
			}
	}
	context->Unmap(staging.Get(), 0);
	Require(correct, "Odd depth edges or stereo seam were not conservatively reduced");
}
