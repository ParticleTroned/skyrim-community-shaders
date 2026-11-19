#define NOMINMAX
#include "Features/Upscaling/CompositeOutputFormat.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"
#include <d3d11sdklayers.h>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

namespace BindingFixture
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Resource binding regression");
	}

	struct LazyShader
	{
		ID3D11ComputeShader* cached = nullptr;
		bool fail = false;
		unsigned requests = 0;
		bool unorm = false;
		ID3D11ComputeShader* get() const { return cached; }
		ID3D11ComputeShader* Get(const wchar_t*, std::initializer_list<std::pair<const char*, const char*>> defines,
			const char*, const char*, const char*)
		{
			++requests;
			unorm = defines.size() == 1 && std::string(defines.begin()->first) == "OUTPUT_UNORM";
			if (!fail)
				cached = reinterpret_cast<ID3D11ComputeShader*>(1);
			return cached;
		}
	};
	struct Upscaling
	{
		LazyShader foveatedCenterBlendCS, foveatedCenterBlendUnormCS;
		bool saveFrame = false;
		bool ShouldReuseOrdinarySaveResources() const { return saveFrame; }
		ID3D11ComputeShader* GetFoveatedCenterBlendCS(DXGI_FORMAT outputFormat);
	};
#include "composite_shader_selection.h"

	namespace RE
	{
		namespace BSGraphics
		{
			struct Texture
			{
				void* resourceView = nullptr;
			};
			enum class TextureFilterMode
			{
				kAnisotropic,
				Comparison
			};
			enum class TextureAddressMode
			{
				Wrap,
				Clamp
			};
			struct RendererShadowState
			{
				Texture* bound = nullptr;
				TextureFilterMode filter = TextureFilterMode::Comparison;
				TextureAddressMode address = TextureAddressMode::Wrap;
				unsigned binds = 0;
				Texture* baseBound = nullptr;
				TextureFilterMode baseFilter = TextureFilterMode::Comparison;
				void SetPSTexture(unsigned slot, Texture* texture)
				{
					Require(slot == 0 || slot == 4);
					(slot == 0 ? baseBound : bound) = texture;
					++binds;
				}
				void SetPSTextureFilterMode(unsigned slot, TextureFilterMode mode)
				{
					Require(slot == 0 || slot == 4);
					(slot == 0 ? baseFilter : filter) = mode;
				}
				void SetPSTextureAddressMode(unsigned slot, TextureAddressMode mode)
				{
					Require(slot == 4);
					address = mode;
				}
			};
		}
		struct NiSourceTexture
		{
			BSGraphics::Texture* rendererTexture = nullptr;
		};
		struct BSShaderMaterial
		{
			enum class Feature
			{
				kEnvironmentMap,
				kMultilayerParallax,
				kEye,
				Other
			};
		};
		struct BSLightingShaderMaterialBase
		{
			BSShaderMaterial::Feature feature = BSShaderMaterial::Feature::Other;
			auto GetFeature() const { return feature; }
		};
		struct BSLightingShaderMaterialEnvmap : BSLightingShaderMaterialBase
		{
			std::shared_ptr<NiSourceTexture> envTexture;
		};
		using BSLightingShaderMaterialMultiLayerParallax = BSLightingShaderMaterialEnvmap;
		using BSLightingShaderMaterialEye = BSLightingShaderMaterialEnvmap;
	}
	namespace SIE
	{
		struct ShaderCache
		{
#include "effect_flags.h"
		};
	}
	void BindOptionalMaterialTexture(RE::BSGraphics::RendererShadowState*, uint32_t, RE::NiSourceTexture*);
