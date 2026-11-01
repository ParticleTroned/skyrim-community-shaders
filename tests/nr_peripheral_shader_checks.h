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

struct ReferencePeripheralFootprint
{
	std::array<std::array<float, 4>, 16> patches{};
	unsigned count = 4;
	std::array<float, 4> bounds{};

	float Weight(UINT x, UINT y) const
	{
		float weight = 0.0f;
		for (unsigned index = 0; index < count; ++index) {
			const auto& patch = patches[index];
			weight += std::max(0.0f, std::min(patch[2], float(x + 1)) - std::max(patch[0], float(x))) *
			          std::max(0.0f, std::min(patch[3], float(y + 1)) - std::max(patch[1], float(y)));
		}
		return weight;
	}
};

ReferencePeripheralFootprint PeripheralSamplingFootprint(const NeuralRendering::PeripheralMapping& mapping, UINT x, UINT y, Size grid, Size source, unsigned subdivisions = 2)
{
	ReferencePeripheralFootprint result;
	result.count = subdivisions * subdivisions;
	result.bounds = { float(source[0]), float(source[1]), 0.0f, 0.0f };
	for (UINT patch = 0; patch < result.count; ++patch) {
		result.patches[patch] = PeripheralSamplingBounds(mapping, x * subdivisions + patch % subdivisions, y * subdivisions + patch / subdivisions,
			{ grid[0] * subdivisions, grid[1] * subdivisions }, source);
		for (UINT axis = 0; axis < 2; ++axis) {
			result.bounds[axis] = std::min(result.bounds[axis], result.patches[patch][axis]);
			result.bounds[axis + 2] = std::max(result.bounds[axis + 2], result.patches[patch][axis + 2]);
		}
	}
	const auto enclosing = PeripheralSamplingBounds(mapping, x, y, grid, source);
	Require(std::floor(result.bounds[0]) >= std::floor(enclosing[0]) && std::floor(result.bounds[1]) >= std::floor(enclosing[1]) &&
				std::ceil(result.bounds[2]) <= std::ceil(enclosing[2]) && std::ceil(result.bounds[3]) <= std::ceil(enclosing[3]),
		"Subdivided footprint expanded its source fetch bounds");
	return result;
}

