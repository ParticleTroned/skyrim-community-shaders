#pragma once

static ID3D11Device* pipelineTestDevice = nullptr;
static std::filesystem::path pipelineShaderDirectory;
namespace Util
{
	ID3D11DeviceChild* CompileShader(const wchar_t* path, const std::vector<std::pair<const char*, const char*>>& defines,
		const char* profile, const char* entry, [[maybe_unused]] ShaderCompileTiming* timing)
	{
		Require(std::string_view(profile) == "cs_5_0" && std::string_view(entry) == "main", "pipeline compile contract");
		std::vector<D3D_SHADER_MACRO> macros;
		for (const auto& [name, value] : defines) macros.push_back({ name, value });
		macros.push_back({ nullptr, nullptr });
		return Compile(pipelineTestDevice, pipelineShaderDirectory / std::filesystem::path(path).filename(), {},
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, macros.data())
		    .Detach();
	}
}

static void CheckPipelineIdentityCopies(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ComputeShader* copy, ID3D11Buffer* cb)
{
	NeuralRendering::Color::Pipeline pipeline;
	Configuration config;
	config.settings.mode = Mode::PreserveSource;
	Observation observation;
	observation.rect = { 3, 5, 17, 19 };
	std::vector<Pixel> source(4096, Pixel{ .125f, .25f, .0625f, .375f });
	for (unsigned i = 0; i < source.size(); ++i) {
		Pixel ignored{}, neural{};
		ApplyAdversarialReconstructionSample(i, source[i], ignored, neural);
	}
	const std::vector<Pixel> sentinel(4096, Pixel{ .5f, .5f, .5f, .5f });
	for (const auto format : identityBaselineFormats) {
		NeuralRendering::Color::Work work;
		HRESULT result = E_FAIL;
		Require(pipeline.Ensure(device, work, observation.rect, format, false, result), "identity copy fixture allocation");
		const auto original = StorePixels(device, context, copy, cb, source, format);
		auto prepared = StorePixels(device, context, copy, cb, sentinel, format);
		auto expected = StorePixels(device, context, copy, cb, sentinel, format);
		auto ownedExpected = StorePixels(device, context, copy, cb, sentinel, format);
		context->CopyResource(work.baseline.resource.Get(), ownedExpected.texture.Get());
		const auto ownedBefore = ReadTextureBytes(device, context, work.baseline.resource.Get());
		const auto& roi = observation.rect;
		const D3D11_BOX box{ roi.baseX, roi.baseY, 0, roi.baseX + roi.width, roi.baseY + roi.height, 1 };
		context->CopySubresourceRegion(expected.texture.Get(), 0, roi.baseX, roi.baseY, 0, original.texture.Get(), 0, &box);
		context->CopySubresourceRegion(ownedExpected.texture.Get(), 0, 0, 0, 0, original.texture.Get(), 0, &box);
		const auto preparedExpected = ReadTextureBytes(device, context, expected.texture.Get());
		const auto pixelBytes = ownedBefore.size() / sentinel.size();
		for (const bool blending : { false, true }) {
			Require(pipeline.Prepare(context, work, original.texture.Get(), prepared.texture.Get(), prepared.uav.Get(), config, observation, blending), "production identity copy preparation");
			Require(ReadTextureBytes(device, context, prepared.texture.Get()) == preparedExpected, "production identity preparation must preserve stored exceptional values and untouched padding");
			Require(ReadTextureBytes(device, context, work.baseline.resource.Get()) == (blending ? ReadTextureBytes(device, context, ownedExpected.texture.Get()) : ownedBefore), "owned baseline copy must follow provider-blending eligibility");
			Require(work.observation.copiedLogicalBytes == roi.Area() * pixelBytes * (blending ? 2u : 1u), "copy accounting must use actual ROI and storage bytes");
			Require(work.observation.retainedBytes == 2 * ownedBefore.size(), "baseline reuse must not change retained memory accounting");
		}
	}
	std::puts("Production identity copies preserve stored exceptional values across six formats.");
}