#include "effect_material_binding.h"
#include "lighting_environment_binding.h"
#include "optional_material_binding.h"

	void TestBindings()
	{
		using namespace RE;
		using namespace RE::BSGraphics;
		Texture valid{ reinterpret_cast<void*>(1) }, unresolved;
		for (uint32_t palette : { 0u, 1u << 19, 1u << 20, (1u << 19) | (1u << 20) }) {
			RendererShadowState effectState;
			effectState.baseBound = effectState.bound = &valid;
			ClearUnusedEffectPalette(&effectState, palette | (1u << 6));
			Require(effectState.baseBound == &valid && effectState.baseFilter == TextureFilterMode::Comparison);
			Require(effectState.bound == (palette ? &valid : nullptr));
			Require(effectState.filter == (palette ? TextureFilterMode::Comparison : TextureFilterMode::kAnisotropic));
			Require(effectState.binds == (palette ? 0u : 1u));
		}
		for (auto feature : { BSShaderMaterial::Feature::kEnvironmentMap, BSShaderMaterial::Feature::kMultilayerParallax, BSShaderMaterial::Feature::kEye }) {
			BSLightingShaderMaterialEnvmap material;
			material.feature = feature;
			RendererShadowState state;
			state.bound = &valid;
			BindLightingEnvironmentTexture(&state, &material);
			Require(!state.bound && state.filter == TextureFilterMode::kAnisotropic);
			material.envTexture = std::make_shared<NiSourceTexture>();
			for (auto* texture : { static_cast<Texture*>(nullptr), &unresolved, &valid }) {
				state.bound = &valid;
				state.filter = TextureFilterMode::Comparison;
				material.envTexture->rendererTexture = texture;
				BindLightingEnvironmentTexture(&state, &material);
				Require(state.bound == (texture == &valid ? &valid : nullptr));
				Require(state.filter == (texture == &valid ? TextureFilterMode::Comparison : TextureFilterMode::kAnisotropic));
			}
		}
		RendererShadowState state;
		BSLightingShaderMaterialBase other;
		state.bound = &valid;
		BindLightingEnvironmentTexture(&state, &other);
		Require(state.binds == 0 && state.bound == &valid && state.filter == TextureFilterMode::Comparison);

		struct
		{
			NiSourceTexture* defaultTextureBlack;
		} runtime;
		struct GraphicsState
		{
			decltype(runtime)& data;
			auto& GetRuntimeData() { return data; }
		} graphics{ runtime };
		auto* graphicsState = &graphics;
		auto* shadowState = &state;
		NiSourceTexture black{ &valid }, texture;
		runtime.defaultTextureBlack = &black;
		struct
		{
			NiSourceTexture* displacementTexture;
			unsigned textureClampMode = 1;
		} material;
		auto* pbrMaterial = &material;
		enum class PBRShaderFlags
		{
			HasDisplacement
		};
		struct Flags
		{
			bool displaced = false;
			void set(PBRShaderFlags) { displaced = true; }
		} shaderFlags;
		for (auto* source : { static_cast<NiSourceTexture*>(nullptr), &black, &texture }) {
			for (auto* rendererTexture : { static_cast<Texture*>(nullptr), &unresolved, &valid }) {
				texture.rendererTexture = rendererTexture;
				material.displacementTexture = source;
				state.bound = &valid;
				state.filter = TextureFilterMode::Comparison;
				shaderFlags.displaced = false;
#include "pbr_displacement_binding.h"
				const bool usable = source == &texture && rendererTexture == &valid;
				Require(shaderFlags.displaced == usable && state.bound == (usable ? &valid : nullptr));
				Require(state.filter == TextureFilterMode::kAnisotropic && state.address == TextureAddressMode::Clamp);
			}
		}
	}

	void TestSelection()
	{
		Upscaling upscaling;
		for (auto format : { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R16G16B16A16_SNORM,
				 DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_D32_FLOAT })
			Require(!upscaling.GetFoveatedCenterBlendCS(format));
		Require(upscaling.foveatedCenterBlendCS.requests == 0 && upscaling.foveatedCenterBlendUnormCS.requests == 0);
		upscaling.saveFrame = true;
		Require(!upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R8G8B8A8_UNORM));
		Require(!upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R16G16B16A16_FLOAT));
		Require(upscaling.foveatedCenterBlendCS.requests == 0 && upscaling.foveatedCenterBlendUnormCS.requests == 0);
		upscaling.saveFrame = false;
		upscaling.foveatedCenterBlendCS.fail = true;
		Require(upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R8G8B8A8_UNORM));
		Require(upscaling.foveatedCenterBlendUnormCS.unorm && upscaling.foveatedCenterBlendCS.requests == 0);
		Require(!upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R16G16B16A16_FLOAT));
		upscaling.saveFrame = true;
		Require(upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R10G10B10A2_UNORM));
		Require(!upscaling.GetFoveatedCenterBlendCS(DXGI_FORMAT_R16G16B16A16_FLOAT));
		Require(upscaling.foveatedCenterBlendCS.requests == 1 && upscaling.foveatedCenterBlendUnormCS.requests == 1);
		Upscaling floatOnly;
		floatOnly.foveatedCenterBlendUnormCS.fail = true;
		Require(!floatOnly.GetFoveatedCenterBlendCS(DXGI_FORMAT_R8G8B8A8_UNORM));
		Require(floatOnly.GetFoveatedCenterBlendCS(DXGI_FORMAT_R16G16B16A16_FLOAT));
		Require(!floatOnly.foveatedCenterBlendCS.unorm);
	}
}

