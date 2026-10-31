// Uses the production adapter and the texture fixtures in nr_model_resolution_shader_test.cpp.
std::array<float, 4> PeripheralSamplingBounds(const NeuralRendering::PeripheralMapping& mapping, UINT x, UINT y, Size grid, Size source)
{
	auto [lower, upper] = mapping.SourceBounds({ float(x) / grid[0], float(y) / grid[1] },
		{ float(x + 1) / grid[0], float(y + 1) / grid[1] });
	for (unsigned axis = 0; axis < 2; ++axis) {
		lower[axis] = std::clamp(lower[axis] * source[axis], 0.0f, float(source[axis]));
		upper[axis] = std::clamp(upper[axis] * source[axis], 0.0f, float(source[axis]));
		if (upper[axis] <= lower[axis]) {
			lower[axis] = std::min(lower[axis], float(source[axis] - 1));
			upper[axis] = lower[axis] + 1.0f;
		}
	}
	return { lower[0], lower[1], upper[0], upper[1] };
}

std::optional<std::array<double, 3>> ReferencePeripheralResidual(std::span<const Pixel> prepared, std::span<const Pixel> neural,
	Size model, double px, double py, const Pixel& source, NeuralRendering::ReconstructionFilter filter, bool guided)
{
	auto result = ReferenceResidual(prepared, neural, model, { { 0, 0 }, model }, px, py, filter);
	if (!guided || !result)
		return result;
	double scale = std::max({ std::abs(double(source[0])), std::abs(double(source[1])), std::abs(double(source[2])) });
	std::array<Pixel, 4> proxy{}, output{};
	for (unsigned tap = 0; tap < 4; ++tap) {
		const auto x = std::clamp(int(std::floor(px)) + int(tap % 2), 0, int(model[0]) - 1);
		const auto y = std::clamp(int(std::floor(py)) + int(tap / 2), 0, int(model[1]) - 1);
		proxy[tap] = prepared[y * model[0] + x];
		output[tap] = neural[y * model[0] + x];
		for (unsigned c = 0; c < 3; ++c)
			scale = std::max(scale, std::abs(double(proxy[tap][c])));
	}
	if (scale <= 1e-6)
		return result;
	const double fx = px - std::floor(px), fy = py - std::floor(py);
	const std::array<double, 3> luminance{ 0.2125, 0.7154, 0.0721 };
	std::array<double, 3> sum{};
	double total = 0;
	for (unsigned tap = 0; tap < 4; ++tap) {
		double distance = 0;
		for (unsigned c = 0; c < 3; ++c) {
			const double d = (double(source[c]) - proxy[tap][c]) / scale;
			distance += luminance[c] * d * d;
		}
		const double weight = (tap % 2 ? fx : 1.0 - fx) * (tap / 2 ? fy : 1.0 - fy) / (1.0 + 16.0 * distance);
		total += weight;
		for (unsigned c = 0; c < 3; ++c)
			sum[c] += weight * (double(output[tap][c]) - proxy[tap][c]);
	}
	for (auto& c : sum) c /= total;
	return sum;
}

