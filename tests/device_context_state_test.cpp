#define NOMINMAX
#include "Utils/DeviceContextState.h"
#include "Utils/FullscreenPassState.h"
#include <Renderdoc/renderdoc_app.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <iostream>
#include <stdexcept>
#include <string>

// Resource naming is observed here; the production helper performs the debug-name write.
namespace Util
{
	std::string resourceName;
	void SetResourceName(ID3D11DeviceChild* resource, const char* format, ...)
	{
		if (!resource)
			throw std::runtime_error("Cannot name a null state");
		char buffer[256]{};
		va_list arguments;
		va_start(arguments, format);
		vsnprintf(buffer, sizeof(buffer), format, arguments);
		va_end(arguments);
		resourceName = buffer;
		winrt::check_hresult(resource->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(resourceName.size()), buffer));
	}
}
static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
static void Check(HRESULT result) { winrt::check_hresult(result); }

static void TestDynamicShaderRestoration(ID3D11Device* device, ID3D11DeviceContext1* context)
{
	constexpr char source[] = R"(
interface IColor { float4 GetColor(); };
class Red : IColor { float4 GetColor() { return float4(1,0,0,1); } };
Red red;
IColor selected;
float4 main() : SV_Target { return selected.GetColor() + red.GetColor(); }
)";
	winrt::com_ptr<ID3DBlob> code;
	Check(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_SKIP_OPTIMIZATION, 0, code.put(), nullptr));
	winrt::com_ptr<ID3D11ClassLinkage> linkage;
	Check(device->CreateClassLinkage(linkage.put()));
	Util::SetResourceName(linkage.get(), "Test::ClassLinkage");
	winrt::com_ptr<ID3D11PixelShader> shader;
	Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), linkage.get(), shader.put()));
	Util::SetResourceName(shader.get(), "Test::LinkedPixelShader");
	winrt::com_ptr<ID3D11ClassInstance> instance;
	Check(linkage->CreateClassInstance("Red", 0, 0, 0, 0, instance.put()));
	Util::SetResourceName(instance.get(), "Test::ClassInstance");
	auto* bound = instance.get();
	context->PSSetShader(shader.get(), &bound, 1);
	winrt::com_ptr<ID3D11DeviceContext1> ownedContext;
	std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
	Require(Util::CreateIsolatedDeviceContextState(device, context, ownedContext, isolated, "Test::DynamicState"), "create dynamic shader isolation");
	{
		const Util::ScopedDeviceContextState guard(context, isolated.get());
		Require(static_cast<bool>(guard), "dynamic shader isolation activates");
		context->ClearState();
	}
	winrt::com_ptr<ID3D11PixelShader> restoredShader;
	winrt::com_ptr<ID3D11ClassInstance> restoredInstance;
	UINT count = 1;
	context->PSGetShader(restoredShader.put(), restoredInstance.put(), &count);
	Require(restoredShader == shader && count == 1 && restoredInstance == instance, "restore dynamic shader and owned class instance");
	context->ClearState();
}

