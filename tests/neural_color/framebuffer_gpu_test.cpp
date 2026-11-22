#include "../ShaderPackageIncludes.h"
#include "Features/Upscaling/NeuralRendering/ComputeStateGuard.h"
#include "Features/Upscaling/NeuralRendering/FramebufferTransaction.h"
#include "Features/Upscaling/NeuralRendering/MainDepthPresentation.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include "d3d_resource_naming.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Require(bool value, std::source_location location = std::source_location::current())
{
	if (!value) {
		std::fprintf(stderr, "Requirement failed at %s:%u\n", location.file_name(), location.line());
		std::abort();
	}
}
static void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }

// The test owns this Context4 instance; only the two native copy entries are intercepted.
class CopyTrace
{
public:
	struct Event
	{
		ID3D11Resource* destination;
		ID3D11Resource* source;
		D3D11_BOX box;
		UINT x, y;
		bool full;
	};
	explicit CopyTrace(ID3D11DeviceContext* context) : context_(context)
	{
		Require(!active_);
		Check(context->QueryInterface(IID_PPV_ARGS(&context4_)));
		Require(context4_.Get() == context);
		original_ = *reinterpret_cast<void***>(context);
		std::copy_n(original_, table_.size(), table_.begin());
		table_[46] = reinterpret_cast<void*>(&CopyRegion);
		table_[47] = reinterpret_cast<void*>(&CopyResource);
		active_ = this;
		InterlockedExchangePointer(reinterpret_cast<void* volatile*>(context_), table_.data());
	}
	~CopyTrace()
	{
		InterlockedExchangePointer(reinterpret_cast<void* volatile*>(context_), original_);
		active_ = nullptr;
	}
	CopyTrace(const CopyTrace&) = delete;
	CopyTrace& operator=(const CopyTrace&) = delete;
	std::vector<Event> events;

private:
	static void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
	{
		auto& trace = *active_;
		trace.events.push_back({ destination, source, {}, 0, 0, true });
		reinterpret_cast<decltype(&CopyResource)>(trace.original_[47])(context, destination, source);
	}
	static void STDMETHODCALLTYPE CopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT subresource,
		UINT x, UINT y, UINT z, ID3D11Resource* source, UINT sourceSubresource, const D3D11_BOX* box)
	{
		auto& trace = *active_;
		Require(box && !subresource && !sourceSubresource && !z);
		trace.events.push_back({ destination, source, *box, x, y, false });
		reinterpret_cast<decltype(&CopyRegion)>(trace.original_[46])(context, destination, subresource, x, y, z, source, sourceSubresource, box);
	}
	ID3D11DeviceContext* context_;
	ComPtr<ID3D11DeviceContext4> context4_;
	void** original_;
	std::array<void*, 149> table_{};
	static inline CopyTrace* active_ = nullptr;
};

using Transaction = NeuralRendering::FramebufferTransaction;
using Regions = std::array<Transaction::EyeRegion, 2>;
constexpr auto deferred = Transaction::Preparation::DeferredRegions;

namespace globals::game
{
	bool isVR = true;
}

static bool PublishMainPair(ID3D11DeviceContext* context, Transaction* a_mainTransaction)
{
	struct
	{
		UINT committedEyeMask = 3, bypassedEyeMask = 0;
	} a_result;
	bool lateSucceeded = false;
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	const bool promoteStagedOutputs = false;
	const uint32_t eyeCount = globals::game::isVR ? 2u : 1u;
	struct EyeResult
	{
		bool bypassed = false;
	};
	std::array<EyeResult, 2> neuralResults{};
	std::array<std::unique_ptr<int>, 2> submitNeuralFloatColorOut, submitNeuralFloatStagedOut;
#include "neural_final_ldr_publication_under_test.h"
}

struct FramebufferFixture
{
	static constexpr UINT width = 16, height = 8;
	ID3D11DeviceContext* context;
	UINT pixelBytes;
	ComPtr<ID3D11Texture2D> destination, scratch, reference, referenceScratch, staging;
	std::vector<unsigned char> baseline, poison;

