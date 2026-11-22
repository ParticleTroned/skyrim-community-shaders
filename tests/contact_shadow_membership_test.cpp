#include "ShaderPackageIncludes.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

using D3D11ShaderTest::Check;
using Microsoft::WRL::ComPtr;

namespace
{
	constexpr UINT clusterCount = 3, visibleCapacity = 256, candidateCapacity = 8, lightCapacity = 1024;
	constexpr UINT memberBit = 0x80000000u, particleFlag = 1u << 12, simpleFlag = 1u << 2;

	struct Light
	{
		float color[3], fade, radius, invRadius, fadeZone, sizeBias;
		float position[2][4];
		UINT roomFlags[4], flags, shadowIndex, spotlightIndex;
		float spotlightFalloff;
	};
	struct Cluster
	{
		float minimum[4], maximum[4];
	};
	struct Grid
	{
		UINT offset, count, padding[2];
	};
	static_assert(sizeof(Light) == 96 && sizeof(Cluster) == 32 && sizeof(Grid) == 16);

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Buffer
	{
		ComPtr<ID3D11Buffer> gpu, staging;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;

		Buffer(ID3D11Device* device, UINT stride, UINT count, bool output, const char* name)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = stride * count;
			desc.StructureByteStride = stride;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.BindFlags = output ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateBuffer(&desc, nullptr, &gpu));
			Util::SetResourceName(gpu.Get(), "ContactShadowTest::%s", name);
			if (output) {
				Check(device->CreateUnorderedAccessView(gpu.Get(), nullptr, &uav));
				Util::SetResourceName(uav.Get(), "ContactShadowTest::%s UAV", name);
				desc.BindFlags = 0;
				desc.MiscFlags = 0;
				desc.StructureByteStride = 0;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Check(device->CreateBuffer(&desc, nullptr, &staging));
				Util::SetResourceName(staging.Get(), "ContactShadowTest::%s Readback", name);
			} else {
				Check(device->CreateShaderResourceView(gpu.Get(), nullptr, &srv));
				Util::SetResourceName(srv.Get(), "ContactShadowTest::%s SRV", name);
			}
		}

		template <class T>
		std::vector<T> Read(ID3D11DeviceContext* context, size_t count)
		{
			std::vector<T> result(count);
			context->CopyResource(staging.Get(), gpu.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::memcpy(result.data(), mapped.pData, count * sizeof(T));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	struct Result
	{
		std::array<std::vector<UINT>, clusterCount> entries, candidates;
	};

	struct Kernel
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<D3D11ShaderTest::ConstantBuffer> frame, camera, feature;
		Buffer lights, clusters, visible, grid, counter, candidates, contactGrid, preparedColours;

		Kernel(ID3D11Device* device, const std::filesystem::path& path, bool vr) :
			lights(device, sizeof(Light), lightCapacity, false, "Lights"),
			clusters(device, sizeof(Cluster), clusterCount, false, "Clusters"),
			visible(device, sizeof(UINT), clusterCount * visibleCapacity, true, "Visible"),
			grid(device, sizeof(Grid), clusterCount, true, "Grid"),
			counter(device, sizeof(UINT), 1, true, "Counter"),
			candidates(device, sizeof(UINT), clusterCount * candidateCapacity, true, "Candidates"),
			contactGrid(device, sizeof(Grid), clusterCount, true, "ContactGrid"),
			preparedColours(device, sizeof(float) * 8, lightCapacity, true, "PreparedColours")
		{
			auto includes = PackageIncludes::AllFeatures(".");
			const D3D_SHADER_MACRO defines[]{ { "CSHADER", "1" }, { vr ? "VR" : nullptr, "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> code, errors;
			const HRESULT compiled = D3DCompileFromFile(path.c_str(), defines, &includes, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0, &code, &errors);
			if (FAILED(compiled) && errors)
				throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
			Check(compiled);
			if (errors)
				std::cout << static_cast<const char*>(errors->GetBufferPointer());
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "ContactShadowTest::ClusterCull");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			D3D11_SHADER_DESC description{};
			Check(reflection->GetDesc(&description));
			std::cout << (vr ? "VR " : "SE ") << path.filename().string()
					  << ": temps=" << description.TempRegisterCount << ", arrays=" << description.TempArrayCount
					  << ", instructions=" << description.InstructionCount << '\n';
			for (UINT i = 0; i < description.ConstantBuffers; ++i) {
				D3D11_SHADER_BUFFER_DESC buffer{};
				Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
				if (buffer.Type != D3D_CT_CBUFFER)
					continue;
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				Check(reflection->GetResourceBindingDescByName(buffer.Name, &binding));
				if (binding.BindPoint == 0)
					frame = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device, reflection.Get(), buffer.Name);
				if (binding.BindPoint == 12)
					camera = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device, reflection.Get(), buffer.Name);
				if (binding.BindPoint == 6)
					feature = std::make_unique<D3D11ShaderTest::ConstantBuffer>(device, reflection.Get(), buffer.Name);
			}
			Require(frame && camera && feature, "Production constant buffers missing");
			const std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			if (vr)
				camera->SetVariable("FrameBuffer::CameraView", std::array{ identity, identity });
			else
				camera->SetVariable("FrameBuffer::CameraView", identity);
			frame->SetVariable("ClusterSize", std::array<UINT, 4>{ clusterCount, 1, 1, 0 });
		}

		Result Run(ID3D11DeviceContext* context, std::span<const Light> input, UINT flags, UINT budget, UINT particleBudget, bool prepareColours)
		{
			Require(input.size() <= lightCapacity, "Too many input lights");
			std::array<Light, lightCapacity> upload{};
			std::copy(input.begin(), input.end(), upload.begin());
			const std::array<Cluster, clusterCount> bounds{
				Cluster{ { -1, -1, -1, 0 }, { 1, 1, 1, 0 } },
				Cluster{ { 7, -1, -1, 0 }, { 9, 1, 1, 0 } },
				Cluster{ { 999, -1, -1, 0 }, { 1001, 1, 1, 0 } }
			};
			context->UpdateSubresource(lights.gpu.Get(), 0, nullptr, upload.data(), 0, 0);
			context->UpdateSubresource(clusters.gpu.Get(), 0, nullptr, bounds.data(), 0, 0);
			// Keep list storage between dispatches to expose stale membership flags.
			const UINT zero[4]{};
			context->ClearUnorderedAccessViewUint(counter.uav.Get(), zero);
			frame->SetVariable("LightCount", static_cast<UINT>(input.size()));
			frame->SetVariable("ContactShadowFlagsPacked", flags);
			frame->SetVariable("ContactShadowParamsPacked", (budget << 16) | (particleBudget << 8));
			frame->SetVariable("PreparePointLightColours", UINT(prepareColours));
			frame->Bind(context);
			camera->Bind(context);
			feature->Bind(context);
			ID3D11ShaderResourceView* srvs[]{ clusters.srv.Get(), lights.srv.Get() };
			ID3D11UnorderedAccessView* uavs[]{ prepareColours ? preparedColours.uav.Get() : nullptr,
				visible.uav.Get(), grid.uav.Get(), counter.uav.Get(), candidates.uav.Get(), contactGrid.uav.Get() };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(0, 6, uavs, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			context->ClearState();
			const auto visibleEntries = visible.Read<UINT>(context, clusterCount * visibleCapacity);
			const auto lightGrid = grid.Read<Grid>(context, clusterCount);
			const auto candidateEntries = candidates.Read<UINT>(context, clusterCount * candidateCapacity);
			const auto shadowGrid = contactGrid.Read<Grid>(context, clusterCount);
			UINT total = 0;
			Result result;
			for (UINT c = 0; c < clusterCount; ++c) {
				Require(lightGrid[c].offset == c * visibleCapacity && lightGrid[c].count <= visibleCapacity, "Invalid visible range");
				Require(shadowGrid[c].count <= std::min(budget, candidateCapacity) &&
							shadowGrid[c].offset <= candidateEntries.size() &&
							shadowGrid[c].count <= candidateEntries.size() - shadowGrid[c].offset,
					"Invalid candidate range");
				result.entries[c].assign(visibleEntries.begin() + lightGrid[c].offset, visibleEntries.begin() + lightGrid[c].offset + lightGrid[c].count);
				result.candidates[c].assign(candidateEntries.begin() + shadowGrid[c].offset, candidateEntries.begin() + shadowGrid[c].offset + shadowGrid[c].count);
				total += shadowGrid[c].count;
			}
			Require(counter.Read<UINT>(context, 1)[0] == total, "Candidate allocation count differs");
			return result;
		}
	};

	Light MakeLight(float brightness = 1, UINT flags = 0, float x = 0, float radius = 2)
	{
		Light light{};
		std::fill_n(light.color, 3, brightness);
		light.fade = 1;
		light.radius = radius;
		light.flags = flags;
		light.position[0][0] = light.position[1][0] = x;
		return light;
	}

	void Validate(const Result& actual, const std::optional<Result>& baseline, size_t lightCount)
	{
		for (UINT c = 0; c < clusterCount; ++c) {
			std::vector<UINT> decoded, marked;
			for (UINT entry : actual.entries[c]) {
				const UINT index = entry & ~memberBit;
				Require(index < lightCount, "Decoded index outside global light buffer");
				decoded.push_back(index);
				if (entry & memberBit)
					marked.push_back(index);
			}
			auto selected = actual.candidates[c];
			std::ranges::sort(selected);
			Require(marked == selected, "Membership differs from final selected candidates");
			Require(std::ranges::is_sorted(decoded) && std::adjacent_find(decoded.begin(), decoded.end()) == decoded.end(), "Visible order changed");
			if (baseline) {
				Require(decoded == baseline->entries[c], "Visible indices/order differ from baseline production shader");
				Require(actual.candidates[c] == baseline->candidates[c], "Candidate selection/order differ from baseline production shader");
			}
		}
	}

	void CheckEntryHelpers(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		constexpr char source[] = R"(
#include "LightLimitFix/Common.hlsli"
RWStructuredBuffer<uint2> result : register(u0);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    uint entry = (id.x / 2) | ((id.x & 1) != 0 ? ClusteredContactShadowMemberBit : 0);
    result[id.x] = uint2(DecodeClusteredLightIndex(entry), HasContactShadowMembership(entry));
})";
		PackageIncludes includes("package/Shaders", "features/Light Limit Fix/Shaders");
		ComPtr<ID3DBlob> code, errors;
		const auto status = D3DCompile(source, sizeof(source) - 1, "cluster-entry-helpers", nullptr, &includes,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, &code, &errors);
		if (FAILED(status) && errors)
			throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
		Check(status);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "ContactShadowTest::DecodeEntries");
		Buffer output(device, sizeof(UINT) * 2, lightCapacity * 2, true, "DecodedEntries");
		ID3D11UnorderedAccessView* view = output.uav.Get();
		context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(lightCapacity * 2 / 64, 1, 1);
		context->ClearState();
		const auto decoded = output.Read<std::array<UINT, 2>>(context, lightCapacity * 2);
		for (UINT i = 0; i < decoded.size(); ++i)
			Require(decoded[i] == std::array<UINT, 2>{ i / 2, i & 1 }, "Production entry helper lost an index or membership bit");
		std::cout << "2048 production entry helper cases passed\n";
	}

	void RunCases(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, const std::filesystem::path& baselinePath)
	{
		Kernel current(device, "features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl", vr);
		std::unique_ptr<Kernel> baseline;
		if (!baselinePath.empty())
			baseline = std::make_unique<Kernel>(device, baselinePath, vr);
		unsigned cases = 0;
		auto run = [&](const std::vector<Light>& lights, UINT flags, UINT budget, UINT particleBudget) {
			Result actual, unprepared;
			for (bool prepareColours : { false, true }) {
				actual = current.Run(context, lights, flags, budget, particleBudget, prepareColours);
				if (flags == 0 || budget == 0)
					for (const auto& selected : actual.candidates)
						Require(selected.empty(), "Disabled contacts retained candidates");
				std::optional<Result> prior;
				if (baseline)
					prior = baseline->Run(context, lights, flags, budget, particleBudget, prepareColours);
				Validate(actual, prior, lights.size());
				if (prepareColours)
					Require(actual.entries == unprepared.entries && actual.candidates == unprepared.candidates,
						"Colour preparation changed contact selection or membership");
				else
					unprepared = actual;
				++cases;
			}
			return actual;
		};

		std::vector<Light> mixed;
		for (UINT i = 0; i < 24; ++i)
			mixed.push_back(MakeLight(float(i / 2 + 1), i % 3 == 0 ? particleFlag : 0));
		mixed.push_back(MakeLight(1e-6f));
		mixed.push_back(MakeLight(100, simpleFlag));
		for (UINT budget : { 0u, 1u, 4u, 8u, 255u })
			for (UINT particleBudget : { 0u, 1u, 2u, 8u })
				for (UINT flags : { 0u, 1u, 2u, 3u })
					run(mixed, flags, budget, particleBudget);

		Require(run({ MakeLight(), MakeLight(), MakeLight(2) }, 1, 1, 0).candidates[0] == std::vector<UINT>{ 2 }, "Weakest replacement failed");
		Require(run({ MakeLight(), MakeLight() }, 1, 1, 0).candidates[0] == std::vector<UINT>{ 0 }, "Tie order changed");
		Require(run({ MakeLight(1, particleFlag), MakeLight(3, particleFlag), MakeLight(2, particleFlag) }, 3, 4, 1).candidates[0] == std::vector<UINT>{ 1 }, "Particle quota replacement failed");
		Require(run({ MakeLight(1e-6f), MakeLight(100, simpleFlag) }, 1, 8, 8).candidates[0].empty(), "Ineligible light selected");

		std::vector<Light> full(256, MakeLight());
		full.back() = MakeLight(100);
		auto boundary = run(full, 1, 1, 0);
		Require(boundary.entries[0].size() == 256 && boundary.candidates[0] == std::vector<UINT>{ 255 }, "256th visible light was not scored");
		full.push_back(MakeLight(1000));
		Require(run(full, 1, 1, 0).candidates[0] == std::vector<UINT>{ 255 }, "Visible cap changed");
		std::vector<Light> high(lightCapacity, MakeLight(1, 0, 10000));
		for (UINT index : { 256u, 511u, 1023u })
			high[index] = MakeLight(float(index));
		Require(run(high, 1, 4, 0).candidates[0] == std::vector<UINT>({ 256, 511, 1023 }), "High global indices changed");
		run(high, 0, 4, 0);
		run(full, 1, 8, 0);
		run({ MakeLight() }, 1, 1, 0);
		run({}, 1, 8, 0);

		auto local = run({ MakeLight(1, 0, 0, 10), MakeLight(1, 0, 8, 10) }, 1, 1, 0);
		Require(local.candidates[0] == std::vector<UINT>{ 0 } && local.candidates[1] == std::vector<UINT>{ 1 }, "Membership must be local to each cluster");
		auto rightOnly = MakeLight(3, 0, 10000);
		rightOnly.position[1][0] = 0;
		auto leftOnly = MakeLight(2);
		leftOnly.position[1][0] = 10000;
		auto eyes = run({ leftOnly, rightOnly }, 1, 1, 0);
		Require(eyes.entries[0].size() == (vr ? 2u : 1u) && eyes.candidates[0] == std::vector<UINT>{ vr ? 1u : 0u }, "Eye union/max score changed");

		std::mt19937 random(0xC011AC7u);
		for (UINT iteration = 0; iteration < 32; ++iteration) {
			std::vector<Light> lights;
			for (UINT i = 0; i < lightCapacity; ++i) {
				auto light = MakeLight(float(random() % 16), random() % 3 == 0 ? particleFlag : 0, float(random() % 40) - 20, float(random() % 6));
				light.position[1][0] = float(random() % 40) - 20;
				lights.push_back(light);
			}
			run(lights, random() % 4, random() % 9, random() % 9);
		}
		std::cout << (vr ? "VR" : "SE") << ": " << cases << " production dispatch cases passed"
				  << (baseline ? " with baseline parity\n" : "\n");
	}
}

int main(int argc, char** argv)
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context));
		CheckEntryHelpers(device.Get(), context.Get());
		for (bool vr : { false, true })
			RunCases(device.Get(), context.Get(), vr, argc > 1 ? argv[1] : "");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