namespace
{
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* profile, std::vector<D3D_SHADER_MACRO> defines)
	{
		PackageIncludes includes("package/Shaders", "features/Upscaling/Shaders");
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	ComPtr<ID3D11ShaderReflection> Reflect(const wchar_t* path, const char* profile, std::vector<D3D_SHADER_MACRO> defines)
	{
		auto code = Compile(path, profile, std::move(defines));
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		return reflection;
	}

	void TestEffectStageEquivalence()
	{
		const std::array vertexIgnored{ "INDEXED_TEXTURE", "ADDBLEND", "MULTBLEND", "PARTICLES", "BLOOD", "MULTBLEND_DECAL", "MSN_SPU_SKINNED" };
		const std::array pixelIgnored{ "STRIP_PARTICLES", "BINORMAL_TANGENT", "SKY_OBJECT", "MSN_SPU_SKINNED" };
		for (bool vertex : { false, true }) {
			for (bool vr : { false, true }) {
				for (unsigned material = 0; material < 4; ++material) {
					for (unsigned output = 0; output < (vertex ? 1u : 3u); ++output) {
						std::vector<D3D_SHADER_MACRO> defines{ { vertex ? "VSHADER" : "PSHADER", "1" }, { "TEXCOORD", "1" }, { "TEXTURE", "1" }, { "VC", "1" } };
						if (vr)
							defines.push_back({ "VR", "1" });
						if (output != 0)
							defines.push_back({ "DEFERRED", "1" });
						if (output == 2)
							defines.push_back({ "CHARACTER_CATEGORY", "1" });
						if (material != 0)
							defines.push_back({ "NORMALS", "1" });
						if (material == 1) {
							defines.push_back({ "FALLOFF", "1" });
							defines.push_back({ "SOFT", "1" });
						} else if (material == 2) {
							defines.push_back({ "MEMBRANE", "1" });
							defines.push_back({ "SKINNED", "1" });
							defines.push_back({ "MOTIONVECTORS_NORMALS", "1" });
						} else if (material == 3) {
							defines.push_back({ "PROJECTED_UV", "1" });
						}
						const auto profile = vertex ? "vs_5_0" : "ps_5_0";
						auto base = Compile(L"package/Shaders/Effect.hlsl", profile, defines);
						const auto compare = [&](const char* macro) {
							auto otherDefines = defines;
							otherDefines.push_back({ macro, "1" });
							auto other = Compile(L"package/Shaders/Effect.hlsl", profile, std::move(otherDefines));
							if (base->GetBufferSize() != other->GetBufferSize() ||
								std::memcmp(base->GetBufferPointer(), other->GetBufferPointer(), base->GetBufferSize()) != 0)
								throw std::runtime_error(std::string("Effect stage key changes compiled bytecode: ") + profile + "/" + macro);
						};
						if (vertex) {
							for (const auto macro : vertexIgnored)
								compare(macro);
						} else {
							for (const auto macro : pixelIgnored)
								compare(macro);
						}
					}
				}
			}
		}
	}

	void TestPreviousOutputBinding()
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		std::array<ComPtr<ID3D11Texture2D>, 2> textures;
		std::array<ComPtr<ID3D11UnorderedAccessView>, 2> outputs;
		for (unsigned index = 0; index < textures.size(); ++index) {
			Check(device->CreateTexture2D(&desc, nullptr, &textures[index]));
			Util::SetResourceName(textures[index].Get(), "ShaderTest::CompositeBinding%u", index);
			Check(device->CreateUnorderedAccessView(textures[index].Get(), nullptr, &outputs[index]));
			Util::SetResourceName(outputs[index].Get(), "ShaderTest::CompositeBinding%u UAV", index);
		}
		ComPtr<ID3D11ShaderResourceView> input;
		Check(device->CreateShaderResourceView(textures[0].Get(), nullptr, &input));
		Util::SetResourceName(input.Get(), "ShaderTest::CompositeBinding SRV");
		auto* priorOutput = outputs[0].Get();
		context->CSSetUnorderedAccessViews(0, 1, &priorOutput, nullptr);
		ID3D11ShaderResourceView* srvs[3]{ input.Get(), nullptr, nullptr };
		ID3D11UnorderedAccessView* uavs[1]{ outputs[1].Get() };
#include "composite_bindings.h"
		ComPtr<ID3D11ShaderResourceView> boundInput;
		ComPtr<ID3D11UnorderedAccessView> boundOutput;
		context->CSGetShaderResources(0, 1, &boundInput);
		context->CSGetUnorderedAccessViews(0, 1, &boundOutput);
		BindingFixture::Require(boundInput.Get() == input.Get() && boundOutput.Get() == outputs[1].Get());
		context->ClearState();
	}

	void TestEffectPixels(const char* baseline)
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
			nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
		ComPtr<ID3D11InfoQueue> messages;
		Check(device.As(&messages));
		const auto read = [](const char* path) {
			std::ifstream file(path, std::ios::binary);
			if (!file)
				throw std::runtime_error("Cannot read Effect shader baseline");
			return std::string(std::istreambuf_iterator<char>(file), {});
		};
		const auto current = read("package/Shaders/Effect.hlsl");
		const auto original = baseline ? read(baseline) : current;
		const auto compile = [](const std::string& source, std::vector<D3D_SHADER_MACRO> defines, const char* profile) {
			PackageIncludes includes("package/Shaders");
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> code, errors;
			const auto result = D3DCompile(source.data(), source.size(), "effect-runtime-test", defines.data(), &includes,
				"main", profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
			if (FAILED(result) && errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(result);
			return code;
		};
		const auto vertexCode = compile(R"(
float4 main(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
})",
			{}, "vs_5_0");
		ComPtr<ID3D11VertexShader> vertex;
		Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertex));
		Util::SetResourceName(vertex.Get(), "EffectTest::Vertex");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11RenderTargetView> target;
		Check(device->CreateTexture2D(&desc, nullptr, &output));
		Util::SetResourceName(output.Get(), "EffectTest::Output");
		Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
		Util::SetResourceName(target.Get(), "EffectTest::OutputRTV");
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Check(device->CreateTexture2D(&desc, nullptr, &staging));
		Util::SetResourceName(staging.Get(), "EffectTest::Readback");
		std::array<ComPtr<ID3D11Texture2D>, 2> textures;
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
		const std::array<std::array<float, 4>, 2> texels{ { { .4f, .6f, .2f, .8f }, { .3f, .5f, .7f, .6f } } };
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		for (unsigned i = 0; i < 2; ++i) {
			D3D11_SUBRESOURCE_DATA data{ texels[i].data(), sizeof(texels[i]), 0 };
			Check(device->CreateTexture2D(&desc, &data, &textures[i]));
			Util::SetResourceName(textures[i].Get(), "EffectTest::Texture%u", i);
			Check(device->CreateShaderResourceView(textures[i].Get(), nullptr, &views[i]));
			Util::SetResourceName(views[i].Get(), "EffectTest::Texture%uSRV", i);
		}
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> sampler;
		Check(device->CreateSamplerState(&samplerDesc, &sampler));
		Util::SetResourceName(sampler.Get(), "EffectTest::Sampler");
		const std::string wrapper = R"(
