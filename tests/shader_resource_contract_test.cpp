#define NOMINMAX
#include "Features/Upscaling/CompositeOutputFormat.h"
#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <array>
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
				void SetPSTexture(unsigned slot, Texture* texture)
				{
					Require(slot == 4);
					bound = texture;
					++binds;
				}
				void SetPSTextureFilterMode(unsigned slot, TextureFilterMode mode)
				{
					Require(slot == 4);
					filter = mode;
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
#include "lighting_environment_binding.h"

	void TestBindings()
	{
		using namespace RE;
		using namespace RE::BSGraphics;
		Texture valid{ reinterpret_cast<void*>(1) }, unresolved;
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

	ComPtr<ID3D11ShaderReflection> Reflect(const wchar_t* path, const char* profile, std::vector<D3D_SHADER_MACRO> defines)
	{
		PackageIncludes includes("package/Shaders", "features/Upscaling/Shaders");
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, defines.data(), &includes, "main", profile,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		return reflection;
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
}

int main()
{
	try {
		BindingFixture::TestSelection();
		BindingFixture::TestBindings();
		TestPreviousOutputBinding();
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
							if (bound != (grayscale != 0) || (bound && binding.BindPoint != 4))
								throw std::runtime_error("Effect grayscale binding does not match its material permutation");
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