static void CheckPipelineBaselineLifecycle(ID3D11Device* device, ID3D11DeviceContext* context)
{
	NeuralRendering::Color::Pipeline pipeline;
	NeuralRendering::Color::Work work;
	Configuration config;
	config.settings.mode = Mode::PreserveSource;
	Observation observation;
	observation.rect = { 3, 5, 17, 19 };
	const std::vector<Pixel> pixels(4096, Pixel{ .125f, .25f, .0625f, .375f });
	const std::vector<Pixel> sentinel(4096, Pixel{ -7, -7, -7, -7 });
	const std::vector<Pixel> changedPixels(4096, Pixel{ .375f, .125f, .25f, .75f });
	auto original = MakeTexture(device, pixels), prepared = MakeTexture(device, sentinel), neural = MakeTexture(device, pixels);
	HRESULT result = E_FAIL;
	Require(pipeline.Ensure(device, work, observation.rect, DXGI_FORMAT_R32G32B32A32_FLOAT, false, result), "pipeline allocation");
	auto* retainedBaseline = work.baseline.resource.Get();
	context->UpdateSubresource(retainedBaseline, 0, nullptr, sentinel.data(), 64 * sizeof(Pixel), 0);
	const auto untouched = ReadTextureBytes(device, context, retainedBaseline);
	const auto prepare = [&](bool blending = false) {
		return pipeline.Prepare(context, work, original.texture.Get(), prepared.texture.Get(), prepared.uav.Get(), config, observation, blending);
	};
	const auto reconstruct = [&]() {
		return pipeline.Reconstruct(context, work, neural.texture.Get(), neural.srv.Get(), prepared.srv.Get(), config);
	};
	const auto invalidated = [&]() {
		Require(!work.prepared && work.baselineSource == BaselineSource::OwnedBaseline, "invalid transactions must reset baseline selection");
	};
	Require(prepare(), "identity preparation");
	Require(work.baselineSource == BaselineSource::IdentityPreparedInput, "exact identity must reuse prepared input");
	Require(work.observation.copiedLogicalBytes == observation.rect.Area() * sizeof(Pixel), "identity must report one ROI copy");
	Require(work.observation.retainedBytes == 2 * 64 * 64 * sizeof(Pixel), "identity keeps both retained allocations");
	Require(ReadTextureBytes(device, context, retainedBaseline) == untouched, "identity must omit the private baseline copy");
	const auto inputBefore = ReadTextureBytes(device, context, prepared.texture.Get());
	// Existing compute bindings must be restored around copies and reconstruction.
	auto* savedSRV = original.srv.Get();
	context->CSSetShaderResources(0, 1, &savedSRV);
	Require(reconstruct(), "identity reconstruction");
	ComPtr<ID3D11ShaderResourceView> restored;
	context->CSGetShaderResources(0, 1, &restored);
	Require(restored.Get() == savedSRV, "pipeline must restore incoming SRV bindings");
	ID3D11ShaderResourceView* empty = nullptr;
	context->CSSetShaderResources(0, 1, &empty);
	const auto identityOutput = ReadTextureBytes(device, context, work.result.resource.Get());
	Require(ReadTextureBytes(device, context, prepared.texture.Get()) == inputBefore, "pipeline must leave prepared input unchanged");
	context->UpdateSubresource(original.texture.Get(), 0, nullptr, changedPixels.data(), 64 * sizeof(Pixel), 0);
	Require(reconstruct(), "reconstruction after source overwrite");
	Require(ReadTextureBytes(device, context, work.result.resource.Get()) == identityOutput, "prepared baseline must survive source overwrite");
	restored.Reset();
	original = {};
	Require(reconstruct(), "reconstruction after source release");
	Require(ReadTextureBytes(device, context, work.result.resource.Get()) == identityOutput, "prepared baseline must outlive the original source");
	original = MakeTexture(device, pixels);
	Require(prepare(true), "provider blending fallback preparation");
	Require(work.baselineSource == BaselineSource::OwnedBaseline, "provider blending must retain owned baseline");
	Require(work.observation.copiedLogicalBytes == 2 * observation.rect.Area() * sizeof(Pixel), "provider blending reports both ROI copies");
	Require(reconstruct(), "owned reconstruction");
	Require(ReadTextureBytes(device, context, work.result.resource.Get()) == identityOutput, "owned and reused pipeline outputs must match");
	// An equal provider output and backbuffer edit must leave the captured baseline intact.
	context->UpdateSubresource(prepared.texture.Get(), 0, nullptr, changedPixels.data(), 64 * sizeof(Pixel), 0);
	context->UpdateSubresource(neural.texture.Get(), 0, nullptr, changedPixels.data(), 64 * sizeof(Pixel), 0);
	Require(reconstruct(), "reconstruction after provider backbuffer overwrite");
	Require(ReadTextureBytes(device, context, work.result.resource.Get()) == identityOutput, "provider writes must not replace the owned baseline or its alpha");
	context->UpdateSubresource(neural.texture.Get(), 0, nullptr, pixels.data(), 64 * sizeof(Pixel), 0);
	config.experiments.profiles[0].transform = Transform::LinearToSRGB;
	config.experiments.profiles[0].domain = Domain::Linear;
	original = MakeTexture(device, changedPixels);
	Require(prepare() && work.baselineSource == BaselineSource::OwnedBaseline, "transformed input must refresh owned baseline");
	Require(work.observation.copiedLogicalBytes == observation.rect.Area() * sizeof(Pixel), "transformed preparation reports only the owned ROI copy");
	const auto transformedBaseline = Read(device, context, work.baseline.resource.Get());
	for (unsigned y = 0; y < observation.rect.height; ++y)
		Require(std::memcmp(transformedBaseline.data() + y * 64, changedPixels.data() + (y + observation.rect.baseY) * 64 + observation.rect.baseX,
					observation.rect.width * sizeof(Pixel)) == 0,
			"transformed preparation must not reuse earlier baseline pixels");
	Require(reconstruct(), "transformed reconstruction");
	config.experiments.profiles[0] = {};
	Require(prepare() && work.baselineSource == BaselineSource::IdentityPreparedInput, "retained slot must return to identity reuse");
	Require(work.baseline.resource.Get() == retainedBaseline, "profile changes must retain owned allocation");
	for (const auto exposure : { std::nextafter(1.f, 2.f), .5f }) {
		config.experiments.profiles[0].exposureMultiplier = exposure;
		Require(prepare() && work.baselineSource == BaselineSource::OwnedBaseline, "non-unit identity exposure must fail reuse closed");
	}
	config.experiments.profiles[0] = {};
	config.experiments.profiles[0].exposureSource = ExposureSource::CapturedHDR;
	Require(prepare() && work.baselineSource == BaselineSource::OwnedBaseline, "non-manual identity exposure must fail reuse closed");
	config.experiments.profiles[0] = {};
	Require(prepare(), "reuse before failed preparation");
	Require(!pipeline.Prepare(context, work, nullptr, prepared.texture.Get(), prepared.uav.Get(), config, observation, false), "missing source must fail preparation");
	invalidated();
	Require(!reconstruct(), "failed preparation must not reconstruct stale baseline");
	Require(prepare(), "reuse before revision change");
	++config.revision;
	Require(!reconstruct(), "configuration revision mismatch must reject transaction");
	--config.revision;
	Require(reconstruct() && work.baselineSource == BaselineSource::IdentityPreparedInput, "revision rejection preserves the original immutable transaction");
	Require(prepare(), "reuse before missing input");
	Require(!pipeline.Reconstruct(context, work, neural.texture.Get(), neural.srv.Get(), nullptr, config), "missing reused SRV must not fall back to stale owned pixels");
	Require(work.baselineSource == BaselineSource::IdentityPreparedInput, "missing input cannot change the latched baseline source");
	Require(prepare(), "reuse before invalid crop");
	auto invalidObservation = observation;
	invalidObservation.reconstructionRegions.count = 1;
	invalidObservation.reconstructionRegions.regions[0] = { 63, 63, 2, 2 };
	Require(!pipeline.Prepare(context, work, original.texture.Get(), prepared.texture.Get(), prepared.uav.Get(), config, invalidObservation, false), "invalid crop must fail before copying");
	invalidated();
	Require(prepare(), "reuse before allocation failure");
	Require(!pipeline.Ensure(nullptr, work, observation.rect, DXGI_FORMAT_R32G32B32A32_FLOAT, false, result), "invalid allocation must fail");
	Require(work.prepared && work.baselineSource == BaselineSource::IdentityPreparedInput, "failed allocation leaves existing resources and their transaction intact");
	Require(pipeline.Ensure(device, work, { 0, 0, 65, 17 }, DXGI_FORMAT_R32G32B32A32_FLOAT, false, result), "resize allocation");
	invalidated();
	Require(work.capacityWidth == 128 && work.capacityHeight == 64, "resize retains both allocation axes");
	Require(prepare(), "reuse after resize");
	work.InvalidatePreparation();
	invalidated();
	work = {};
	invalidated();
	Require(pipeline.Ensure(device, work, observation.rect, DXGI_FORMAT_R32G32B32A32_FLOAT, false, result) && prepare(), "trimmed slot recreation");
	// Retain test references so abandonment can model device loss without leaking test resources.
	auto baselineLease = work.baseline, resultLease = work.result;
	work.Abandon();
	invalidated();
	baselineLease.resource->Release();
	baselineLease.srv->Release();
	resultLease.resource->Release();
	resultLease.srv->Release();
	resultLease.uav->Release();
	work = {};
	Require(pipeline.Ensure(device, work, observation.rect, DXGI_FORMAT_R32G32B32A32_FLOAT, false, result) && prepare(), "recreate after abandonment");
	// A later region's failed preflight must not change the first region's private result.
	Require(reconstruct(), "first eye private reconstruction");
	const auto firstEyeResult = ReadTextureBytes(device, context, work.result.resource.Get());
	NeuralRendering::Color::Work second;
	Require(!pipeline.Reconstruct(context, second, neural.texture.Get(), neural.srv.Get(), prepared.srv.Get(), config), "second eye failure");
	Require(ReadTextureBytes(device, context, work.result.resource.Get()) == firstEyeResult, "second eye failure must retain the first private result");
	Configuration inactive;
	inactive.settings.mode = Mode::LegacyRaw;
	Require(!inactive.Enabled(), "ordinary LegacyRaw must bypass colour processing");
	std::puts("Production colour pipeline lifecycle and copy accounting checks passed.");
}

static void CheckColorDebugMessages(ID3D11Device* device)
{
	if (!(device->GetCreationFlags() & D3D11_CREATE_DEVICE_DEBUG)) {
		std::puts("D3D11 debug layer unavailable; API-message checks skipped.");
		return;
	}
	ComPtr<ID3D11InfoQueue> messages;
	Check(device->QueryInterface(IID_PPV_ARGS(&messages)));
	for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
		SIZE_T length = 0;
		Check(messages->GetMessage(i, nullptr, &length));
		std::vector<std::byte> bytes(length);
		auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
		Check(messages->GetMessage(i, message, &length));
		if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
			std::fprintf(stderr, "%s\n", message->pDescription);
			Require(false, "D3D11 debug layer reported an invalid binding or operation");
		}
	}
	std::puts("D3D11 debug layer: no warnings or errors.");
}