#undef main
float4 main(float4 position : SV_Position) : SV_Target0 {
    PS_INPUT input = (PS_INPUT)0;
    input.Position = position;
    input.TexCoord0 = float4(.5, .5, 1, 1);
    input.WorldPosition = float4(0, 0, 1, 1);
    input.Color = 1;
    input.FogAlpha = 1;
    return EffectMain(input).Diffuse;
})";
		unsigned cases = 0;
		for (bool vr : { false, true })
			for (bool texture : { false, true })
				for (bool deferred : { false, true }) {
					std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "VC", "1" }, { "TEXCOORD", "1" } };
					if (vr)
						defines.push_back({ "VR", "1" });
					if (texture)
						defines.push_back({ "TEXTURE", "1" });
					if (deferred)
						defines.push_back({ "DEFERRED", "1" });
					const auto dynamicCode = compile("#define main EffectMain\n" + current + wrapper, defines, "ps_5_0");
					for (unsigned gray = 0; gray < 4; ++gray) {
						auto reference = original;
						for (const auto& [name, enabled] : { std::pair{ "GrayscaleToColor", gray & 1 }, { "GrayscaleToAlpha", gray & 2 } }) {
							const std::string expression = std::string("Permutation::PixelShaderDescriptor & Permutation::EffectFlags::") + name;
							for (auto at = reference.find(expression); at != std::string::npos; at = reference.find(expression))
								reference.replace(at, expression.size(), enabled ? "true" : "false");
						}
						auto referenceDefines = defines;
						if (gray & 1)
							referenceDefines.push_back({ "GRAYSCALE_TO_COLOR", "1" });
						if (gray & 2)
							referenceDefines.push_back({ "GRAYSCALE_TO_ALPHA", "1" });
						const auto referenceCode = compile("#define main EffectMain\n" + reference + wrapper, referenceDefines, "ps_5_0");
						for (bool ignoreAlpha : { false, true })
							for (bool absent : { false, true }) {
								std::array<std::array<float, 4>, 2> pixels{};
								for (unsigned variant = 0; variant < 2; ++variant) {
									context->ClearState();
									const auto& code = variant ? dynamicCode : referenceCode;
									ComPtr<ID3D11PixelShader> shader;
									ComPtr<ID3D11ShaderReflection> reflection;
									Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
									Util::SetResourceName(shader.Get(), "EffectTest::Pixel");
									Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
									D3D11_SHADER_DESC shaderDesc{};
									Check(reflection->GetDesc(&shaderDesc));
									std::vector<std::unique_ptr<D3D11ShaderTest::ConstantBuffer>> constants;
									for (unsigned i = 0; i < shaderDesc.ConstantBuffers; ++i) {
										D3D11_SHADER_BUFFER_DESC cb{};
										Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&cb));
										auto buffer = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device.Get(), reflection.Get(), cb.Name);
										for (const char* name : { "BaseColor", "BaseColorScale", "PropertyColor" }) {
											D3D11_SHADER_VARIABLE_DESC variable{};
											if (SUCCEEDED(buffer->reflection->GetVariableByName(name)->GetDesc(&variable)))
												buffer->SetVariable(name, std::array{ 1.f, 1.f, 1.f, 1.f });
										}
										D3D11_SHADER_VARIABLE_DESC variable{};
										if (SUCCEEDED(buffer->reflection->GetVariableByName("Permutation::PixelShaderDescriptor")->GetDesc(&variable)))
											buffer->SetVariable("Permutation::PixelShaderDescriptor", (gray << 19) | (ignoreAlpha ? 1u << 21 : 0u));
										buffer->Bind(context.Get(), D3D11ShaderTest::Stage::Pixel);
										constants.push_back(std::move(buffer));
									}
									ID3D11ShaderResourceView* base = absent ? nullptr : views[0].Get();
									ID3D11ShaderResourceView* palette = absent || !gray ? nullptr : views[1].Get();
									context->PSSetShaderResources(0, 1, &base);
									context->PSSetShaderResources(4, 1, &palette);
									context->PSSetSamplers(0, 1, sampler.GetAddressOf());
									context->PSSetSamplers(4, 1, sampler.GetAddressOf());
									context->VSSetShader(vertex.Get(), nullptr, 0);
									context->PSSetShader(shader.Get(), nullptr, 0);
									context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
									D3D11_VIEWPORT viewport{ 0, 0, 1, 1, 0, 1 };
									context->RSSetViewports(1, &viewport);
									context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
									context->ClearRenderTargetView(target.Get(), std::array{ -9.f, -9.f, -9.f, -9.f }.data());
									context->Draw(3, 0);
									context->CopyResource(staging.Get(), output.Get());
									D3D11_MAPPED_SUBRESOURCE mapped{};
									Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
									std::memcpy(pixels[variant].data(), mapped.pData, sizeof(pixels[variant]));
									context->Unmap(staging.Get(), 0);
								}
								for (unsigned channel = 0; channel < 4; ++channel)
									if (!std::isfinite(pixels[1][channel]) || pixels[1][channel] == -9.f ||
										std::abs(pixels[0][channel] - pixels[1][channel]) > 1e-6f)
										throw std::runtime_error("Effect runtime palette differs from its specialized reference");
								std::array<float, 4> expected{ 1, 1, 1, 1 };
								if (texture || gray)
									expected = absent ? std::array<float, 4>{} : texels[0];
								if (ignoreAlpha && (texture || gray))
									expected[3] = 1;
								if (gray & 1)
									for (unsigned channel = 0; channel < 3; ++channel)
										expected[channel] = absent ? 0.f : texels[1][channel];
								if (gray & 2)
									expected[3] = absent ? 0.f : texels[1][3];
								for (unsigned channel = 0; channel < 4; ++channel)
									if (std::abs(pixels[1][channel] - expected[channel]) > 1e-6f)
										throw std::runtime_error("Effect palette produced an unexpected color or alpha");
								++cases;
							}
					}
				}
		for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
			SIZE_T size = 0;
			Check(messages->GetMessage(i, nullptr, &size));
			std::vector<std::byte> storage(size);
			auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
			Check(messages->GetMessage(i, message, &size));
			if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
				throw std::runtime_error(message->pDescription);
		}
		context->ClearState();
		std::cout << cases << " Effect runtime/reference WARP pixel comparisons passed\n";
	}
}