static void TestExtendedBindingRestoration(ID3D11Device* device, ID3D11DeviceContext1* context)
{
	context->ClearState();
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = 1024;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	winrt::com_ptr<ID3D11Buffer> constants;
	Check(device->CreateBuffer(&desc, nullptr, constants.put()));
	Util::SetResourceName(constants.get(), "Test::RangedConstants");
	auto* cb = constants.get();
	const UINT first = 16, count = 32;
	constexpr std::array setBuffers{
		&ID3D11DeviceContext1::VSSetConstantBuffers1, &ID3D11DeviceContext1::HSSetConstantBuffers1,
		&ID3D11DeviceContext1::DSSetConstantBuffers1, &ID3D11DeviceContext1::GSSetConstantBuffers1,
		&ID3D11DeviceContext1::PSSetConstantBuffers1, &ID3D11DeviceContext1::CSSetConstantBuffers1
	};
	constexpr std::array getBuffers{
		&ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::HSGetConstantBuffers1,
		&ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext1::GSGetConstantBuffers1,
		&ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext1::CSGetConstantBuffers1
	};
	for (auto set : setBuffers)
		(context->*set)(13, 1, &cb, &first, &count);
	constexpr std::array setWholeBuffers{
		&ID3D11DeviceContext1::VSSetConstantBuffers, &ID3D11DeviceContext1::HSSetConstantBuffers,
		&ID3D11DeviceContext1::DSSetConstantBuffers, &ID3D11DeviceContext1::GSSetConstantBuffers,
		&ID3D11DeviceContext1::PSSetConstantBuffers, &ID3D11DeviceContext1::CSSetConstantBuffers
	};
	for (auto set : setWholeBuffers)
		(context->*set)(12, 1, &cb);
	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	Check(device->CreateSamplerState(&samplerDesc, sampler.put()));
	Util::SetResourceName(sampler.get(), "Test::HighSlotSampler");
	auto* sample = sampler.get();
	constexpr std::array setSamplers{
		&ID3D11DeviceContext1::VSSetSamplers, &ID3D11DeviceContext1::HSSetSamplers,
		&ID3D11DeviceContext1::DSSetSamplers, &ID3D11DeviceContext1::GSSetSamplers,
		&ID3D11DeviceContext1::PSSetSamplers, &ID3D11DeviceContext1::CSSetSamplers
	};
	constexpr std::array getSamplers{
		&ID3D11DeviceContext1::VSGetSamplers, &ID3D11DeviceContext1::HSGetSamplers,
		&ID3D11DeviceContext1::DSGetSamplers, &ID3D11DeviceContext1::GSGetSamplers,
		&ID3D11DeviceContext1::PSGetSamplers, &ID3D11DeviceContext1::CSGetSamplers
	};
	for (auto set : setSamplers)
		(context->*set)(15, 1, &sample);
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof(UINT);
	winrt::com_ptr<ID3D11Buffer> counterBuffer;
	Check(device->CreateBuffer(&desc, nullptr, counterBuffer.put()));
	Util::SetResourceName(counterBuffer.get(), "Test::Counter");
	D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
	viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	viewDesc.Buffer.NumElements = desc.ByteWidth / desc.StructureByteStride;
	viewDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
	winrt::com_ptr<ID3D11UnorderedAccessView> counterView;
	Check(device->CreateUnorderedAccessView(counterBuffer.get(), &viewDesc, counterView.put()));
	Util::SetResourceName(counterView.get(), "Test::Counter UAV");
	auto* output = counterView.get();
	const UINT outputSlot = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? 63u : 7u;
	const UINT initialCount = 23;
	context->CSSetUnorderedAccessViews(outputSlot, 1, &output, &initialCount);
	winrt::com_ptr<ID3D11DeviceContext1> ownedContext;
	std::unique_ptr<Util::IsolatedDeviceContextState> outer, inner;
	Require(Util::CreateIsolatedDeviceContextState(device, context, ownedContext, outer, "Test::Outer"), "create outer state");
	Require(Util::CreateIsolatedDeviceContextState(device, context, ownedContext, inner, "Test::Inner"), "create inner state");
	{
		Util::ScopedDeviceContextState guard(context, outer.get());
		Require(static_cast<bool>(guard), "extended outer state activates");
		const Util::ScopedDeviceContextState duplicate(context, outer.get());
		Require(!static_cast<bool>(duplicate), "same state cannot be recursively entered");
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		{
			const Util::ScopedDeviceContextState nested(context, inner.get());
			Require(static_cast<bool>(nested), "different nested state activates");
			context->ClearState();
		}
		D3D11_PRIMITIVE_TOPOLOGY topology{};
		context->IAGetPrimitiveTopology(&topology);
		Require(topology == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, "nested restoration preserves outer bindings");
	}
	for (auto get : getBuffers) {
		winrt::com_ptr<ID3D11Buffer> restored;
		UINT restoredFirst = 0, restoredCount = 0;
		(context->*get)(13, 1, restored.put(), &restoredFirst, &restoredCount);
		Require(restored == constants && restoredFirst == first && restoredCount == count, "restore all six constant-buffer windows");
		restored = nullptr;
		(context->*get)(12, 1, restored.put(), &restoredFirst, &restoredCount);
		Require(restored == constants && restoredFirst == 0 && restoredCount == D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT, "whole-buffer bindings survive alongside ranged bindings");
		restored = nullptr;
		(context->*get)(11, 1, restored.put(), nullptr, nullptr);
		Require(!restored, "unbound slots remain empty alongside bound buffers");
	}
	for (auto get : getSamplers) {
		winrt::com_ptr<ID3D11SamplerState> restored;
		(context->*get)(15, 1, restored.put());
		Require(restored == sampler, "restore all six high sampler slots");
	}
	winrt::com_ptr<ID3D11UnorderedAccessView> restoredOutput;
	context->CSGetUnorderedAccessViews(outputSlot, 1, restoredOutput.put());
	Require(restoredOutput == counterView, "restore highest supported compute UAV slot");
	context->ClearState();
	desc = {};
	desc.ByteWidth = sizeof(UINT);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	winrt::com_ptr<ID3D11Buffer> readback;
	Check(device->CreateBuffer(&desc, nullptr, readback.put()));
	Util::SetResourceName(readback.get(), "Test::CounterReadback");
	context->CopyStructureCount(readback.get(), 0, counterView.get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	Check(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped));
	const UINT restoredCount = *static_cast<const UINT*>(mapped.pData);
	context->Unmap(readback.get(), 0);
	Require(restoredCount == initialCount, "restoration preserves hidden UAV counter");
}