std::optional<std::array<double, 3>> ReferencePeripheralResidual(std::span<const Pixel> prepared, std::span<const Pixel> neural,
	Size model, double px, double py, const Pixel& source, bool guided
#ifdef DEVBENCH_BRIDGE_ENABLED
	,
	const NeuralRendering::ReconstructionTuning& tuning
#endif
)
{
	auto result = ReferenceResidual(prepared, neural, model, { { 0, 0 }, model }, px, py);
	if (!result)
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
	const double fx = px - std::floor(px), fy = py - std::floor(py);
	const std::array<double, 3> luminance{ 0.2125, 0.7154, 0.0721 };
	std::array<double, 3> sum{}, proxySum{};
	double total = 0;
	for (unsigned tap = 0; tap < 4; ++tap) {
		double distance = 0;
		for (unsigned c = 0; guided && scale > 1e-6 && c < 3; ++c) {
			const double d = (double(source[c]) - proxy[tap][c]) / scale;
			distance += luminance[c] * d * d;
		}
		const double weight = (tap % 2 ? fx : 1.0 - fx) * (tap / 2 ? fy : 1.0 - fy) / (1.0 + 16.0 * distance);
		total += weight;
		for (unsigned c = 0; c < 3; ++c) {
			sum[c] += weight * (double(output[tap][c]) - proxy[tap][c]);
			proxySum[c] += weight * proxy[tap][c];
		}
	}
	double sourceY = 0, proxyY = 0;
	for (unsigned c = 0; c < 3; ++c) {
		sum[c] /= total;
		proxySum[c] /= total;
		sourceY += luminance[c] * std::abs(source[c]);
		proxyY += luminance[c] * std::abs(proxySum[c]);
	}
	double transfer = proxyY > sourceY ? sourceY / proxyY : 1.0;
#ifdef DEVBENCH_BRIDGE_ENABLED
	transfer = 1.0 + (transfer - 1.0) * tuning.shadowTransferPercent * 0.01;
#endif
	for (auto& c : sum) c *= transfer;
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
	for (unsigned condition : { 0u, 1u, 2u, 3u, 4u, 5u, 6u }) {
		auto reference = prepared;
		const bool uniformGain = condition == 4 || condition == 6;
		if (uniformGain)
			for (auto& eye : reference)
				std::ranges::fill(eye, Pixel{ 5.0f, 5.0f, 5.0f, 1.0f });
		auto native = reference;
		for (UINT eye = 0; eye < args.size(); ++eye) {
			for (UINT i = 0; i < prepared[eye].size(); ++i) {
				auto& pixel = native[eye][i];
				pixel[0] += condition == 0 ? 0.0f : condition == 1 ? 0.125f :
				                                                     float(int((i * 7 + eye * 5) % 11) - 5) / 32.0f;
				pixel[1] += condition == 0 ? 0.0f : condition == 1 ? -0.25f :
				                                                     float(int((i / model[0] + eye) % 7) - 3) / 16.0f;
				pixel[3] = 1;
				if (uniformGain) {
					const float value = condition == 4 ? 6.25f : 3.75f;
					pixel = { value, value, value, 1.0f };
				}
			}
			if (condition == 3) {
				native[eye][0][0] = std::numeric_limits<float>::quiet_NaN();
				native[eye][(model[1] / 2) * model[0] + model[0] / 2][1] = std::numeric_limits<float>::infinity();
			}
			if (condition == 5) {
				reference[eye][0][0] = std::numeric_limits<float>::quiet_NaN();
				reference[eye][(model[1] / 2) * model[0] + model[0] / 2][1] = std::numeric_limits<float>::infinity();
			}
			context->UpdateSubresource(batch.targets[eye].color.resource.Get(), 0, nullptr, reference[eye].data(), model[0] * sizeof(Pixel), 0);
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
					const auto residual = ReferencePeripheralResidual(reference[eye], native[eye], model, px, py, colors[i],
						batch.reconstructionDecision.mode == NeuralRendering::GuidedReconstructionMode::GuidedLowResolution
#ifdef DEVBENCH_BRIDGE_ENABLED
						,
						args[eye].reconstructionTuning
#endif
					);
					const auto weight = weights.empty() ? 1.0f : float(weights[i]) / 255.0f;
					for (UINT channel = 0; channel < 3; ++channel)
						Near(pixels[i][channel], colors[i][channel] + (residual ? float((*residual)[channel]) * weight : 0.0f),
							"Compressed residual lost spatial phase, eye isolation or finite fallback");
					Require(pixels[i][3] == colors[i][3], "Compressed residual changed source alpha");
					if (uniformGain) {
						const auto luminance = [](const Pixel& value) { return 0.2125f * value[0] + 0.7154f * value[1] + 0.0721f * value[2]; };
						const auto sourceY = luminance(colors[i]);
						const auto magnitude = 0.2125f * std::abs(colors[i][0]) + 0.7154f * std::abs(colors[i][1]) + 0.0721f * std::abs(colors[i][2]);
						const auto gain = condition == 4 ? 0.25f : -0.25f;
						Near(luminance(pixels[i]), sourceY +
#ifdef DEVBENCH_BRIDGE_ENABLED
													   (5.0f + (std::min(magnitude, 5.0f) - 5.0f) * args[eye].reconstructionTuning.shadowTransferPercent * 0.01f) * gain * weight,
#else
													   std::min(magnitude, 5.0f) * gain * weight,
#endif
							"A bright compressed proxy transferred its absolute edit into a source shadow");
					}
					if (weight == 0.0f)
						Require(pixels[i] == colors[i], "Compressed category selection changed a protected pixel");
				}
		}
	}
}

