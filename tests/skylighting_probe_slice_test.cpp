#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d_resource_naming.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	constexpr uint32_t volumeWidth = 8;
	constexpr uint32_t volumeDepth = 8;
	constexpr size_t volumeElements = volumeWidth * volumeWidth * volumeDepth;

	void Check(HRESULT result)
	{
		if (FAILED(result))
			throw std::runtime_error("Skylighting shader test HRESULT " + std::to_string(result));
	}

	struct ShaderIncludes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Skylighting/Shaders" };

		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			const auto result = feature.Open(type, name, parent, data, size);
			return SUCCEEDED(result) ? result : package.Open(type, name, parent, data, size);
		}

		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	struct ConstantBuffer
	{
		ID3D11ShaderReflectionConstantBuffer* reflection;
		std::vector<std::byte> bytes;
		ComPtr<ID3D11Buffer> buffer;
		UINT slot;

		ConstantBuffer(ID3D11Device* device, ID3D11ShaderReflection* shader, const char* name) :
			reflection(shader->GetConstantBufferByName(name))
		{
			D3D11_SHADER_BUFFER_DESC reflected{};
			Check(reflection->GetDesc(&reflected));
			bytes.resize(reflected.Size);
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			Check(shader->GetResourceBindingDescByName(name, &binding));
			slot = binding.BindPoint;
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = reflected.Size;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Check(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()));
			Util::SetResourceName(buffer.Get(), "SkylightingTest::%s", name);
		}

		template <class T>
		void SetVariable(const char* name, const T& value)
		{
			D3D11_SHADER_VARIABLE_DESC variable{};
			Check(reflection->GetVariableByName(name)->GetDesc(&variable));
			if (sizeof(value) > variable.Size || variable.StartOffset + sizeof(value) > bytes.size())
				throw std::runtime_error("Reflected shader variable exceeds its constant buffer");
			std::memcpy(bytes.data() + variable.StartOffset, &value, sizeof(value));
		}

		template <class T>
		void SetMember(const char* variable, const char* member, const T& value)
		{
			auto* reflectedVariable = reflection->GetVariableByName(variable);
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			Check(reflectedVariable->GetDesc(&variableDesc));
			D3D11_SHADER_TYPE_DESC memberDesc{};
			Check(reflectedVariable->GetType()->GetMemberTypeByName(member)->GetDesc(&memberDesc));
			const size_t offset = variableDesc.StartOffset + memberDesc.Offset;
			if (offset + sizeof(value) > bytes.size())
				throw std::runtime_error("Reflected Skylighting setting exceeds its constant buffer");
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		}

		void Bind(ID3D11DeviceContext* context)
		{
			context->UpdateSubresource(buffer.Get(), 0, nullptr, bytes.data(), 0, 0);
			ID3D11Buffer* raw = buffer.Get();
			context->CSSetConstantBuffers(slot, 1, &raw);
		}
	};

	template <class T>
	struct Volume
	{
		ComPtr<ID3D11Texture3D> texture;
		ComPtr<ID3D11Texture3D> staging;
		ComPtr<ID3D11UnorderedAccessView> uav;

		Volume(ID3D11Device* device, DXGI_FORMAT format, const char* name)
		{
			D3D11_TEXTURE3D_DESC desc{};
			desc.Width = desc.Height = volumeWidth;
			desc.Depth = volumeDepth;
			desc.MipLevels = 1;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture3D(&desc, nullptr, texture.GetAddressOf()));
			Util::SetResourceName(texture.Get(), "SkylightingTest::%s", name);
			Check(device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "SkylightingTest::%s UAV", name);
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture3D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "SkylightingTest::%s Staging", name);
		}

		void Fill(ID3D11DeviceContext* context, const std::vector<T>& values)
		{
			context->UpdateSubresource(texture.Get(), 0, nullptr, values.data(),
				volumeWidth * sizeof(T), volumeWidth * volumeWidth * sizeof(T));
		}

		std::vector<T> Read(ID3D11DeviceContext* context)
		{
			std::vector<T> result(volumeElements);
			context->CopyResource(staging.Get(), texture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			for (uint32_t z = 0; z < volumeDepth; ++z) {
				for (uint32_t y = 0; y < volumeWidth; ++y) {
					const auto* row = static_cast<const std::byte*>(mapped.pData) + z * mapped.DepthPitch + y * mapped.RowPitch;
					std::memcpy(result.data() + (z * volumeWidth + y) * volumeWidth, row, volumeWidth * sizeof(T));
				}
			}
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	struct ShadowFixture
	{
		ComPtr<ID3D11Buffer> light;
		ComPtr<ID3D11ShaderResourceView> lightView;
		std::array<ComPtr<ID3D11Texture2D>, 2> depth;
		std::array<ComPtr<ID3D11ShaderResourceView>, 2> depthView;
		ComPtr<ID3D11SamplerState> sampler;

		ShadowFixture(ID3D11Device* device)
		{
			struct DirectionalShadowLight
			{
				std::array<std::array<float, 16>, 2> projection;
				std::array<std::array<float, 16>, 2> inverseProjection;
				std::array<float, 2> endSplitDistances;
				std::array<float, 2> startSplitDistances;
			} lightData{};
			// Both column-major cascade matrices project every probe inside the depth map.
			for (auto& projection : lightData.projection) {
				projection[12] = 0.5f;
				projection[13] = 0.5f;
				projection[14] = 0.5f;
				projection[15] = 1.0f;
			}
			lightData.endSplitDistances = { 2.0f, 4.0f };
			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = sizeof(lightData);
			bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bufferDesc.StructureByteStride = sizeof(lightData);
			D3D11_SUBRESOURCE_DATA initialLight{ &lightData };
			Check(device->CreateBuffer(&bufferDesc, &initialLight, light.GetAddressOf()));
			Util::SetResourceName(light.Get(), "SkylightingTest::DirectionalShadowLight");
			Check(device->CreateShaderResourceView(light.Get(), nullptr, lightView.GetAddressOf()));
			Util::SetResourceName(lightView.Get(), "SkylightingTest::DirectionalShadowLight SRV");

			for (size_t lit = 0; lit < depth.size(); ++lit) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				textureDesc.Width = textureDesc.Height = textureDesc.MipLevels = 1;
				textureDesc.ArraySize = 2;
				textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				textureDesc.SampleDesc.Count = 1;
				textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
				textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				const float depthValue = static_cast<float>(lit);
				const D3D11_SUBRESOURCE_DATA initialDepth[]{ { &depthValue, sizeof(float) }, { &depthValue, sizeof(float) } };
				Check(device->CreateTexture2D(&textureDesc, initialDepth, depth[lit].GetAddressOf()));
				Util::SetResourceName(depth[lit].Get(), "SkylightingTest::%sCascadeDepth", lit ? "Lit" : "Dark");
				D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
				viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				viewDesc.Texture2DArray.MipLevels = 1;
				viewDesc.Texture2DArray.ArraySize = 2;
				Check(device->CreateShaderResourceView(depth[lit].Get(), &viewDesc, depthView[lit].GetAddressOf()));
				Util::SetResourceName(depthView[lit].Get(), "SkylightingTest::%sCascadeDepth SRV", lit ? "Lit" : "Dark");
			}
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "SkylightingTest::ShadowComparisonSampler");
		}

		void Bind(ID3D11DeviceContext* context, bool lit)
		{
			ID3D11ShaderResourceView* views[]{ lightView.Get(), depthView[lit].Get() };
			context->CSSetShaderResources(2, 2, views);
			ID3D11SamplerState* rawSampler = sampler.Get();
			context->CSSetSamplers(0, 1, &rawSampler);
		}
	};

	struct SliceCase
	{
		const char* name;
		uint32_t start;
		uint32_t count;
		uint32_t dispatchDepth;
		bool invalid;
		bool shadowAvailable;
		bool onScreen = false;
		bool shadowLit = true;
		uint32_t frameCount = 0;
	};

	void RunPermutation(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, bool vr)
	{
		ShaderIncludes includes;
		std::vector<D3D_SHADER_MACRO> defines{ { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" } };
		if (vr)
			defines.push_back({ "VR", "" });
		defines.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> bytecode, errors;
		const auto compiled = D3DCompileFromFile(path.c_str(),
			defines.data(), &includes, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, bytecode.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
		Check(compiled);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
		Util::SetResourceName(shader.Get(), "SkylightingTest::UpdateProbesCS %s", vr ? "VR" : "Flat");
		ConstantBuffer feature(device, reflection.Get(), "SharedData::FeatureData");
		ConstantBuffer shared(device, reflection.Get(), "SharedData::SharedData");
		ConstantBuffer frame(device, reflection.Get(), "FrameBuffer::PerFrame");
		feature.SetMember("SharedData::skylightingSettings", "Enabled", 1u);
		feature.SetMember("SharedData::skylightingSettings", "ArrayDims", std::array{ volumeWidth, volumeWidth, volumeDepth });
		feature.SetMember("SharedData::skylightingSettings", "ArrayOrigin", std::array{ 1u, 2u, 3u });
		// Keep the occlusion projection outside coverage so the test needs no scene depth.
		std::array<float, 16> occlusionProjection{};
		occlusionProjection[3] = 3.0f;
		feature.SetMember("SharedData::skylightingSettings", "OcclusionViewProj", occlusionProjection);

		Volume<std::array<float, 4>> probes(device, DXGI_FORMAT_R32G32B32A32_FLOAT, "Probes");
		Volume<uint32_t> accumulation(device, DXGI_FORMAT_R32_UINT, "Accumulation");
		Volume<uint32_t> history(device, DXGI_FORMAT_R32_UINT, "ShadowHistory");
		Volume<float> visibility(device, DXGI_FORMAT_R32_FLOAT, "ShadowVisibility");
		ShadowFixture shadow(device);
		std::vector<uint32_t> initialHistory(volumeElements);
		std::vector<float> initialVisibility(volumeElements);
		for (size_t i = 0; i < volumeElements; ++i) {
			initialHistory[i] = 0xAAAA0000u | (static_cast<uint32_t>(i) << 1);
			initialVisibility[i] = -1.0f - static_cast<float>(i);
		}
		const SliceCase cases[]{
			{ "full volume", 0, volumeDepth, volumeDepth, false, false },
			{ "offset history and slice-count guard", 3, 2, 3, false, false },
			{ "tail slice and volume-depth guard", 7, 3, 3, false, false },
			{ "invalid probe without shadow data", 3, 2, 2, true, false },
			{ "invalid offscreen probe reset", 3, 2, 2, true, true },
			{ "valid offscreen history retained", 3, 2, 2, false, true },
			{ "lit cascade updates offset history", 3, 2, 2, false, true, true, true, 10 },
			{ "dark cascade updates offset history", 3, 2, 2, false, true, true, false, 17 }
		};
		for (const auto& test : cases) {
			context->ClearState();
			probes.Fill(context, std::vector<std::array<float, 4>>(volumeElements));
			accumulation.Fill(context, std::vector<uint32_t>(volumeElements));
			history.Fill(context, initialHistory);
			visibility.Fill(context, initialVisibility);
			feature.SetMember("SharedData::skylightingSettings", "ProbeUpdateSliceStart", test.start);
			feature.SetMember("SharedData::skylightingSettings", "ProbeUpdateSliceCount", test.count);
			feature.SetMember("SharedData::skylightingSettings", "ShadowDataAvailable", static_cast<uint32_t>(test.shadowAvailable));
			feature.SetMember("SharedData::skylightingSettings", "ValidMargin", std::array{ 0, 0, test.invalid ? static_cast<int>(volumeDepth) : 0, 0 });
			feature.Bind(context);
			shared.SetVariable("SharedData::FrameCountAlwaysActive", test.frameCount);
			shared.SetVariable("SharedData::CameraData", std::array{ 1.0f, 0.0f, 0.0f, 1.0f });
			shared.Bind(context);
			// Row-major projection fixes NDC depth at 0.5 and linear depth at 1.
			std::array<float, 16> cameraProjection{};
			cameraProjection[11] = 0.5f;
			cameraProjection[15] = test.onScreen ? 1.0f : 0.0f;
			frame.SetVariable("FrameBuffer::CameraViewProj", cameraProjection);
			frame.Bind(context);
			if (test.onScreen)
				shadow.Bind(context, test.shadowLit);
			ID3D11UnorderedAccessView* views[]{ probes.uav.Get(), accumulation.uav.Get(), history.uav.Get(), visibility.uav.Get() };
			context->CSSetUnorderedAccessViews(0, 4, views, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, test.dispatchDepth);
			context->ClearState();
			const auto actualHistory = history.Read(context);
			const auto actualVisibility = visibility.Read(context);
			for (size_t i = 0; i < volumeElements; ++i) {
				const auto z = static_cast<uint32_t>(i / (volumeWidth * volumeWidth));
				const bool selected = z >= test.start && z - test.start < test.count;
				uint32_t expectedHistory = initialHistory[i];
				float expectedVisibility = initialVisibility[i];
				if (selected && (test.invalid || !test.shadowAvailable || test.onScreen)) {
					expectedHistory = test.invalid ? UINT32_MAX : initialHistory[i];
					if (!test.shadowAvailable || test.onScreen) {
						const uint32_t historyBit = 1u << (test.frameCount % 32);
						expectedHistory &= ~historyBit;
						if (!test.onScreen || test.shadowLit)
							expectedHistory |= historyBit;
					}
					expectedVisibility = static_cast<float>(std::popcount(expectedHistory)) / 32.0f;
				}
				if (actualHistory[i] != expectedHistory || actualVisibility[i] != expectedVisibility)
					throw std::runtime_error(std::string(vr ? "VR " : "SE/AE ") + test.name +
											 " changed the wrong probe history or visibility at element " + std::to_string(i));
			}
			std::cout << (vr ? "VR " : "SE/AE ") << test.name << " passed\n";
		}
	}
}

int main(int argc, char** argv)
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		const std::filesystem::path path = argc > 1 ? argv[1] : "features/Skylighting/Shaders/Skylighting/UpdateProbesCS.hlsl";
		RunPermutation(device.Get(), context.Get(), path, false);
		RunPermutation(device.Get(), context.Get(), path, true);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