	FramebufferFixture(ID3D11Device* device, ID3D11DeviceContext* ctx, DXGI_FORMAT format, UINT bytes) :
		context(ctx), pixelBytes(bytes), baseline(width * height * bytes), poison(baseline.size(), 0xED)
	{
		for (size_t i = 0; i < baseline.size(); ++i)
			baseline[i] = static_cast<unsigned char>((i * 13 + i / 7) % 179);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Format = format;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		Check(device->CreateTexture2D(&desc, nullptr, &destination));
		Util::SetResourceName(destination.Get(), "NRTest::Destination");
		Check(device->CreateTexture2D(&desc, nullptr, &reference));
		Util::SetResourceName(reference.Get(), "NRTest::Reference");
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		Check(device->CreateTexture2D(&desc, nullptr, &scratch));
		Util::SetResourceName(scratch.Get(), "NRTest::Scratch");
		Check(device->CreateTexture2D(&desc, nullptr, &referenceScratch));
		Util::SetResourceName(referenceScratch.Get(), "NRTest::ReferenceScratch");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, &staging));
		Util::SetResourceName(staging.Get(), "NRTest::Readback");
		Reset();
	}
	void Reset()
	{
		context->UpdateSubresource(destination.Get(), 0, nullptr, baseline.data(), width * pixelBytes, 0);
		context->UpdateSubresource(reference.Get(), 0, nullptr, baseline.data(), width * pixelBytes, 0);
		context->UpdateSubresource(scratch.Get(), 0, nullptr, poison.data(), width * pixelBytes, 0);
	}
	std::vector<unsigned char> Read(ID3D11Texture2D* texture)
	{
		context->CopyResource(staging.Get(), texture);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<unsigned char> bytes(baseline.size());
		for (UINT y = 0; y < height; ++y)
			std::copy_n(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch,
				width * pixelBytes, bytes.data() + y * width * pixelBytes);
		context->Unmap(staging.Get(), 0);
		return bytes;
	}
	Regions Plan(UINT eyeCount, NeuralRendering::ComputeSubrect output, NeuralRendering::ComputeSubrect compute)
	{
		Regions regions{};
		for (UINT eye = 0; eye < eyeCount; ++eye)
			regions[eye] = { scratch.Get(), 0, eye * width / eyeCount, output, compute };
		return regions;
	}
	static D3D11_BOX Box(const Transaction::EyeRegion& region)
	{
		const auto x = region.baseOffsetX + region.output.baseX + region.compute.baseX;
		const auto y = region.output.baseY + region.compute.baseY;
		return { x, y, 0, x + region.compute.width, y + region.compute.height, 1 };
	}
	void Modify(ID3D11Texture2D* texture, const Transaction::EyeRegion& region)
	{
		// Two separated writes leave baseline gaps inside the conservative rectangle.
		auto box = Box(region);
		box.right = box.left + 1;
		context->UpdateSubresource(texture, 0, &box, poison.data(), pixelBytes, 0);
		box.left = Box(region).right - 1;
		box.right = box.left + 1;
		context->UpdateSubresource(texture, 0, &box, poison.data(), pixelBytes, 0);
	}
};