static void TestIsolationFallback()
{
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
	winrt::com_ptr<ID3D11DeviceContext1> context1;
	std::unique_ptr<Util::IsolatedDeviceContextState> state;
	Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, state, "Test::BeforeHooks"), "create state before hooks");
	Util::RegisterDeviceContextStateForHooks(context.get());
	Util::DisableDeviceContextStateIsolation(context.get());
	Util::RegisterDeviceContextStateForHooks(context.get());
	auto* previous = state.get();
	Require(!Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, state, "Invalid") && state.get() == previous,
		"latched fallback rejects later state creation without replacing existing resources");
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	const Util::ScopedDeviceContextState guard(context1.get(), state.get());
	Require(!static_cast<bool>(guard), "latched fallback rejects previously created isolated states");
	D3D11_PRIMITIVE_TOPOLOGY topology{};
	context->IAGetPrimitiveTopology(&topology);
	Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, "rejected isolation preserves context state");
}
template <UINT Slot, UINT ResourceCount, bool SaveSampler>
static void TestFullscreenRestoration(ID3D11Device* device, ID3D11DeviceContext1* context, UINT outputSlot)
{
	context->ClearState();
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = 1024;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	winrt::com_ptr<ID3D11Buffer> buffer;
	Check(device->CreateBuffer(&bufferDesc, nullptr, buffer.put()));
	Util::SetResourceName(buffer.get(), "Test::FullscreenConstants");
	auto* cb = buffer.get();
	const UINT first = 16, count = 16;
	context->PSSetConstantBuffers1(Slot, 1, &cb, &first, &count);
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = desc.Height = 16;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	Check(device->CreateTexture2D(&desc, nullptr, texture.put()));
	Util::SetResourceName(texture.get(), "Test::FullscreenAlias");
	Check(device->CreateShaderResourceView(texture.get(), nullptr, srv.put()));
	Util::SetResourceName(srv.get(), "Test::FullscreenAlias SRV");
	Check(device->CreateRenderTargetView(texture.get(), nullptr, rtv.put()));
	Util::SetResourceName(rtv.get(), "Test::FullscreenAlias RTV");
	auto* input = srv.get();
	context->PSSetShaderResources(0, 1, &input);
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	winrt::com_ptr<ID3D11Texture2D> unorderedTexture;
	winrt::com_ptr<ID3D11UnorderedAccessView> unorderedView;
	Check(device->CreateTexture2D(&desc, nullptr, unorderedTexture.put()));
	Util::SetResourceName(unorderedTexture.get(), "Test::FullscreenUnordered");
	Check(device->CreateUnorderedAccessView(unorderedTexture.get(), nullptr, unorderedView.put()));
	Util::SetResourceName(unorderedView.get(), "Test::FullscreenUnordered UAV");
	auto* unorderedOutput = unorderedView.get();
	context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, outputSlot, 1, &unorderedOutput, nullptr);
	D3D11_QUERY_DESC query{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
	winrt::com_ptr<ID3D11Predicate> predicate;
	Check(device->CreatePredicate(&query, predicate.put()));
	Util::SetResourceName(predicate.get(), "Test::FullscreenPredicate");
	context->SetPredication(predicate.get(), TRUE);
	try {
		Util::FullscreenPassState<Slot, ResourceCount, SaveSampler> restore(context);
		winrt::com_ptr<ID3D11Predicate> activePredicate;
		context->GetPredication(activePredicate.put(), nullptr);
		Require(!activePredicate, "fullscreen work ignores engine predication");
		auto* output = rtv.get();
		context->OMSetRenderTargets(1, &output, nullptr);
		ID3D11Buffer* empty = nullptr;
		context->PSSetConstantBuffers(Slot, 1, &empty);
		throw std::runtime_error("fullscreen pass aborted");
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()) == "fullscreen pass aborted", error.what());
	}
	winrt::com_ptr<ID3D11ShaderResourceView> restoredInput;
	context->PSGetShaderResources(0, 1, restoredInput.put());
	Require(restoredInput == srv, "restore input after releasing aliased output");
	winrt::com_ptr<ID3D11Buffer> restoredBuffer;
	UINT restoredFirst = 0, restoredCount = 0;
	context->PSGetConstantBuffers1(Slot, 1, restoredBuffer.put(), &restoredFirst, &restoredCount);
	Require(restoredBuffer == buffer && restoredFirst == first && restoredCount == count, "restore constant-buffer window");
	winrt::com_ptr<ID3D11Predicate> restoredPredicate;
	BOOL value = FALSE;
	context->GetPredication(restoredPredicate.put(), &value);
	Require(restoredPredicate == predicate && value, "restore fullscreen predication");
	winrt::com_ptr<ID3D11UnorderedAccessView> restoredOutput;
	context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, outputSlot, 1, restoredOutput.put());
	Require(restoredOutput == unorderedView, "restore output UAV displaced by fullscreen targets");
	context->ClearState();
}

