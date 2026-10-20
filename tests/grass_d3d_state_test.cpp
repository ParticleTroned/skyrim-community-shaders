#include "Features/GrassOptimizations/GrassD3DState.h"
#include "d3d_resource_naming.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <d3dcompiler.h>
#include <stdexcept>
#include <wrl/client.h>

namespace
{
	using Microsoft::WRL::ComPtr;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	struct UnwindProbe
	{};
	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext1> context;
		std::array<ComPtr<ID3D11Buffer>, 5> constants;
		std::array<ComPtr<ID3D11ShaderResourceView>, 4> srvs;
		std::array<ComPtr<ID3D11UnorderedAccessView>, 15> uavs;
		UINT availableUavs;
		ComPtr<ID3D11UnorderedAccessView> overlappingUav;
		ComPtr<ID3D11ComputeShader> shader;
		std::array<ComPtr<ID3D11Buffer>, 3> vertices;
		ComPtr<ID3D11Buffer> indices;
		ComPtr<ID3D11InputLayout> layout;

		ComPtr<ID3D11Buffer> Buffer(UINT flags, UINT index)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 1024;
			desc.BindFlags = flags;
			ComPtr<ID3D11Buffer> buffer;
			Require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)), "Create buffer");
			Util::SetResourceName(buffer.Get(), "GrassStateTest::Buffer%u-%u", flags, index);
			return buffer;
		}
		ComPtr<ID3D11Texture2D> Texture(UINT index)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 4;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			ComPtr<ID3D11Texture2D> texture;
			Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "Create texture");
			Util::SetResourceName(texture.Get(), "GrassStateTest::Texture%u", index);
			return texture;
		}
		explicit Fixture(D3D_FEATURE_LEVEL level) : availableUavs(level >= D3D_FEATURE_LEVEL_11_1 ? 15u : 8u)
		{
			ComPtr<ID3D11DeviceContext> base;
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
						D3D11_SDK_VERSION, &device, nullptr, &base)),
				"Create WARP device at requested feature level");
			Require(SUCCEEDED(base.As(&context)), "Query 11.1 context");
			for (UINT i = 0; i < constants.size(); ++i)
				constants[i] = Buffer(D3D11_BIND_CONSTANT_BUFFER, i);
			for (UINT i = 0; i < uavs.size(); ++i) {
				auto texture = Texture(i);
				Require(SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uavs[i])), "Create UAV");
				Util::SetResourceName(uavs[i].Get(), "GrassStateTest::UAV%u", i);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto texture = Texture(100 + i);
				Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &srvs[i])), "Create SRV");
				Util::SetResourceName(srvs[i].Get(), "GrassStateTest::SRV%u", i);
				if (i == 0) {
					Require(SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &overlappingUav)), "Create overlapping UAV");
					Util::SetResourceName(overlappingUav.Get(), "GrassStateTest::OverlappingUAV");
				}
			}
			ComPtr<ID3DBlob> bytecode;
			constexpr char cs[] = "[numthreads(1,1,1)] void main() {}";
			Require(SUCCEEDED(D3DCompile(cs, sizeof(cs), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &bytecode, nullptr)), "Compile CS");
			Require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Create CS");
			Util::SetResourceName(shader.Get(), "GrassStateTest::ComputeShader");
			bytecode.Reset();
			constexpr char vs[] = "float4 main(float3 pos:POSITION):SV_Position {return float4(pos,1);}";
			Require(SUCCEEDED(D3DCompile(vs, sizeof(vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &bytecode, nullptr)), "Compile VS");
			const D3D11_INPUT_ELEMENT_DESC element{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			Require(SUCCEEDED(device->CreateInputLayout(&element, 1, bytecode->GetBufferPointer(), bytecode->GetBufferSize(), &layout)), "Create layout");
			Util::SetResourceName(layout.Get(), "GrassStateTest::Layout");
			for (UINT i = 0; i < vertices.size(); ++i)
				vertices[i] = Buffer(D3D11_BIND_VERTEX_BUFFER, i);
			indices = Buffer(D3D11_BIND_INDEX_BUFFER, 0);
		}
	};

	void CheckCompute(Fixture& f, UINT requested, bool holes, bool unwind)
	{
		auto* c = f.context.Get();
		c->ClearState();
		const UINT used = std::min(requested, 14u);
		std::array<ID3D11Buffer*, 5> buffers{};
		std::array<UINT, 5> first{}, count{};
		for (UINT i = 0; i < buffers.size(); ++i) {
			buffers[i] = holes && i == 2 ? nullptr : f.constants[i].Get();
			first[i] = i % 4 * 16;
			count[i] = 16;
			if (i == 0)
				c->CSSetConstantBuffers(i, 1, &buffers[i]);
			else
				c->CSSetConstantBuffers1(i, 1, &buffers[i], &first[i], &count[i]);
			ComPtr<ID3D11Buffer> actual;
			c->CSGetConstantBuffers1(i, 1, &actual, &first[i], &count[i]);
		}
		for (UINT i = 0; i < f.srvs.size(); ++i) {
			auto* view = holes && i == 1 ? nullptr : f.srvs[i].Get();
			c->CSSetShaderResources(i, 1, &view);
		}
		for (UINT i = 0; i < f.availableUavs; ++i) {
			auto* view = holes && i % 3 == 1 ? nullptr : f.uavs[i].Get();
			c->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
		}
		c->CSSetShader(f.shader.Get(), nullptr, 0);
		try {
			D3DState::ComputeState restore(c, requested);
			std::array<ID3D11ShaderResourceView*, 3> emptySRVs{};
			c->CSSetShaderResources(0, 3, emptySRVs.data());
			std::array<ID3D11UnorderedAccessView*, 14> emptyUAVs{};
			c->CSSetUnorderedAccessViews(0, used, emptyUAVs.data(), nullptr);
			if (used) {
				auto* overlap = f.overlappingUav.Get();
				c->CSSetUnorderedAccessViews(0, 1, &overlap, nullptr);
			}
			std::array<ID3D11Buffer*, 4> emptyBuffers{};
			c->CSSetConstantBuffers(0, 4, emptyBuffers.data());
			c->CSSetShader(nullptr, nullptr, 0);
			if (unwind)
				throw UnwindProbe{};
		} catch (const UnwindProbe&) {}
		for (UINT i = 0; i < buffers.size(); ++i) {
			ComPtr<ID3D11Buffer> actual;
			UINT begin = 0, length = 0;
			c->CSGetConstantBuffers1(i, 1, &actual, &begin, &length);
			Require(actual.Get() == buffers[i] && begin == first[i] && length == count[i], "Restore all CS buffer windows");
		}
		for (UINT i = 0; i < f.srvs.size(); ++i) {
			ComPtr<ID3D11ShaderResourceView> actual;
			c->CSGetShaderResources(i, 1, &actual);
			Require(actual.Get() == (holes && i == 1 ? nullptr : f.srvs[i].Get()), "Restore CS SRVs despite overlapping temporary UAV");
		}
		for (UINT i = 0; i < f.availableUavs; ++i) {
			ComPtr<ID3D11UnorderedAccessView> actual;
			c->CSGetUnorderedAccessViews(i, 1, &actual);
			Require(actual.Get() == (holes && i % 3 == 1 ? nullptr : f.uavs[i].Get()), "Restore CS UAVs and preserve adjacent slots");
		}
		ComPtr<ID3D11ComputeShader> shader;
		c->CSGetShader(&shader, nullptr, nullptr);
		Require(shader.Get() == f.shader.Get(), "Restore compute shader");
	}

	void CheckDraw(Fixture& f, bool holes, bool unwind)
	{
		auto* c = f.context.Get();
		c->ClearState();
		std::array<ID3D11Buffer*, 3> vertices{ f.vertices[0].Get(), holes ? nullptr : f.vertices[1].Get(), f.vertices[2].Get() };
		const std::array<UINT, 3> strides{ 12, 32, 16 }, offsets{ 4, 64, 128 };
		c->IASetVertexBuffers(0, 3, vertices.data(), strides.data(), offsets.data());
		c->IASetIndexBuffer(f.indices.Get(), DXGI_FORMAT_R16_UINT, 6);
		c->IASetInputLayout(f.layout.Get());
		c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		auto* cb = f.constants[0].Get();
		const UINT first = 16, count = 16;
		c->VSSetConstantBuffers1(9, 1, &cb, &first, &count);
		const std::array<ID3D11ShaderResourceView*, 3> srvs{ f.srvs[0].Get(), holes ? nullptr : f.srvs[1].Get(), f.srvs[2].Get() };
		c->VSSetShaderResources(2, UINT(srvs.size()), srvs.data());
		try {
			GrassD3D::DrawState restore(c);
			const std::array<ID3D11Buffer*, 2> changed{ f.vertices[1].Get(), f.vertices[0].Get() };
			c->IASetVertexBuffers(0, 2, changed.data(), strides.data(), offsets.data());
			c->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
			c->IASetInputLayout(nullptr);
			c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			ID3D11Buffer* emptyCB = nullptr;
			c->VSSetConstantBuffers(9, 1, &emptyCB);
			const std::array<ID3D11ShaderResourceView*, 2> changedSRVs{ f.srvs[2].Get(), f.srvs[0].Get() };
			c->VSSetShaderResources(2, UINT(changedSRVs.size()), changedSRVs.data());
			if (unwind)
				throw UnwindProbe{};
		} catch (const UnwindProbe&) {}
		for (UINT i = 0; i < vertices.size(); ++i) {
			ComPtr<ID3D11Buffer> actual;
			UINT stride = 0, offset = 0;
			c->IAGetVertexBuffers(i, 1, &actual, &stride, &offset);
			Require(actual.Get() == vertices[i] && stride == strides[i] && offset == offsets[i], "Restore vertex slots and preserve adjacent slot");
		}
		ComPtr<ID3D11Buffer> index, constants;
		DXGI_FORMAT format{};
		UINT offset = 0, begin = 0, length = 0;
		c->IAGetIndexBuffer(&index, &format, &offset);
		Require(index.Get() == f.indices.Get() && format == DXGI_FORMAT_R16_UINT && offset == 6, "Restore index buffer");
		c->VSGetConstantBuffers1(9, 1, &constants, &begin, &length);
		Require(constants.Get() == cb && begin == first && length == count, "Restore VS constant window");
		ComPtr<ID3D11InputLayout> layout;
		c->IAGetInputLayout(&layout);
		Require(layout.Get() == f.layout.Get(), "Restore layout");
		D3D11_PRIMITIVE_TOPOLOGY topology{};
		c->IAGetPrimitiveTopology(&topology);
		Require(topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "Restore topology");
		for (UINT i = 0; i < srvs.size(); ++i) {
			ComPtr<ID3D11ShaderResourceView> view;
			c->VSGetShaderResources(2 + i, 1, &view);
			Require(view.Get() == srvs[i], "Restore grass VS resources and preserve adjacent slot");
		}
	}
}

int main()
{
	try {
		for (auto level : { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 }) {
			Fixture fixture(level);
			for (bool holes : { false, true })
				for (bool unwind : { false, true }) {
					for (UINT used : { 0u, 1u, 3u, 4u })
						CheckCompute(fixture, used, holes, unwind);
					if (level >= D3D_FEATURE_LEVEL_11_1)
						for (UINT used : { 14u, 32u })
							CheckCompute(fixture, used, holes, unwind);
					CheckDraw(fixture, holes, unwind);
				}
		}
		std::puts("Grass D3D state: WARP restoration, hazards, windows, null slots and exception paths passed");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Grass D3D state: %s\n", error.what());
		return 1;
	}
}