static void VerifyRegions(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format, UINT pixelBytes)
{
	FramebufferFixture fixture(device, context, format, pixelBytes);
	for (UINT eyeCount : { 1u, 2u }) {
		const UINT eyeWidth = fixture.width / eyeCount;
		for (UINT active = 0; active < (1u << eyeCount); ++active) {
			for (bool full : { false, true }) {
				fixture.Reset();
				auto regions = fixture.Plan(eyeCount,
					full ? NeuralRendering::ComputeSubrect{ 0, 0, eyeWidth, fixture.height } : NeuralRendering::ComputeSubrect{ 1, 1, eyeWidth - 1, 6 },
					full ? NeuralRendering::ComputeSubrect{ 0, 0, eyeWidth, fixture.height } : NeuralRendering::ComputeSubrect{ 1, 2, eyeWidth - 2, 4 });
				const auto frozen = regions;
				const bool fullSurface = full && active == (1u << eyeCount) - 1;
				const UINT copies = fullSurface ? 1 : ((active & 1u) + ((active >> 1u) & 1u));
				{
					CopyTrace trace(context);
					Transaction transaction(context, fixture.destination.Get(), fixture.scratch.Get(), deferred);
					Require(transaction.IsValid() && trace.events.empty());
					if (active) {
						Require(transaction.PrepareRegions(regions, active, eyeCount, eyeWidth, fixture.height));
						Require(trace.events.size() == copies);
						regions = {};
						for (UINT eye = 0; eye < eyeCount; ++eye)
							if (active & (1u << eye))
								fixture.Modify(fixture.scratch.Get(), frozen[eye]);
					}
					Require(transaction.Commit(active != 0) == (active != 0));
					Require(!transaction.Commit(true) && trace.events.size() == 2 * copies);
					for (UINT index = 0; index < copies; ++index) {
						const auto& prepare = trace.events[index];
						const auto& publish = trace.events[index + copies];
						Require(prepare.destination == fixture.scratch.Get() && prepare.source == fixture.destination.Get());
						Require(publish.destination == fixture.destination.Get() && publish.source == fixture.scratch.Get());
						Require(prepare.full == fullSurface && publish.full == fullSurface);
						if (!fullSurface) {
							const UINT eye = active == 2 ? 1 : index;
							const auto box = fixture.Box(frozen[eye]);
							Require(std::memcmp(&prepare.box, &box, sizeof(box)) == 0 && std::memcmp(&publish.box, &box, sizeof(box)) == 0);
							Require(prepare.x == box.left && publish.x == box.left && prepare.y == box.top && publish.y == box.top);
						}
					}
				}
				{
					Transaction old(context, fixture.reference.Get(), fixture.referenceScratch.Get());
					for (UINT eye = 0; eye < eyeCount; ++eye)
						if (active & (1u << eye))
							fixture.Modify(fixture.referenceScratch.Get(), frozen[eye]);
					Require(old.Commit(true));
				}
				Require(fixture.Read(fixture.destination.Get()) == fixture.Read(fixture.reference.Get()));
				if (!active)
					Require(fixture.Read(fixture.scratch.Get()) == fixture.poison);
				// UI composition happens after the regional main transaction, on the real framebuffer.
				context->UpdateSubresource(fixture.destination.Get(), 0, nullptr, fixture.baseline.data(), fixture.width * pixelBytes, 0);
				{
					CopyTrace trace(context);
					Transaction postUi(context, fixture.destination.Get(), fixture.scratch.Get());
					Require(postUi.Commit(true) && trace.events.size() == 2 && trace.events[0].full && trace.events[1].full);
				}
				Require(fixture.Read(fixture.destination.Get()) == fixture.baseline);
			}
		}
	}
	for (UINT failure = 0; failure < 3; ++failure) {
		fixture.Reset();
		auto regions = fixture.Plan(2, { 0, 0, 8, 8 }, { 1, 1, 6, 6 });
		try {
			Transaction failed(context, fixture.destination.Get(), fixture.scratch.Get(), deferred);
			Require(failed.PrepareRegions(regions, 3, 2, 8, 8));
			fixture.Modify(fixture.scratch.Get(), regions[0]);
			if (failure)
				fixture.Modify(fixture.scratch.Get(), regions[1]);
			if (failure == 2)
				throw std::runtime_error("later blend failure");
			Require(!failed.Commit(false) && !failed.Commit(true));
		} catch (const std::runtime_error&) {}
		Require(fixture.Read(fixture.destination.Get()) == fixture.baseline);
	}
	std::printf("Passed regional/full reference, poison, empty/mono/stereo, failure and post-UI bytes: format=%u bytes=%u\n", format, pixelBytes);
}