int main(int argc, char** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	try {
		Util::RegisterDeviceContextStateForHooks(nullptr);
		const bool manual = argc > 1 && std::string(argv[1]) == "--manual";
		RENDERDOC_API_1_7_0* capture = nullptr;
		if (argc > 2) {
			const auto library = LoadLibraryExA(argv[2], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			Require(library != nullptr, "load capture library");
			const auto api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(library, "RENDERDOC_GetAPI"));
			Require(api && api(eRENDERDOC_API_Version_1_3_0, reinterpret_cast<void**>(&capture)) == 1, "capture API");
			capture->UnloadCrashHandler();
			if (argc > 3)
				capture->SetCaptureFilePathTemplate(argv[3]);
			Require(capture->SetCaptureOptionU32(eRENDERDOC_Option_AllowUnsupportedVendorExtensions, 0x10DE) == 1, "vendor passthrough");
		}

		if (!manual)
			TestIsolationFallback();
		Util::UseManualDeviceContextState(manual);
		std::cout << "Creating test device" << std::endl;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		std::cout << "Creating isolated state" << std::endl;
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		std::unique_ptr<Util::IsolatedDeviceContextState> isolated;
		Require(Util::CreateIsolatedDeviceContextState(device.get(), context.get(), context1, isolated, "Test::Isolated"), "create isolated state");
		Require(manual || Util::resourceName == "Test::Isolated", "isolated state receives resource name");
		auto* originalIsolated = isolated.get();
		Require(!Util::CreateIsolatedDeviceContextState(nullptr, context.get(), context1, isolated, "Invalid") && isolated.get() == originalIsolated, "failed initialization retains valid state");
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = 64;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		winrt::com_ptr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&bufferDesc, nullptr, buffer.put()));
		Util::SetResourceName(buffer.get(), "Test::ContextConstantBuffer");
		auto* cb = buffer.get();
		context->VSSetConstantBuffers(3, 1, &cb);
		context->PSSetConstantBuffers(4, 1, &cb);
		context->GSSetConstantBuffers(5, 1, &cb);
		context->CSSetConstantBuffers(6, 1, &cb);
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = 32;
		textureDesc.Height = 32;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		std::array<winrt::com_ptr<ID3D11Texture2D>, 2> textures;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, 2> views;
		std::array<ID3D11RenderTargetView*, 2> targets;
		for (std::size_t i = 0; i < 2; ++i) {
			Check(device->CreateTexture2D(&textureDesc, nullptr, textures[i].put()));
			Util::SetResourceName(textures[i].get(), "Test::ContextTarget%zu", i);
			Check(device->CreateRenderTargetView(textures[i].get(), nullptr, views[i].put()));
			Util::SetResourceName(views[i].get(), "Test::ContextTarget%zu RTV", i);
			targets[i] = views[i].get();
		}
		context->OMSetRenderTargets(2, targets.data(), nullptr);
		const D3D11_VIEWPORT viewports[] = { { 0, 0, 32, 32, 0, 1 }, { 2, 3, 20, 21, .1f, .8f } };
		const D3D11_RECT scissors[] = { { 0, 0, 31, 30 }, { 2, 3, 18, 19 } };
		context->RSSetViewports(2, viewports);
		context->RSSetScissorRects(2, scissors);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		const float blendFactor[] = { .1f, .2f, .3f, .4f };
		context->OMSetBlendState(nullptr, blendFactor, 0x12345678);
		context->OMSetDepthStencilState(nullptr, 37);
		D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
		winrt::com_ptr<ID3D11Predicate> predicate;
		Check(device->CreatePredicate(&predicateDesc, predicate.put()));
		Util::SetResourceName(predicate.get(), "Test::ContextPredicate");
		context->SetPredication(predicate.get(), TRUE);
		auto verify = [&] {
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			context->IAGetPrimitiveTopology(&topology);
			Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, "restore topology");
			D3D11_VIEWPORT restoredViewports[2]{};
			UINT count = 2;
			context->RSGetViewports(&count, restoredViewports);
			Require(count == 2 && !std::memcmp(viewports, restoredViewports, sizeof(viewports)), "restore all viewports");
			D3D11_RECT restoredScissors[2]{};
			count = 2;
			context->RSGetScissorRects(&count, restoredScissors);
			Require(count == 2 && !std::memcmp(scissors, restoredScissors, sizeof(scissors)), "restore all scissors");
			ID3D11RenderTargetView* restoredTargets[2]{};
			context->OMGetRenderTargets(2, restoredTargets, nullptr);
			bool same = restoredTargets[0] == targets[0] && restoredTargets[1] == targets[1];
			for (auto* target : restoredTargets)
				if (target)
					target->Release();
			Require(same, "restore multiple render targets");
			for (int stage = 0; stage < 4; ++stage) {
				winrt::com_ptr<ID3D11Buffer> restored;
				if (stage == 0)
					context->VSGetConstantBuffers(3, 1, restored.put());
				if (stage == 1)
					context->PSGetConstantBuffers(4, 1, restored.put());
				if (stage == 2)
					context->GSGetConstantBuffers(5, 1, restored.put());
				if (stage == 3)
					context->CSGetConstantBuffers(6, 1, restored.put());
				Require(restored.get() == buffer.get(), "restore shader-stage bindings");
			}
			float restoredBlend[4]{};
			UINT mask = 0;
			context->OMGetBlendState(nullptr, restoredBlend, &mask);
			Require(mask == 0x12345678 && !std::memcmp(restoredBlend, blendFactor, sizeof(blendFactor)), "restore blend values");
			UINT stencil = 0;
			context->OMGetDepthStencilState(nullptr, &stencil);
			Require(stencil == 37, "restore stencil reference");
			winrt::com_ptr<ID3D11Predicate> restoredPredicate;
			BOOL value = FALSE;
			context->GetPredication(restoredPredicate.put(), &value);
			Require(restoredPredicate.get() == predicate.get() && value, "restore predication");
		};
		Util::RegisterDeviceContextStateForHooks(context.get());
		verify();
		Util::resourceName = "NoRepeatedCreation";
		Util::RegisterDeviceContextStateForHooks(context.get());
		Require(Util::resourceName == "NoRepeatedCreation", "context registration does not allocate native states");
		verify();
		if (capture)
			capture->StartFrameCapture(device.get(), nullptr);
		std::cout << "Testing scoped restoration" << std::endl;
		const auto initialReferences = isolated->native ? isolated->native->AddRef() : 0u;
		if (isolated->native)
			isolated->native->Release();
		for (int iteration = 0; iteration < 64; ++iteration) {
			{
				Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
				Require(static_cast<bool>(guard), "state guard activates");
				D3D11_PRIMITIVE_TOPOLOGY prepared{};
				context->IAGetPrimitiveTopology(&prepared);
				Require(prepared == (iteration ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED), "isolated state retains prepared bindings between scopes");
				context->ClearState();
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				if (iteration == 1) {
					guard.Restore();
					verify();
					guard.Restore();
				}
			}
			verify();
		}
		if (isolated->native) {
			const auto restoredReferences = isolated->native->AddRef();
			isolated->native->Release();
			Require(initialReferences == restoredReferences, "native state wrapper references remain balanced");
		}
		try {
			Util::ScopedDeviceContextState guard(context1.get(), isolated.get());
			context->ClearState();
			throw std::runtime_error("render aborted");
		} catch (const std::runtime_error&) {}
		verify();
		context->ClearState();
		TestExtendedBindingRestoration(device.get(), context1.get());
		TestDynamicShaderRestoration(device.get(), context1.get());
		for (UINT slot : { 0u, 7u }) {
			TestFullscreenRestoration<0, 3, true>(device.get(), context1.get(), slot);
			TestFullscreenRestoration<1, 1, false>(device.get(), context1.get(), slot);
		}
		if (capture)
			Require(capture->EndFrameCapture(device.get(), nullptr) == 1, "finalize capture");
		Check(device->GetDeviceRemovedReason());
		std::cout << "D3D11 WARP full-state restoration passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
