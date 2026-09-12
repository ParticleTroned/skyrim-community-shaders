#define NOMINMAX
#include "Utils/ShaderInclude.h"
#include "d3d_resource_naming.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
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
		ComPtr<ID3D11Texture2D> occlusionDepth;
		ComPtr<ID3D11ShaderResourceView> occlusionView;
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
			D3D11_TEXTURE2D_DESC occlusionDesc{};
			occlusionDesc.Width = occlusionDesc.Height = occlusionDesc.MipLevels = occlusionDesc.ArraySize = 1;
			occlusionDesc.Format = DXGI_FORMAT_R32_FLOAT;
			occlusionDesc.SampleDesc.Count = 1;
			occlusionDesc.Usage = D3D11_USAGE_IMMUTABLE;
			occlusionDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const float occlusionValue = 1.0f;
			D3D11_SUBRESOURCE_DATA initialOcclusion{ &occlusionValue, sizeof(float) };
			Check(device->CreateTexture2D(&occlusionDesc, &initialOcclusion, occlusionDepth.GetAddressOf()));
			Util::SetResourceName(occlusionDepth.Get(), "SkylightingTest::OcclusionDepth");
			Check(device->CreateShaderResourceView(occlusionDepth.Get(), nullptr, occlusionView.GetAddressOf()));
			Util::SetResourceName(occlusionView.Get(), "SkylightingTest::OcclusionDepth SRV");
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			Check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
			Util::SetResourceName(sampler.Get(), "SkylightingTest::ShadowComparisonSampler");
		}

		void Bind(ID3D11DeviceContext* context, bool lit, bool directional)
		{
			ID3D11ShaderResourceView* occlusion = occlusionView.Get();
			context->CSSetShaderResources(0, 1, &occlusion);
			if (directional) {
				ID3D11ShaderResourceView* views[]{ lightView.Get(), depthView[lit].Get() };
				context->CSSetShaderResources(2, 2, views);
			}
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
		bool occlusionCovered = false;
		bool rejectFirstJitter = false;
	};

	using ProbeLighting = std::array<float, 4>;

	struct ProbeState
	{
		std::vector<ProbeLighting> lighting{ volumeElements, { 4.0f, 0.0f, 0.0f, 0.0f } };
		std::vector<uint16_t> accumulation = std::vector<uint16_t>(volumeElements);
		std::vector<uint32_t> history = std::vector<uint32_t>(volumeElements);
		std::vector<float> visibility = std::vector<float>(volumeElements);

		ProbeState()
		{
			for (size_t i = 0; i < volumeElements; ++i) {
				accumulation[i] = static_cast<uint16_t>(((i % 32) << 8) | (17 + i % 200));
				history[i] = 0xAAAA0000u | (static_cast<uint32_t>(i) << 1);
				visibility[i] = -1.0f - static_cast<float>(i);
			}
		}
	};

	bool IsSelected(size_t element, const SliceCase& test)
	{
		const auto z = static_cast<uint32_t>(element / (volumeWidth * volumeWidth));
		return z >= test.start && z - test.start < test.count;
	}

	struct ProbeFixture
	{
		ID3D11DeviceContext* context;
		std::string runtime;
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Volume<ProbeLighting> probes;
		Volume<uint16_t> accumulation;
		Volume<uint32_t> history;
		Volume<float> visibility;
		ShadowFixture shadow;
		std::unique_ptr<ConstantBuffer> feature, shared, frame;

		ProbeFixture(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, bool vr) :
			context(context), runtime(vr ? "VR " : "SE/AE "),
			probes(device, DXGI_FORMAT_R32G32B32A32_FLOAT, "Probes"),
			accumulation(device, DXGI_FORMAT_R16_UINT, "Accumulation"),
			history(device, DXGI_FORMAT_R32_UINT, "ShadowHistory"),
			visibility(device, DXGI_FORMAT_R32_FLOAT, "ShadowVisibility"), shadow(device)
		{
			ShaderIncludes includes;
			std::vector<D3D_SHADER_MACRO> defines{ { "COMPUTESHADER", "" }, { "WINPC", "" }, { "DX11", "" } };
			if (vr)
				defines.push_back({ "VR", "" });
			defines.push_back({ nullptr, nullptr });
			ComPtr<ID3DBlob> bytecode, errors;
			const auto compiled = D3DCompileFromFile(path.c_str(), defines.data(), &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bytecode.GetAddressOf(), errors.GetAddressOf());
			if (errors)
				std::cerr << static_cast<const char*>(errors->GetBufferPointer());
			Check(compiled);
			Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "SkylightingTest::UpdateProbesCS %s", vr ? "VR" : "Flat");
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			shared = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::SharedData");
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			Set("Enabled", 1u);
			Set("ArrayDims", std::array{ volumeWidth, volumeWidth, volumeDepth });
			Set("ArrayOrigin", std::array{ 1u, 2u, 3u });
			Set("ProbeFieldSize", 10240.0f);
			Set("OcclusionSHBasis4Pi", ProbeLighting{ 8.0f, 0.0f, 0.0f, 0.0f });
		}

		template <class T>
		void Set(const char* member, const T& value)
		{
			feature->SetMember("SharedData::skylightingSettings", member, value);
		}

		void Reset(const ProbeState& state)
		{
			context->ClearState();
			probes.Fill(context, state.lighting);
			accumulation.Fill(context, state.accumulation);
			history.Fill(context, state.history);
			visibility.Fill(context, state.visibility);
		}

		void Dispatch(const SliceCase& test)
		{
			Set("ProbeUpdateSliceStart", test.start);
			Set("ProbeUpdateSliceCount", test.count);
			Set("ShadowDataAvailable", static_cast<uint32_t>(test.shadowAvailable));
			Set("ValidMargin", std::array{ 0, 0, test.invalid ? static_cast<int>(volumeDepth) : 0, 0 });
			// Align physical probe x=0 with camera-relative x=0 for the jitter rejection case.
			Set("PosOffset", std::array{ test.rejectFirstJitter ? -4480.0f : 0.0f, 0.0f, 0.0f });
			std::array<float, 16> occlusionProjection{};
			occlusionProjection[3] = test.occlusionCovered ? 0.0f : 3.0f;
			occlusionProjection[11] = 0.5f;
			Set("OcclusionViewProj", occlusionProjection);
			feature->Bind(context);
			shared->SetVariable("SharedData::FrameCountAlwaysActive", test.frameCount);
			shared->SetVariable("SharedData::CameraData", std::array{ 1.0f, 0.0f, 0.0f, 1.0f });
			shared->Bind(context);
			std::array<float, 16> cameraProjection{};
			cameraProjection[11] = test.rejectFirstJitter ? 0.9f : 0.5f;
			cameraProjection[8] = test.rejectFirstJitter ? 1.0f / 128.0f : 0.0f;
			cameraProjection[15] = test.onScreen ? 1.0f : 0.0f;
			frame->SetVariable("FrameBuffer::CameraViewProj", cameraProjection);
			frame->Bind(context);
			shadow.Bind(context, test.shadowLit, test.onScreen);
			ID3D11UnorderedAccessView* views[]{ probes.uav.Get(), accumulation.uav.Get(), history.uav.Get(), visibility.uav.Get() };
			context->CSSetUnorderedAccessViews(0, 4, views, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, test.dispatchDepth);
			context->ClearState();
		}

		void Verify(const ProbeState& expected, const std::string& name)
		{
			const auto actualLighting = probes.Read(context);
			const auto actualAccumulation = accumulation.Read(context);
			const auto actualHistory = history.Read(context);
			const auto actualVisibility = visibility.Read(context);
			for (size_t i = 0; i < volumeElements; ++i) {
				const auto fail = [&](const char* field) {
					throw std::runtime_error(runtime + name + " incorrect " + field + " at element " + std::to_string(i));
				};
				if (actualHistory[i] != expected.history[i])
					fail("shadow history");
				if (actualVisibility[i] != expected.visibility[i])
					fail("shadow visibility");
				if (actualAccumulation[i] != expected.accumulation[i])
					fail("packed probe state");
				for (size_t component = 0; component < 4; ++component) {
					if (!std::isfinite(actualLighting[i][component]) ||
						std::abs(actualLighting[i][component] - expected.lighting[i][component]) > 0.00001f)
						fail("SH lighting");
				}
			}
		}
	};

	void CheckSlices(ProbeFixture& fixture)
	{
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
			ProbeState expected;
			fixture.Reset(expected);
			fixture.Dispatch(test);
			for (size_t i = 0; i < volumeElements; ++i) {
				if (!IsSelected(i, test))
					continue;
				if (test.invalid) {
					expected.history[i] = UINT32_MAX;
					expected.visibility[i] = 1.0f;
					expected.accumulation[i] = 0;
					expected.lighting[i] = { std::sqrt(4.0f * std::numbers::pi_v<float>), 0.0f, 0.0f, 0.0f };
				}
				if (!test.shadowAvailable || test.onScreen) {
					expected.history[i] = (expected.history[i] << 1) | (!test.onScreen || test.shadowLit ? 1u : 0u);
					expected.visibility[i] = static_cast<float>(std::popcount(expected.history[i])) / 32.0f;
				}
				if (test.onScreen) {
					const auto cursor = ((expected.accumulation[i] >> 8) + 1u) % 32;
					expected.accumulation[i] = static_cast<uint16_t>((cursor << 8) | (expected.accumulation[i] & 255u));
				}
			}
			fixture.Verify(expected, test.name);
			std::cout << fixture.runtime << test.name << " passed\n";
		}
	}

	void CheckTemporalConvergence(ProbeFixture& fixture)
	{
		struct Schedule
		{
			const char* name;
			std::array<uint32_t, 4> firstFrames;
			uint32_t period;
		};
		// Replay the second stationary batch's accepted-update frames, including corner reuse.
		const Schedule schedules[]{
			{ "Performance", { 25, 32, 40, 48 }, 192 },
			{ "Hoshipa", { 10, 12, 15, 18 }, 72 },
			{ "32-frame spacing", { 0, 32, 64, 96 }, 128 }
		};
		for (const auto& schedule : schedules) {
			SliceCase test{ schedule.name, 3, 2, 2, false, true, true, false };
			ProbeState expected;
			const auto initial = expected;
			for (size_t i = 0; i < volumeElements; ++i) {
				if (IsSelected(i, test)) {
					expected.history[i] = UINT32_MAX;
					expected.visibility[i] = 1.0f;
				}
			}
			fixture.Reset(expected);
			for (uint32_t update = 0; update < 64; ++update) {
				test.frameCount = schedule.firstFrames[update % 4] + schedule.period * (update / 4);
				test.shadowLit = update >= 32;
				fixture.Dispatch(test);
				const uint32_t phaseUpdates = update % 32 + 1;
				const uint32_t mask = phaseUpdates == 32 ? (test.shadowLit ? UINT32_MAX : 0u) :
				                      test.shadowLit     ? ((1u << phaseUpdates) - 1u) :
				                                           (UINT32_MAX << phaseUpdates);
				for (size_t i = 0; i < volumeElements; ++i) {
					if (!IsSelected(i, test))
						continue;
					expected.history[i] = mask;
					expected.visibility[i] = test.shadowLit ? phaseUpdates / 32.0f : 1.0f - phaseUpdates / 32.0f;
					const uint32_t cursor = ((initial.accumulation[i] >> 8) + update + 1) % 32;
					expected.accumulation[i] = static_cast<uint16_t>((cursor << 8) | (initial.accumulation[i] & 255u));
				}
				fixture.Verify(expected, std::string(schedule.name) + " accepted update " + std::to_string(update + 1));
			}
			std::cout << fixture.runtime << schedule.name << " 32 dark then 32 lit observations passed\n";
		}
	}

	void CheckRejectedJitterRecovery(ProbeFixture& fixture)
	{
		SliceCase test{ "independent cursors and rejected jitter recovery", 3, 2, 2, false, true, true, false, 0, false, true };
		ProbeState expected;
		for (size_t i = 0; i < volumeElements; ++i) {
			if (IsSelected(i, test))
				expected.accumulation[i] = static_cast<uint16_t>((((i / volumeWidth) % 2) << 8) | 19u);
		}
		fixture.Reset(expected);
		for (uint32_t attempt = 0; attempt < 2; ++attempt) {
			test.frameCount = 32 * attempt;
			fixture.Dispatch(test);
			for (size_t i = 0; i < volumeElements; ++i) {
				if (!IsSelected(i, test))
					continue;
				const uint32_t startCursor = static_cast<uint32_t>((i / volumeWidth) % 2);
				// At x=0, jitter 0 and 2 leave depth coverage; jitter 1 remains inside.
				if (i % volumeWidth == 0 && startCursor + attempt == 1) {
					expected.history[i] <<= 1;
					expected.visibility[i] = static_cast<float>(std::popcount(expected.history[i])) / 32.0f;
				}
				expected.accumulation[i] = static_cast<uint16_t>(((startCursor + attempt + 1) << 8) | 19u);
			}
			fixture.Verify(expected, std::string(test.name) + " attempt " + std::to_string(attempt + 1));
		}
		std::cout << fixture.runtime << test.name << " passed\n";
	}

	void CheckAccumulationIndependence(ProbeFixture& fixture)
	{
		SliceCase test{ "SH count, saturation and shadow cursor independence", 3, 2, 2, false, true, true, true, 17, true };
		ProbeState expected;
		for (size_t i = 0; i < volumeElements; ++i)
			expected.accumulation[i] = static_cast<uint16_t>(((i % 32) << 8) | (254 + i % 2));
		fixture.Reset(expected);
		for (uint32_t update = 0; update < 2; ++update) {
			fixture.Dispatch(test);
			for (size_t i = 0; i < volumeElements; ++i) {
				if (!IsSelected(i, test))
					continue;
				const uint32_t blendCount = (expected.accumulation[i] & 255u) + 1;
				expected.lighting[i][0] += (8.0f - expected.lighting[i][0]) / static_cast<float>(blendCount);
				const uint32_t cursor = ((expected.accumulation[i] >> 8) + 1) % 32;
				expected.accumulation[i] = static_cast<uint16_t>((cursor << 8) | 255u);
				expected.history[i] = (expected.history[i] << 1) | 1u;
				expected.visibility[i] = static_cast<float>(std::popcount(expected.history[i])) / 32.0f;
			}
			fixture.Verify(expected, std::string(test.name) + " update " + std::to_string(update + 1));
		}
		// A new covered probe starts both accumulators independently of its discarded packed state.
		test.invalid = true;
		fixture.Dispatch(test);
		for (size_t i = 0; i < volumeElements; ++i) {
			if (!IsSelected(i, test))
				continue;
			const float unoccluded = std::sqrt(4.0f * std::numbers::pi_v<float>);
			expected.lighting[i][0] = unoccluded + (8.0f - unoccluded) / 15.0f;
			expected.accumulation[i] = 0x0101u;
			expected.history[i] = UINT32_MAX;
			expected.visibility[i] = 1.0f;
		}
		fixture.Verify(expected, "invalid covered probe resets SH count and shadow cursor");
		std::cout << fixture.runtime << test.name << " and invalid reset passed\n";
	}

	void RunPermutation(ID3D11Device* device, ID3D11DeviceContext* context, const std::filesystem::path& path, bool vr)
	{
		ProbeFixture fixture(device, context, path, vr);
		CheckSlices(fixture);
		CheckTemporalConvergence(fixture);
		CheckRejectedJitterRecovery(fixture);
		CheckAccumulationIndependence(fixture);
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
