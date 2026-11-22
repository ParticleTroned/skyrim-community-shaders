#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Features/LightLimitFix/PreparedPointLightColours.h"
#include "ShaderPackageIncludes.h"
#include "adaptive_balance_test_settings.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	using D3D11ShaderTest::Stage;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Pixel = std::array<float, 4>;
	constexpr unsigned capacity = 1024;
	std::uint64_t comparisons = 0, unequal = 0;
	float maximumRelativeError = 0;

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	ComPtr<ID3DBlob> Compile(const wchar_t* path, bool vr, bool pbr, bool reference, bool compute, bool baseline = false)
	{
		std::cerr << "Compiling " << std::filesystem::path(path).string() << " vr=" << vr << " pbr=" << pbr << " reference=" << reference << "\n";
		PackageIncludes includes("package/Shaders", "features/Light Limit Fix/Shaders", {}, "tests");
		std::vector<D3D_SHADER_MACRO> macros{ { compute ? "COMPUTESHADER" : "PSHADER", "1" } };
		if (vr)
			macros.push_back({ "VR", "1" });
		if (pbr)
			macros.push_back({ "TRUE_PBR", "1" });
		if (reference)
			macros.push_back({ "CSX_PERF_REFERENCE_FLAGS", "16" });
		if (baseline)
			macros.push_back({ "POINT_LIGHT_REFERENCE_TEST", "1" });
		macros.push_back({ nullptr, nullptr });
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(path, macros.data(), &includes, "main", compute ? "cs_5_0" : "ps_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | (compute ? 0 : D3DCOMPILE_WARNINGS_ARE_ERRORS) | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.GetAddressOf(), errors.GetAddressOf());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Structured
	{
		ComPtr<ID3D11Buffer> buffer;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
		Structured(ID3D11Device* device, unsigned stride, unsigned count, const void* initial = nullptr)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = stride * count;
			desc.StructureByteStride = stride;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			D3D11_SUBRESOURCE_DATA data{ initial };
			Check(device->CreateBuffer(&desc, initial ? &data : nullptr, buffer.GetAddressOf()));
			Util::SetResourceName(buffer.Get(), "PreparedColourTest::Buffer");
			Check(device->CreateShaderResourceView(buffer.Get(), nullptr, srv.GetAddressOf()));
			Util::SetResourceName(srv.Get(), "PreparedColourTest::Buffer SRV");
			Check(device->CreateUnorderedAccessView(buffer.Get(), nullptr, uav.GetAddressOf()));
			Util::SetResourceName(uav.Get(), "PreparedColourTest::Buffer UAV");
		}
	};

	struct Light
	{
		std::array<float, 3> colour;
		float fade;
		float radius = 100, invRadius = 0.01f, fadeZone = 1, sizeBias = 0;
		Pixel positions[2]{};
		std::array<unsigned, 4> rooms{};
		unsigned flags = 0, shadow = 0, spotlight = 0;
		float falloff = 0;
	};
	static_assert(sizeof(Light) == 96);

	struct Fixture
	{
		ID3D11Device* device;
		ID3D11DeviceContext* context;
		ComPtr<ID3D11ComputeShader> producer;
		ComPtr<ID3D11ShaderReflection> producerReflection, consumerReflection;
		std::array<std::array<ComPtr<ID3D11PixelShader>, 2>, 2> consumers;
		ComPtr<ID3D11VertexShader> vertex;
		std::unique_ptr<ConstantBuffer> feature, culling, frame, permutation, samples;
		std::unique_ptr<Structured> colours;
		ComPtr<ID3D11Buffer> colourReadback;
		Structured lights, clusters, indices, grid, counter, contactIndices, contactGrid;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11RenderTargetView> rtv;
		std::array<Light, capacity> lightData{};

		Fixture(ID3D11Device* a_device, ID3D11DeviceContext* a_context, bool vr, bool reference) :
			device(a_device), context(a_context), lights(device, sizeof(Light), capacity),
			clusters(device, 32, 17), indices(device, 4, 256 * 17), grid(device, 16, 17),
			counter(device, 4, 1), contactIndices(device, 4, 8 * 17), contactGrid(device, 16, 17)
		{
			RecreateColours();
			D3D11_BUFFER_DESC readback{};
			readback.ByteWidth = 32 * capacity;
			readback.Usage = D3D11_USAGE_STAGING;
			readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&readback, nullptr, colourReadback.GetAddressOf()));
			Util::SetResourceName(colourReadback.Get(), "PreparedColourTest::ColourReadback");
			auto code = Compile(L"features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl", vr, false, reference, true);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, producer.GetAddressOf()));
			Util::SetResourceName(producer.Get(), "PreparedColourTest::ClusterCull");
			ComPtr<ID3D11ShaderReflection> reflection;
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			producerReflection = reflection;
			D3D11_SHADER_DESC shaderDesc{};
			Check(reflection->GetDesc(&shaderDesc));
			std::cout << "ClusterCull " << (vr ? "VR" : "SE/AE") << " reference=" << reference
					  << " instructions=" << shaderDesc.InstructionCount << " temps=" << shaderDesc.TempRegisterCount << '\n';
			feature = std::make_unique<ConstantBuffer>(device, reflection.Get(), "SharedData::FeatureData");
			culling = std::make_unique<ConstantBuffer>(device, reflection.Get(), "PerFrame");
			frame = std::make_unique<ConstantBuffer>(device, reflection.Get(), "FrameBuffer::PerFrame");
			Require(feature->slot == 6 && culling->slot == 0, "Producer constant bindings changed");
			for (unsigned variant = 0; variant < 4; ++variant) {
				const unsigned pbr = variant % 2, baseline = variant / 2;
				code = Compile(L"tests/prepared_point_light_colour.hlsl", vr, pbr != 0, reference, false, baseline != 0);
				Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, consumers[pbr][baseline].GetAddressOf()));
				Util::SetResourceName(consumers[pbr][baseline].Get(), "PreparedColourTest::Consumer");
				reflection.Reset();
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
				consumerReflection = reflection;
				permutation = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Permutation::PerShader");
				samples = std::make_unique<ConstantBuffer>(device, reflection.Get(), "Samples");
			}
			constexpr char source[] = "float4 main(uint id:SV_VertexID):SV_Position { return float4(id==2?3:-1,id==1?3:-1,0,1); }";
			Check(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, code.ReleaseAndGetAddressOf(), nullptr));
			Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex.GetAddressOf()));
			Util::SetResourceName(vertex.Get(), "PreparedColourTest::Fullscreen");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = capacity;
			desc.Height = 2;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			Check(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Util::SetResourceName(output.Get(), "PreparedColourTest::Output");
			Check(device->CreateRenderTargetView(output.Get(), nullptr, rtv.GetAddressOf()));
			Util::SetResourceName(rtv.Get(), "PreparedColourTest::Output RTV");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "PreparedColourTest::Readback");
		}

		void RecreateColours() { colours = std::make_unique<Structured>(device, 32, capacity); }

		void PoisonColours()
		{
			ID3D11ShaderResourceView* nullView = nullptr;
			context->PSSetShaderResources(41, 1, &nullView);
			const UINT poison[4]{ 0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000 };
			context->ClearUnorderedAccessViewUint(colours->uav.Get(), poison);
		}

		void Prepare(unsigned count, bool enabled, unsigned gridWidth)
		{
			ID3D11ShaderResourceView* nullView = nullptr;
			context->PSSetShaderResources(41, 1, &nullView);
			PoisonColours();
			context->UpdateSubresource(lights.buffer.Get(), 0, nullptr, lightData.data(), 0, 0);
			culling->SetVariable("LightCount", count);
			culling->SetVariable("PreparePointLightColours", unsigned(enabled));
			culling->SetVariable("ClusterSize", std::array<unsigned, 4>{ gridWidth, 1, 1, 0 });
			culling->Bind(context);
			feature->Bind(context);
			frame->Bind(context);
			const UINT zero[4]{};
			context->ClearUnorderedAccessViewUint(counter.uav.Get(), zero);
			context->ClearUnorderedAccessViewUint(clusters.uav.Get(), zero);
			ID3D11ShaderResourceView* srvs[] = { clusters.srv.Get(), lights.srv.Get() };
			context->CSSetShaderResources(0, 2, srvs);

			ID3D11UnorderedAccessView* uavs[] = { enabled ? colours->uav.Get() : nullptr, indices.uav.Get(), grid.uav.Get(), counter.uav.Get(), contactIndices.uav.Get(), contactGrid.uav.Get() };
			context->CSSetUnorderedAccessViews(0, 6, uavs, nullptr);
			context->CSSetShader(producer.Get(), nullptr, 0);
			context->Dispatch((gridWidth + 15) / 16, 1, 1);
			std::fill(std::begin(uavs), std::end(uavs), nullptr);
			context->CSSetUnorderedAccessViews(0, 6, uavs, nullptr);
			std::fill(std::begin(srvs), std::end(srvs), nullptr);
			context->CSSetShaderResources(0, 2, srvs);
			context->CopyResource(colourReadback.Get(), colours->buffer.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(colourReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const auto* records = static_cast<const std::array<unsigned, 8>*>(mapped.pData);
			bool valid = true;
			for (unsigned i = 0; i < capacity; ++i) {
				if (enabled && i < count)
					valid &= records[i][3] == 0 && records[i][7] == 0;
				else
					valid &= std::all_of(records[i].begin(), records[i].end(), [](unsigned value) { return value == 0x7fc00000; });
			}
			context->Unmap(colourReadback.Get(), 0);
			Require(valid, "Producer omitted a valid light or wrote outside the upload");
		}

		void Compare(unsigned count, bool pbr, unsigned scene, bool ready, bool strict = false, bool hdr = false)
		{
			feature->SetMember("SharedData::lightLimitFixSettings", "ClusteredLightFlags", ready ? 3u : 1u);
			feature->Bind(context, Stage::Pixel);
			permutation->SetVariable("Permutation::ExtraShaderDescriptor", scene);
			permutation->Bind(context, Stage::Pixel);
			samples->SetVariable("strictLight", unsigned(strict));
			samples->SetVariable("preserveHDR", unsigned(hdr));
			samples->SetVariable("attenuation", 0.731f);
			samples->Bind(context, Stage::Pixel);
			ID3D11ShaderResourceView* views[] = { lights.srv.Get(), ready ? colours->srv.Get() : nullptr };
			context->PSSetShaderResources(35, 1, &views[0]);
			context->PSSetShaderResources(41, 1, &views[1]);
			ID3D11RenderTargetView* target = rtv.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{ 0, 0, float(capacity), 1, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(consumers[pbr][0].Get(), nullptr, 0);
			context->Draw(3, 0);
			viewport.TopLeftY = 1;
			context->RSSetViewports(1, &viewport);
			context->PSSetShader(consumers[pbr][1].Get(), nullptr, 0);
			context->Draw(3, 0);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->CopyResource(staging.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const auto* actual = static_cast<const Pixel*>(mapped.pData);
			const auto* expected = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + mapped.RowPitch);
			bool passed = true;
			bool hasReferenceColour = false;
			for (unsigned i = 0; i < count; ++i) {
				for (unsigned channel = 0; channel < 3; ++channel) {
					const float a = actual[i][channel], b = expected[i][channel];
					hasReferenceColour |= b != 0.0f;
					const float error = std::abs(a - b) / std::max(1e-30f, std::abs(b));
					maximumRelativeError = std::max(maximumRelativeError, error);
					++comparisons;
					unequal += std::bit_cast<unsigned>(a) != std::bit_cast<unsigned>(b);
					if (!std::isfinite(a) || !std::isfinite(b) || std::bit_cast<unsigned>(a) != std::bit_cast<unsigned>(b)) {
						if (passed)
							std::cerr << "Mismatch index=" << i << " channel=" << channel << " actual=" << a << " reference=" << b << " pbr=" << pbr << " scene=" << scene << " ready=" << ready << '\n';
						passed = false;
					}
				}
			}
			context->Unmap(staging.Get(), 0);
			Require(count <= 1 || hasReferenceColour, "Reference draw produced no light");
			Require(passed, "Prepared colour differs from the frozen inline baseline");
		}
	};

	void CheckValidity()
	{
		using namespace LightLimitFixDetail;
		Require(!PointLightColourInputs::HasNonlinearTransform(false, 1.8f, 1, 1, false), "Disabled LL enabled preparation");
		Require(!PointLightColourInputs::HasNonlinearTransform(true, 1, 1, 1, false), "Unit LL gamma enabled preparation");
		Require(PointLightColourInputs::HasNonlinearTransform(true, 1.8f, 1, 1, false), "Active LL gamma skipped preparation");
		Require(PointLightColourInputs::HasNonlinearTransform(false, 1, 1.2f, 1, false), "LL-off curve skipped preparation");
		Require(PointLightColourInputs::HasNonlinearTransform(false, 1, 1, 0.7f, false), "LL-off saturation skipped preparation");
		Require(PointLightColourInputs::HasNonlinearTransform(false, 1, 1, 1, true), "LL-off utility controls skipped preparation");
		PreparedPointLightColours state;
		PointLightColourInputs inputs;
		for (std::size_t i = PointLightColourInputs::UtilityValueOffset; i < inputs.values.size(); i += 2)
			inputs.values[i] = 1.0f;
		inputs.utilityEnabled = true;
		Require(!inputs.HasUtilityTransform(), "Enabled neutral utility requested preparation");
		for (std::size_t i = PointLightColourInputs::UtilityValueOffset; i < inputs.values.size(); ++i) {
			const float neutral = inputs.values[i];
			inputs.values[i] = 0.5f;
			Require(inputs.HasUtilityTransform(), "Active utility control skipped preparation");
			inputs.utilityEnabled = false;
			Require(!inputs.HasUtilityTransform(), "Disabled utility requested preparation");
			inputs.utilityEnabled = true;
			inputs.values[i] = neutral;
		}
		inputs.utilityEnabled = false;
		Require(!state.CanPrepare() && state.Flags(true) == 1, "Unknown inputs admitted preparation");
		state.PublishInputs(inputs);
		state.Complete(true);
		Require(!state.CanPrepare() && !state.IsReady(), "Neutral inputs admitted preparation");
		inputs.transformEnabled = true;
		state.PublishInputs(inputs);
		state.Complete(true);
		state.PublishInputs(inputs);
		Require(state.IsReady() && state.Flags(true) == 3 && state.Flags(false) == 0, "Readiness publication invalidated itself");
		for (auto& value : inputs.values) {
			value = 2;
			state.PublishInputs(inputs);
			Require(!state.IsReady(), "Colour input change retained stale colours");
			state.Complete(true);
		}
		inputs.linearLighting = true;
		state.PublishInputs(inputs);
		Require(!state.IsReady(), "LL change retained stale colours");
		state.Complete(true);
		inputs.utilityEnabled = true;
		state.PublishInputs(inputs);
		Require(!state.IsReady(), "Utility enable retained stale colours");
		state.Complete(true);
		state.Invalidate();
		Require(!state.IsReady(), "Resource or light upload invalidation failed");
		state.Complete(false);
		Require(!state.IsReady(), "Failed producer admitted colours");
		inputs.values[0] = 0.0f;
		state.PublishInputs(inputs);
		state.Complete(true);
		inputs.values[0] = -0.0f;
		state.PublishInputs(inputs);
		Require(!state.IsReady(), "Signed zero change retained stale colours");
	}
}

int main()
{
	try {
		CheckValidity();
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		constexpr std::array colours{
			Pixel{ 0, 0, 0, 0 }, Pixel{ -0.1f, 0.2f, 0.3f, 0 }, Pixel{ 0.4f, 0.2f, 0.1f, 0 },
			Pixel{ 100, 500, 1000, 0 }, Pixel{ 1e-8f, 1e-8f, 1e-8f, 0 }, Pixel{ 65000, 65504, 66000, 0 },
			Pixel{ 4.2e13f, 4.3287612227584e13f, 4.4e13f, 0 }, Pixel{ -2, -1, -0.5f, 0 }
		};
		constexpr std::array<unsigned, 5> types{ 0, 1u << 13, 1u << 14, (1u << 13) | (1u << 14), 1u << 31 };
		for (bool vr : { false, true }) {
			for (bool reference : { false, true }) {
				Fixture fixture(device.Get(), context.Get(), vr, reference);
				for (unsigned i = 0; i < capacity; ++i) {
					std::copy_n(colours[i % colours.size()].begin(), 3, fixture.lightData[i].colour.begin());
					fixture.lightData[i].fade = i % 3 == 0 ? 0 : 0.83f;
					fixture.lightData[i].flags = types[(i / 8) % types.size()] | ((i / 40) % 2 ? 1u << 11 : 0);
				}
				for (bool ll : { false, true }) {
					for (unsigned controls = 0; controls < 5; ++controls) {
						fixture.feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(ll));
						fixture.feature->SetMember("SharedData::linearLightingSettings", "lightGamma", 1.8f);
						for (const auto& [name, value] : { std::pair{ "pointLightMult", 0.87f }, std::pair{ "linearPointLightMult", 0.71f },
								 std::pair{ "spotlightMult", 0.62f }, std::pair{ "linearSpotlightMult", 0.91f },
								 std::pair{ "omnidirectionalBulbMult", 0.55f }, std::pair{ "linearOmnidirectionalBulbMult", 1.1f } })
							fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", name, controls == 0 ? 1.0f : value);
						fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", controls == 1 || controls == 4 ? 1.2f : 1.0f);
						fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", controls == 2 || controls == 4 ? 0.7f : 1.0f);
						auto appearance = AdaptiveBalanceTest::NeutralAppearance();
						if (controls >= 3) {
							appearance[86] = std::bit_cast<float>(1u);
							for (unsigned offset : { 57u, 59u, 61u, 63u, 65u, 67u })
								appearance[offset] = 0.15f;
							for (unsigned offset : { 60u, 62u, 64u, 66u, 85u })
								appearance[offset] = 0.8f;
						}
						fixture.feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", appearance);
						for (unsigned count : { 0u, 1u, capacity }) {
							fixture.Prepare(count, true, controls % 2 ? 17 : 1);
							for (bool pbr : { false, true })
								fixture.Compare(count, pbr, 1, true);
						}
						fixture.PoisonColours();
						for (bool pbr : { false, true }) {
							for (unsigned scene : { 0u, 2u, 3u })
								fixture.Compare(capacity, pbr, scene, true);
							fixture.Compare(capacity, pbr, 1, true, true);
							fixture.Compare(capacity, pbr, 1, true, false, true);
						}
						fixture.Prepare(capacity, false, 1);
						for (bool pbr : { false, true })
							fixture.Compare(capacity, pbr, 1, false);
						std::reverse(fixture.lightData.begin(), fixture.lightData.end());
						fixture.RecreateColours();
						fixture.Prepare(capacity, true, 1);
						for (bool pbr : { false, true })
							fixture.Compare(capacity, pbr, 1, true);
					}
				}
				context->ClearState();
			}
		}
		std::cout << comparisons << " channels checked; " << unequal << " differ in bits; maximum relative error=" << maximumRelativeError << '\n';
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