int main(int argc, char** argv)
{
	try {
		BindingFixture::TestSelection();
		BindingFixture::TestBindings();
		TestPreviousOutputBinding();
		TestEffectPixels(argc > 1 ? argv[1] : nullptr);
		TestEffectStageEquivalence();
		for (bool vr : { false, true }) {
			for (unsigned grayscale = 0; grayscale < 4; ++grayscale) {
				for (bool deferred : { false, true }) {
					for (unsigned materialLayout = 0; materialLayout < 3; ++materialLayout) {
						std::vector<D3D_SHADER_MACRO> defines{ { "PSHADER", "1" }, { "TEXCOORD", "1" }, { "VC", "1" } };
						if (materialLayout != 0)
							defines.push_back({ "TEXTURE", "1" });
						if (materialLayout == 2) {
							defines.push_back({ "NORMALS", "1" });
							defines.push_back({ "FALLOFF", "1" });
							defines.push_back({ "SOFT", "1" });
						}
						if (vr)
							defines.push_back({ "VR", "1" });
						if (deferred)
							defines.push_back({ "DEFERRED", "1" });
						if (grayscale & 1)
							defines.push_back({ "GRAYSCALE_TO_COLOR", "1" });
						if (grayscale & 2)
							defines.push_back({ "GRAYSCALE_TO_ALPHA", "1" });
						auto shader = Reflect(L"package/Shaders/Effect.hlsl", "ps_5_0", defines);
						for (const char* name : { "TexGrayscaleSampler", "SampGrayscaleSampler" }) {
							D3D11_SHADER_INPUT_BIND_DESC binding{};
							const bool bound = SUCCEEDED(shader->GetResourceBindingDescByName(name, &binding));
							if (!bound || binding.BindPoint != 4)
								throw std::runtime_error("Shared Effect shader lost its runtime palette binding");
						}
					}
				}
			}
			for (bool unorm : { false, true }) {
				std::vector<D3D_SHADER_MACRO> defines;
				if (vr)
					defines.push_back({ "VR", "1" });
				if (unorm)
					defines.push_back({ "OUTPUT_UNORM", "1" });
				auto shader = Reflect(L"features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl", "cs_5_0", defines);
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				Check(shader->GetResourceBindingDescByName("OutputColor", &binding));
				if (binding.Type != D3D_SIT_UAV_RWTYPED || binding.BindPoint != 0 ||
					binding.ReturnType != (unorm ? D3D_RETURN_TYPE_UNORM : D3D_RETURN_TYPE_FLOAT))
					throw std::runtime_error("Composite UAV return type does not match its output variant");
			}
		}
		std::cout << "Shader selection and material bindings, 48 effect layouts and 4 composite UAV layouts passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