void CheckPeripheralResidual(ID3D11Device* device, ID3D11DeviceContext* context, NeuralRendering::ModelResolution& adapter,
	const NeuralRendering::ModelResolution::Batch& batch, const std::array<NeuralRendering::RendererApplyArgs, 2>& args,
	const std::array<Texture, 2>& outputs, std::span<const Pixel> colors, const std::array<std::vector<Pixel>, 2>& prepared,
	const Texture& input, std::span<const Texture> masks = {})
{
	const auto& plan = args[0].peripheralCompression;
	const Size source{ plan.source.width, plan.source.height }, model{ plan.model.width, plan.model.height };
	std::optional<Shader> blend;
	if (!masks.empty())
		blend.emplace(device, L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", AdapterCompiler::vr, "features/Upscaling/Shaders");
	for (unsigned condition : { 0u, 1u, 2u, 3u }) {
		auto native = prepared;
		for (UINT eye = 0; eye < args.size(); ++eye) {
			for (UINT i = 0; i < prepared[eye].size(); ++i) {
				auto& pixel = native[eye][i];
				pixel[0] += condition == 0 ? 0.0f : condition == 1 ? 0.125f :
				                                                     float(int((i * 7 + eye * 5) % 11) - 5) / 32.0f;
				pixel[1] += condition == 0 ? 0.0f : condition == 1 ? -0.25f :
				                                                     float(int((i / model[0] + eye) % 7) - 3) / 16.0f;
				pixel[3] = 1;
			}
			if (condition == 3) {
				native[eye][0][0] = std::numeric_limits<float>::quiet_NaN();
				native[eye][(model[1] / 2) * model[0] + model[0] / 2][1] = std::numeric_limits<float>::infinity();
			}
			context->UpdateSubresource(batch.targets[eye].output.resource.Get(), 0, nullptr, native[eye].data(), model[0] * sizeof(Pixel), 0);
		}
		HRESULT result = S_OK;
		Require(adapter.Reconstruct(args, batch, result), "Compressed residual reconstruction failed");
		adapter.Commit(args, batch);
		for (UINT eye = 0; eye < args.size(); ++eye) {
			std::optional<Texture> selected;
			if (!masks.empty() && !args[eye].modelOutputSelection) {
				selected.emplace(device, source, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors.size(), sentinel));
				NeuralRendering::ComputeStateGuard<8, 4, NeuralRendering::ComputeStatePolicy::ClearBindingsAndPredication, 3> guard(context);
				LegacySelect(device, context, *blend, source, outputs[eye], input, &masks[eye],
					NeuralRendering::ComputeSubrect{ 0, 0, source[0], source[1] }, false, *selected);
			}
			const auto pixels = (selected ? *selected : outputs[eye]).Read<Pixel>(device, context);
			const auto weights = masks.empty() ? std::vector<std::uint8_t>{} : masks[eye].Read<std::uint8_t>(device, context);
			for (UINT y = 0; y < source[1]; ++y)
				for (UINT x = 0; x < source[0]; ++x) {
					const auto i = y * source[0] + x;
					const auto position = args[eye].peripheralCompression.mapping.Forward({ (float(x) + 0.5f) / source[0], (float(y) + 0.5f) / source[1] });
					const double px = position[0] * model[0] - 0.5f, py = position[1] * model[1] - 0.5f;
					const auto residual = ReferencePeripheralResidual(prepared[eye], native[eye], model, px, py, colors[i], args[eye].reconstructionFilter,
						batch.reconstructionDecision.mode == NeuralRendering::GuidedReconstructionMode::GuidedLowResolution);
					const auto weight = weights.empty() ? 1.0f : float(weights[i]) / 255.0f;
					for (UINT channel = 0; channel < 3; ++channel)
						Near(pixels[i][channel], colors[i][channel] + (residual ? float((*residual)[channel]) * weight : 0.0f),
							"Compressed residual lost spatial phase, eye isolation or finite fallback");
					Require(pixels[i][3] == colors[i][3], "Compressed residual changed source alpha");
					if (weight == 0.0f)
						Require(pixels[i] == colors[i], "Compressed category selection changed a protected pixel");
				}
		}
	}
}