static void VerifyLifecycle(ID3D11Device* device, ID3D11DeviceContext* context)
{
	FramebufferFixture f(device, context, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
	for (UINT invalid = 0; invalid < 15; ++invalid) {
		auto regions = f.Plan(2, { 1, 1, 7, 7 }, { 1, 1, 6, 6 });
		UINT active = 3, eyes = 2, eyeWidth = 8, height = 8;
		switch (invalid) {
		case 0:
			regions[1].compute = {};
			break;
		case 1:
			regions[1].compute.baseX = UINT_MAX;
			break;
		case 2:
			regions[1].compute.width = UINT_MAX;
			break;
		case 3:
			regions[1].output.baseY = UINT_MAX;
			break;
		case 4:
			regions[1].output.width = 8;
			break;
		case 5:
			regions[1].baseOffsetX = 0;
			break;
		case 6:
			regions[0].baseOffsetX = 8;
			break;
		case 7:
			regions[1].target = f.referenceScratch.Get();
			break;
		case 8:
			regions[1].subresource = 1;
			break;
		case 9:
			active = 4;
			break;
		case 10:
			active = 0;
			break;
		case 11:
			eyes = 3;
			break;
		case 12:
			eyeWidth = UINT_MAX;
			break;
		case 13:
			height = 7;
			break;
		case 14:
			regions[1].compute.baseY = 7;
			break;
		}
		CopyTrace trace(context);
		Transaction transaction(context, f.destination.Get(), f.scratch.Get(), deferred);
		Require(!transaction.PrepareRegions(regions, active, eyes, eyeWidth, height));
		Require(!transaction.PrepareRegions(f.Plan(2, { 0, 0, 8, 8 }, { 0, 0, 8, 8 }), 3, 2, 8, 8));
		Require(!transaction.Commit(true) && trace.events.empty());
	}
	for (bool commitFirst : { false, true }) {
		CopyTrace trace(context);
		Transaction transaction(context, f.destination.Get(), f.scratch.Get(), deferred);
		auto regions = f.Plan(2, { 0, 0, 8, 8 }, { 0, 0, 8, 8 });
		if (commitFirst)
			Require(!transaction.Commit(true));
		Require(transaction.PrepareRegions(regions, 3, 2, 8, 8) == !commitFirst);
		Require(!transaction.PrepareRegions(regions, 3, 2, 8, 8));
		Require(!transaction.Commit(true) && trace.events.size() == (commitFirst ? 0u : 1u));
	}
	// Replacement owners cannot redirect an already initialized transaction.
	{
		auto* oldScratch = f.scratch.Get();
		auto regions = f.Plan(2, { 0, 0, 8, 8 }, { 1, 1, 6, 6 });
		Transaction transaction(context, f.destination.Get(), f.scratch.Get(), deferred);
		Require(transaction.PrepareRegions(regions, 1, 2, 8, 8));
		f.scratch = f.referenceScratch;
		f.Modify(oldScratch, regions[0]);
		Require(transaction.Commit(true));
	}
	Require(f.Read(f.destination.Get()) != f.baseline);
	std::puts("Passed invalid regions, frozen resources, commit-before-init and single-use lifecycle");
}

static void VerifyState(ID3D11Device* device, ID3D11DeviceContext* context)
{
	FramebufferFixture f(device, context, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
	ComPtr<ID3D11RenderTargetView> rtv;
	Check(device->CreateRenderTargetView(f.destination.Get(), nullptr, &rtv));
	Util::SetResourceName(rtv.Get(), "NRTest::Destination RTV");
	D3D11_TEXTURE2D_DESC desc{};
	f.destination->GetDesc(&desc);
	desc.Format = DXGI_FORMAT_D32_FLOAT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	ComPtr<ID3D11Texture2D> depth;
	Check(device->CreateTexture2D(&desc, nullptr, &depth));
	Util::SetResourceName(depth.Get(), "NRTest::Depth");
	ComPtr<ID3D11DepthStencilView> dsv;
	Check(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv));
	Util::SetResourceName(dsv.Get(), "NRTest::Depth DSV");
	ComPtr<ID3D11Predicate> predicate;
	const D3D11_QUERY_DESC query{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
	Check(device->CreatePredicate(&query, &predicate));
	Util::SetResourceName(predicate.Get(), "NRTest::Predicate");
	context->Begin(predicate.Get());
	context->End(predicate.Get());
	ComPtr<ID3D11UnorderedAccessView> uav;
	Check(device->CreateUnorderedAccessView(f.scratch.Get(), nullptr, &uav));
	Util::SetResourceName(uav.Get(), "NRTest::Scratch UAV");
	for (UINT outcome = 0; outcome < 4; ++outcome) {
		auto* bound = rtv.Get();
		context->OMSetRenderTargets(1, &bound, dsv.Get());
		context->SetPredication(predicate.Get(), TRUE);
		try {
			Transaction transaction(context, f.destination.Get(), f.scratch.Get(), deferred);
			ComPtr<ID3D11RenderTargetView> current;
			ComPtr<ID3D11DepthStencilView> currentDepth;
			ComPtr<ID3D11Predicate> currentPredicate;
			BOOL value = TRUE;
			context->OMGetRenderTargets(1, &current, &currentDepth);
			context->GetPredication(&currentPredicate, &value);
			Require(!current && !currentDepth && !currentPredicate && value == FALSE);
			if (outcome != 1) {
				auto regions = f.Plan(2, { 0, 0, 8, 8 }, { 1, 1, 6, 6 });
				if (outcome == 2)
					regions[1].compute = {};
				Require(transaction.PrepareRegions(regions, 3, 2, 8, 8) == (outcome != 2));
			}
			if (outcome == 3)
				throw std::runtime_error("state unwind");
			if (outcome == 0) {
				auto* output = uav.Get();
				context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
				{
					NeuralRendering::ComputeStateGuard<1, 1, NeuralRendering::ComputeStatePolicy::PreserveBindings> state(context);
					Require(PublishMainPair(context, &transaction));
					ComPtr<ID3D11UnorderedAccessView> currentOutput;
					context->CSGetUnorderedAccessViews(0, 1, &currentOutput);
					Require(!currentOutput);
				}
				ComPtr<ID3D11UnorderedAccessView> restored;
				context->CSGetUnorderedAccessViews(0, 1, &restored);
				Require(restored.Get() == output);
				output = nullptr;
				context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			}
		} catch (const std::runtime_error&) {}
		ComPtr<ID3D11RenderTargetView> restored;
		ComPtr<ID3D11DepthStencilView> restoredDepth;
		ComPtr<ID3D11Predicate> restoredPredicate;
		BOOL value = FALSE;
		context->OMGetRenderTargets(1, &restored, &restoredDepth);
		context->GetPredication(&restoredPredicate, &value);
		Require(restored.Get() == rtv.Get() && restoredDepth.Get() == dsv.Get() && restoredPredicate.Get() == predicate.Get() && value == TRUE);
	}
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->SetPredication(nullptr, FALSE);
	ComPtr<ID3D11DeviceContext> deferredContext;
	Check(device->CreateDeferredContext(0, &deferredContext));
	Transaction badContext(deferredContext.Get(), f.destination.Get(), f.scratch.Get(), deferred);
	Transaction alias(context, f.scratch.Get(), f.scratch.Get(), deferred);
	Transaction noUav(context, f.destination.Get(), f.reference.Get(), deferred);
	Require(!badContext.IsValid() && !alias.IsValid() && !noUav.IsValid());
	std::puts("Passed RTV/DSV/predication restoration on success, empty, failure and exception; compute cleanup and resource contracts");
}

static void VerifyPostUpscaleHiddenArea(ID3D11Device* device, ID3D11DeviceContext* context, const char* shaderPath, const char* shaderRoot)
{
	constexpr unsigned eyeWidth = 32, height = 24, width = eyeWidth * 2;
	ComPtr<ID3DBlob> bytecode, errors;
	PackageIncludes includes(shaderRoot);
	Check(D3DCompileFromFile(std::filesystem::path(shaderPath).c_str(), nullptr, &includes,
		"main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS,
		0, &bytecode, &errors));
	ComPtr<ID3D11ComputeShader> shader;
	Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader));
	std::array<float, width * height> depths{};
	std::array<unsigned, width * height> colors{};
	colors.fill(0x80402010u);
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
			depths[y * width + x] = (x < 4 || x >= width - 4) ? 0.0f : 0.5f;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial{ depths.data(), width * sizeof(float), 0 };
	ComPtr<ID3D11Texture2D> depth, color, staging;
	Check(device->CreateTexture2D(&desc, &initial, &depth));
	ComPtr<ID3D11ShaderResourceView> depthSRV;
	Check(device->CreateShaderResourceView(depth.Get(), nullptr, &depthSRV));
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	initial.pSysMem = colors.data();
	Check(device->CreateTexture2D(&desc, &initial, &color));
	ComPtr<ID3D11UnorderedAccessView> colorUAV;
	Check(device->CreateUnorderedAccessView(color.Get(), nullptr, &colorUAV));
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = 8 * sizeof(unsigned);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> constants;
	Check(device->CreateBuffer(&bufferDesc, nullptr, &constants));
	auto run = [&](unsigned depthEyeWidth, unsigned depthHeight) {
		context->UpdateSubresource(color.Get(), 0, nullptr, colors.data(), width * sizeof(unsigned), 0);
		context->CSSetShader(shader.Get(), nullptr, 0);
		auto* srv = depthSRV.Get();
		auto* uav = colorUAV.Get();
		auto* cb = constants.Get();
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetConstantBuffers(0, 1, &cb);
		for (unsigned eye = 0; eye < 2; ++eye) {
			const std::array<unsigned, 8> values{
				eye * depthEyeWidth, eye * eyeWidth, 0, 0,
				depthEyeWidth, depthHeight, eyeWidth, height
			};
			context->UpdateSubresource(cb, 0, nullptr, values.data(), 0, 0);
			context->Dispatch((eyeWidth + 7) / 8, (height + 7) / 8, 1);
		}
		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CopyResource(staging.Get(), color.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		bool exact = true;
		for (unsigned y = 0; y < height; ++y) {
			const auto* row = reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (unsigned x = 0; x < width; ++x) {
				const bool hidden = x < 6 || x >= width - 6;
				exact = exact && row[x] == (hidden ? 0u : colors[y * width + x]);
			}
		}
		context->Unmap(staging.Get(), 0);
		return exact;
	};
	const auto identity = reinterpret_cast<std::uintptr_t>(depth.Get());
	const NeuralRendering::MainDepthPresentationProof proof{ 7, identity };
	Require(proof.SupportsOutputLayout(eyeWidth / 2, height / 2, eyeWidth, height, 7, identity));
	Require(run(eyeWidth, height));
	Require(!run(eyeWidth / 2, height / 2));
	std::puts("Passed post-upscale hidden-area mapping; reduced-grid control detects wrong eye/depth sampling");
}

int main(int argc, char** argv)
{
	Require(argc == 3);
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL level{};
	const auto debugResult = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
		D3D11_SDK_VERSION, &device, &level, &context);
	if (FAILED(debugResult)) {
		Require(debugResult == DXGI_ERROR_SDK_COMPONENT_MISSING);
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, &level, &context));
		std::puts("D3D debug layer unavailable");
	}
	for (const auto [format, bytes] : std::array<std::pair<DXGI_FORMAT, UINT>, 8>{ { { DXGI_FORMAT_R8G8B8A8_UNORM, 4 }, { DXGI_FORMAT_R10G10B10A2_UNORM, 4 },
			 { DXGI_FORMAT_R16G16B16A16_FLOAT, 8 }, { DXGI_FORMAT_R32G32B32A32_FLOAT, 16 },
			 { DXGI_FORMAT_R11G11B10_FLOAT, 4 }, { DXGI_FORMAT_R16G16B16A16_UNORM, 8 },
			 { DXGI_FORMAT_B8G8R8A8_UNORM, 4 }, { DXGI_FORMAT_B8G8R8X8_UNORM, 4 } } }) {
		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ format, 0 };
		Check(device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support)));
		constexpr UINT required = D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
		if ((support.OutFormatSupport2 & required) != required) {
			std::printf("Format %u is ineligible for the route on this WARP device (typed UAV support)\n", format);
			continue;
		}
		VerifyRegions(device.Get(), context.Get(), format, bytes);
	}
	VerifyLifecycle(device.Get(), context.Get());
	VerifyState(device.Get(), context.Get());
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 8;
	desc.Height = 4;
	desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	std::array<unsigned, 32> baseline{};
	baseline.fill(0x80402010u);
	D3D11_SUBRESOURCE_DATA initial{ baseline.data(), 8 * sizeof(unsigned), 0 };
	ComPtr<ID3D11Texture2D> destination, scratch, staging, wrongSize;
	ComPtr<ID3D11RenderTargetView> rtv;
	Check(device->CreateTexture2D(&desc, &initial, &destination));
	Check(device->CreateRenderTargetView(destination.Get(), nullptr, &rtv));
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	Check(device->CreateTexture2D(&desc, nullptr, &scratch));
	desc.Width = 4;
	Check(device->CreateTexture2D(&desc, nullptr, &wrongSize));
	desc.Width = 8;
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(device->CreateTexture2D(&desc, nullptr, &staging));
	auto read = [&]() {
		context->CopyResource(staging.Get(), destination.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::array<unsigned, 32> result{};
		for (unsigned y = 0; y < 4; ++y) {
			const auto* row = reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (unsigned x = 0; x < 8; ++x) result[y * 8 + x] = row[x];
		}
		context->Unmap(staging.Get(), 0);
		return result;
	};
	auto* bound = rtv.Get();
	context->OMSetRenderTargets(1, &bound, nullptr);
	{
		NeuralRendering::FramebufferTransaction bad(context.Get(), destination.Get(), wrongSize.Get());
		Require(!bad.IsValid());
		Require(!bad.Commit(true));
	}
	std::array<unsigned, 16> left{}, right{};
	left.fill(0xFF0000FFu);
	right.fill(0xFF00FF00u);
	const D3D11_BOX leftBox{ 0, 0, 0, 4, 4, 1 }, rightBox{ 4, 0, 0, 8, 4, 1 };
	{
		NeuralRendering::FramebufferTransaction failed(context.Get(), destination.Get(), scratch.Get());
		Require(failed.IsValid());
		context->UpdateSubresource(scratch.Get(), 0, &leftBox, left.data(), 4 * sizeof(unsigned), 0);
		Require(!failed.Commit(false));
	}
	Require(read() == baseline);
	{
		NeuralRendering::FramebufferTransaction pair(context.Get(), destination.Get(), scratch.Get());
		Require(pair.IsValid());
		context->UpdateSubresource(scratch.Get(), 0, &leftBox, left.data(), 4 * sizeof(unsigned), 0);
		context->UpdateSubresource(scratch.Get(), 0, &rightBox, right.data(), 4 * sizeof(unsigned), 0);
		Require(pair.Commit(true));
	}
	const auto result = read();
	for (unsigned y = 0; y < 4; ++y)
		for (unsigned x = 0; x < 8; ++x)
			Require(result[y * 8 + x] == (x < 4 ? left[0] : right[0]));
	ComPtr<ID3D11RenderTargetView> restored;
	context->OMGetRenderTargets(1, &restored, nullptr);
	Require(restored.Get() == rtv.Get());
	// A post-UI transaction must copy the current destination again.
	context->UpdateSubresource(destination.Get(), 0, nullptr, baseline.data(), 8 * sizeof(unsigned), 0);
	{
		NeuralRendering::FramebufferTransaction postUi(context.Get(), destination.Get(), scratch.Get());
		Require(postUi.Commit(true));
	}
	Require(read() == baseline);
	std::puts("Passed non-UAV framebuffer stereo commit, failed-eye preservation, fresh copy and RTV restoration");
	VerifyPostUpscaleHiddenArea(device.Get(), context.Get(), argv[1], argv[2]);
	ComPtr<ID3D11InfoQueue> info;
	if (SUCCEEDED(device.As(&info))) {
		for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index) {
			SIZE_T size = 0;
			Check(info->GetMessage(index, nullptr, &size));
			std::vector<unsigned char> storage(size);
			auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
			Check(info->GetMessage(index, message, &size));
			if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
				std::fprintf(stderr, "%s\n", message->pDescription);
				Require(false);
			}
		}
		std::puts("D3D debug layer: no warnings or errors");
	}
}
