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
	ValidateWindLayout(reflection.Get(), "NativeGeometry", vr);
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::CullCS");
	D3D11_SHADER_BUFFER_DESC parameterLayout{};
	D3D11_SHADER_VARIABLE_DESC indexCountLayout{}, strengthLayout{}, weightLayout{};
	const auto parameterReflection = reflection->GetConstantBufferByName("Parameters");
	Check(parameterReflection->GetDesc(&parameterLayout));
	Check(parameterReflection->GetVariableByName("IndexCounts")->GetDesc(&indexCountLayout));
	Check(parameterReflection->GetVariableByName("CollisionStrength")->GetDesc(&strengthLayout));
	Check(parameterReflection->GetVariableByName("MeshWeights")->GetDesc(&weightLayout));
	Require(weightLayout.StartOffset == 192 && weightLayout.Size == 16, "Drawable mesh costs changed the CPU parameter layout");
	Require(strengthLayout.StartOffset == 172 && strengthLayout.Size == sizeof(float), "Collision strength changed the CPU culling layout");
	Require(parameterLayout.Size == 208 && indexCountLayout.StartOffset == 176 && indexCountLayout.Size == 16, "Indirect index counts changed the CPU parameter layout");
	const UINT eyes = vr ? 2 : 1, capacity = 128;
	using Record = std::array<uint32_t, 8>;
	const std::array<Record, 3> records{ Record{ 0, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 },
		Record{ 0x4400, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 },
		Record{ 0xb800, 0x3c003800, 0x3c00, 0, 0x3c000000, 0x3c000000, 0, 0 } };
	struct Slice
	{
		UINT first, count, end, padding;
		std::array<float, 4> origin;
	};
	const std::array<Slice, 3> slices{ Slice{}, Slice{ 0, 1, 1, 0, { 0, 0, 0, .125f } }, Slice{ 1, 2, 3, 0, { 0, 0, 0, .875f } } };
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
	std::vector<uint32_t> sentinel(capacity * eyes * 3, 0xdeadbeef);
	auto output = makeBuffer(UINT(sentinel.size() * sizeof(uint32_t)), D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, sentinel.data(), "GrassTest::Output");
	auto extras = makeBuffer(capacity * eyes * 3 * 24, D3D11_BIND_UNORDERED_ACCESS, 24, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, nullptr, "GrassTest::Extras");
	std::array<uint32_t, 48> initialArguments;
	initialArguments.fill(0xdeadbeef);
	auto args = makeBuffer(sizeof(initialArguments), D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, initialArguments.data(), "GrassTest::Args");
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
	const auto makeOutputViews = [&]() {
		std::array<ID3D11Buffer*, 4> buffers{ output.Get(), extras.Get(), args.Get(), counters.Get() };
		for (UINT i = 0; i < views.size(); ++i) {
			D3D11_BUFFER_DESC desc{};
			buffers[i]->GetDesc(&desc);
			D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
			view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			view.Format = i == 1 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
			view.Buffer.NumElements = desc.ByteWidth / (i == 1 ? 24 : 4);
			view.Buffer.Flags = i == 1 ? 0 : D3D11_BUFFER_UAV_FLAG_RAW;
			Check(device->CreateUnorderedAccessView(buffers[i], &view, views[i].ReleaseAndGetAddressOf()));
			Util::SetResourceName(views[i].Get(), "GrassTest::Output%u UAV", i);
		}
	};
	makeOutputViews();
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
	params.SetVariable("SliceOffset", 1u);
	params.SetVariable("Capacity", capacity);
	params.SetVariable("EyeCount", eyes);
	std::array<UINT, 4> indexCounts{ 9, 6, 3, 0 };
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .05f });
	params.SetVariable("QualityRadius", .05f);
	params.SetVariable("MeshWeights", std::array<float, 4>{ 1, 1, 1, 1 });
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
	const auto dispatch = [&](bool frustum, bool density, bool hiZ, bool lod, UINT groups = 1) {
		params.SetVariable("IndexCounts", indexCounts);
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
		context->Dispatch(groups, 1, 1);
		uavs.fill(nullptr);
		context->CSSetUnorderedAccessViews(0, 4, uavs.data(), nullptr);
		const auto values = read(args.Get());
		for (UINT slot = 0; slot < 6; ++slot) {
			const UINT expectedIndices = slot < 3 * eyes ? indexCounts[slot / eyes] : 0;
			Require(values[slot * 8 + 3] == expectedIndices, "Indirect draw has stale or missing mesh index counts");
			for (UINT word = 0; word < 8; ++word)
				if (word != 3 && word != 4)
					Require(values[slot * 8 + word] == 0, "Indirect draw offsets or padding were not reset");
		}
		return values;
	};
	auto values = dispatch(false, false, false, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 3, "Full-density mode dropped an instance");
	auto counts = read(counters.Get());
	Require(counts[10] == 3 * eyes && counts[11] == 3 * eyes, "Missing-pyramid diagnostics do not reconcile");
	const auto sourceIndex = [](uint32_t encoded, size_t limit) {
		const float value = std::bit_cast<float>(encoded);
		Require(std::isfinite(value) && value >= 0 && value < limit && value == std::floor(value), "Invalid survivor source index");
		return UINT(value);
	};
	auto compacted = read(output.Get());
	auto fades = read(extras.Get());
	for (UINT eye = 0; eye < eyes; ++eye) {
		for (UINT instance = 0; instance < 3; ++instance) {
			const auto index = sourceIndex(compacted[eye * capacity + instance], records.size());
			auto original = records.begin() + index;
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
		Require(compacted[eye * capacity + 3] == 0xdeadbeef, "Dispatch wrote beyond survivor count");
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
	params.SetVariable("MeshWeights", std::array<float, 4>{ 16, 8, 1, 1 });
	params.SetVariable("MeshCostBias", 1.0f);
	params.SetVariable("CostBiasStartDistance", 0.0f);
	params.SetVariable("RenderDistance", 2.0f);
	params.SetVariable("OverrideDistance", 1u);
	params.SetVariable("EdgeFadeStart", .85f);
	for (bool lod : { false, true }) {
		values = dispatch(true, false, false, lod);
		for (UINT eye = 0; eye < eyes; ++eye)
			Require(values[(lod ? 2 * eyes + eye : eye) * 8 + 4] == (lod ? 2u : 0u), "Distance cost did not follow the drawable LOD");
	}
	params.SetVariable("RenderDistance", 0.0f);
	params.SetVariable("FullPixels", 2.1f);
	for (bool lod : { false, true }) {
		values = dispatch(true, true, false, lod);
		for (UINT eye = 0; eye < eyes; ++eye)
			Require(values[(lod ? 2 * eyes + eye : eye) * 8 + 4] == (lod ? 2u : 0u), "Density cost did not follow the drawable LOD");
	}
	params.SetVariable("FarPixels", .01f);
	params.SetVariable("MeshWeights", std::array<float, 4>{ 16, 1, 8, 1 });
	values = dispatch(true, true, false, true);
	for (UINT eye = 0; eye < eyes; ++eye)
		Require(values[(eyes + eye) * 8 + 4] == 2, "Mid LOD inherited another tier's mesh cost");
	params.SetVariable("CostBiasStartDistance", 6000.0f);
	values = dispatch(true, true, false, false);
	for (UINT eye = 0; eye < eyes; ++eye)
		Require(values[eye * 8 + 4] == 2, "Near grass lost mesh-cost protection");
	params.SetVariable("MeshWeights", std::array<float, 4>{ 1, 1, 1, 1 });
	params.SetVariable("MeshCostBias", 0.0f);
	params.SetVariable("FullPixels", 16.0f);
	params.SetVariable("FarPixels", 64.0f);
	params.SetVariable("OverrideDistance", 0u);
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, 1.1f });
	params.SetVariable("CollisionEnabled", 1u);
	params.SetVariable("CollisionStrength", 1.0f);
	for (float distance : { 0.0f, 1.0f, 2048.0f }) {
		params.SetVariable("CollisionDistance", distance);
		values = dispatch(true, false, false, false);
		for (UINT eye = 0; eye < eyes; ++eye)
			Require(values[eye * 8 + 4] == (distance == 2048 ? 3u : 2u), "Collision distance did not tighten only unreachable collision bounds");
	}
	params.SetVariable("CollisionDistance", 2048.0f);
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, 0, .5f });
	for (float strength : { 0.0f, 1.0f, 3.0f, 5.0f }) {
		params.SetVariable("CollisionStrength", strength);
		for (UINT enabled : { 0u, 1u }) {
			params.SetVariable("CollisionEnabled", enabled);
			for (float distance : { 0.0f, 1.0f, 2048.0f }) {
				params.SetVariable("CollisionDistance", distance);
				values = dispatch(true, false, false, false);
				for (UINT eye = 0; eye < eyes; ++eye)
					Require(values[eye * 8 + 4] == (enabled && distance == 2048 && strength > 1 ? 3u : 2u),
						"Collision strength expanded bounds outside its enabled, reachable area");
			}
		}
	}
	params.SetVariable("CollisionStrength", 1.0f);
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
	params.SetVariable("ModelBound", std::array<float, 4>{ 0, 0, .5f, .8f });
	values = dispatch(false, false, true, false);
	for (UINT eye = 0; eye < eyes; ++eye) Require(values[eye * 8 + 4] == 3, "Oversized depth footprints did not fail open");
	counts = read(counters.Get());
	Require(counts[14] == eyes, "Oversized depth footprints were counted more than once");
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
			if (records[sourceIndex(compacted[eye * capacity + instance], records.size())] == record)
				return true;
		return false;
	};
	Require(contains(0, records[1]) && !contains(0, records[2]), "Left eye did not use its shared frustum");
	if (vr)
		Require(!contains(1, records[1]) && contains(1, records[2]), "Right eye used the left frustum");
	frustum.SetVariable("CullingPlanesValid", 0u);
	frustum.Bind(context.Get());
	std::array<Slice, 3> sparse{ Slice{}, Slice{ 2, 1, 1, 0, { 0, 0, 0, .625f } }, Slice{} };
	context->UpdateSubresource(table.Get(), 0, nullptr, sparse.data(), 0, 0);
	params.SetVariable("SliceCount", 1u);
	params.SetVariable("InstanceCount", 1u);
	values = dispatch(false, false, false, false);
	compacted = read(output.Get());
	fades = read(extras.Get());
	for (UINT eye = 0; eye < eyes; ++eye) {
		Require(values[eye * 8 + 4] == 1 && sourceIndex(compacted[eye * capacity], records.size()) == 2, "Filtered slices lost their raw record offset");
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
		stereo->SetVariable("StereoEnabled", 1.0f);
		stereo->Bind(context.Get());
	}

	// Verify multisets, paired extras and canaries; compaction order is deliberately unspecified.
	for (const UINT outputCapacity : { 256u, GrassPolicy::kMaxBatchInstances }) {
		const UINT outputSlots = outputCapacity * eyes * 3 + 1;
		std::vector<Record> manyRecords(outputCapacity, records[0]);
		for (UINT i = 0; i < outputCapacity; ++i) {
			manyRecords[i][7] = (i & 1023u) | ((i >> 10) << 16);
			const float z = i % 3 == 0 ? .025f : i % 3 == 1 ? .5f :
			                                                  4.0f;
			manyRecords[i][1] = (manyRecords[i][1] & 0xffff0000u) | DirectX::PackedVector::XMConvertFloatToHalf(z);
		}
		auto manyInput = makeBuffer(UINT(manyRecords.size() * sizeof(Record)), D3D11_BIND_SHADER_RESOURCE, 0,
			D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, manyRecords.data(), "GrassTest::BoundaryInput");
		inputView.BufferEx.NumElements = outputCapacity * 8;
		Check(device->CreateShaderResourceView(manyInput.Get(), &inputView, inputSRV.ReleaseAndGetAddressOf()));
		Util::SetResourceName(inputSRV.Get(), "GrassTest::BoundaryInput SRV");
		output = makeBuffer(outputSlots * 4, D3D11_BIND_UNORDERED_ACCESS, 0,
			D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, nullptr, "GrassTest::BoundaryOutput");
		std::vector<uint32_t> extraSentinels(outputSlots * 6, 0xdeadbeef);
		extras = makeBuffer(outputSlots * 24, D3D11_BIND_UNORDERED_ACCESS, 24,
			D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, extraSentinels.data(), "GrassTest::BoundaryExtras");
		makeOutputViews();
		params.SetVariable("Capacity", outputCapacity);
		params.SetVariable("BandPixels", 0.0f);
		params.SetVariable("MidPixels", 1e9f);
		const auto resetOutputs = [&]() {
			const UINT canary[4]{ 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
			context->ClearUnorderedAccessViewUint(views[0].Get(), canary);
			context->UpdateSubresource(extras.Get(), 0, nullptr, extraSentinels.data(), 0, 0);
		};
		for (bool diagnostic : { true, false }) {
			std::vector<D3D_SHADER_MACRO> boundaryDefines;
			if (vr)
				boundaryDefines.push_back({ "VR", "1" });
			if (diagnostic)
				boundaryDefines.push_back({ "GRASS_DIAGNOSTICS", "1" });
			auto boundaryCode = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl", "cs_5_0", boundaryDefines);
			Check(device->CreateComputeShader(boundaryCode->GetBufferPointer(), boundaryCode->GetBufferSize(), nullptr, shader.ReleaseAndGetAddressOf()));
			Util::SetResourceName(shader.Get(), "GrassTest::BoundaryCullCS");
			const auto sizes = outputCapacity == 256 ? std::vector<UINT>{ 0, 1, 2, 31, 63, 64, 65, 127, 128, 129, 256 } : std::vector<UINT>{ outputCapacity };
			for (UINT instances : sizes) {
				const UINT split = instances / 2;
				const std::array<Slice, 3> manySlices{ Slice{}, Slice{ 0, split, split, 0, { 0, 0, 0, .25f } },
					Slice{ split, instances - split, instances, 0, { 0, 0, 0, .75f } } };
				context->UpdateSubresource(table.Get(), 0, nullptr, manySlices.data(), 0, 0);
				params.SetVariable("SliceCount", 2u);
				params.SetVariable("InstanceCount", instances);
				const UINT groups = (std::max)(1u, (instances + 63) / 64);
				for (UINT tier = 0; tier < 4; ++tier) {
					resetOutputs();
					indexCounts = tier == 0 ? std::array<UINT, 4>{ 18, 0, 0, 0 } : std::array<UINT, 4>{ 18, 12, 6, 0 };
					params.SetVariable("MidPixels", tier == 3 ? 8.0f : 1e9f);
					params.SetVariable("FarPixels", tier == 2 ? 1e9f : tier == 3 ? 4.0f :
																				   0.0f);
					values = dispatch(false, false, false, tier != 0, groups);
					compacted = read(output.Get());
					fades = read(extras.Get());
					for (UINT slot = 0; slot < 6; ++slot) {
						const UINT expectedInstances = slot >= 3 * eyes ? 0 : tier == 3       ? instances / 3 + (slot / eyes < instances % 3 ? 1u : 0u) :
						                                                  slot / eyes == tier ? instances :
						                                                                        0;
						Require(values[slot * 8 + 4] == expectedInstances, "Compaction lost or accumulated survivors at a group boundary");
						if (slot >= 3 * eyes)
							continue;
						std::vector<bool> seen(instances, false);
						for (UINT survivor = 0; survivor < expectedInstances; ++survivor) {
							const UINT offset = slot * outputCapacity + survivor;
							const UINT record = sourceIndex(compacted[offset], instances);
							Require(record < instances && !seen[record], "Compaction duplicated a source or emitted an inactive lane");
							seen[record] = true;
							Require(tier != 3 || record % 3 == slot / eyes, "Compaction moved an instance to another LOD bin");
							Require(std::bit_cast<float>(fades[offset * 6 + 3]) == (record < split ? .25f : .75f), "Compaction detached a record from its fade");
							for (UINT axis = 0; axis < 3; ++axis)
								Require(fades[offset * 6 + axis] == 0, "Compaction changed the source origin");
							for (UINT time = 0; time < 2; ++time) {
								const float angle = .4f * (time == 0 ? 4.25f : 3.75f);
								const float wind = (std::sin(3.14159265358979323846f * std::sin(angle)) + std::sin(6.28318530717958647692f * std::sin(angle))) * .3f + .2f * std::cos(3.14159265358979323846f * std::cos(angle));
								Require(std::abs(std::bit_cast<float>(fades[offset * 6 + 4 + time]) - wind) < 1e-5f, "Compaction changed current or previous wind");
							}
						}
						if (expectedInstances < outputCapacity) {
							const UINT guard = slot * outputCapacity + expectedInstances;
							Require(compacted[guard] == 0xdeadbeef && fades[guard * 6] == 0xdeadbeef, "Compaction overwrote a bin canary");
						}
					}
					Require(compacted.back() == 0xdeadbeef && fades.back() == 0xdeadbeef, "Compaction exceeded output capacity");
					counts = read(counters.Get());
					Require(counts[0] == (diagnostic ? instances * eyes : 0), "Boundary diagnostics changed candidate counts");
				}
			}
			params.SetVariable("MinPixels", 1e8f);
			params.SetVariable("FullPixels", 2e8f);
			values = dispatch(false, true, false, false, outputCapacity / 64);
			for (UINT slot = 0; slot < 6; ++slot)
				Require(values[slot * 8 + 4] == 0, "Fully culled batch retained previous draw counts");
			params.SetVariable("SliceCount", 0u);
			values = dispatch(false, false, false, false, outputCapacity / 64);
			for (UINT slot = 0; slot < 6; ++slot)
				Require(values[slot * 8 + 4] == 0, "Missing slices emitted uninitialized lanes");
		}
	}
}