void CheckPeripheralAdapter(ID3D11Device* device, ID3D11DeviceContext* context, bool mixedGuides, bool shaped = false, unsigned compression = 20, bool sceneCategories = false, bool directScene = false)
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
	std::vector<Texture> masks;
	if (sceneCategories) {
		for (unsigned eye = 0; eye < outputs.size(); ++eye) {
			std::vector<std::uint8_t> weights(colors.size());
			constexpr std::array<std::uint8_t, 4> strengths{ 0, 64, 166, 255 };
			for (UINT i = 0; i < weights.size(); ++i)
				weights[i] = directScene && i % 17u == 0u ? 255u : strengths[(i % source[0] / 13u + i / source[0] / 7u + eye) % strengths.size()];
			masks.emplace_back(device, source, DXGI_FORMAT_R8_UNORM, weights);
		}
	}
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
	base.peripheralCompressionPercent = compression;
	base.guidedReconstructionEnabled = AdapterCompiler::guided;
	base.peripheralMask = { { 0.47f, 0.52f }, { 0.32f, 0.32f }, shaped };
	if (shaped)
		base.viewportCrop = { { guides[0] + 40, guides[1] + 20 }, { 20, 10, guides[0] + 20, guides[1] + 10 },
			{ source[0] + 60, source[1] + 30 }, { 30, 15, source[0] + 30, source[1] + 15 } };
	if (shaped && !mixedGuides) {
		base.viewportCrop.fullInput = base.viewportCrop.fullOutput;
		base.viewportCrop.input = base.viewportCrop.output;
	}
	std::optional<Texture> categorySource, authoredSelectionDepth, currentSelectionDepth;
	if (directScene) {
		Require(sceneCategories && !mixedGuides, "Direct scene fixture requires the pre-DLSS raster");
		const Size full{ base.viewportCrop.fullInput.width * 2u, base.viewportCrop.fullInput.height };
		using Tuple = std::array<std::uint16_t, 2>;
		std::vector<Tuple> tuples(full[0] * full[1]);
		constexpr std::array<std::uint16_t, 4> codes{ 85, 170, 255, 0 };
		for (unsigned eye = 0; eye < 2; ++eye)
			for (UINT y = 0; y < source[1]; ++y)
				for (UINT x = 0; x < source[0]; ++x) {
					const auto code = codes[(x / 13u + y / 7u + eye) % codes.size()];
					const auto index = (y + base.viewportCrop.input.top) * full[0] + eye * base.viewportCrop.fullInput.width + base.viewportCrop.input.left + x;
					tuples[index] = { 0u, static_cast<std::uint16_t>(code * 257u) };
				}
		categorySource.emplace(device, full, DXGI_FORMAT_R16G16_UNORM, tuples);
		authoredSelectionDepth.emplace(device, full, DXGI_FORMAT_R32_FLOAT, std::vector(full[0] * full[1], 0.75f));
		std::vector<float> visibleDepth(colors.size(), 1.0f);
		for (UINT i = 0; i < visibleDepth.size(); i += 17u)
			visibleDepth[i] = 0.25f;
		currentSelectionDepth.emplace(device, source, DXGI_FORMAT_R32_FLOAT, visibleDepth);
	}
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
	const auto stateRestored = [&](const char* stage) {
		ComPtr<ID3D11Buffer> observed;
		UINT observedFirst = 0, observedCount = 0;
		context1->CSGetConstantBuffers1(1, 1, &observed, &observedFirst, &observedCount);
		if (observed != previous || observedFirst != first || observedCount != count)
			std::cerr << stage << ": peripheral b1 expected " << previous.Get() << ' ' << first << '/' << count << ", observed "
					  << observed.Get() << ' ' << observedFirst << '/' << observedCount << '\n';
		Require(observed == previous && observedFirst == first && observedCount == count, "Peripheral dispatch corrupted b1 window");
	};
	stateRestored("fixture setup");
	for (UINT percent : { 30u, 67u, 100u }) {
		base.modelResolutionPercent = percent;
		std::array args{ base, base };
		for (UINT eye = 0; eye < args.size(); ++eye) {
			args[eye].featureSlot = eye;
			if (shaped && eye == 1)
				args[eye].peripheralMask.center[0] = 1.0f - args[eye].peripheralMask.center[0];
			args[eye].colorOutput = outputs[eye].texture.Get();
			if (!mixedGuides && percent != 30)
				args[eye].modelOutputSelection = ModelOutputSelection{ outputs[eye].uav };
			if (sceneCategories) {
				auto& inputArgs = args[eye];
				const ComputeSubrect full{ 0, 0, source[0], source[1] };
				inputArgs.sceneCharacterStrengths = inputArgs.characterVisualIsolation = true;
				inputArgs.actorSelection = masks[eye].srv;
				inputArgs.actorSelectionSupport = inputArgs.computeSubrect = full;
				inputArgs.roi = BuildRoiDescriptor(full, full, { source[0], source[1] }, false);
				if (directScene)
					inputArgs.modelOutputSelection = ModelOutputSelection{ outputs[eye].uav };
				if (inputArgs.modelOutputSelection) {
					inputArgs.modelOutputSelection->mask = masks[eye].srv;
					inputArgs.modelOutputSelection->maskSupport = full;
					if (directScene) {
						SceneCategorySelection scene;
						scene.categories = categorySource->srv;
						scene.authoredDepth = authoredSelectionDepth->srv;
						scene.currentDepth = currentSelectionDepth->srv;
						auto& constants = scene.constants;
						constants.sourceMapping = { eye * base.viewportCrop.fullInput.width + base.viewportCrop.input.left,
							base.viewportCrop.input.top, eye * base.viewportCrop.fullInput.width, 0u };
						constants.authoredRegion = { base.viewportCrop.input.left, base.viewportCrop.input.top, source[0], source[1] };
						constants.reductions = { 1.0f, 191.0f / 255.0f, 89.0f / 255.0f, 0.0f };
						constants.options = { 0.0f, 1.0f, 0.01f, 0.0f };
						constants.depthLinearization = { 100.0f, 1.0f, 99.0f, 100.0f };
						inputArgs.actorSelection.Reset();
						inputArgs.sceneCategorySelection = scene;
						inputArgs.modelOutputSelection->mask.Reset();
						inputArgs.modelOutputSelection->sceneCategories = scene;
					}
				}
			}
		}
		ModelResolution adapter;
		HRESULT result = S_OK;
		const bool planned = adapter.PreparePeripheralCompression(args, false, result);
		if (!planned || !args[0].peripheralCompression.Active())
			std::cerr << "compression=" << compression << " NR=" << percent << " FOV=" << shaped << " mixed=" << mixedGuides
					  << " reason=" << GetPeripheralCompressionReasonName(args[0].peripheralCompression.reason) << '\n';
		Require(planned && args[0].peripheralCompression.Active(), "Eligible compression did not plan");
		const auto plan = args[0].peripheralCompression;
		if (directScene) {
			for (bool authored : { false, true }) {
				auto invalid = args;
				auto& scene = *invalid[0].sceneCategorySelection;
				(authored ? scene.authoredDepth : scene.currentDepth) = categorySource->srv;
				invalid[0].modelOutputSelection->sceneCategories = scene;
				Require(!adapter.AdditionalBytes(invalid), "A category tuple was admitted as scalar scene depth");
			}
			auto inconsistent = args;
			inconsistent[0].modelOutputSelection->sceneCategories.reset();
			inconsistent[0].modelOutputSelection->mask = masks[0].srv;
			Require(!adapter.AdditionalBytes(inconsistent), "Conflicting scene and materialized selections were admitted");
		}
		if (sceneCategories) {
			for (unsigned unsupported = 0; unsupported < 9; ++unsupported) {
				auto rejected = args;
				auto& eye = rejected[1];
				switch (unsupported) {
				case 0:
					eye.sceneCharacterStrengths = false;
					break;
				case 1:
					eye.providerBlending = true;
					break;
				case 2:
					eye.controlMask = masks[1].texture;
					break;
				case 3:
					eye.actorSelection.Reset();
					eye.sceneCategorySelection.reset();
					break;
				case 4:
					--eye.actorSelectionSupport.width;
					break;
				case 5:
					eye.characterVisualIsolation = false;
					break;
				case 6:
					--eye.computeSubrect.width;
					eye.roi = BuildRoiDescriptor(eye.computeSubrect, eye.computeSubrect, { source[0], source[1] }, true);
					break;
				case 7:
					if (!eye.modelOutputSelection)
						continue;
					eye.modelOutputSelection->mask = masks[0].srv;
					break;
				case 8:
					if (!eye.modelOutputSelection)
						continue;
					--eye.modelOutputSelection->maskSupport.width;
					break;
				}
				Require(adapter.PreparePeripheralCompression(rejected, false, result) &&
							!rejected[0].peripheralCompression.Active() && !rejected[1].peripheralCompression.Active(),
					"Unsupported category/actor policy escaped the stereo compression fallback");
			}
			auto compact = args;
			Require(adapter.PreparePeripheralCompression(compact, true, result) && !compact[0].peripheralCompression.Active(),
				"Category scope admitted unsupported compact inputs");
		}
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
		const auto selectionCompilations = AdapterCompiler::ordinarySelectionAttempts;
		Require(adapter.Prepare(args, batch, result, percent == 30 ? std::span<const ModelResolution::Targets>{} : std::span<const ModelResolution::Targets>(targets)), "Compressed production preparation failed");
		Require(AdapterCompiler::ordinarySelectionAttempts == selectionCompilations, "Compression compiled an unused ordinary selection shader");
		Require(bool(batch.sceneConstants) == directScene, "Ordinary selection allocated an unused scene constant buffer");
		if (directScene) {
			ModelResolution::Batch replacement;
			Require(adapter.Prepare(args, replacement, result, percent == 30 ? std::span<const ModelResolution::Targets>{} : std::span<const ModelResolution::Targets>(targets)),
				"Repeated scene preparation failed");
			Require(!adapter.Reconstruct(args, batch, result) && result == E_INVALIDARG,
				"Overwritten scene preparation reached reconstruction");
			batch = std::move(replacement);
		}
		for (unsigned field = 0; field < 4; ++field) {
			auto stale = args;
			auto& eye = stale[0];
			switch (field) {
			case 0:
				++eye.frameId;
				break;
			case 1:
				++eye.sourceWorldFrame;
				break;
			case 2:
				++eye.generation;
				break;
			case 3:
				eye.featureSlot += 2;
				break;
			}
			Require(!adapter.Reconstruct(stale, batch, result) && result == E_INVALIDARG,
				"A different source transaction reached retained reconstruction");
		}
		stateRestored("preparation");
		Require(batch.arguments[0].colorWidth == model[0] && batch.arguments[0].guideWidth == grid[0] &&
					batch.arguments[0].computeSubrect.Area() == plan.candidatePixels &&
					batch.arguments[0].modelResolutionHistory->peripheralMapping == plan.mapping,
			"Projected native geometry/history disagrees with plan");
		const std::array prepared{ Texture(batch.targets[0].color, model).Read<Pixel>(device, context), Texture(batch.targets[1].color, model).Read<Pixel>(device, context) };
		for (UINT y = 0; y < model[1]; ++y)
			for (UINT x = 0; x < model[0]; ++x) {
				const auto bounds = PeripheralSamplingBounds(plan.mapping, x, y, model, source);
				const auto [lx, ly, ux, uy] = bounds;
				Pixel sum{};
				float area = 0;
				for (UINT sy = UINT(std::floor(ly)); sy < UINT(std::ceil(uy)); ++sy)
					for (UINT sx = UINT(std::floor(lx)); sx < UINT(std::ceil(ux)); ++sx) {
						const float weight = (std::min(ux, float(sx + 1)) - std::max(lx, float(sx))) * (std::min(uy, float(sy + 1)) - std::max(ly, float(sy)));
						area += weight;
						for (UINT c = 0; c < 4; ++c) sum[c] += colors[sy * source[0] + sx][c] * weight;
					}
				for (UINT c = 0; c < 4; ++c)
					Require(std::abs(prepared[0][y * model[0] + x][c] - sum[c] / area) < 0.0005f, "Nonlinear area filter differs from source-cell integral");
			}
		const auto sampledDepth = Texture(batch.targets[0].depth, grid).Read<float>(device, context);
		const auto sampledMotion = Texture(batch.targets[0].motion, grid).Read<Motion>(device, context);
		for (UINT y = 0; y < grid[1]; ++y)
			for (UINT x = 0; x < grid[0]; ++x) {
				const auto bounds = PeripheralSamplingBounds(plan.mapping, x, y, grid, guides);
				const UINT lx = UINT(std::floor(bounds[0])), ly = UINT(std::floor(bounds[1]));
				const UINT ux = UINT(std::ceil(bounds[2])), uy = UINT(std::ceil(bounds[3]));
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
				const auto anchor = plan.mapping.Inverse({ (float(x) + 0.5f) / grid[0], (float(y) + 0.5f) / grid[1] });
				const auto before = plan.mapping.Forward(anchor), after = plan.mapping.Forward({ anchor[0] + velocity[0] * float(base.viewportCrop.fullInput.width) / guides[0],
																	  anchor[1] + velocity[1] * float(base.viewportCrop.fullInput.height) / guides[1] });
				for (UINT axis = 0; axis < 2; ++axis)
					Near(sampledMotion[y * grid[0] + x][axis], after[axis] - before[axis], "Peripheral endpoint motion differs");
			}
		CheckPeripheralResidual(device, context, adapter, batch, args, outputs, colors, prepared, input, masks);
		stateRestored("reconstruction");
		if (AdapterCompiler::guided && percent == 67) {
			adapter.InvalidateShaders();
			AdapterCompiler::failGuided = true;
			ModelResolution::Batch fallback;
			Require(adapter.Prepare(args, fallback, result) && fallback.reconstructionDecision.reason == GuidedReconstructionReason::ShaderUnavailable &&
						fallback.reconstructionShader == args[0].peripheralShaders.reconstruct,
				"Optional Guided failure lost the compressed reconstruction fallback");
			AdapterCompiler::failGuided = false;
			CheckPeripheralResidual(device, context, adapter, fallback, args, outputs, colors, prepared, input, masks);
			stateRestored("guided fallback");
		}
		if (percent == 100) {
			for (unsigned failure : { 0u, 1u, 2u, 3u }) {
				adapter.InvalidateShaders();
				AdapterCompiler::peripheralAllocationFailureAttempt = AdapterCompiler::peripheralAttempts + 1u + failure % 2u;
				AdapterCompiler::throwPeripheralAllocationFailure = failure >= 2;
				Require(!adapter.PreparePeripheralCompression(args, false, result) && result == E_OUTOFMEMORY,
					"Shader allocation failure was concealed by an ordinary fallback");
				Require(!args[0].peripheralCompression.Active() && !args[1].peripheralCompression.Active() &&
							!args[0].peripheralShaders.prepare && !args[1].peripheralShaders.prepare,
					"Failed shader allocation published a partial pair plan");
				AdapterCompiler::peripheralAllocationFailureAttempt = 0;
				Require(adapter.PreparePeripheralCompression(args, false, result) && args[0].peripheralCompression.Active(),
					"Recovered allocation failure permanently disabled compression");
			}
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
		args[1].peripheralMask.active = true;
		args[1].peripheralMask.radii[0] = 0.0f;
		Require(adapter.PreparePeripheralCompression(args, false, result) && !args[0].peripheralCompression.Active() && !args[1].peripheralCompression.Active(), "Unsupported eye failed to retain ordinary pair");
		if (percent == 67 && !shaped && !mixedGuides && !AdapterCompiler::guided && AdapterCompiler::filter == ReconstructionFilter::Bilinear) {
			args[1].peripheralMask.radii[0] = std::numeric_limits<float>::quiet_NaN();
			ModelResolution::Batch fallback;
			Require(adapter.PreparePeripheralCompression(args, false, result) && adapter.Prepare(args, fallback, result), "Invalid mask prevented ordinary preparation");
			for (const auto& eye : fallback.arguments)
				context->CopyResource(eye.colorOutput, eye.colorInput);
			Require(adapter.Reconstruct(args, fallback, result), "Invalid mask prevented ordinary reconstruction");
			adapter.Commit(args, fallback);
			for (const auto& output : outputs) {
				const auto pixels = output.Read<Pixel>(device, context);
				Require(pixels == colors, "Invalid mask fallback changed source colour or alpha");
			}
			stateRestored("ordinary fallback");
		}
	}
	ID3D11Buffer* empty = nullptr;
	context->CSSetConstantBuffers(1, 1, &empty);
}