void CheckPeripheralStereoRounding(ID3D11Device* device, ID3D11DeviceContext* context)
{
	using namespace NeuralRendering;
	constexpr std::array<Size, 2> sizes{ Size{ 307, 202 }, Size{ 1628, 352 } };
	std::array<std::vector<Pixel>, 2> colors;
	std::vector<Texture> inputs, depths, motions, outputs;
	std::array<RendererApplyArgs, 2> args;
	for (unsigned eye = 0; eye < args.size(); ++eye) {
		const auto size = sizes[eye];
		colors[eye].resize(size[0] * size[1]);
		for (unsigned i = 0; i < colors[eye].size(); ++i)
			colors[eye][i] = { float(i % 13) * 0.125f, float(i % 7) * 0.25f, 0.4f, float(i % 5) * 0.125f };
		inputs.emplace_back(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, colors[eye]);
		depths.emplace_back(device, size, DXGI_FORMAT_R32_FLOAT, std::vector(colors[eye].size(), 0.5f));
		motions.emplace_back(device, size, DXGI_FORMAT_R32G32_FLOAT, std::vector(colors[eye].size(), Motion{}));
		outputs.emplace_back(device, size, DXGI_FORMAT_R32G32B32A32_FLOAT, std::vector(colors[eye].size(), sentinel));
		auto& input = args[eye];
		input.device = device;
		input.context = context;
		input.featureSlot = eye;
		input.colorInput = inputs.back().texture.Get();
		input.colorOutput = outputs.back().texture.Get();
		input.depthGuide = depths.back().texture.Get();
		input.depthGuideSRV = depths.back().srv.Get();
		input.motionVectors = motions.back().texture.Get();
		input.colorWidth = input.outputWidth = input.guideWidth = size[0];
		input.colorHeight = input.outputHeight = input.guideHeight = size[1];
		input.viewportCrop = UpscalingDLSS::ViewportCrop::Identity(size[0], size[1], size[0], size[1]);
		input.renderingMode = RenderingMode::ReducedResolution;
		input.insertionPoint = ResolveInsertionPoint(*input.renderingMode);
		input.peripheralCompressionPercent = 61;
		input.guidedReconstructionEnabled = AdapterCompiler::guided;
		input.reset = true;
	}
	ModelResolution adapter;
	HRESULT result = S_OK;
	for (unsigned percent : { 40u, 39u }) {
		for (auto& eye : args)
			eye.modelResolutionPercent = percent;
		Require(adapter.PreparePeripheralCompression(args, false, result), "Stereo rounding planning failed");
		for (const auto& eye : args)
			Require(eye.peripheralCompression.Active(), "Stereo rounding discarded compression");
		if (percent == 39) {
			Require(args[0].peripheralCompression.candidatePixels == args[0].peripheralCompression.baselinePixels &&
						args[1].peripheralCompression.candidatePixels < args[1].peripheralCompression.baselinePixels,
				"Shader fixture lost its asymmetric savings boundary");
		}
		Require(adapter.AdditionalBytes(args).has_value(), "Stereo rounding storage was not admitted");
		ModelResolution::Batch batch;
		Require(adapter.Prepare(args, batch, result), "Stereo rounding preparation failed");
		for (unsigned eye = 0; eye < args.size(); ++eye)
			context->CopyResource(batch.targets[eye].output.resource.Get(), batch.targets[eye].color.resource.Get());
		Require(adapter.Reconstruct(args, batch, result), "Stereo rounding reconstruction failed");
		adapter.Commit(args, batch);
		for (unsigned eye = 0; eye < args.size(); ++eye) {
			const auto actual = outputs[eye].Read<Pixel>(device, context);
			Require(actual == colors[eye], "Stereo rounding changed a zero-correction source pixel");
		}
	}
}

