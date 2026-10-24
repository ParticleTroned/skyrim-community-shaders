// Uses the production adapter and the texture fixtures in nr_model_resolution_shader_test.cpp.
void CheckPeripheralAdapter(ID3D11Device* device, ID3D11DeviceContext* context, bool mixedGuides)
{
	using namespace NeuralRendering;
	constexpr Size source{ 127, 75 };
	const Size guides = mixedGuides ? Size{ 83, 49 } : source;
	std::vector<Pixel> colors(source[0] * source[1]);
	for (UINT i = 0; i < colors.size(); ++i)
		colors[i] = { float(i % 13) * 0.125f - 0.5f, float(i % 7) * 1.25f, 0.4f, float(i % 5) * 0.125f };
	std::vector<float> depths(guides[0] * guides[1]);
	std::vector<Motion> velocities(depths.size());
	for (UINT i = 0; i < depths.size(); ++i) {
		depths[i] = float(i % 79) / 79.0f;
		velocities[i] = { float(int(i % 7) - 3) * 0.25f, float(int(i % 5) - 2) * 0.001f };
	}
	Texture input(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, colors);
	Texture depth(device, guides, DXGI_FORMAT_R32_FLOAT, depths);
	Texture motion(device, guides, DXGI_FORMAT_R32G32_FLOAT, velocities);
	std::array outputs{ Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors.size(), sentinel)),
		Texture(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors.size(), sentinel)) };
	RendererApplyArgs base;
	base.device = device;
	base.context = context;
	base.colorInput = input.texture.Get();
	base.depthGuide = depth.texture.Get();
	base.depthGuideSRV = depth.srv.Get();
	base.motionVectors = motion.texture.Get();
	base.colorWidth = base.outputWidth = source[0];
	base.colorHeight = base.outputHeight = source[1];
	base.guideWidth = guides[0];
	base.guideHeight = guides[1];
	base.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(guides[0], guides[1], source[0], source[1]);
	base.renderingMode = mixedGuides ? RenderingMode::FullResolution : RenderingMode::ReducedResolution;
	base.featureUpscaling = mixedGuides;
	base.reset = true;
	base.reconstructionFilter = AdapterCompiler::filter;
	base.peripheralCompressionEnabled = true;
	ComPtr<ID3D11DeviceContext1> context1;
	Check(context->QueryInterface(IID_PPV_ARGS(&context1)));
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = 4096;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> previous;
	Check(device->CreateBuffer(&desc, nullptr, &previous));
	Util::SetResourceName(previous.Get(), "NRPeripheralTest::PreviousConstants");
	auto* raw = previous.Get();
	const UINT first = 16, count = 32;
	context1->CSSetConstantBuffers1(1, 1, &raw, &first, &count);
	const auto stateRestored = [&] {
		ComPtr<ID3D11Buffer> observed;
		UINT observedFirst = 0, observedCount = 0;
		context1->CSGetConstantBuffers1(1, 1, &observed, &observedFirst, &observedCount);
		Require(observed == previous && observedFirst == first && observedCount == count, "Peripheral dispatch corrupted b1 window");
	};
	for (UINT percent : { 30u, 67u, 100u }) {
		base.modelResolutionPercent = percent;
		std::array args{ base, base };
		for (UINT eye = 0; eye < args.size(); ++eye) {
			args[eye].featureSlot = eye;
			args[eye].colorOutput = outputs[eye].texture.Get();
			if (!mixedGuides && percent != 30)
				args[eye].modelOutputSelection = ModelOutputSelection{ outputs[eye].uav };
		}
		ModelResolution adapter;
		HRESULT result = S_OK;
		Require(adapter.PreparePeripheralCompression(args, false, result) && args[0].peripheralCompression.Active(), "Eligible compression did not plan");
		const auto plan = args[0].peripheralCompression;
		const Size model{ plan.model.width, plan.model.height }, grid{ plan.guides.width, plan.guides.height };
		Require(plan.candidatePixels < plan.baselinePixels && adapter.AdditionalBytes(args).has_value(), "Compressed storage was not admitted");
#ifdef DEVBENCH_BRIDGE_ENABLED
		for (const auto mode : { PeripheralCompressionOverride::Ordinary, PeripheralCompressionOverride::Identity }) {
			auto baseline = args;
			for (auto& eye : baseline) {
				eye.peripheralOverride = mode;
				eye.modelOutputSelection.reset();
			}
			const auto compiled = AdapterCompiler::calls;
			Require(adapter.PreparePeripheralCompression(baseline, false, result) && AdapterCompiler::calls == compiled,
				"Baseline comparison compiled a compression shader");
			const auto& ordinary = baseline[0].peripheralCompression;
			Require(!ordinary.Active() && ordinary.source == plan.source && ordinary.model == plan.ordinaryModel &&
						ordinary.guides == plan.ordinaryGuides && ordinary.mapping == PeripheralMapping{} &&
						ordinary.candidatePixels == ordinary.baselinePixels,
				"Baseline comparison lost its actual geometry");
			Require(percent != 100 || !ModelResolution::Required(baseline[0]), "Identity comparison added preparation at 100 percent");
		}
#endif
		// Backend retirement may invalidate caches after admission; this transaction retains its shaders.
		adapter.Reset();
		ModelResolution::Batch batch;
		std::array<ModelResolution::Targets, 2> targets{};
		const auto own = [](const Texture& texture) { return Color::Texture{ texture.texture, texture.srv, texture.uav }; };
		if (percent != 30) {
			for (auto& target : targets) {
				target.depth = own(Texture(device, grid, DXGI_FORMAT_R32_FLOAT, std::vector(grid[0] * grid[1], 1.0f)));
				target.motion = own(Texture(device, grid, DXGI_FORMAT_R32G32_FLOAT, std::vector(grid[0] * grid[1], Motion{})));
				if (percent == 100) {
					target.color = own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(model[0] * model[1], sentinel)));
					target.output = own(Texture(device, model, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(model[0] * model[1], sentinel)));
				}
			}
		}
		Require(adapter.Prepare(args, batch, result, percent == 30 ? std::span<const ModelResolution::Targets>{} : std::span<const ModelResolution::Targets>(targets)), "Compressed production preparation failed");
		stateRestored();
		Require(batch.arguments[0].colorWidth == model[0] && batch.arguments[0].guideWidth == grid[0] &&
					batch.arguments[0].computeSubrect.Area() == plan.candidatePixels &&
					batch.arguments[0].modelResolutionHistory->peripheralMapping == plan.mapping,
			"Projected native geometry/history disagrees with plan");
		const auto prepared = Texture(batch.targets[0].color, model).Read<Pixel>(device, context);
		for (UINT y = 0; y < model[1]; ++y)
			for (UINT x = 0; x < model[0]; ++x) {
				const float lx = std::max(0.0f, plan.mapping.Inverse(float(x) / model[0], 0) * source[0]);
				const float ly = std::max(0.0f, plan.mapping.Inverse(float(y) / model[1], 1) * source[1]);
				const float ux = std::min(float(source[0]), plan.mapping.Inverse(float(x + 1) / model[0], 0) * source[0]);
				const float uy = std::min(float(source[1]), plan.mapping.Inverse(float(y + 1) / model[1], 1) * source[1]);
				Pixel sum{};
				float area = 0;
				for (UINT sy = UINT(std::floor(ly)); sy < UINT(std::ceil(uy)); ++sy)
					for (UINT sx = UINT(std::floor(lx)); sx < UINT(std::ceil(ux)); ++sx) {
						const float weight = (std::min(ux, float(sx + 1)) - std::max(lx, float(sx))) * (std::min(uy, float(sy + 1)) - std::max(ly, float(sy)));
						area += weight;
						for (UINT c = 0; c < 4; ++c) sum[c] += colors[sy * source[0] + sx][c] * weight;
					}
				for (UINT c = 0; c < 4; ++c)
					Require(std::abs(prepared[y * model[0] + x][c] - sum[c] / area) < 0.0005f, "Nonlinear area filter differs from source-cell integral");
			}
		const auto sampledDepth = Texture(batch.targets[0].depth, grid).Read<float>(device, context);
		const auto sampledMotion = Texture(batch.targets[0].motion, grid).Read<Motion>(device, context);
		for (UINT y = 0; y < grid[1]; ++y)
			for (UINT x = 0; x < grid[0]; ++x) {
				const UINT lx = UINT(std::floor(std::max(0.0f, plan.mapping.Inverse(float(x) / grid[0], 0) * guides[0])));
				const UINT ly = UINT(std::floor(std::max(0.0f, plan.mapping.Inverse(float(y) / grid[1], 1) * guides[1])));
				const UINT ux = std::min(guides[0], UINT(std::ceil(plan.mapping.Inverse(float(x + 1) / grid[0], 0) * guides[0])));
				const UINT uy = std::min(guides[1], UINT(std::ceil(plan.mapping.Inverse(float(y + 1) / grid[1], 1) * guides[1])));
				float nearest = 2;
				Motion velocity{};
				for (UINT sy = ly; sy < uy; ++sy)
					for (UINT sx = lx; sx < ux; ++sx) {
						const auto index = sy * guides[0] + sx;
						if (depths[index] < nearest) {
							nearest = depths[index];
							velocity = velocities[index];
						}
					}
				Near(sampledDepth[y * grid[0] + x], nearest, "Peripheral depth surface changed");
				for (UINT axis = 0; axis < 2; ++axis) {
					const float anchor = plan.mapping.Inverse((float(axis ? y : x) + 0.5f) / grid[axis], axis);
					Near(sampledMotion[y * grid[0] + x][axis], plan.mapping.Forward(anchor + velocity[axis], axis) - plan.mapping.Forward(anchor, axis), "Peripheral endpoint motion differs");
				}
			}
		for (bool residual : { false, true }) {
			for (UINT eye = 0; eye < args.size(); ++eye) {
				auto native = prepared;
				for (auto& pixel : native) {
					pixel[0] += residual ? 0.125f : 0.0f;
					pixel[1] -= residual ? 0.25f : 0.0f;
					pixel[3] = 1;
				}
				context->UpdateSubresource(batch.targets[eye].output.resource.Get(), 0, nullptr, native.data(), model[0] * sizeof(Pixel), 0);
			}
			Require(adapter.Reconstruct(args, batch, result), "Compressed residual reconstruction failed");
			stateRestored();
			adapter.Commit(args, batch);
			for (const auto& output : outputs) {
				const auto pixels = output.Read<Pixel>(device, context);
				for (UINT i = 0; i < pixels.size(); ++i)
					for (UINT c = 0; c < 4; ++c)
						Near(pixels[i][c], colors[i][c] + (residual ? c == 0 ? 0.125f : c == 1 ? -0.25f :
						                                                                         0 :
																	  0),
							"Compressed residual changed detail, HDR or alpha");
			}
		}
		if (percent == 100) {
			adapter.InvalidateShaders();
			AdapterCompiler::failPeripheral = true;
			Require(adapter.PreparePeripheralCompression(args, false, result) && args[0].peripheralCompression.reason == PeripheralCompressionReason::ShaderUnavailable,
				"Optional shader failure did not fall back before allocation");
			const auto attempts = AdapterCompiler::peripheralAttempts;
			Require(adapter.PreparePeripheralCompression(args, false, result) && AdapterCompiler::peripheralAttempts == attempts, "Optional shader failure retried every frame");
			AdapterCompiler::failPeripheral = false;
			adapter.InvalidateShaders();
			Require(adapter.PreparePeripheralCompression(args, false, result) && args[0].peripheralCompression.Active(), "Invalidation did not restore compression shaders");
		}
		auto stale = args;
		stale[1].peripheralCompression.mapping.work[0] = 1;
		Require(!adapter.Reconstruct(stale, batch, result), "Stale mapping reached reconstruction");
		args[1].guidedReconstructionEnabled = true;
		args[1].modelResolutionPercent = 50;
		Require(adapter.PreparePeripheralCompression(args, false, result) && !args[0].peripheralCompression.Active() && !args[1].peripheralCompression.Active(), "Unsupported eye failed to retain ordinary pair");
	}
	ID3D11Buffer* empty = nullptr;
	context->CSSetConstantBuffers(1, 1, &empty);
}
