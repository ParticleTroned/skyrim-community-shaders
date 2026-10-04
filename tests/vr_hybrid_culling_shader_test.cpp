#include "Features/VRHybridCullingPolicy.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <d3dcompiler.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	using namespace VRHybridCullingPolicy;
	using D3D11ShaderTest::Check;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Kernel
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<D3D11ShaderTest::ConstantBuffer> constants;

		Kernel(ID3D11Device* device, const wchar_t* path, const char* constantsName, bool reversedDepth, bool diagnostics = false, const char* source = nullptr, bool refinementAB = false, bool polygonBaseline = false)
		{
			ComPtr<ID3DBlob> code, errors;
			std::vector<D3D_SHADER_MACRO> defines;
			if (reversedDepth)
				defines.push_back({ "CSX_DEPTH_ORDER_TEST_REVERSED", "1" });
			if (diagnostics)
				defines.push_back({ "CSX_HIZ_DIAGNOSTICS", "1" });
			if (refinementAB) {
				defines.push_back({ "CSX_HIZ_REFINEMENT_AB", "1" });
				defines.push_back({ "CSX_HIZ_INTERSECTION_AB", "1" });
				defines.push_back({ "CSX_HIZ_CLIP_AB", "1" });
			}
			if (polygonBaseline)
				defines.push_back({ "CSX_HIZ_POLYGON_BASELINE", "1" });
			defines.push_back({ nullptr, nullptr });
			Util::CustomInclude includes{ "package/Shaders" };
			static std::map<std::tuple<std::wstring, bool, bool, bool, bool, std::string>, ComPtr<ID3DBlob>> compiled;
			auto& cached = compiled[{ std::wstring(path), reversedDepth, diagnostics, refinementAB, polygonBaseline, source ? source : "" }];
			if (cached) {
				code = cached;
			} else {
				std::wcout << L"Compiling " << path << L" reversed=" << reversedDepth << L" diagnostics=" << diagnostics
						   << std::endl;
				constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
				const auto result = source ?
				                        D3DCompile(source, std::strlen(source), nullptr, defines.data(), &includes, "main", "cs_5_0",
											flags, 0, code.GetAddressOf(), errors.GetAddressOf()) :
				                        D3DCompileFromFile(path, defines.data(), &includes, "main", "cs_5_0",
											flags, 0, code.GetAddressOf(), errors.GetAddressOf());
				if (FAILED(result) && errors)
					throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
				Check(result);
				cached = code;
			}
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
			Util::SetResourceName(shader.Get(), "HybridCullingTest::%s", constantsName);
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())));
			constants = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device, reflection.Get(), constantsName);
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			const auto bound = reflection->GetResourceBindingDescByName("TraversalDiagnostics", &binding);
			Require(diagnostics ? SUCCEEDED(bound) && binding.BindPoint == 1 : FAILED(bound),
				"Diagnostic UAV leaked into the production shader or changed slots");
		}

		template <class T>
		void Bind(ID3D11DeviceContext* context, const T& values)
		{
			Require(constants->bytes.size() == sizeof(T), "CPU and reflected shader constant sizes differ");
			std::memcpy(constants->bytes.data(), &values, sizeof(T));
			constants->Bind(context);
			context->CSSetShader(shader.Get(), nullptr, 0);
		}
	};

	struct StructuredBuffer
	{
		ComPtr<ID3D11Buffer> buffer, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;

		StructuredBuffer(ID3D11Device* device, UINT stride, UINT count, UINT flags, const void* data = nullptr)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = stride * count;
			desc.StructureByteStride = stride;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.BindFlags = flags;
			D3D11_SUBRESOURCE_DATA initial{ data, 0, 0 };
			Check(device->CreateBuffer(&desc, data ? &initial : nullptr, buffer.GetAddressOf()));
			Util::SetResourceName(buffer.Get(), "HybridCullingTest::StructuredBuffer");
			if (flags & D3D11_BIND_SHADER_RESOURCE) {
				Check(device->CreateShaderResourceView(buffer.Get(), nullptr, srv.GetAddressOf()));
				Util::SetResourceName(srv.Get(), "HybridCullingTest::StructuredBuffer SRV");
			}
			if (flags & D3D11_BIND_UNORDERED_ACCESS) {
				Check(device->CreateUnorderedAccessView(buffer.Get(), nullptr, uav.GetAddressOf()));
				Util::SetResourceName(uav.Get(), "HybridCullingTest::StructuredBuffer UAV");
				desc.BindFlags = 0;
				desc.MiscFlags = 0;
				desc.StructureByteStride = 0;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Check(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()));
				Util::SetResourceName(staging.Get(), "HybridCullingTest::ReadbackBuffer");
			}
		}
	};

	struct Fixture
	{
		ID3D11Device* device;
		ID3D11DeviceContext* context;
		bool reversedDepth;
		UINT sourceWidth, sourceHeight;
		BuildConstants buildConstants{};
		TestConstants testConstants{};
		Kernel build, reduce, test, diagnosticTest;
		std::unique_ptr<Kernel> polygonTest, diagnosticPolygonTest;
		std::vector<std::array<std::uint32_t, 24>> lastDiagnostics;
		ComPtr<ID3D11Texture2D> source, pyramid, staging;
		ComPtr<ID3D11ShaderResourceView> sourceView, pyramidView;
		std::vector<ComPtr<ID3D11ShaderResourceView>> mipViews;
		std::vector<ComPtr<ID3D11UnorderedAccessView>> mipOutputs;

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth, UINT eyeWidth = 32, UINT eyeHeight = 32, UINT reduction = 4, bool refinementAB = false) :
			device(device), context(context), reversedDepth(reversedDepth), sourceWidth(2 * eyeWidth), sourceHeight(eyeHeight),
			build(device, L"package/Shaders/VRHybridCulling/BuildDepthCS.hlsl", "BuildConstants", reversedDepth),
			reduce(device, L"package/Shaders/VRHybridCulling/ReduceDepthCS.hlsl", "ReduceConstants", reversedDepth),
			test(device, L"package/Shaders/VRHybridCulling/TestBoundsCS.hlsl", "TestConstants", reversedDepth, false, nullptr, refinementAB),
			diagnosticTest(device, L"package/Shaders/VRHybridCulling/TestBoundsCS.hlsl", "TestConstants", reversedDepth, true, nullptr, refinementAB)
		{
			if (refinementAB) {
				polygonTest = std::make_unique<Kernel>(device, L"package/Shaders/VRHybridCulling/TestBoundsCS.hlsl", "TestConstants", reversedDepth, false, nullptr, true, true);
				diagnosticPolygonTest = std::make_unique<Kernel>(device, L"package/Shaders/VRHybridCulling/TestBoundsCS.hlsl", "TestConstants", reversedDepth, true, nullptr, true, true);
			}
			testConstants.eyes = { EyeRect{ 0, 0, eyeWidth, eyeHeight }, EyeRect{ eyeWidth, 0, eyeWidth, eyeHeight } };
			Require(TryMakeBuildConstants(testConstants.eyes, sourceWidth, sourceHeight, reduction,
						buildConstants, testConstants.pyramid),
				"Invalid fixture dimensions");
			for (auto& matrix : testConstants.viewProjection)
				for (UINT row = 0; row < 4; ++row)
					matrix[row][row] = 1.0f;
			Require(test.constants->Offset("CameraAdjust") == offsetof(TestConstants, cameraAdjust) &&
						test.constants->Offset("EyeRect") == offsetof(TestConstants, eyes) &&
						test.constants->Offset("PyramidSize") == offsetof(TestConstants, pyramid) &&
						test.constants->Offset("ObjectCount") == offsetof(TestConstants, objectCount) &&
						test.constants->Offset("PixelGuardBand") == offsetof(TestConstants, pixelGuardBand),
				"Reflected shader ABI differs from CPU constants");

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = sourceWidth;
			desc.Height = sourceHeight;
			desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&desc, nullptr, source.GetAddressOf()));
			Util::SetResourceName(source.Get(), "HybridCullingTest::SourceDepth");
			Check(device->CreateShaderResourceView(source.Get(), nullptr, sourceView.GetAddressOf()));
			Util::SetResourceName(sourceView.Get(), "HybridCullingTest::SourceDepth SRV");
			desc.Width = testConstants.pyramid.width;
			desc.Height = testConstants.pyramid.height;
			desc.ArraySize = 2;
			desc.MipLevels = testConstants.pyramid.mipCount;
			desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
			Check(device->CreateTexture2D(&desc, nullptr, pyramid.GetAddressOf()));
			Util::SetResourceName(pyramid.Get(), "HybridCullingTest::DepthPyramid");
			Check(device->CreateShaderResourceView(pyramid.Get(), nullptr, pyramidView.GetAddressOf()));
			Util::SetResourceName(pyramidView.Get(), "HybridCullingTest::DepthPyramid SRV");
			for (UINT mip = 0; mip < desc.MipLevels; ++mip) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = desc.Format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Texture2DArray.MostDetailedMip = mip;
				srvDesc.Texture2DArray.MipLevels = 1;
				srvDesc.Texture2DArray.ArraySize = 2;
				mipViews.emplace_back();
				Check(device->CreateShaderResourceView(pyramid.Get(), &srvDesc, mipViews.back().GetAddressOf()));
				Util::SetResourceName(mipViews.back().Get(), "HybridCullingTest::DepthMip%u SRV", mip);
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format = desc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
				uavDesc.Texture2DArray.MipSlice = mip;
				uavDesc.Texture2DArray.ArraySize = 2;
				mipOutputs.emplace_back();
				Check(device->CreateUnorderedAccessView(pyramid.Get(), &uavDesc, mipOutputs.back().GetAddressOf()));
				Util::SetResourceName(mipOutputs.back().Get(), "HybridCullingTest::DepthMip%u UAV", mip);
			}
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "HybridCullingTest::DepthReadback");
		}

		void Unbind()
		{
			ID3D11ShaderResourceView* views[3]{};
			ID3D11UnorderedAccessView* outputs[2]{};
			context->CSSetShaderResources(0, 3, views);
			context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		}

		float EncodeDepth(float depth) const
		{
			// Native mask zero remains invalid independently of the projection order.
			return reversedDepth && std::isfinite(depth) && depth > 0.0f && depth <= 1.0f ? 1.0f - depth : depth;
		}

		void Build(std::span<const float> pixels)
		{
			Require(pixels.size() == sourceWidth * sourceHeight, "Unexpected source pixel count");
			std::vector<float> encoded(pixels.begin(), pixels.end());
			for (auto& depth : encoded)
				depth = EncodeDepth(depth);
			context->UpdateSubresource(source.Get(), 0, nullptr, encoded.data(), sourceWidth * sizeof(float), 0);
			build.Bind(context, buildConstants);
			auto* sourceSRV = sourceView.Get();
			auto* outputUAV = mipOutputs[0].Get();
			context->CSSetShaderResources(0, 1, &sourceSRV);
			context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
			context->Dispatch((buildConstants.outputWidth + 7) / 8, (buildConstants.outputHeight + 7) / 8, 2);
			Unbind();
			for (UINT mip = 1; mip < testConstants.pyramid.mipCount; ++mip) {
				ReduceConstants constants{ std::max(testConstants.pyramid.width >> mip, 1u),
					std::max(testConstants.pyramid.height >> mip, 1u), {} };
				reduce.Bind(context, constants);
				sourceSRV = mipViews[mip - 1].Get();
				outputUAV = mipOutputs[mip].Get();
				context->CSSetShaderResources(0, 1, &sourceSRV);
				context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
				context->Dispatch((constants.outputWidth + 7) / 8, (constants.outputHeight + 7) / 8, 2);
				Unbind();
			}
		}

		std::vector<float> ReadMip(UINT eye, UINT mip)
		{
			context->CopyResource(staging.Get(), pyramid.Get());
			const auto subresource = D3D11CalcSubresource(mip, eye, testConstants.pyramid.mipCount);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped));
			const auto width = std::max(testConstants.pyramid.width >> mip, 1u);
			const auto height = std::max(testConstants.pyramid.height >> mip, 1u);
			std::vector<float> pixels(width * height);
			for (UINT row = 0; row < height; ++row)
				std::memcpy(pixels.data() + row * width, static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch, width * sizeof(float));
			context->Unmap(staging.Get(), subresource);
			return pixels;
		}

		std::vector<std::uint32_t> Test(std::span<const OBBTransform> objects, bool sourceAvailable = true)
		{
			StructuredBuffer bounds(device, sizeof(OBBTransform), static_cast<UINT>(objects.size()), D3D11_BIND_SHADER_RESOURCE, objects.data());
			StructuredBuffer results(device, sizeof(std::uint32_t), static_cast<UINT>(objects.size()), D3D11_BIND_UNORDERED_ACCESS);
			testConstants.objectCount = static_cast<UINT>(objects.size());
			auto constants = testConstants;
			if (reversedDepth)
				for (auto& matrix : constants.viewProjection)
					for (UINT column = 0; column < 4; ++column)
						matrix[2][column] = matrix[3][column] - matrix[2][column];
			const bool polygonBaseline = polygonTest && (constants.reserved & 2u) != 0;
			(polygonBaseline ? *polygonTest : test).Bind(context, constants);
			ID3D11ShaderResourceView* views[]{ bounds.srv.Get(), pyramidView.Get(), sourceAvailable ? sourceView.Get() : nullptr };
			auto* output = results.uav.Get();
			context->CSSetShaderResources(0, 3, views);
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch((testConstants.objectCount + 63) / 64, 1, 1);
			Unbind();
			context->CopyResource(results.staging.Get(), results.buffer.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(results.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<std::uint32_t> values(objects.size());
			std::memcpy(values.data(), mapped.pData, values.size() * sizeof(std::uint32_t));
			context->Unmap(results.staging.Get(), 0);
			StructuredBuffer diagnostics(device, sizeof(lastDiagnostics[0]), static_cast<UINT>(objects.size()), D3D11_BIND_UNORDERED_ACCESS);
			(polygonBaseline ? *diagnosticPolygonTest : diagnosticTest).Bind(context, constants);
			ID3D11UnorderedAccessView* outputs[]{ output, diagnostics.uav.Get() };
			context->CSSetShaderResources(0, 3, views);
			context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
			context->Dispatch((testConstants.objectCount + 63) / 64, 1, 1);
			Unbind();
			context->CopyResource(results.staging.Get(), results.buffer.Get());
			Check(context->Map(results.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const bool identical = std::memcmp(values.data(), mapped.pData, values.size() * sizeof(std::uint32_t)) == 0;
			context->Unmap(results.staging.Get(), 0);
			Require(identical, "Diagnostic shader changed visibility");
			context->CopyResource(diagnostics.staging.Get(), diagnostics.buffer.Get());
			Check(context->Map(diagnostics.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			lastDiagnostics.resize(objects.size());
			std::memcpy(lastDiagnostics.data(), mapped.pData, lastDiagnostics.size() * sizeof(lastDiagnostics[0]));
			context->Unmap(diagnostics.staging.Get(), 0);
			for (std::size_t index = 0; index < lastDiagnostics.size(); ++index) {
				const auto& record = lastDiagnostics[index];
				Require(record[1] <= 128 && record[2] <= record[1] && record[3] <= 12 * record[2],
					"Diagnostic traversal work exceeded the budget");
				Require(record[4] + record[5] + record[17] + record[21] + record[7] <= record[3] && record[6] <= 6 * record[2],
					"Diagnostic proof paths exceed the corresponding face or triangle attempts");
				Require(record[6] == 0 && record[7] == 0,
					"Guarded proof reported a base-only bias shortcut");
				Require(record[8] + record[9] <= 4 * record[5] && record[10] + record[11] <= record[3] &&
							record[13] <= record[1] && record[14] <= record[12] && record[15] <= 2,
					"Plane-cache, clipping or source-refinement counters exceeded their work");
				Require((record[0] == 257) == (values[index] == 0), "Diagnostic reasons disagree with visibility");
			}
			return values;
		}
	};

	void ChecksProjectedRegionShortcuts(Fixture& fixture)
	{
		constexpr const char* source = R"(
#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"
struct RegionCase { float4 a, b, c, rectangle; float depth, bias; float2 padding; };
StructuredBuffer<RegionCase> Cases : register(t0);
RWStructuredBuffer<uint2> Results : register(u0);
cbuffer RegionConstants : register(b0) { uint Count; uint3 Padding; };
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Count) return;
    RegionCase value = Cases[id.x];
    Results[id.x] = uint2(
        ProjectedBounds::HasUnresolvedVertex(value.a.xyz, value.b.xyz, value.c.xyz,
            value.rectangle.xy, value.rectangle.zw, value.depth, value.bias),
        ProjectedBounds::PlaneProvesOccluded(value.a.xyz, value.b.xyz, value.c.xyz,
            value.rectangle.xy, value.rectangle.zw, value.depth, value.bias));
})";
		struct RegionCase
		{
			std::array<float, 4> a, b, c, rectangle;
			float depth, bias;
			std::array<float, 2> padding{};
		};
		std::vector<RegionCase> cases;
		static_assert(sizeof(RegionCase) == 80);
		std::vector<std::array<UINT, 2>> expected;
		const auto add = [&](RegionCase value, UINT unresolved, UINT plane) {
			cases.push_back(value);
			expected.push_back({ unresolved, plane });
		};
		// The exact plane is z = 1/4 + x/32; its regional minimum is 1/2.
		const RegionCase slope{ { 0, 0, 0.25f }, { 16, 0, 0.75f }, { 0, 16, 0.25f }, { 8, 2, 10, 4 }, 0.375f, 0.0625f };
		for (const bool reflected : { false, true }) {
			auto value = slope;
			if (reflected)
				std::swap(value.b, value.c);
			add(value, 0, 1);
			for (auto* vertex : { &value.a, &value.b, &value.c }) {
				(*vertex)[0] += 16360.0f;
				(*vertex)[1] += 16360.0f;
			}
			value.rectangle = { 16368, 16362, 16370, 16364 };
			add(value, 0, 1);
		}
		auto value = slope;
		value.a[2] = value.c[2] = 0.75f;
		value.b[2] = 0.25f;
		value.depth = 0.3125f;
		add(value, 0, 1);
		for (const float depth : { 0.375f, std::nextafter(0.375f, 0.0f), std::nextafter(0.375f, 1.0f) }) {
			value = slope;
			value.depth = depth;
			value.bias = 0.125f;
			add(value, 0, 0);
		}
		for (const float delta : { 0.0f, std::nextafter(16.0f, 17.0f) - 16.0f }) {
			value = { { 0, 0, 0.25f }, { 8, 8, 0.75f }, { 16, 16 + delta, 0.25f }, { 5, 5, 10, 10 }, 0.125f, 0.0625f };
			add(value, 0, 0);
			std::swap(value.b, value.c);
			add(value, 0, 0);
		}
		value = slope;
		value.a[2] = std::numeric_limits<float>::quiet_NaN();
		add(value, 0, 0);
		value = { { 0, 0, 0.25f }, { 1e-10f, 0, 0.75f }, { 0, 1e-10f, 0.25f },
			{ 0.5e-10f, 0.2e-10f, 0.6e-10f, 0.3e-10f }, 0.125f, 0.0625f };
		add(value, 0, 0);
		// The low-depth vertex lies on each inclusive edge or corner in turn.
		constexpr std::array<std::array<float, 2>, 8> boundary{ { { 8, 10 }, { 12, 10 }, { 10, 8 }, { 10, 12 },
			{ 8, 8 }, { 8, 12 }, { 12, 8 }, { 12, 12 } } };
		for (const auto& point : boundary) {
			value = { { point[0], point[1], 0.5f }, { 20, 20, 0.75f }, { 22, 20, 0.75f }, { 8, 8, 12, 12 }, 0.375f, 0.125f };
			add(value, 1, 0);
		}
		for (UINT edge = 0; edge < 4; ++edge) {
			value = { { boundary[edge][0], boundary[edge][1], 0.5f }, { 20, 20, 0.75f }, { 22, 20, 0.75f }, { 8, 8, 12, 12 }, 0.375f, 0.125f };
			const UINT axis = edge / 2;
			value.a[axis] = std::nextafter(value.a[axis], 10.0f);
			add(value, 1, 0);
			value.a[axis] = std::nextafter(boundary[edge][axis], edge % 2 == 0 ? 0.0f : 20.0f);
			add(value, 0, 0);
		}
		for (auto& item : cases) {
			if (fixture.reversedDepth) {
				item.a[2] = 1.0f - item.a[2];
				item.b[2] = 1.0f - item.b[2];
				item.c[2] = 1.0f - item.c[2];
				item.depth = 1.0f - item.depth;
			}
		}
		Kernel kernel(fixture.device, L"ProjectedBoundsHelperTest.hlsl", "RegionConstants", fixture.reversedDepth, false, source);
		StructuredBuffer inputs(fixture.device, sizeof(RegionCase), static_cast<UINT>(cases.size()), D3D11_BIND_SHADER_RESOURCE, cases.data());
		StructuredBuffer outputs(fixture.device, 2 * sizeof(UINT), static_cast<UINT>(cases.size()), D3D11_BIND_UNORDERED_ACCESS);
		kernel.Bind(fixture.context, std::array<UINT, 4>{ static_cast<UINT>(cases.size()), 0, 0, 0 });
		auto* input = inputs.srv.Get();
		auto* output = outputs.uav.Get();
		fixture.context->CSSetShaderResources(0, 1, &input);
		fixture.context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		fixture.context->Dispatch((static_cast<UINT>(cases.size()) + 63) / 64, 1, 1);
		fixture.Unbind();
		fixture.context->CopyResource(outputs.staging.Get(), outputs.buffer.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(fixture.context->Map(outputs.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<std::array<UINT, 2>> actual(cases.size());
		std::memcpy(actual.data(), mapped.pData, actual.size() * sizeof(actual[0]));
		fixture.context->Unmap(outputs.staging.Get(), 0);
		for (std::size_t index = 0; index < cases.size(); ++index)
			for (UINT helper = 0; helper < 2; ++helper)
				if (actual[index][helper] != expected[index][helper])
					throw std::runtime_error("Projected-region shortcut mismatch: case=" + std::to_string(index) +
											 " helper=" + std::to_string(helper) + " reversed=" + std::to_string(fixture.reversedDepth));
	}

	OBBTransform Box(float x = 0.0f, float y = 0.0f, float z = 0.75f, float extent = 0.05f)
	{
		OBBTransform result{};
		result.entry[0][0] = result.entry[1][1] = result.entry[2][2] = extent;
		result.entry[0][3] = x;
		result.entry[1][3] = y;
		result.entry[2][3] = z;
		result.entry[3][3] = 1.0f;
		return result;
	}

	OBBTransform BoxForGuardedPixels(const Fixture& fixture, std::array<float, 4> rectangle)
	{
		const auto& eye = fixture.testConstants.eyes[0];
		const float guard = fixture.testConstants.pixelGuardBand;
		const float left = rectangle[0] + guard;
		const float top = rectangle[1] + guard;
		const float right = rectangle[2] - guard;
		const float bottom = rectangle[3] - guard;
		Require(left < right && top < bottom, "Guarded rectangle has no interior");
		auto object = Box();
		object.entry[0][0] = (right - left) / eye.width;
		object.entry[1][1] = (bottom - top) / eye.height;
		object.entry[0][3] = (left + right) / eye.width - 1.0f;
		object.entry[1][3] = 1.0f - (top + bottom) / eye.height;
		return object;
	}

	bool SourceCellsProveOcclusion(const Fixture& fixture, std::span<const float> pixels,
		std::array<UINT, 4> cells, float nearestDepth)
	{
		const auto reduction = fixture.testConstants.pyramid.sourceReduction;
		for (const auto& eye : fixture.testConstants.eyes) {
			for (UINT y = cells[1] * reduction; y < (cells[3] + 1) * reduction; ++y) {
				for (UINT x = cells[0] * reduction; x < (cells[2] + 1) * reduction; ++x) {
					if (x >= eye.width || y >= eye.height)
						return false;
					const float depth = pixels[(eye.y + y) * fixture.sourceWidth + eye.x + x];
					if (!std::isfinite(depth) || depth <= 0.0f || depth > 1.0f ||
						nearestDepth <= depth + fixture.testConstants.depthBias)
						return false;
				}
			}
		}
		return true;
	}

	void RetainsUnavailableRefinementSource(Fixture& fixture)
	{
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		fixture.Build(pixels);
		const std::array objects{ Box() };
		Require(fixture.Test(objects)[0] == 0, "Source validation fixture has no solid-depth proof");
		Require(fixture.Test(objects, false)[0] == 1, "An unbound original-depth source established visibility rejection");
		const auto eyes = fixture.testConstants.eyes;
		fixture.testConstants.eyes[1].x = std::numeric_limits<std::uint32_t>::max();
		Require(fixture.Test(objects)[0] == 1, "An overflowing stereo source rectangle established visibility rejection");
		fixture.testConstants.eyes = eyes;
		Require(fixture.Test(objects)[0] == 0, "A rejected source contaminated a subsequent valid batch");
	}

	void RefinesPastPaddedEyeCells(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		Fixture fixture(device, context, reversedDepth, 1344, 1492);
		fixture.testConstants.pixelGuardBand = 2.0f;
		const std::array objects{ BoxForGuardedPixels(fixture, { 1000.25f, 400.25f, 1323.75f, 723.75f }) };
		fixture.Build(std::vector<float>(fixture.sourceWidth * fixture.sourceHeight, 0.4f));
		// Interior coverage remains provable even when coarse cells include far-valued padding.
		Require(fixture.Test(objects)[0] == 0, "Padded coarse cells prevented an interior occlusion proof");
	}

	void PreservesRefinedFootprintAndStereo(Fixture& fixture)
	{
		const std::array objects{ BoxForGuardedPixels(fixture, { 8.25f, 8.25f, 19.75f, 19.75f }) };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		for (UINT eye = 0; eye < 2; ++eye)
			pixels[12 * fixture.sourceWidth + fixture.testConstants.eyes[eye].x + 20] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "Refinement retained an unrelated clear cell outside the guarded footprint");
		for (UINT eye = 0; eye < 2; ++eye) {
			for (const float untrusted : { 0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN() }) {
				const auto offset = 12 * fixture.sourceWidth + fixture.testConstants.eyes[eye].x + 8;
				pixels[offset] = untrusted;
				fixture.Build(pixels);
				Require(fixture.Test(objects)[0] == 1, "Refinement omitted a guarded clear, masked or invalid pixel in one eye");
				pixels[offset] = 0.4f;
			}
		}
		const std::array border{ BoxForGuardedPixels(fixture, { -0.25f, 8.25f, 7.75f, 19.75f }) };
		fixture.Build(std::vector<float>(fixture.sourceWidth * fixture.sourceHeight, 0.4f));
		Require(fixture.Test(border)[0] == 1, "Refinement bypassed an uncovered border guard");
	}

	void RefinesThinRectangles(Fixture& fixture)
	{
		const std::array objects{ BoxForGuardedPixels(fixture, { 4.25f, 12.25f, 27.75f, 15.75f }) };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		for (UINT eye = 0; eye < 2; ++eye)
			pixels[4 * fixture.sourceWidth + fixture.testConstants.eyes[eye].x + 12] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "A thin rectangle included unrelated depth after refinement");
		pixels[14 * fixture.sourceWidth + fixture.testConstants.eyes[1].x + 12] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 1, "Thin-rectangle refinement omitted the second eye's visible depth");
	}

	void RetainsFinerOccluderDetail(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		for (const UINT reduction : { 2u, 4u }) {
			Fixture fixture(device, context, reversedDepth, 32, 32, reduction);
			fixture.testConstants.pixelGuardBand = 2.0f;
			const std::array objects{ BoxForGuardedPixels(fixture, { 11.25f, 11.25f, 20.75f, 20.75f }) };
			for (const float untrusted : { 0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN() }) {
				std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
				for (const auto& eye : fixture.testConstants.eyes)
					pixels[16 * fixture.sourceWidth + eye.x + 9] = untrusted;
				// The outside pixel shares a coarse leaf with the guard, but not a finer leaf.
				if (reduction == 2)
					Require(SourceCellsProveOcclusion(fixture, pixels, { 5, 5, 10, 10 }, 0.7f),
						"Fine-depth fixture has no complete source-pixel proof");
				fixture.Build(pixels);
				Require(fixture.Test(objects)[0] == 0u,
					"Finer leaves failed to recover occluder detail lost by coarse reduction");
				if (reduction == 4)
					Require(fixture.lastDiagnostics[0][12] != 0 && fixture.lastDiagnostics[0][14] != 0,
						"Coarser depth fixture did not exercise selective source-pixel refinement");
				for (const auto& eye : fixture.testConstants.eyes) {
					const auto inside = 16 * fixture.sourceWidth + eye.x + 16;
					pixels[inside] = untrusted;
					fixture.Build(pixels);
					Require(fixture.Test(objects)[0] == 1, "Finer leaves lost a covered stereo hole or mask");
					pixels[inside] = 0.4f;
				}
			}
		}
	}

	void ChecksSourceRefinementAB(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		for (const UINT reduction : { 2u, 4u }) {
			Fixture fixture(device, context, reversedDepth, 32, 32, reduction, true);
			const std::array objects{ BoxForGuardedPixels(fixture, { 11.25f, 11.25f, 20.75f, 20.75f }) };
			std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
			for (const auto& eye : fixture.testConstants.eyes)
				pixels[16 * fixture.sourceWidth + eye.x + (reduction == 2 ? 10 : 9)] = 1.0f;
			fixture.Build(pixels);
			Require(fixture.Test(objects)[0] == 0 && fixture.lastDiagnostics[0][14] != 0,
				"Refinement ON failed to resolve an unrelated source pixel");
			fixture.testConstants.reserved = 1;
			Require(fixture.Test(objects)[0] == 1 && fixture.lastDiagnostics[0][13] == 0 && fixture.lastDiagnostics[0][15] == 0,
				"Refinement OFF read original depth or accepted an unresolved leaf");
			fixture.testConstants.reserved = 2;
			Require(fixture.Test(objects)[0] == 0 && fixture.lastDiagnostics[0][20] == 0,
				"Polygon baseline disabled source refinement or used direct intersection proofs");
			fixture.testConstants.reserved = 8;
			Require(fixture.Test(objects)[0] == 1, "Unknown A/B flag did not fail conservatively");
			fixture.testConstants.reserved = 0;
			for (const auto& eye : fixture.testConstants.eyes) {
				pixels[16 * fixture.sourceWidth + eye.x + 16] = 1.0f;
				fixture.Build(pixels);
				Require(fixture.Test(objects)[0] == 1, "Refinement ON lost a covered stereo hole");
				pixels[16 * fixture.sourceWidth + eye.x + 16] = 0.4f;
			}
		}
	}

	void RefinesOnlyUnresolvedCells(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		Fixture fixture(device, context, reversedDepth, 32, 32, 1);
		const std::array objects{ BoxForGuardedPixels(fixture, { 1.25f, 0.25f, 6.75f, 7.75f }) };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "Budget fixture failed its coarse solid-depth proof");
		pixels[4 * fixture.sourceWidth + fixture.testConstants.eyes[1].x] = 1.0f;
		Require(SourceCellsProveOcclusion(fixture, pixels, { 1, 0, 6, 7 },
					objects[0].entry[2][3] - objects[0].entry[2][2]),
			"Budget fixture accidentally includes the clear pixel in its guarded cells");
		fixture.Build(pixels);
		// One inconclusive branch no longer consumes complete finer rectangles.
		Require(fixture.Test(objects)[0] == 0, "Adaptive traversal failed to retain completed coarse proofs");
		Require(fixture.lastDiagnostics[0][1] < 64, "Adaptive traversal reloaded complete finer grids");
	}

	void RetainsNearestVertexBeforeRefinement(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		for (const UINT reduction : { 1u, 2u, 4u, 8u }) {
			Fixture fixture(device, context, reversedDepth, 32, 32, reduction);
			const std::array objects{ BoxForGuardedPixels(fixture, { 8.25f, 8.25f, 23.75f, 23.75f }) };
			const float nearest = objects[0].entry[2][3] - objects[0].entry[2][2];
			std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
			fixture.Build(pixels);
			Require(fixture.Test(objects)[0] == 0 && fixture.lastDiagnostics[0][1] == 8,
				"Nearest-vertex check added work to a successful four-load coarse proof");
			for (UINT eye = 0; eye < 2; ++eye) {
				// The nearest tied corner selected last projects to (22.75, 9.25).
				const auto witness = 9 * fixture.sourceWidth + fixture.testConstants.eyes[eye].x + 22;
				for (const float depth : { 1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(),
						 nearest - 0.5f * fixture.testConstants.depthBias }) {
					pixels[witness] = depth;
					fixture.Build(pixels);
					Require(fixture.Test(objects)[0] == 1, "Nearest-vertex depth, mask or bias was lost in one eye");
					const auto& record = fixture.lastDiagnostics[0];
					Require(record[0] == (eye == 0 ? 8u : 2049u) && record[1] == eye * 4 + (reduction == 1 || reduction == 8 ? 5u : 6u) &&
								record[2] == 0 && record[3] == 0,
						"Unresolved nearest vertex reached face refinement or reloaded a coarse leaf");
				}
				pixels[witness] = 0.4f;
			}
			pixels[22 * fixture.sourceWidth + 9] = 1.0f;
			fixture.Build(pixels);
			Require(fixture.Test(objects)[0] == 1 && fixture.lastDiagnostics[0][2] > 0,
				"A hidden nearest vertex incorrectly proved the rest of the box hidden");
		}
	}

	void ExhaustsActualLoadBudget(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		Fixture fixture(device, context, reversedDepth, 256, 256, 1);
		fixture.testConstants.pixelGuardBand = 1.0f;
		auto sloped = Box();
		sloped.entry[0][0] = sloped.entry[1][1] = 0.8f;
		sloped.entry[2][0] = 0.25f;
		sloped.entry[2][2] = 0.01f;
		sloped.entry[2][3] = 0.65f;
		const std::array objects{ sloped };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight);
		for (UINT y = 0; y < fixture.sourceHeight; ++y)
			for (UINT x = 0; x < fixture.sourceWidth; ++x) {
				const float ndcX = (static_cast<float>(x % 256) + 0.5f) / 128.0f - 1.0f;
				pixels[y * fixture.sourceWidth + x] = 0.64f + 0.25f * ndcX / 0.8f - 0.02f;
			}
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 1, "Partial traversal established an occlusion proof");
		Require(fixture.lastDiagnostics[0][0] == 5 && fixture.lastDiagnostics[0][1] == 64,
			"Budget exhaustion did not stop after exactly 64 first-eye depth reads");
	}

	void TraversesMaximumMipDepth(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		Fixture fixture(device, context, reversedDepth, 8192, 8, 2);
		Require(fixture.testConstants.pyramid.mipCount == 12, "Fixture did not reach the admitted mip limit");
		const std::array objects{ BoxForGuardedPixels(fixture, { 8.25f, 2.25f, 8183.75f, 5.75f }) };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		for (const auto& eye : fixture.testConstants.eyes)
			for (UINT y = 0; y < eye.height; ++y) {
				pixels[y * fixture.sourceWidth + eye.x] = 1.0f;
				pixels[y * fixture.sourceWidth + eye.x + eye.width - 1] = 1.0f;
			}
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "Deep one-dimensional traversal lost a covered region or exceeded its stack");
		pixels[4 * fixture.sourceWidth + fixture.testConstants.eyes[1].x + 8180] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 1, "Deep traversal omitted a second-eye leaf at a high cell coordinate");
		Require((fixture.lastDiagnostics[0][0] >> 8) == 6, "Unresolved deep leaf was not attributed to the second eye");
	}

	void ChecksRefinedProofsAgainstSourcePixels(Fixture& fixture)
	{
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		const auto reduction = fixture.testConstants.pyramid.sourceReduction;
		pixels[(3 * reduction + 2) * fixture.sourceWidth + 5 * reduction + 1] = 1.0f;
		pixels[(5 * reduction + 2) * fixture.sourceWidth + fixture.testConstants.eyes[1].x + 2 * reduction + 1] = 0.0f;
		std::vector<OBBTransform> objects;
		std::vector<bool> proofs;
		for (UINT top = 1; top <= 6; ++top) {
			for (UINT bottom = top; bottom <= 6; ++bottom) {
				for (UINT left = 1; left <= 6; ++left) {
					for (UINT right = left; right <= 6; ++right) {
						if ((right - left + 1) * reduction <= 2.0f * fixture.testConstants.pixelGuardBand + 0.5f ||
							(bottom - top + 1) * reduction <= 2.0f * fixture.testConstants.pixelGuardBand + 0.5f)
							continue;
						const auto object = BoxForGuardedPixels(fixture,
							{ left * reduction + 0.25f, top * reduction + 0.25f,
								(right + 1) * reduction - 0.25f, (bottom + 1) * reduction - 0.25f });
						objects.push_back(object);
						proofs.push_back(SourceCellsProveOcclusion(fixture, pixels, { left, top, right, bottom },
							object.entry[2][3] - object.entry[2][2]));
					}
				}
			}
		}
		fixture.Build(pixels);
		const auto visibility = fixture.Test(objects);
		bool rejected = false, retained = false;
		for (std::size_t index = 0; index < objects.size(); ++index) {
			Require(visibility[index] != 0 || proofs[index], "Refinement rejected a rectangle without complete source-pixel evidence");
			rejected |= visibility[index] == 0;
			retained |= visibility[index] != 0;
		}
		Require(rejected && retained, "Source-pixel oracle fixture did not exercise both visibility outcomes");
	}

	void ExcludesEmptyProjectedCorners(Fixture& fixture)
	{
		auto diamond = Box();
		diamond.entry[0][0] = diamond.entry[1][0] = diamond.entry[1][1] = 0.25f;
		diamond.entry[0][1] = -0.25f;
		const std::array objects{ diamond };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		for (const auto& eye : fixture.testConstants.eyes)
			pixels[4 * fixture.sourceWidth + eye.x + 4] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "An empty projected rectangle corner blocked face occlusion");
		for (const auto& eye : fixture.testConstants.eyes) {
			for (float invalid : { 0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN() }) {
				const auto offset = 16 * fixture.sourceWidth + eye.x + 16;
				pixels[offset] = invalid;
				fixture.Build(pixels);
				Require(fixture.Test(objects)[0] == 1, "Face coverage lost a visible or invalid pixel in one eye");
				pixels[offset] = 0.4f;
			}
		}
	}

	void UsesLocalFaceDepth(Fixture& fixture)
	{
		auto sloped = BoxForGuardedPixels(fixture, { 8.25f, 8.25f, 19.75f, 19.75f });
		sloped.entry[2][0] = 0.25f;
		sloped.entry[2][3] = 0.65f;
		const std::array objects{ sloped };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight);
		for (const auto& eye : fixture.testConstants.eyes)
			for (UINT y = 0; y < eye.height; ++y)
				for (UINT x = 0; x < eye.width; ++x)
					pixels[y * fixture.sourceWidth + eye.x + x] = 0.01f + 0.025f * x;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "One nearest-box depth prevented a valid sloped-face proof");
		for (const auto& eye : fixture.testConstants.eyes) {
			const auto offset = 14 * fixture.sourceWidth + eye.x + 17;
			const float original = pixels[offset];
			pixels[offset] = 0.9f;
			fixture.Build(pixels);
			Require(fixture.Test(objects)[0] == 1, "A locally visible sloped face was hidden in one eye");
			pixels[offset] = original;
		}
	}

	void PreservesFaceProofsAcrossAxisPermutations(Fixture& fixture)
	{
		auto sloped = BoxForGuardedPixels(fixture, { 8.25f, 8.25f, 19.75f, 19.75f });
		sloped.entry[2][0] = 0.25f;
		sloped.entry[2][3] = 0.65f;
		constexpr std::array<std::array<UINT, 3>, 6> permutations{ { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
		std::vector<OBBTransform> objects;
		for (const auto& permutation : permutations) {
			for (UINT signs = 0; signs < 8; ++signs) {
				auto object = sloped;
				for (UINT row = 0; row < 3; ++row)
					for (UINT column = 0; column < 3; ++column)
						object.entry[row][column] = sloped.entry[row][permutation[column]] * ((signs & (1u << column)) ? -1.0f : 1.0f);
				objects.push_back(object);
			}
		}
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight);
		for (const auto& eye : fixture.testConstants.eyes)
			for (UINT y = 0; y < eye.height; ++y)
				for (UINT x = 0; x < eye.width; ++x)
					pixels[y * fixture.sourceWidth + eye.x + x] = 0.01f + 0.025f * x;
		fixture.Build(pixels);
		Require(fixture.Test(objects) == std::vector<std::uint32_t>(objects.size(), 0),
			"Equivalent reflected or permuted boxes lost the local face proof");
		for (const auto& diagnostic : fixture.lastDiagnostics)
			Require(diagnostic[2] != 0, "Axis-permutation fixture did not reach the face refinement path");
		for (const auto& eye : fixture.testConstants.eyes) {
			const auto offset = 14 * fixture.sourceWidth + eye.x + 17;
			const float original = pixels[offset];
			for (const float depth : { 0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN() }) {
				pixels[offset] = depth;
				fixture.Build(pixels);
				Require(fixture.Test(objects) == std::vector<std::uint32_t>(objects.size(), 1),
					"Reflected or permuted face bounds lost a visibility hole in one eye");
			}
			pixels[offset] = original;
		}
	}

	struct RayBoxOracle
	{
		double inverse[3][3]{};
		double translation[3]{};

		explicit RayBoxOracle(const OBBTransform& object)
		{
			double augmented[3][6]{};
			for (UINT row = 0; row < 3; ++row) {
				translation[row] = object.entry[row][3];
				for (UINT column = 0; column < 3; ++column)
					augmented[row][column] = object.entry[row][column];
				augmented[row][row + 3] = 1.0;
			}
			for (UINT column = 0; column < 3; ++column) {
				UINT pivot = column;
				for (UINT row = column + 1; row < 3; ++row)
					if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
						pivot = row;
				Require(std::abs(augmented[pivot][column]) > 1e-12, "Singular oracle box");
				for (UINT entry = 0; entry < 6; ++entry)
					std::swap(augmented[pivot][entry], augmented[column][entry]);
				const auto scale = augmented[column][column];
				for (auto& entry : augmented[column])
					entry /= scale;
				for (UINT row = 0; row < 3; ++row) {
					if (row == column)
						continue;
					const auto factor = augmented[row][column];
					for (UINT entry = 0; entry < 6; ++entry)
						augmented[row][entry] -= factor * augmented[column][entry];
				}
			}
			for (UINT row = 0; row < 3; ++row)
				for (UINT column = 0; column < 3; ++column)
					inverse[row][column] = augmented[row][column + 3];
		}

		bool Hit(double ndcX, double ndcY, double perspective, double skew, double& depth) const
		{
			const double origin[]{ ndcX - translation[0], ndcY - translation[1], -translation[2] };
			const double direction[]{ ndcX * perspective - skew, ndcY * perspective, 1.0 };
			double entry = 0.0, exit = 100.0;
			for (UINT axis = 0; axis < 3; ++axis) {
				double o = 0.0, d = 0.0;
				for (UINT column = 0; column < 3; ++column) {
					o += inverse[axis][column] * origin[column];
					d += inverse[axis][column] * direction[column];
				}
				if (std::abs(d) < 1e-12) {
					if (std::abs(o) > 1.0)
						return false;
				} else {
					const double first = (-1.0 - o) / d, second = (1.0 - o) / d;
					entry = std::max(entry, std::min(first, second));
					exit = std::min(exit, std::max(first, second));
				}
			}
			depth = entry / (1.0 + perspective * entry);
			return entry <= exit;
		}
	};

	void RetainsLocalFaceBiasInEitherEye(Fixture& fixture)
	{
		const float originalBias = fixture.testConstants.depthBias;
		fixture.testConstants.depthBias = 0.01f;
		auto object = BoxForGuardedPixels(fixture, { 8.25f, 8.25f, 19.75f, 19.75f });
		object.entry[2][0] = 0.25f;
		object.entry[2][3] = 0.65f;
		const RayBoxOracle oracle(object);
		const auto& eye = fixture.testConstants.eyes[0];
		double objectDepth;
		Require(oracle.Hit(2.0 * 13.25 / eye.width - 1.0, 1.0 - 2.0 * 14.5 / eye.height, 0.0, 0.0, objectDepth),
			"Local bias fixture missed the independently intersected box face");
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.1f);
		fixture.Build(pixels);
		Require(fixture.Test(std::array{ object })[0] == 0, "Local bias fixture has no covered baseline");
		const float touching = static_cast<float>(objectDepth);
		for (const auto& targetEye : fixture.testConstants.eyes) {
			const auto offset = 14 * fixture.sourceWidth + targetEye.x + 13;
			for (const float depth : { touching - 0.5f * fixture.testConstants.depthBias, touching,
					 std::nextafter(touching, 0.0f), std::nextafter(touching, 1.0f) }) {
				Require(objectDepth <= depth + fixture.testConstants.depthBias,
					"Independent local depth must remain within the configured bias");
				pixels[offset] = depth;
				fixture.Build(pixels);
				Require(fixture.Test(std::array{ object })[0] == 1,
					"A local face touching the occluder bias was rejected in one eye");
				Require(fixture.lastDiagnostics[0][2] != 0, "Local bias case did not reach face refinement");
			}
			pixels[offset] = 0.1f;
		}
		fixture.testConstants.depthBias = originalBias;
	}

	void ChecksFaceProofsAgainstRays(Fixture& fixture)
	{
		const auto original = fixture.testConstants;
		std::vector<OBBTransform> objects;
		for (UINT index = 0; index < 96; ++index) {
			const float angle = index * 0.73f, c = std::cos(angle), s = std::sin(angle);
			const float width = 0.08f + (index % 5) * 0.04f, height = 0.03f + (index % 7) * 0.04f;
			auto object = Box((index % 8 - 3.5f) * 0.12f, (index / 8 - 5.5f) * 0.06f, 0.65f, 0.02f);
			object.entry[0][0] = c * width;
			object.entry[0][1] = -s * height;
			object.entry[1][0] = s * width;
			object.entry[1][1] = c * height;
			object.entry[2][0] = (index % 3 - 1.0f) * 0.16f;
			object.entry[2][1] = (index % 5 - 2.0f) * 0.04f;
			objects.push_back(object);
		}
		const auto nearParallelStart = objects.size();
		for (int exponent : { 8, 12, 16, 20, 24 }) {
			for (float sign : { -1.0f, 1.0f }) {
				auto object = Box(0.0f, 0.0f, 0.65f);
				object.entry[0][0] = object.entry[0][1] = 0.18f;
				object.entry[1][0] = 0.18f;
				object.entry[1][1] = 0.18f + sign * std::ldexp(1.0f, -exponent);
				object.entry[2][0] = 0.10f;
				object.entry[2][1] = -0.10f;
				object.entry[0][2] = 0.06f;
				object.entry[1][2] = -0.06f;
				object.entry[2][2] = 0.015f;
				objects.push_back(object);
			}
		}
		bool rejected = false, retained = false, nearParallelRefined = false;
		for (float perspective : { 0.0f, 0.4f }) {
			for (UINT eye = 0; eye < 2; ++eye) {
				fixture.testConstants.viewProjection[eye][3][2] = perspective;
				fixture.testConstants.viewProjection[eye][0][2] = perspective == 0.0f ? 0.0f : (eye == 0 ? -0.04f : 0.04f);
			}
			for (UINT pattern = 0; pattern < 4; ++pattern) {
				std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.2f);
				for (const auto& eye : fixture.testConstants.eyes)
					for (UINT y = 0; y < eye.height; ++y)
						for (UINT x = 0; x < eye.width; ++x) {
							auto& depth = pixels[y * fixture.sourceWidth + eye.x + x];
							if (pattern == 1)
								depth = 0.1f + 0.65f * x / eye.width;
							else if (pattern == 2 && (x + 3 * y + eye.x) % 23 == 0)
								depth = 1.0f;
							else if (pattern == 3 && (2 * x + y + eye.x) % 31 == 0)
								depth = 0.0f;
						}
				fixture.Build(pixels);
				const auto visibility = fixture.Test(objects);
				for (std::size_t index = 0; index < objects.size(); ++index) {
					nearParallelRefined |= index >= nearParallelStart && fixture.lastDiagnostics[index][2] != 0;
					retained |= visibility[index] != 0;
					if (visibility[index] != 0)
						continue;
					rejected = true;
					const RayBoxOracle oracle(objects[index]);
					UINT rayHits = 0;
					for (UINT eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
						const auto& eye = fixture.testConstants.eyes[eyeIndex];
						for (UINT y = 0; y < eye.height; ++y)
							for (UINT x = 0; x < eye.width; ++x)
								for (int dy = -1; dy <= 1; ++dy)
									for (int dx = -1; dx <= 1; ++dx) {
										const double nx = 2.0 * (x + 0.5 + dx * original.pixelGuardBand) / eye.width - 1.0;
										const double ny = 1.0 - 2.0 * (y + 0.5 + dy * original.pixelGuardBand) / eye.height;
										double objectDepth;
										if (!oracle.Hit(nx, ny, perspective, fixture.testConstants.viewProjection[eyeIndex][0][2], objectDepth))
											continue;
										++rayHits;
										const float sceneDepth = pixels[y * fixture.sourceWidth + eye.x + x];
										Require(sceneDepth > 0.0f && objectDepth > sceneDepth,
											"A rejected face has a visible guarded ray in the independent 3D box oracle");
									}
					}
					Require(index < nearParallelStart || rayHits != 0,
						"Near-parallel projection had no independent ray evidence");
				}
			}
		}
		Require(rejected && retained, "Ray oracle did not exercise both visibility outcomes");
		Require(nearParallelRefined, "Near-parallel faces did not exercise region refinement");
		fixture.testConstants = original;
	}

	void ChecksSeparatingEdges(Fixture& fixture)
	{
		constexpr const char* source = R"(
#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"
struct RegionCase { float4 a, b, c, rectangle; };
StructuredBuffer<RegionCase> Cases : register(t0);
RWStructuredBuffer<uint> Results : register(u0);
cbuffer RegionConstants : register(b0) { uint Count; uint3 Padding; };
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Count) return;
    RegionCase value = Cases[id.x];
    float3 normal, magnitude;
    ProjectedBounds::BuildPlane(value.a.xyz, value.b.xyz, value.c.xyz, normal, magnitude);
    Results[id.x] = ProjectedBounds::TriangleOutsideRegion(value.a.xyz, value.b.xyz, value.c.xyz,
        normal, magnitude, value.rectangle.xy, value.rectangle.zw);
})";
		struct RegionCase
		{
			std::array<float, 4> a, b, c, rectangle;
		};
		std::vector<RegionCase> cases;
		std::uint32_t seed = 17;
		const auto random = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
		for (const float offset : { 0.0f, 8192.0f }) {
			for (const float scale : { 0.03125f, 32.0f }) {
				for (int index = 0; index < 1000; ++index) {
					RegionCase value{};
					for (auto* vertex : { &value.a, &value.b, &value.c })
						*vertex = { offset + scale * random(), offset + scale * random(), random(), 0 };
					value.rectangle = { offset + scale * random(), offset + scale * random(), offset + scale * random(), offset + scale * random() };
					if (value.rectangle[0] > value.rectangle[2])
						std::swap(value.rectangle[0], value.rectangle[2]);
					if (value.rectangle[1] > value.rectangle[3])
						std::swap(value.rectangle[1], value.rectangle[3]);
					cases.push_back(value);
				}
			}
		}
		const auto boundaries = cases.size();
		// Inclusive corner/edge contact and a degenerate line always retain the clipping path.
		cases.push_back({ { 0, 0, 0, 0 }, { 1, 0, 1, 0 }, { 0, 1, 0, 0 }, { 1, 0, 2, 1 } });
		cases.push_back({ { 0, 0, 0, 0 }, { 2, 0, 1, 0 }, { 1, 0, 0, 0 }, { 3, 0, 4, 1 } });
		cases.push_back({ { 0, 0, 0, 0 }, { 1, 1, 1, 0 }, { 2, 2, 0, 0 }, { 0, 0, 2, 2 } });
		const auto overlaps = [](const RegionCase& value) {
			using Point = std::array<double, 2>;
			std::vector<Point> polygon{ { value.a[0], value.a[1] }, { value.b[0], value.b[1] }, { value.c[0], value.c[1] } };
			for (int plane = 0; plane < 4 && !polygon.empty(); ++plane) {
				const int axis = plane / 2;
				const double sign = plane % 2 ? -1 : 1;
				const double boundary = value.rectangle[axis + (plane % 2 ? 2 : 0)];
				std::vector<Point> output;
				auto previous = polygon.back();
				double previousDistance = sign * (previous[axis] - boundary);
				for (const auto current : polygon) {
					const double distance = sign * (current[axis] - boundary);
					if ((distance >= 0) != (previousDistance >= 0)) {
						const double weight = previousDistance / (previousDistance - distance);
						output.push_back({ previous[0] + weight * (current[0] - previous[0]), previous[1] + weight * (current[1] - previous[1]) });
					}
					if (distance >= 0)
						output.push_back(current);
					previous = current;
					previousDistance = distance;
				}
				polygon = std::move(output);
			}
			return !polygon.empty();
		};
		StructuredBuffer inputs(fixture.device, sizeof(RegionCase), static_cast<UINT>(cases.size()), D3D11_BIND_SHADER_RESOURCE, cases.data());
		StructuredBuffer outputs(fixture.device, sizeof(UINT), static_cast<UINT>(cases.size()), D3D11_BIND_UNORDERED_ACCESS);
		Kernel kernel(fixture.device, L"TriangleRegionSeparationTest.hlsl", "RegionConstants", fixture.reversedDepth, false, source);
		kernel.Bind(fixture.context, std::array<UINT, 4>{ static_cast<UINT>(cases.size()), 0, 0, 0 });
		auto* input = inputs.srv.Get();
		auto* output = outputs.uav.Get();
		fixture.context->CSSetShaderResources(0, 1, &input);
		fixture.context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		fixture.context->Dispatch((static_cast<UINT>(cases.size()) + 63) / 64, 1, 1);
		fixture.Unbind();
		fixture.context->CopyResource(outputs.staging.Get(), outputs.buffer.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(fixture.context->Map(outputs.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<UINT> actual(cases.size());
		std::memcpy(actual.data(), mapped.pData, actual.size() * sizeof(UINT));
		fixture.context->Unmap(outputs.staging.Get(), 0);
		UINT separated = 0;
		for (std::size_t index = 0; index < cases.size(); ++index) {
			if (!actual[index])
				continue;
			++separated;
			Require(index < boundaries && !overlaps(cases[index]), "Separating-edge rejection removed inclusive triangle coverage");
		}
		Require(separated > 100, "Separating-edge fixtures did not exercise useful rejections");
	}

	void ChecksDirectIntersectionDepth(Fixture& fixture)
	{
		constexpr const char* source = R"(
#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"
struct RegionCase { float4 a, b, c, rectangle; float depth, bias; float2 padding; };
StructuredBuffer<RegionCase> Cases : register(t0);
RWStructuredBuffer<uint> Results : register(u0);
cbuffer RegionConstants : register(b0) { uint Count; uint3 Padding; };
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Count) return;
    RegionCase v = Cases[id.x];
    float3 normal, magnitude, firstEdge, secondEdge, closingEdge;
    ProjectedBounds::BuildPlane(v.a.xyz, v.b.xyz, v.c.xyz, normal, magnitude);
    bool separated = ProjectedBounds::TriangleOutsideRegion(v.a.xyz, v.b.xyz, v.c.xyz, normal, magnitude,
        v.rectangle.xy, v.rectangle.zw, firstEdge, secondEdge, closingEdge);
    bool direct = !separated && ProjectedBounds::DirectIntersectionProvesOccluded(v.a.xyz, v.b.xyz, v.c.xyz,
        firstEdge, secondEdge, closingEdge, normal, magnitude, v.rectangle.xy, v.rectangle.zw, v.depth, v.bias);
    bool plane = ProjectedBounds::PreparedPlaneProvesOccluded(v.a.xyz, normal, magnitude,
        max(v.rectangle.xy, min(v.a.xy, min(v.b.xy, v.c.xy))),
        min(v.rectangle.zw, max(v.a.xy, max(v.b.xy, v.c.xy))), v.depth, v.bias);
    Results[id.x] = (direct ? 1u : 0u) | (plane ? 2u : 0u) | (separated ? 4u : 0u);
})";
		struct RegionCase
		{
			std::array<float, 4> a, b, c, rectangle;
			float depth, bias = kDefaultDepthBias + 64.0f / 16777216.0f;
			std::array<float, 2> padding{};
		};
		static_assert(sizeof(RegionCase) == 80);
		std::vector<RegionCase> cases;
		std::uint32_t seed = 91;
		const auto random = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
		for (float offset : { 0.0f, 8192.0f })
			for (float scale : { 0.03125f, 32.0f })
				for (int index = 0; index < 1000; ++index) {
					RegionCase v{};
					for (auto* point : { &v.a, &v.b, &v.c })
						*point = { offset + scale * random(), offset + scale * random(), random(), 0 };
					v.rectangle = { offset + scale * random(), offset + scale * random(), offset + scale * random(), offset + scale * random() };
					for (int axis = 0; axis < 2; ++axis)
						if (v.rectangle[axis] > v.rectangle[axis + 2])
							std::swap(v.rectangle[axis], v.rectangle[axis + 2]);
					v.depth = random();
					cases.push_back(v);
				}
		const auto useful = cases.size();
		cases.push_back({ { 0, 0, 0.2f, 0 }, { 10, 0, 0.8f, 0 }, { 9, 10, 0.1f, 0 }, { 9.7f, 0, 9.9f, 10 }, 0.5f });
		const auto uncertain = cases.size();
		cases.push_back({ { 0, 0, 0.5f, 0 }, { 2, 0, 0.5f, 0 }, { 1, 0, 0.5f, 0 }, { 0, 0, 2, 1 }, 0.4f });
		cases.push_back({ { 0, 0, 0.5f, 0 }, { 2, 0, 0.5f, 0 }, { 0, 2, 0.5f, 0 }, { 0, 0, 1, 1 }, 0.5f });
		cases.push_back({ { 0, 0, 0.4f, 0 }, { 2, 0, 0.6f, 0 }, { 0, 2, 0.6f, 0 }, { 1, 0, 1, 0 }, 0.5f });
		for (auto& v : cases)
			if (fixture.reversedDepth) {
				for (auto* point : { &v.a, &v.b, &v.c }) (*point)[2] = 1.0f - (*point)[2];
				v.depth = 1.0f - v.depth;
			}
		const auto oracle = [&](const RegionCase& v) {
			using Point = std::array<double, 3>;
			std::vector<Point> polygon{ { v.a[0], v.a[1], v.a[2] }, { v.b[0], v.b[1], v.b[2] }, { v.c[0], v.c[1], v.c[2] } };
			for (int plane = 0; plane < 4 && !polygon.empty(); ++plane) {
				const int axis = plane / 2;
				const double sign = plane % 2 ? -1 : 1;
				const double boundary = v.rectangle[axis + (plane % 2 ? 2 : 0)];
				std::vector<Point> output;
				auto previous = polygon.back();
				double previousDistance = sign * (previous[axis] - boundary);
				for (const auto current : polygon) {
					const double distance = sign * (current[axis] - boundary);
					if ((distance >= 0) != (previousDistance >= 0)) {
						const double weight = previousDistance / (previousDistance - distance);
						Point intersection{};
						for (int lane = 0; lane < 3; ++lane) intersection[lane] = previous[lane] + weight * (current[lane] - previous[lane]);
						output.push_back(intersection);
					}
					if (distance >= 0)
						output.push_back(current);
					previous = current;
					previousDistance = distance;
				}
				polygon = std::move(output);
			}
			for (const auto point : polygon)
				if (fixture.reversedDepth ? point[2] >= static_cast<double>(v.depth) - v.bias : point[2] <= static_cast<double>(v.depth) + v.bias)
					return false;
			return true;
		};
		StructuredBuffer inputs(fixture.device, sizeof(RegionCase), static_cast<UINT>(cases.size()), D3D11_BIND_SHADER_RESOURCE, cases.data());
		StructuredBuffer outputs(fixture.device, sizeof(UINT), static_cast<UINT>(cases.size()), D3D11_BIND_UNORDERED_ACCESS);
		Kernel kernel(fixture.device, L"DirectIntersectionDepthTest.hlsl", "RegionConstants", fixture.reversedDepth, false, source);
		kernel.Bind(fixture.context, std::array<UINT, 4>{ static_cast<UINT>(cases.size()), 0, 0, 0 });
		auto* input = inputs.srv.Get();
		auto* output = outputs.uav.Get();
		fixture.context->CSSetShaderResources(0, 1, &input);
		fixture.context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		fixture.context->Dispatch((static_cast<UINT>(cases.size()) + 63) / 64, 1, 1);
		fixture.Unbind();
		fixture.context->CopyResource(outputs.staging.Get(), outputs.buffer.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(fixture.context->Map(outputs.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::vector<UINT> actual(cases.size());
		std::memcpy(actual.data(), mapped.pData, actual.size() * sizeof(UINT));
		fixture.context->Unmap(outputs.staging.Get(), 0);
		UINT proofs = 0;
		for (std::size_t index = 0; index < cases.size(); ++index)
			if (actual[index] & 1) {
				++proofs;
				Require(oracle(cases[index]), "Direct intersection proof removed visible triangle coverage");
			}
		Require(proofs > 100 && actual[useful] == 1, "Direct proof did not avoid a clip missed by the plane and separating-edge shortcuts");
		for (auto index = uncertain; index < cases.size(); ++index)
			Require((actual[index] & 1) == 0, "Direct proof rejected equality, inclusive edge contact or degenerate geometry");
	}

	void ChecksFarClipAB(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth)
	{
		Fixture fixture(device, context, reversedDepth, 32, 32, 2, true);
		const std::array objects{ Box(0, 0, 0.98f), Box(0, 0, 0.02f), Box(2, 0, 0.98f) };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		fixture.Build(pixels);
		Require(fixture.Test(objects) == std::vector<UINT>{ 0, 1, 1 } && fixture.lastDiagnostics[0][23] != 0,
			"Far clamping lost its conservative proof or bypassed near/viewport guards");
		fixture.testConstants.reserved = 4;
		Require(fixture.Test(objects) == std::vector<UINT>{ 1, 1, 1 } && fixture.lastDiagnostics[0][23] == 0,
			"Far-clip baseline bypassed its retention rule or counted clamping");
		fixture.testConstants.reserved = 0;
		for (UINT eye = 0; eye < 2; ++eye) {
			const auto offset = 16 * fixture.sourceWidth + fixture.testConstants.eyes[eye].x + 16;
			pixels[offset] = 1;
			fixture.Build(pixels);
			Require(fixture.Test(objects)[0] == 1, "Far clamping ignored a visible source pixel in one eye");
			pixels[offset] = 0.4f;
		}
	}

	void ChecksGuardedVertexDepthProofs(Fixture& fixture)
	{
		constexpr const char* source = R"(
#include "Common/DepthOrder.hlsli"
#include "VRHybridCulling/ProjectedBounds.hlsli"
struct RegionCase { float4 vertices[8]; float4 rectangle; float depth, bias; float2 padding; };
StructuredBuffer<RegionCase> Cases : register(t0);
RWStructuredBuffer<uint> Results : register(u0);
#ifdef CSX_HIZ_DIAGNOSTICS
RWStructuredBuffer<HiZTraversalDiagnostic> TraversalDiagnostics : register(u1);
#endif
cbuffer RegionConstants : register(b0) { uint Count; uint3 Padding; };
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Count) return;
    RegionCase value = Cases[id.x];
    [unroll] for (uint vertex = 0; vertex < 8; ++vertex) ProjectedBounds::Vertices[vertex] = value.vertices[vertex].xyz;
    ProjectedBounds::PrepareFaces();
#ifdef CSX_HIZ_DIAGNOSTICS
    HiZTraversalDiagnostic diagnostic = (HiZTraversalDiagnostic)0;
#endif
    Results[id.x] = ProjectedBounds::OccludedInRegion(
        value.rectangle.xy, value.rectangle.zw, value.depth, value.bias HIZ_DIAGNOSTIC_ARGUMENT);
    bool repeated = ProjectedBounds::OccludedInRegion(
        value.rectangle.xy, value.rectangle.zw, value.depth, value.bias HIZ_DIAGNOSTIC_ARGUMENT);
    if (repeated != (Results[id.x] != 0)) Results[id.x] = 2;
#ifdef CSX_HIZ_DIAGNOSTICS
    TraversalDiagnostics[id.x] = diagnostic;
#endif
})";
		struct RegionCase
		{
			std::array<std::array<float, 4>, 8> vertices;
			std::array<float, 4> rectangle{ 0, 0, 32, 32 };
			float depth, bias = kDefaultDepthBias;
			std::array<float, 2> padding{};
		};
		static_assert(sizeof(RegionCase) == 160);
		constexpr float depthUnit = 1.0f / 16777216.0f;
		std::vector<RegionCase> cases;
		std::vector<UINT> expected;
		const auto add = [&](const OBBTransform& object, float depth, bool hidden) {
			RegionCase value{};
			value.depth = depth;
			for (UINT vertex = 0; vertex < 8; ++vertex) {
				std::array<float, 3> position{};
				for (UINT row = 0; row < 3; ++row) {
					position[row] = object.entry[row][3];
					for (UINT axis = 0; axis < 3; ++axis)
						position[row] += object.entry[row][axis] * ((vertex & (1u << axis)) ? 1.0f : -1.0f);
				}
				value.vertices[vertex] = { (position[0] + 1.0f) * 16.0f, (1.0f - position[1]) * 16.0f, position[2], 0 };
			}
			if (hidden) {
				const RayBoxOracle oracle(object);
				UINT hits = 0;
				for (UINT y = 0; y < 32; ++y)
					for (UINT x = 0; x < 32; ++x)
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx) {
								double objectDepth;
								if (!oracle.Hit(2.0 * (x + 0.5 + dx) / 32.0 - 1.0, 1.0 - 2.0 * (y + 0.5 + dy) / 32.0,
										0.0, 0.0, objectDepth))
									continue;
								++hits;
								Require(objectDepth > static_cast<double>(depth) + value.bias,
									"Guarded-vertex proof has a visible guarded ray in the independent box oracle");
							}
				Require(hits != 0, "Guarded-vertex proof fixture has no independent ray intersections");
			}
			if (fixture.reversedDepth) {
				for (auto& vertex : value.vertices)
					vertex[2] = 1.0f - vertex[2];
				value.depth = 1.0f - value.depth;
			}
			cases.push_back(value);
			expected.push_back(hidden ? 1u : 0u);
		};
		for (UINT shape = 0; shape < 3; ++shape) {
			for (UINT reflection = 0; reflection < 8; ++reflection) {
				auto object = Box(0, 0, 0.5f, 0.25f);
				object.entry[2][2] = 8 * depthUnit;
				if (shape != 0) {
					object.entry[0][1] = 0.0625f;
					object.entry[2][0] = 16 * depthUnit;
					object.entry[2][1] = (shape == 1 ? 8 : -8) * depthUnit;
				}
				for (UINT axis = 0; axis < 3; ++axis) {
					object.entry[2][3] += std::abs(object.entry[2][axis]);
					for (UINT row = 0; row < 3; ++row)
						object.entry[row][axis] *= (reflection & (1u << axis)) ? -1.0f : 1.0f;
				}
				// Adjacent depth units bracket the full guarded threshold, including equality.
				for (UINT gap : { 7u, 8u, 9u, 16u, 72u, 73u })
					add(object, 0.5f - gap * depthUnit, gap > 72);
			}
		}
		// A sloped face whose nearest corner is unresolved must retain visibility.
		auto split = Box(0, 0, 0.5f, 0.25f);
		split.entry[2][0] = 0.125f;
		split.entry[2][1] = -0.125f;
		split.entry[2][2] = 0.03125f;
		add(split, 0.46875f - 16 * depthUnit, false);
		// A covered depth boundary forces clipping; repeated regions must reuse its lazy plane.
		for (const float offset : { 0.0f, 8192.0f }) {
			RegionCase clipping{};
			clipping.vertices.fill({ offset + 7, offset + 11, fixture.EncodeDepth(0.8f), 0 });
			clipping.vertices[0] = { offset + 7, offset + 8, fixture.EncodeDepth(0.8f), 0 };
			clipping.vertices[1] = { offset + 10, offset + 8, fixture.EncodeDepth(0.2f), 0 };
			clipping.rectangle = { offset + 8, offset + 8, offset + 8.5f, offset + 11 };
			clipping.depth = fixture.EncodeDepth(0.5f);
			cases.push_back(clipping);
			expected.push_back(0);
		}
		StructuredBuffer inputs(fixture.device, sizeof(RegionCase), static_cast<UINT>(cases.size()), D3D11_BIND_SHADER_RESOURCE, cases.data());
		StructuredBuffer outputs(fixture.device, sizeof(UINT), static_cast<UINT>(cases.size()), D3D11_BIND_UNORDERED_ACCESS);
		StructuredBuffer diagnostics(fixture.device, sizeof(fixture.lastDiagnostics[0]), static_cast<UINT>(cases.size()), D3D11_BIND_UNORDERED_ACCESS);
		for (const bool diagnostic : { false, true }) {
			Kernel kernel(fixture.device, L"GuardedVertexDepthProofTest.hlsl", "RegionConstants", fixture.reversedDepth, diagnostic, source);
			kernel.Bind(fixture.context, std::array<UINT, 4>{ static_cast<UINT>(cases.size()), 0, 0, 0 });
			auto* input = inputs.srv.Get();
			ID3D11UnorderedAccessView* targets[]{ outputs.uav.Get(), diagnostic ? diagnostics.uav.Get() : nullptr };
			fixture.context->CSSetShaderResources(0, 1, &input);
			fixture.context->CSSetUnorderedAccessViews(0, 2, targets, nullptr);
			fixture.context->Dispatch((static_cast<UINT>(cases.size()) + 63) / 64, 1, 1);
			fixture.Unbind();
			fixture.context->CopyResource(outputs.staging.Get(), outputs.buffer.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(fixture.context->Map(outputs.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<UINT> actual(cases.size());
			std::memcpy(actual.data(), mapped.pData, actual.size() * sizeof(actual[0]));
			fixture.context->Unmap(outputs.staging.Get(), 0);
			Require(actual == expected, "Guarded proof changed its strict bias, reflected/sheared bounds or visibility");
			if (!diagnostic)
				continue;
			fixture.context->CopyResource(diagnostics.staging.Get(), diagnostics.buffer.Get());
			Check(fixture.context->Map(diagnostics.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<std::array<UINT, 24>> records(cases.size());
			std::memcpy(records.data(), mapped.pData, records.size() * sizeof(records[0]));
			fixture.context->Unmap(diagnostics.staging.Get(), 0);
			bool reused = false, skipped = false;
			for (const auto& record : records) {
				Require(record[6] == 0 && record[7] == 0, "Guarded proof reported base-only bias work");
				reused |= record[11] != 0;
				skipped |= record[9] != 0;
			}
			Require(reused && skipped, "Region oracle cases did not exercise lazy reuse and contained clipping planes");
		}
	}

	void CoversAllReductionPixels(ID3D11Device* device, ID3D11DeviceContext* context, bool reversedDepth, UINT eyeWidth, UINT eyeHeight, bool includeInvalidDepth, UINT reduction = 4)
	{
		Fixture fixture(device, context, reversedDepth, eyeWidth, eyeHeight, reduction);
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight);
		for (UINT y = 0; y < fixture.sourceHeight; ++y)
			for (UINT x = 0; x < fixture.sourceWidth; ++x)
				pixels[y * fixture.sourceWidth + x] = (1 + ((x * 17 + y * 31) % 97)) / 100.0f;
		if (includeInvalidDepth) {
			pixels[5 * fixture.sourceWidth + 3] = 0.0f;
			pixels[7 * fixture.sourceWidth + eyeWidth + 3] = std::numeric_limits<float>::quiet_NaN();
		}
		fixture.Build(pixels);
		const auto paddedPixelWidth = fixture.testConstants.pyramid.width * fixture.testConstants.pyramid.sourceReduction;
		const auto paddedPixelHeight = fixture.testConstants.pyramid.height * fixture.testConstants.pyramid.sourceReduction;
		for (UINT eye = 0; eye < 2; ++eye) {
			for (UINT mip = 0; mip < fixture.testConstants.pyramid.mipCount; ++mip) {
				const auto actual = fixture.ReadMip(eye, mip);
				const auto width = std::max(fixture.testConstants.pyramid.width >> mip, 1u);
				const auto height = std::max(fixture.testConstants.pyramid.height >> mip, 1u);
				const auto coverage = fixture.testConstants.pyramid.sourceReduction << mip;
				for (UINT y = 0; y < height; ++y) {
					for (UINT x = 0; x < width; ++x) {
						float expected = reversedDepth ? 1.0f : 0.0f;
						// A one-texel mip axis repeats existing coverage, without inventing padding.
						for (UINT sy = y * coverage; sy < std::min((y + 1) * coverage, paddedPixelHeight); ++sy) {
							for (UINT sx = x * coverage; sx < std::min((x + 1) * coverage, paddedPixelWidth); ++sx) {
								float sample = fixture.EncodeDepth(1.0f);
								if (sx < eyeWidth && sy < eyeHeight) {
									sample = fixture.EncodeDepth(pixels[sy * fixture.sourceWidth + eye * eyeWidth + sx]);
									if (!std::isfinite(sample) || sample <= 0.0f || sample > 1.0f)
										sample = fixture.EncodeDepth(1.0f);
								}
								expected = reversedDepth ? std::min(expected, sample) : std::max(expected, sample);
							}
						}
						Require(actual[y * width + x] == expected, "Mip lost a source pixel, mixed eyes, or mishandled padding/masks");
					}
				}
			}
		}
	}

	void CoversVisibilityAndFailures(Fixture& fixture)
	{
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		std::array objects{ Box(), Box(0, 0, 0.2f), Box(0, 0, 0.45f), Box(0, 0, 0.02f),
			Box(0, 0, 0.98f), Box(2.0f, 0), Box(), Box(0.94f, 0), Box(0, 0.94f), Box() };
		objects[6].entry[0][0] = std::numeric_limits<float>::quiet_NaN();
		objects[9].entry[3][0] = 0.25f;
		fixture.Build(pixels);
		Require(fixture.Test(objects) == std::vector<std::uint32_t>{ 0, 1, 1, 1, 0, 1, 1, 1, 1, 1 },
			"Depth direction, equality, clipping, uncovered guard margins or invalid bounds failed");

		for (UINT y = 0; y < fixture.sourceHeight; ++y)
			for (UINT x = fixture.sourceWidth / 2; x < fixture.sourceWidth; ++x)
				pixels[y * fixture.sourceWidth + x] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(std::span(objects).first(1))[0] == 1, "One visible eye must retain the object");
		for (const float invalid : { 0.0f, 1.0f, -1.0f, 2.0f, std::numeric_limits<float>::infinity() }) {
			std::fill(pixels.begin(), pixels.end(), invalid);
			fixture.Build(pixels);
			Require(fixture.Test(std::span(objects).first(1))[0] == 1, "Clear, masked or invalid depth rejected an object");
		}
	}

	void CoversMixedStereoVisibility(Fixture& fixture)
	{
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		pixels[16 * fixture.sourceWidth + 8] = 1.0f;
		pixels[16 * fixture.sourceWidth + 32 + 16] = 1.0f;
		const std::array objects{ Box(-0.5f, 0), Box(), Box(0.5f, 0) };
		fixture.Build(pixels);
		Require(fixture.Test(objects) == std::vector<std::uint32_t>{ 1, 1, 0 },
			"Mixed first-eye, second-eye and occluded objects lost independent stereo results");
	}

	void SeparatesViewportRetentionReasons(Fixture& fixture)
	{
		const std::array objects{ Box(2.0f), Box(0.98f), Box(0.94f), Box() };
		fixture.Build(std::vector<float>(fixture.sourceWidth * fixture.sourceHeight, 0.4f));
		Require(fixture.Test(objects) == std::vector<std::uint32_t>{ 1, 1, 1, 0 },
			"Viewport reason classification changed visibility");
		Require(fixture.lastDiagnostics[0][0] == 9 && fixture.lastDiagnostics[1][0] == 10 &&
					fixture.lastDiagnostics[2][0] == 3 && fixture.lastDiagnostics[3][0] == 257,
			"Wholly offscreen, partial-viewport and guard-only cases share a diagnostic reason");
	}

	void BiasRetainsTouchingBounds(Fixture& fixture)
	{
		const auto original = fixture.testConstants.depthBias;
		fixture.testConstants.depthBias = 0.125f;
		const std::array objects{ Box(0, 0, 0.625f, 0.125f), Box(0, 0, 0.5f, 0.125f), Box(0, 0, 0.75f, 0.125f) };
		fixture.Build(std::vector<float>(fixture.sourceWidth * fixture.sourceHeight, 0.375f));
		Require(fixture.Test(objects) == std::vector<std::uint32_t>{ 1, 1, 0 },
			"Bias must retain touching or nearer bounds and reject only strictly farther bounds");
		fixture.testConstants.depthBias = original;
	}

	void CoversEveryOverlappingCell(Fixture& fixture)
	{
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		auto object = Box();
		object.entry[0][0] = 0.4f;
		object.entry[1][1] = 0.4f;
		const std::array objects{ object };
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "Solid occluder did not reject a covered box");
		// A clear pixel anywhere inside the box defeats an occlusion proof.
		for (UINT eye = 0; eye < 2; ++eye) {
			for (UINT y = 10; y <= 22; y += 3) {
				for (UINT x = 10; x <= 22; x += 3) {
					const auto offset = y * fixture.sourceWidth + eye * 32 + x;
					pixels[offset] = 1.0f;
					fixture.Build(pixels);
					if (fixture.Test(objects)[0] != 1)
						throw std::runtime_error("An overlapping Hi-Z cell was omitted: eye=" + std::to_string(eye) +
												 " x=" + std::to_string(x) + " y=" + std::to_string(y));
					pixels[offset] = 0.4f;
				}
			}
		}
	}

	void CoversShearedCornerExtents(Fixture& fixture)
	{
		auto object = Box();
		object.entry[0][0] = 0.25f;
		object.entry[0][1] = 0.30f;
		object.entry[0][2] = 0.05f;
		object.entry[1][0] = 0.15f;
		object.entry[1][1] = 0.30f;
		object.entry[1][2] = -0.05f;
		object.entry[2][0] = 0.03f;
		object.entry[2][1] = -0.02f;
		object.entry[2][2] = 0.04f;
		const std::array objects{ object };
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 0, "A valid sheared OBB was not tested against a solid occluder");
		// This pixel lies near a projected corner, beyond the diagonal-only bound.
		pixels[9 * fixture.sourceWidth + 24] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 1, "Off-diagonal OBB axes lost visibility near an extremal corner");
	}

	void CoversPerspectiveAndCameraAdjustment(Fixture& fixture)
	{
		const auto original = fixture.testConstants;
		for (UINT eye = 0; eye < 2; ++eye) {
			auto& matrix = fixture.testConstants.viewProjection[eye];
			for (auto& row : matrix)
				for (auto& value : row)
					value = 0.0f;
			matrix[0][0] = matrix[1][1] = 1.0f;
			matrix[2][2] = 100.0f / 99.9f;
			matrix[2][3] = -10.0f / 99.9f;
			matrix[3][2] = 1.0f;
			fixture.testConstants.cameraAdjust[eye][0] = 10.0f;
			fixture.testConstants.cameraAdjust[eye][1] = 20.0f;
			fixture.testConstants.cameraAdjust[eye][2] = 30.0f;
		}
		fixture.testConstants.viewProjection[1][0][2] = 1.0f;
		fixture.testConstants.cameraAdjust[1][0] = 11.0f;
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		const std::array objects{ Box(10, 20, 32), Box(10, 20, 30.02f) };
		fixture.Build(pixels);
		Require(fixture.Test(objects) == std::vector<std::uint32_t>{ 0, 1 }, "Perspective depth or eye-plane clipping failed");
		for (UINT y = 15; y <= 17; ++y)
			for (UINT x = 23; x <= 25; ++x)
				pixels[y * fixture.sourceWidth + 32 + x] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(objects)[0] == 1, "The second eye camera adjustment or asymmetric projection was ignored");
		fixture.testConstants = original;
	}

	void PreservesSmallBoundsAtLargeWorldCoordinates(Fixture& fixture)
	{
		const auto original = fixture.testConstants;
		for (auto& adjustment : fixture.testConstants.cameraAdjust)
			adjustment[0] = 100000000.0f;
		auto object = Box(100000000.0f);
		object.entry[0][0] = 0.25f;
		std::vector<float> pixels(fixture.sourceWidth * fixture.sourceHeight, 0.4f);
		pixels[16 * fixture.sourceWidth + 20] = 1.0f;
		fixture.Build(pixels);
		Require(fixture.Test(std::array{ object })[0] == 1,
			"World-coordinate cancellation shrank a projected box past a visible pixel");
		fixture.testConstants = original;
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &requested, 1,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		for (const bool reversedDepth : { false, true }) {
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 17, 13, true);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 32, 16, false);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 16, 32, false);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 4, 4, false);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 8, 4, false);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 17, 13, true, 2);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 2, 4, false, 2);
			CoversAllReductionPixels(device.Get(), context.Get(), reversedDepth, 4, 2, false, 2);
			Fixture fixture(device.Get(), context.Get(), reversedDepth);
			RetainsUnavailableRefinementSource(fixture);
			ChecksProjectedRegionShortcuts(fixture);
			ChecksGuardedVertexDepthProofs(fixture);
			ChecksSeparatingEdges(fixture);
			ChecksDirectIntersectionDepth(fixture);
			ChecksFarClipAB(device.Get(), context.Get(), reversedDepth);
			CoversVisibilityAndFailures(fixture);
			CoversMixedStereoVisibility(fixture);
			SeparatesViewportRetentionReasons(fixture);
			BiasRetainsTouchingBounds(fixture);
			CoversEveryOverlappingCell(fixture);
			CoversShearedCornerExtents(fixture);
			CoversPerspectiveAndCameraAdjustment(fixture);
			PreservesSmallBoundsAtLargeWorldCoordinates(fixture);
			RefinesPastPaddedEyeCells(device.Get(), context.Get(), reversedDepth);
			PreservesRefinedFootprintAndStereo(fixture);
			RefinesThinRectangles(fixture);
			RefinesOnlyUnresolvedCells(device.Get(), context.Get(), reversedDepth);
			RetainsFinerOccluderDetail(device.Get(), context.Get(), reversedDepth);
			ChecksSourceRefinementAB(device.Get(), context.Get(), reversedDepth);
			RetainsNearestVertexBeforeRefinement(device.Get(), context.Get(), reversedDepth);
			ExhaustsActualLoadBudget(device.Get(), context.Get(), reversedDepth);
			TraversesMaximumMipDepth(device.Get(), context.Get(), reversedDepth);
			ChecksRefinedProofsAgainstSourcePixels(fixture);
			ExcludesEmptyProjectedCorners(fixture);
			UsesLocalFaceDepth(fixture);
			PreservesFaceProofsAcrossAxisPermutations(fixture);
			RetainsLocalFaceBiasInEitherEye(fixture);
			ChecksFaceProofsAgainstRays(fixture);
			Fixture fine(device.Get(), context.Get(), reversedDepth, 32, 32, 2);
			Fixture intersectionAB(device.Get(), context.Get(), reversedDepth, 32, 32, 2, true);
			for (const UINT controls : { 0u, 2u }) {
				intersectionAB.testConstants.reserved = controls;
				ExcludesEmptyProjectedCorners(intersectionAB);
				UsesLocalFaceDepth(intersectionAB);
				PreservesFaceProofsAcrossAxisPermutations(intersectionAB);
				ChecksFaceProofsAgainstRays(intersectionAB);
				CoversVisibilityAndFailures(intersectionAB);
			}
			CoversVisibilityAndFailures(fine);
			CoversMixedStereoVisibility(fine);
			BiasRetainsTouchingBounds(fine);
			CoversEveryOverlappingCell(fine);
			CoversPerspectiveAndCameraAdjustment(fine);
			ChecksRefinedProofsAgainstSourcePixels(fine);
			ChecksFaceProofsAgainstRays(fine);
			SeparatesViewportRetentionReasons(fine);
			CoversShearedCornerExtents(fine);
			PreservesSmallBoundsAtLargeWorldCoordinates(fine);
			PreservesRefinedFootprintAndStereo(fine);
			RefinesThinRectangles(fine);
			ExcludesEmptyProjectedCorners(fine);
			UsesLocalFaceDepth(fine);
			PreservesFaceProofsAcrossAxisPermutations(fine);
			RetainsLocalFaceBiasInEitherEye(fine);
			std::cout << "Hi-Z WARP tests passed (" << (reversedDepth ? "reversed test ordering" : "standard ordering")
					  << "): mip coverage, bounded face refinement, source-pixel and 3D ray oracles, stereo, bias, perspective and failure fallback\n";
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