void CheckPeripheralAdapter(ID3D11Device* device, ID3D11DeviceContext* context, bool mixedGuides, bool shaped = false, unsigned compression = 20, bool sceneCategories = false, bool directScene = false
#ifdef DEVBENCH_BRIDGE_ENABLED
	,
	NeuralRendering::ReconstructionTuning tuning = {}
#endif
)
{
	using namespace NeuralRendering;
	constexpr Size source{ 127, 75 };
	const Size guides = mixedGuides ? Size{ 83, 49 } : source;
	std::vector<Pixel> colors(source[0] * source[1]);
	for (UINT i = 0; i < colors.size(); ++i)
		colors[i] = { float(i % 13) * 0.125f - 0.5f, float(i % 7) * 1.25f, 0.4f, float(i % 5) * 0.125f };
	colors[1] = { 0.0f, 0.0f, 0.0f, 0.375f };
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
	base.sourceWorldFrame = 42;
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
	base.insertionPoint = ResolveInsertionPoint(*base.renderingMode);
	base.featureUpscaling = mixedGuides;
	base.reset = true;
	base.peripheralCompressionPercent = compression;
	base.guidedReconstructionEnabled = AdapterCompiler::guided;
#ifdef DEVBENCH_BRIDGE_ENABLED
	base.reconstructionTuning = tuning;
	if (tuning.faceDetailProtectionPercent) {
		base.faceDetailRegions.current = true;
		base.faceDetailRegions.count = 1;
		base.faceDetailRegions.bounds[0] = { 0.3f, 0.25f, 0.6f, 0.65f };
	}
	const auto subdivisions = tuning.footprintSubdivisions;
#else
	const auto subdivisions = 2u;
#endif
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
	for (UINT percent : { 30u, 67u, 90u, 100u }) {
		if (percent == 90 && (mixedGuides || shaped || sceneCategories))
			continue;
		base.modelResolutionPercent = percent;
		std::array args{ base, base };
		for (UINT eye = 0; eye < args.size(); ++eye) {
			args[eye].featureSlot = eye;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (tuning.faceDetailProtectionPercent && eye == 1)
				args[eye].faceDetailRegions.bounds[0] = { 0.34f, 0.26f, 0.64f, 0.66f };
#endif
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
		if (!mixedGuides && !shaped && !sceneCategories) {
			PrewarmRequest request{
				.source = { source[0], source[1] },
				.device = reinterpret_cast<std::uintptr_t>(device),
				.context = reinterpret_cast<std::uintptr_t>(context),
				.generation = 1,
				.slotMask = 0xcu,
				.networkPreset = 2,
				.modelResolutionPercent = std::max(90u, percent),
				.peripheralCompressionPercent = compression,
				.pressureResolutionEnabled = true,
			};
			const auto compiled = AdapterCompiler::calls;
			const auto warm = adapter.ProjectPrewarm(request, percent);
			Require(warm && AdapterCompiler::calls == compiled,
				"Prewarm projection failed or compiled a shader");
			for (const auto& inputArgs : args) {
				RendererApplyArgs projected;
				Require(ModelResolution::Project(inputArgs, projected), "Live model projection failed");
				const auto layout = BuildNativeEvaluationLayout({ projected.colorWidth, projected.colorHeight },
					{ projected.guideWidth, projected.guideHeight }, { projected.outputWidth, projected.outputHeight }, {},
					projected.computeSubrect, UpscalingDLSS::BuildMotionVectorPixelScale(projected.viewportCrop), projected.featureUpscaling);
				Require(warm->layout == layout && warm->networkPreset == request.networkPreset,
					"Prewarm and live scale/compression contracts differ");
			}
			const auto colorWork = Color::Work::AllocationExtent(warm->layout.output.valid);
			const auto reserved = PrewarmTransportBytes(*warm, { colorWork[0], colorWork[1] });
			const auto privateBytes = adapter.AdditionalBytes(args);
			Require(reserved && privateBytes && *reserved >= *privateBytes,
				"Prewarm reservation does not cover actual private model allocations");
			Require(!adapter.ProjectPrewarm(request, 29) && !adapter.ProjectPrewarm(request, 101),
				"Invalid effective model resolution escaped prewarm validation");
			request.peripheralCompressionPercent = 0;
			const auto ordinary = adapter.ProjectPrewarm(request, percent);
			const auto expected = BuildModelResolutionExtent(source[0], source[1], percent);
			Require(ordinary && ordinary->layout.creation.input == expected && ordinary->layout.creation.output == expected &&
						ordinary->modelProjection == (percent != 100) && AdapterCompiler::calls == compiled,
				"Uncompressed prewarm changed scale rounding or introduced GPU preparation");
			request.source = {};
			Require(!adapter.ProjectPrewarm(request, percent), "Missing source geometry must defer prewarm");
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (tuning.faceDetailProtectionPercent) {
			const bool faceDetailActive = std::ranges::any_of(args, [](const auto& eye) { return eye.peripheralCompression.mapping.faceDetail.enabled != 0; });
			for (const auto& eye : args)
				Require(eye.peripheralShaders.experimentalPreparation.faceDetailProtection == faceDetailActive,
					"Inactive protection selected an experimental face preparation shader");
			Require(!mixedGuides || !plan.mapping.faceDetail.enabled, "Face protection escaped the stateless pre-DLSS route");
			if (!mixedGuides && percent == 100)
				Require(plan.mapping.faceDetail.enabled != 0, "Face protection did not reach the shaders");
			auto ordinary = args;
			for (auto& eye : ordinary) eye.reconstructionTuning.faceDetailProtectionPercent = 0;
			Require(adapter.PlanPeripheralCompression(ordinary, false), "Baseline face budget planning failed");
			for (unsigned eye = 0; eye < args.size(); ++eye)
				Require(args[eye].peripheralCompression.model == ordinary[eye].peripheralCompression.model &&
							args[eye].peripheralCompression.guides == ordinary[eye].peripheralCompression.guides,
					"Adaptive face protection increased allocation");
			auto missingEye = args;
			missingEye[1].faceDetailRegions.current = false;
			Require(adapter.PlanPeripheralCompression(missingEye, false) &&
						!missingEye[0].peripheralCompression.mapping.faceDetail.enabled && !missingEye[1].peripheralCompression.mapping.faceDetail.enabled,
				"Missing source evidence did not retain ordinary compression for both eyes");
			for (bool invalidFrame : { false, true }) {
				auto stale = args;
				if (invalidFrame)
					for (auto& eye : stale) eye.sourceWorldFrame = std::numeric_limits<std::uint32_t>::max();
				else
					++stale[1].sourceWorldFrame;
				Require(adapter.PlanPeripheralCompression(stale, false) &&
							!stale[0].peripheralCompression.mapping.faceDetail.enabled && !stale[1].peripheralCompression.mapping.faceDetail.enabled,
					"Unmatched or invalid source frames activated face protection");
			}
		}
#endif
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
		if (AdapterCompiler::guided && percent == 100) {
			auto unsupported = args;
			unsupported[1].guidedReconstructionEnabled = false;
			ModelResolution::Batch fallback;
			const auto attempts = AdapterCompiler::guidedAttempts;
			Require(adapter.Prepare(unsupported, fallback, result) && fallback.reconstructionDecision.reason == GuidedReconstructionReason::Disabled,
				"One disabled eye did not retain Basic compression for the pair at full NR resolution");
			Require(AdapterCompiler::guidedAttempts == attempts, "Disabled guidance compiled an unused shader");
		}
		const auto selectionCompilations = AdapterCompiler::ordinarySelectionAttempts;
		Require(adapter.Prepare(args, batch, result, percent == 30 ? std::span<const ModelResolution::Targets>{} : std::span<const ModelResolution::Targets>(targets)), "Compressed production preparation failed");
		Require(batch.reconstructionDecision.mode == (AdapterCompiler::guided ? GuidedReconstructionMode::GuidedLowResolution : GuidedReconstructionMode::Basic),
			"Compression did not select reconstruction from the actual reduced grid");
#ifdef DEVBENCH_BRIDGE_ENABLED
		for (const auto& eye : batch.arguments) {
			const auto& evidence = eye.executionContext.modelReconstruction;
			Require(evidence && eye.executionContext.peripheralCompression && evidence->effectivePercent == percent &&
						evidence->unpackedModel == eye.executionContext.peripheralCompression->model,
				"Compression reconstruction evidence lost the actual sampling grid");
		}
#endif
		Require(AdapterCompiler::ordinarySelectionAttempts == selectionCompilations, "Compression compiled an unused ordinary selection shader");
		Require(bool(batch.sceneConstants) == directScene, "Ordinary selection allocated an unused scene constant buffer");
		if (AdapterCompiler::guided && percent == 100) {
			const auto compilations = AdapterCompiler::calls;
			ModelResolution::Batch warmed;
			Require(adapter.Prepare(args, warmed, result, targets) && warmed.reconstructionShader == batch.reconstructionShader &&
						AdapterCompiler::calls == compilations,
				"Repeated compressed preparation recompiled the guided shader");
			batch = std::move(warmed);
		}
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		{
			auto stale = args;
			for (auto& eye : stale)
				eye.reconstructionTuning.footprintSubdivisions = subdivisions == 2u ? 1u : 2u;
			ModelResolution::Batch invalid;
			const auto compiled = AdapterCompiler::calls;
			Require(!adapter.AdditionalBytes(stale) && !adapter.Prepare(stale, invalid, result) &&
						result == E_INVALIDARG && AdapterCompiler::calls == compiled,
				"Changed preparation settings reused a previously selected shader");
		}
		if (percent == 67 && mixedGuides && shaped && !sceneCategories && AdapterCompiler::guided) {
			std::array<ComPtr<ID3D11ComputeShader>, 3> cached;
			const auto original = args[0].reconstructionTuning;
			const auto history = batch.arguments[0].modelResolutionHistory;
			for (const auto divisions : { 1u, 2u, 4u, 1u, 2u, 4u, 2u }) {
				for (auto& eye : args)
					eye.reconstructionTuning.footprintSubdivisions = divisions;
				const auto compilations = AdapterCompiler::calls;
				Require(adapter.PreparePeripheralCompression(args, false, result), "Footprint switch failed");
				const auto index = divisions == 1u ? 0u : divisions == 2u ? 1u :
				                                                            2u;
				if (cached[index])
					Require(args[0].peripheralShaders.prepare == cached[index] && AdapterCompiler::calls == compilations,
						"Returning to a footprint variant recompiled its shader");
				cached[index] = args[0].peripheralShaders.prepare;
				ModelResolution::Batch switched;
				auto expectedHistory = history;
				expectedHistory->experimentalPreparation = args[0].reconstructionTuning.Preparation();
				Require(adapter.AdditionalBytes(args, ModelResolution::Sharing::Guides) == 0 && adapter.Prepare(args, switched, result, targets) &&
							switched.targets[0].color.resource == batch.targets[0].color.resource && switched.arguments[0].modelResolutionHistory == expectedHistory,
					"Footprint tuning replaced storage or lost its native history reset key");
				batch = std::move(switched);
			}
			for (auto& eye : args)
				eye.reconstructionTuning = original;
			ModelResolution::Batch restored;
			Require(adapter.PreparePeripheralCompression(args, false, result) && adapter.Prepare(args, restored, result, targets),
				"Original footprint did not restore after the cache sweep");
			batch = std::move(restored);
		}
#endif
		for (UINT y = 0; y < model[1]; ++y)
			for (UINT x = 0; x < model[0]; ++x) {
				const auto footprint = PeripheralSamplingFootprint(plan.mapping, x, y, model, source, subdivisions);
				const auto [lx, ly, ux, uy] = footprint.bounds;
				Pixel sum{};
				float area = 0;
				for (UINT sy = UINT(std::floor(ly)); sy < UINT(std::ceil(uy)); ++sy)
					for (UINT sx = UINT(std::floor(lx)); sx < UINT(std::ceil(ux)); ++sx) {
						const float weight = footprint.Weight(sx, sy);
						area += weight;
						for (UINT c = 0; c < 4; ++c) sum[c] += colors[sy * source[0] + sx][c] * weight;
					}
				Require(area > 0.0f && std::isfinite(area), "Subdivided color footprint lost its finite source area");
				for (UINT c = 0; c < 4; ++c)
					Require(std::abs(prepared[0][y * model[0] + x][c] - sum[c] / area) < 0.0005f, "Nonlinear area filter differs from subdivided source-cell integral");
			}
		const auto sampledDepth = Texture(batch.targets[0].depth, grid).Read<float>(device, context);
		const auto sampledMotion = Texture(batch.targets[0].motion, grid).Read<Motion>(device, context);
		for (UINT y = 0; y < grid[1]; ++y)
			for (UINT x = 0; x < grid[0]; ++x) {
				const auto footprint = PeripheralSamplingFootprint(plan.mapping, x, y, grid, guides, subdivisions);
				const UINT lx = UINT(std::floor(footprint.bounds[0])), ly = UINT(std::floor(footprint.bounds[1]));
				const UINT ux = UINT(std::ceil(footprint.bounds[2])), uy = UINT(std::ceil(footprint.bounds[3]));
				float nearest = 2;
				Motion velocity{};
				Size nearestPixel{};
				for (UINT sy = ly; sy < uy; ++sy)
					for (UINT sx = lx; sx < ux; ++sx) {
						if (footprint.Weight(sx, sy) <= 0.0f)
							continue;
						const auto index = sy * guides[0] + sx;
						if (depths[index] < nearest) {
							nearest = depths[index];
							velocity = velocities[index];
							nearestPixel = { sx, sy };
						}
					}
				Near(sampledDepth[y * grid[0] + x], nearest, "Peripheral depth surface changed");
				NeuralRendering::PeripheralMapping::Point anchor{ (float(nearestPixel[0]) + 0.5f) / guides[0], (float(nearestPixel[1]) + 0.5f) / guides[1] };
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (!tuning.depthSampleMotion)
					anchor = plan.mapping.Inverse({ (float(x) + 0.5f) / grid[0], (float(y) + 0.5f) / grid[1] });
#endif
				const auto before = plan.mapping.Forward(anchor), after = plan.mapping.Forward({ anchor[0] + velocity[0] * float(base.viewportCrop.fullInput.width) / guides[0],
																	  anchor[1] + velocity[1] * float(base.viewportCrop.fullInput.height) / guides[1] });
				for (UINT axis = 0; axis < 2; ++axis)
					Near(sampledMotion[y * grid[0] + x][axis], after[axis] - before[axis], "Peripheral endpoint motion differs");
			}
		CheckPeripheralResidual(device, context, adapter, batch, args, outputs, colors, prepared, input, masks);
		stateRestored("reconstruction");
		if (AdapterCompiler::guided && (percent == 67 || percent == 100)) {
			adapter.InvalidateShaders();
			AdapterCompiler::failGuided = true;
			ModelResolution::Batch fallback;
			Require(adapter.Prepare(args, fallback, result) && fallback.reconstructionDecision.reason == GuidedReconstructionReason::ShaderUnavailable &&
						fallback.reconstructionDecision.mode == GuidedReconstructionMode::Basic,
				"Optional Guided failure lost the compressed reconstruction fallback");
			const auto attempts = AdapterCompiler::guidedAttempts;
			Require(adapter.Prepare(args, fallback, result) && AdapterCompiler::guidedAttempts == attempts &&
						fallback.reconstructionDecision.reason == GuidedReconstructionReason::ShaderUnavailable,
				"Failed compressed guidance retried compilation on the next frame");
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
		if (percent == 67 && !shaped && !mixedGuides && !AdapterCompiler::guided) {
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