void RunDepthReduction(bool vr, UINT reduction = 2)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
	auto code = Compile(L"features/Grass Optimizations/Shaders/GrassOptimizations/GrassDepthCS.hlsl", "cs_5_0", { { "BASE_REDUCTION", reduction == 4 ? "4" : "2" } });
	ComPtr<ID3D11ShaderReflection> reflection;
	Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
	Util::SetResourceName(shader.Get(), "GrassTest::DepthCS");
	const UINT eyes = vr ? 2 : 1, extent = reduction + 1;
	std::vector<float> pixels(extent * eyes * extent, .2f);
	if (vr)
		for (UINT y = 0; y < extent; ++y)
			for (UINT x = extent; x < 2 * extent; ++x) pixels[y * 2 * extent + x] = .7f;
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
	auto input = texture(extent * eyes, extent, D3D11_BIND_SHADER_RESOURCE, pixels.data(), extent * eyes * 4, "GrassTest::DepthSource");
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
	params.SetVariable("SourceWidth", extent * eyes);
	params.SetVariable("SourceHeight", extent);
	params.SetVariable("Eyes", eyes);
	params.SetVariable("FirstLevel", 1u);
	params.SetVariable("ActiveWidth", extent * eyes);
	params.SetVariable("ActiveHeight", extent);
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
