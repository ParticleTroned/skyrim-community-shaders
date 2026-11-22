#include "ShaderPackageIncludes.h"
#include "adaptive_balance_test_settings.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using D3D11ShaderTest::Check;
using Microsoft::WRL::ComPtr;

namespace
{
	constexpr UINT visibleCapacity = 256, candidateCapacity = 8, lightCapacity = 1024;
	constexpr UINT memberBit = 0x80000000u, particleFlag = 1u << 12, simpleFlag = 1u << 2;
	using Matrix = std::array<float, 16>;
	constexpr Matrix identityView{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

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
			if (!result.empty())
				std::memcpy(result.data(), mapped.pData, count * sizeof(T));
			context->Unmap(staging.Get(), 0);
			return result;
		}
	};

	struct Result
	{
		std::vector<std::vector<UINT>> entries, candidates;
		std::vector<std::array<UINT, 8>> colours;
	};

	ComPtr<ID3DBlob> Compile(const std::filesystem::path& path, bool vr, bool reversed = false, const char* entry = "main")
	{
		auto includes = PackageIncludes::AllFeatures(".");
		const D3D_SHADER_MACRO defines[]{ { "CSHADER", "1" }, { vr ? "VR" : "FLAT", "1" },
			{ reversed ? "REVERSE_Z" : "STANDARD_Z", "1" }, { nullptr, nullptr } };
		ComPtr<ID3DBlob> code, errors;
		const HRESULT compiled = D3DCompileFromFile(path.c_str(), defines, &includes, entry, "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, &code, &errors);
		if (FAILED(compiled) && errors)
			throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
		Check(compiled);
		if (errors)
			std::cout << static_cast<const char*>(errors->GetBufferPointer());
		return code;
	}

	void DispatchClusters(ID3D11DeviceContext* context, ID3D11ShaderReflection* reflection, std::array<UINT, 3> dimensions)
	{
		std::array<UINT, 3> threads{};
		reflection->GetThreadGroupSize(&threads[0], &threads[1], &threads[2]);
		std::array<UINT, 3> groups{};
		for (UINT axis = 0; axis < 3; ++axis) {
			Require(threads[axis] > 0 && dimensions[axis] > 0, "Invalid dispatch dimensions");
			groups[axis] = 1 + (dimensions[axis] - 1) / threads[axis];
		}
		context->Dispatch(groups[0], groups[1], groups[2]);
	}

	struct Kernel
	{
		const bool stereo;
		UINT clusterCount = 3;
		std::array<UINT, 3> dimensions{ 3, 1, 1 };
		std::vector<Cluster> bounds{
			Cluster{ { -1, -1, -1, 0 }, { 1, 1, 1, 0 } },
			Cluster{ { 7, -1, -1, 0 }, { 9, 1, 1, 0 } },
			Cluster{ { 999, -1, -1, 0 }, { 1001, 1, 1, 0 } }
		};
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<D3D11ShaderTest::ConstantBuffer> frame, camera, feature;
		Buffer lights, clusters, visible, grid, counter, candidates, contactGrid, preparedColours;

		Kernel(ID3D11Device* device, const std::filesystem::path& path, bool vr) :
			stereo(vr),
			lights(device, sizeof(Light), lightCapacity, false, "Lights"),
			clusters(device, sizeof(Cluster), clusterCount, false, "Clusters"),
			visible(device, sizeof(UINT), clusterCount * visibleCapacity, true, "Visible"),
			grid(device, sizeof(Grid), clusterCount, true, "Grid"),
			counter(device, sizeof(UINT), 1, true, "Counter"),
			candidates(device, sizeof(UINT), clusterCount * candidateCapacity, true, "Candidates"),
			contactGrid(device, sizeof(Grid), clusterCount, true, "ContactGrid"),
			preparedColours(device, sizeof(float) * 8, lightCapacity, true, "PreparedColours")
		{
			auto code = Compile(path, vr);
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "ContactShadowTest::ClusterCull");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			D3D11_SHADER_DESC description{};
			Check(reflection->GetDesc(&description));
			std::cout << (vr ? "VR " : "SE ") << path.filename().string()
					  << ": temps=" << description.TempRegisterCount << ", arrays=" << description.TempArrayCount
					  << ", instructions=" << description.InstructionCount << '\n';
			ComPtr<ID3DBlob> assembly;
			Check(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly));
			std::istringstream lines(static_cast<const char*>(assembly->GetBufferPointer()));
			std::string line;
			UINT sharedBytes = 0;
			while (std::getline(lines, line)) {
				if (line.starts_with("dcl_tgsm_structured")) {
					std::replace(line.begin(), line.end(), ',', ' ');
					std::istringstream declaration(line);
					std::string opcode, name;
					UINT stride = 0, count = 0;
					Require(bool(declaration >> opcode >> name >> stride >> count), "Cannot read shared memory declaration");
					sharedBytes += stride * count;
				} else if (line.starts_with("dcl_tgsm_raw")) {
					std::istringstream declaration(line.substr(line.find(',') + 1));
					UINT bytes = 0;
					Require(bool(declaration >> bytes), "Cannot read raw shared memory declaration");
					sharedBytes += bytes;
				}
			}
			Require(sharedBytes > 0 && sharedBytes <= 32768, "Invalid SM5 shared memory footprint");
			std::cout << "TGSM bytes=" << sharedBytes << '\n';
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
			feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", AdaptiveBalanceTest::NeutralAppearance());
			for (const char* name : { "pointLightCurve", "pointLightSaturation", "pointLightMult", "linearPointLightMult",
					 "spotlightMult", "linearSpotlightMult", "omnidirectionalBulbMult", "linearOmnidirectionalBulbMult" })
				feature->SetMember("SharedData::adaptiveBalanceSettings", name, 1.0f);
			feature->SetMember("SharedData::linearLightingSettings", "lightGamma", 2.2f);
			SetView(identityView, identityView);
			frame->SetVariable("ClusterSize", std::array<UINT, 4>{ clusterCount, 1, 1, 0 });
		}

		void SetView(const Matrix& left, const Matrix& right)
		{
			if (stereo)
				camera->SetVariable("FrameBuffer::CameraView", std::array{ left, right });
			else
				camera->SetVariable("FrameBuffer::CameraView", left);
		}

		void SetLayout(ID3D11Device* device, std::array<UINT, 3> size, std::vector<Cluster> inputBounds)
		{
			const UINT count = size[0] * size[1] * size[2];
			Require(count > 0 && inputBounds.size() == count, "Invalid test layout");
			if (count != clusterCount) {
				clusters = Buffer(device, sizeof(Cluster), count, false, "Clusters");
				visible = Buffer(device, sizeof(UINT), count * visibleCapacity, true, "Visible");
				grid = Buffer(device, sizeof(Grid), count, true, "Grid");
				candidates = Buffer(device, sizeof(UINT), count * candidateCapacity, true, "Candidates");
				contactGrid = Buffer(device, sizeof(Grid), count, true, "ContactGrid");
			}
			dimensions = size;
			clusterCount = count;
			bounds = std::move(inputBounds);
			frame->SetVariable("ClusterSize", std::array<UINT, 4>{ size[0], size[1], size[2], 0 });
		}

		Result Run(ID3D11DeviceContext* context, std::span<const Light> input, UINT flags, UINT budget, UINT particleBudget, bool prepareColours)
		{
			Require(input.size() <= lightCapacity, "Too many input lights");
			std::array<Light, lightCapacity> upload{};
			std::copy(input.begin(), input.end(), upload.begin());
			context->UpdateSubresource(lights.gpu.Get(), 0, nullptr, upload.data(), 0, 0);
			context->UpdateSubresource(clusters.gpu.Get(), 0, nullptr, bounds.data(), 0, 0);
			// Keep list storage between dispatches to expose stale membership flags.
			const UINT zero[4]{};
			context->ClearUnorderedAccessViewUint(counter.uav.Get(), zero);
			const UINT poison[4]{ 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
			context->ClearUnorderedAccessViewUint(preparedColours.uav.Get(), poison);
			frame->SetVariable("LightCount", static_cast<UINT>(input.size()));
			frame->SetVariable("ContactShadowFlagsPacked", flags);
			frame->SetVariable("ContactShadowParamsPacked", (budget << 16) | (particleBudget << 8));
			frame->SetVariable("PreparePointLightColours", UINT(prepareColours));
			frame->Bind(context);
			camera->Bind(context);
			feature->Bind(context);
			ID3D11ShaderResourceView* srvs[]{ clusters.srv.Get(), lights.srv.Get() };
			// A bound poisoned UAV detects writes even when colour preparation is disabled.
			ID3D11UnorderedAccessView* uavs[]{ preparedColours.uav.Get(),
				visible.uav.Get(), grid.uav.Get(), counter.uav.Get(), candidates.uav.Get(), contactGrid.uav.Get() };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(0, 6, uavs, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			DispatchClusters(context, reflection.Get(), dimensions);
			context->ClearState();
			const auto visibleEntries = visible.Read<UINT>(context, clusterCount * visibleCapacity);
			const auto lightGrid = grid.Read<Grid>(context, clusterCount);
			const auto candidateEntries = candidates.Read<UINT>(context, clusterCount * candidateCapacity);
			const auto shadowGrid = contactGrid.Read<Grid>(context, clusterCount);
			UINT total = 0;
			Result result;
			result.entries.resize(clusterCount);
			result.candidates.resize(clusterCount);
			result.colours = preparedColours.Read<std::array<UINT, 8>>(context, input.size());
			for (const auto& colour : result.colours) {
				if (prepareColours)
					Require(colour[3] == 0 && colour[7] == 0, "A valid light's prepared colours were not written");
				else
					Require(std::ranges::all_of(colour, [](UINT value) { return value == 0xdeadbeef; }), "Disabled preparation wrote colours");
			}
			for (UINT c = 0; c < clusterCount; ++c) {
				Require(lightGrid[c].offset == c * visibleCapacity && lightGrid[c].count <= visibleCapacity, "Invalid visible range");
				Require(shadowGrid[c].count <= std::min(budget, candidateCapacity) &&
							shadowGrid[c].offset <= candidateEntries.size() &&
							shadowGrid[c].count <= candidateEntries.size() - shadowGrid[c].offset,
					"Invalid candidate range");
				result.entries[c].assign(visibleEntries.begin() + lightGrid[c].offset, visibleEntries.begin() + lightGrid[c].offset + lightGrid[c].count);
				result.candidates[c].assign(candidateEntries.begin() + shadowGrid[c].offset, candidateEntries.begin() + shadowGrid[c].offset + shadowGrid[c].count);
				Require(shadowGrid[c].count != 0 || shadowGrid[c].offset == 0, "Empty contact grid retained an offset");
				total += shadowGrid[c].count;
			}
			Require(counter.Read<UINT>(context, 1)[0] == total, "Candidate allocation count differs");
			return result;
		}
	};

	struct VisibilityReference
	{
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		std::unique_ptr<Buffer> output;
		UINT clusterCount = 0;

		VisibilityReference(ID3D11Device* device, bool vr)
		{
			auto code = Compile("tests/cluster_visibility_reference.hlsl", vr, false, "ReferenceVisibility");
			Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
			Util::SetResourceName(shader.Get(), "ContactShadowTest::ExactVisibility");
			Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		}

		void Validate(ID3D11Device* device, ID3D11DeviceContext* context, Kernel& kernel, const Result& actual)
		{
			if (clusterCount != kernel.clusterCount) {
				clusterCount = kernel.clusterCount;
				output = std::make_unique<Buffer>(device, UINT(sizeof(UINT)), clusterCount * (visibleCapacity + 1), true, "ExactVisibility");
			}
			kernel.frame->Bind(context);
			kernel.camera->Bind(context);
			ID3D11ShaderResourceView* srvs[]{ kernel.clusters.srv.Get(), kernel.lights.srv.Get() };
			ID3D11UnorderedAccessView* uav = output->uav.Get();
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(6, 1, &uav, nullptr);
			context->CSSetShader(shader.Get(), nullptr, 0);
			DispatchClusters(context, reflection.Get(), { clusterCount, 1, 1 });
			context->ClearState();
			const auto expected = output->Read<UINT>(context, clusterCount * (visibleCapacity + 1));
			for (UINT c = 0; c < clusterCount; ++c) {
				const UINT offset = c * (visibleCapacity + 1);
				Require(actual.entries[c].size() == expected[offset], "Broad phase changed the exact visible count");
				for (UINT i = 0; i < expected[offset]; ++i)
					Require((actual.entries[c][i] & ~memberBit) == expected[offset + 1 + i], "Broad phase lost or reordered an exact contributor");
			}
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
		if (baseline)
			Require(actual.colours == baseline->colours, "Prepared colours differ from baseline production shader");
		for (size_t c = 0; c < actual.entries.size(); ++c) {
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
				Require(actual.entries[c] == baseline->entries[c], "Packed visible entries/order differ from baseline production shader");
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

	std::vector<Cluster> BuildProjectedBounds(ID3D11Device* device, ID3D11DeviceContext* context,
		std::array<UINT, 3> size, bool vr, bool reversed, float jitter)
	{
		auto code = Compile("features/Light Limit Fix/Shaders/LightLimitFix/ClusterBuildingCS.hlsl", vr, reversed);
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
		Util::SetResourceName(shader.Get(), "ContactShadowTest::ClusterBuild");
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
		D3D11ShaderTest::ConstantBuffer frame(device, reflection.Get(), "PerFrame");
		D3D11ShaderTest::ConstantBuffer camera(device, reflection.Get(), "FrameBuffer::PerFrame");
		constexpr float nearPlane = 1, farPlane = 16384;
		frame.SetVariable("LightsNear", nearPlane);
		frame.SetVariable("LightsFar", farPlane);
		frame.SetVariable("ClusterSize", std::array<UINT, 4>{ size[0], size[1], size[2], 0 });
		std::array<std::array<float, 16>, 2> inverses{};
		const float a = reversed ? nearPlane / (nearPlane - farPlane) : farPlane / (farPlane - nearPlane);
		const float b = reversed ? -farPlane * nearPlane / (nearPlane - farPlane) : -farPlane * nearPlane / (farPlane - nearPlane);
		for (UINT eye = 0; eye < 2; ++eye) {
			const float sx = eye ? 1.3f : 0.9f, sy = eye ? 1.1f : 1.4f;
			const float ox = (eye ? -0.17f : 0.11f) + jitter / (float(size[0]) * 64);
			const float oy = -0.07f + jitter / (float(size[1]) * 64);
			inverses[eye] = { 1 / sx, 0, 0, -ox / sx, 0, 1 / sy, 0, -oy / sy,
				0, 0, 0, 1, 0, 0, 1 / b, -a / b };
		}
		if (vr)
			camera.SetVariable("FrameBuffer::CameraProjInverse", inverses);
		else
			camera.SetVariable("FrameBuffer::CameraProjInverse", inverses[0]);
		Buffer output(device, sizeof(Cluster), size[0] * size[1] * size[2], true, "BuiltClusters");
		frame.Bind(context);
		camera.Bind(context);
		ID3D11UnorderedAccessView* uav = output.uav.Get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(shader.Get(), nullptr, 0);
		DispatchClusters(context, reflection.Get(), size);
		context->ClearState();
		auto bounds = output.Read<Cluster>(context, size[0] * size[1] * size[2]);
		for (const auto& box : bounds)
			for (UINT axis = 0; axis < 3; ++axis)
				Require(std::isfinite(box.minimum[axis]) && std::isfinite(box.maximum[axis]) && box.minimum[axis] <= box.maximum[axis],
					"Projected test fixture produced invalid bounds");
		return bounds;
	}

	void RunProjectedCases(ID3D11Device* device, ID3D11DeviceContext* context, bool vr,
		Kernel& current, Kernel* baseline, auto&& run)
	{
		for (bool reversed : { false, true })
			for (float jitter : { -0.5f, 0.375f }) {
				const std::array<UINT, 3> size{ 17, 19, 32 };
				auto bounds = BuildProjectedBounds(device, context, size, vr, reversed, jitter);
				current.SetLayout(device, size, bounds);
				if (baseline)
					baseline->SetLayout(device, size, bounds);
				std::vector<Light> lights;
				for (UINT i = 0; i < 128; ++i) {
					const auto& box = bounds[(i * 127) % bounds.size()];
					auto light = MakeLight(float(i % 11 + 1), i % 2 ? particleFlag : 0, 0, (box.maximum[2] - box.minimum[2]) * 0.5f);
					for (UINT eye = 0; eye < 2; ++eye)
						for (UINT axis = 0; axis < 3; ++axis)
							light.position[eye][axis] = (box.minimum[axis] + box.maximum[axis]) * 0.5f + (eye ? light.radius : -light.radius);
					lights.push_back(light);
				}
				for (UINT flags : { 0u, 3u })
					run(lights, flags, 8, 2);
			}
	}

	void RunBroadPhaseCases(ID3D11Device* device, Kernel& current, Kernel* baseline, auto&& run)
	{
		auto layout = [&](std::array<UINT, 3> size, const std::vector<Cluster>& bounds) {
			current.SetLayout(device, size, bounds);
			if (baseline)
				baseline->SetLayout(device, size, bounds);
		};
		for (auto size : { std::array<UINT, 3>{ 1, 1, 1 }, { 2, 3, 2 }, { 16, 16, 4 },
				 { 15, 7, 3 }, { 17, 19, 5 }, { 32, 32, 8 } }) {
			std::vector<Cluster> bounds;
			for (UINT z = 0; z < size[2]; ++z)
				for (UINT y = 0; y < size[1]; ++y)
					for (UINT x = 0; x < size[0]; ++x) {
						const float px = float(x) * 4 - 32, py = float(y) * 4 - 32, pz = float(z) * 4 - 8;
						bounds.push_back({ { px, py, pz, 0 }, { px + 1, py + 1, pz + 1, 0 } });
					}
			// Interior extrema must participate even when no corner child contains them.
			if (bounds.size() > 33)
				bounds[33] = { { -1000, -1000, -1000, 0 }, { 1000, 1000, 1000, 0 } };
			layout(size, bounds);
			for (UINT count : { 0u, 1u, 31u, 32u, 33u, lightCapacity }) {
				std::vector<Light> lights(count, MakeLight(1, 0, 10000, 0.25f));
				for (UINT i = 0; i < count; ++i) {
					if (i % 31 == 0 || i == 1023) {
						const auto& box = bounds[(i * 37) % bounds.size()];
						lights[i] = MakeLight(float(i % 7 + 1), i % 2 ? particleFlag : 0, 0, 0.5f);
						for (UINT axis = 0; axis < 3; ++axis)
							lights[i].position[0][axis] = lights[i].position[1][axis] = (box.minimum[axis] + box.maximum[axis]) * 0.5f;
					}
				}
				for (UINT flags : { 0u, 3u })
					run(lights, flags, 8, 2);
			}
			// All-retained/capped dispatch followed by an empty mask reuses every output buffer.
			std::vector<Light> dense(300, MakeLight(1, 0, 0, 100000));
			dense[255].color[0] = 1000;
			run(dense, 3, 8, 2);
			auto empty = run({ MakeLight(1, 0, 1000000, 1) }, 3, 8, 2);
			for (const auto& entries : empty.entries)
				Require(entries.empty(), "Empty mask retained a populated grid entry");
		}

		std::vector<Cluster> separated(17, Cluster{ { -1, -1, -1, 0 }, { 1, 1, 1, 0 } });
		separated.back() = { { 999, -1, -1, 0 }, { 1001, 1, 1, 0 } };
		layout({ 17, 1, 1 }, separated);
		auto later = run({ MakeLight(4, 0, 1000, 1) }, 1, 8, 0);
		Require(std::bit_cast<float>(later.colours[0][0]) == 4.0f, "Rejected group-zero light lost its authored colour");
		Require(later.entries.front().empty() && later.entries.back().size() == 1,
			"Later-group contributor or designated-group colour publication changed");

		const Cluster unit{ { -1, -1, -1, 0 }, { 1, 1, 1, 0 } };
		layout({ 1, 1, 1 }, { unit });
		std::vector<Light> boundary;
		for (float radius : { 0.0f, std::numeric_limits<float>::denorm_min(), 1e-20f, 0.25f, 1.0f, -1.0f, 1e10f, 1e30f })
			for (UINT axis = 0; axis < 3; ++axis)
				for (float sign : { -1.0f, 1.0f }) {
					const float tangent = sign * (1 + std::abs(radius));
					for (float position : { std::nextafter(tangent, 0.0f), tangent, std::nextafter(tangent, sign * INFINITY) }) {
						auto light = MakeLight(1, 0, 0, radius);
						light.position[0][axis] = light.position[1][axis] = position;
						boundary.push_back(light);
					}
				}
		run(boundary, 3, 8, 2);
		run({ MakeLight(), MakeLight(1, 0, 0, 0), MakeLight(1, 0, -4, 100) }, 3, 8, 0);

		std::mt19937 random(0xB04DFACEu);
		for (UINT iteration = 0; iteration < 32; ++iteration) {
			const float scale = std::ldexp(1.0f, int(random() % 100) - 50);
			std::vector<Cluster> bounds(37, unit);
			for (auto& box : bounds)
				for (UINT axis = 0; axis < 3; ++axis) {
					box.minimum[axis] = (float(random() % 32) - 16) * scale;
					box.maximum[axis] = box.minimum[axis] + float(random() % 8) * scale;
				}
			layout({ 37, 1, 1 }, bounds);
			std::vector<Light> lights;
			for (UINT i = 0; i < 120; ++i) {
				const auto& box = bounds[i % bounds.size()];
				auto light = MakeLight(float(i % 7), i % 3 == 0 ? particleFlag : 0, 0, float(i % 5) * scale);
				for (UINT axis = 0; axis < 3; ++axis) {
					light.position[0][axis] = box.maximum[axis] + light.radius * 0.577350269f;
					light.position[1][axis] = box.minimum[axis] - light.radius * 0.577350269f;
				}
				for (UINT eye = 0; eye < 2; ++eye)
					light.position[eye][i % 3] = std::nextafter(light.position[eye][i % 3], i % 2 ? INFINITY : -INFINITY);
				lights.push_back(light);
			}
			run(lights, 3, 8, 2);
		}

		for (float invalid : { std::numeric_limits<float>::quiet_NaN(), INFINITY, -INFINITY, 2.0f })
			for (UINT ordinal : { 0u, 31u, 33u, 1023u })
				for (bool malformedMaximum : { false, true }) {
					std::vector<Cluster> bounds(1024, unit);
					if (malformedMaximum)
						bounds[ordinal].maximum[0] = -invalid;
					else
						bounds[ordinal].minimum[0] = invalid;
					layout({ 16, 16, 4 }, bounds);
					run({ MakeLight(), MakeLight(1, 0, 4, 2), MakeLight(1, 0, -4, -3) }, 3, 8, 2);
				}
		layout({ 1, 1, 1 }, { unit });
		std::vector<Light> malformed;
		for (float value : { std::numeric_limits<float>::quiet_NaN(), INFINITY, -INFINITY, std::numeric_limits<float>::max() }) {
			malformed.push_back(MakeLight(1, 0, value));
			malformed.push_back(MakeLight(1, 0, 0, value));
			auto right = MakeLight(1, 0, 10000);
			right.position[1][0] = value;
			malformed.push_back(right);
		}
		run(malformed, 3, 8, 2);
	}

	void RunCases(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, const std::filesystem::path& baselinePath)
	{
		Kernel current(device, "features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl", vr);
		std::unique_ptr<Kernel> baseline;
		if (!baselinePath.empty())
			baseline = std::make_unique<Kernel>(device, baselinePath, vr);
		VisibilityReference reference(device, vr);
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
				reference.Validate(device, context, current, actual);
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
		const Matrix leftView{ 0.8f, 0, 0.6f, -0.03f, 0, 1, 0, 1.0f, -0.6f, 0, 0.8f, -2.0f, 0, 0, 0, 1 };
		const Matrix rightView{ 0.8f, 0, 0.6f, 0.03f, 0, 1, 0, 1.0f, -0.6f, 0, 0.8f, -2.0f, 0, 0, 0, 1 };
		current.SetView(leftView, rightView);
		if (baseline)
			baseline->SetView(leftView, rightView);
		run(mixed, 3, 8, 2);
		current.SetView(identityView, identityView);
		if (baseline)
			baseline->SetView(identityView, identityView);
		RunBroadPhaseCases(device, current, baseline.get(), run);
		RunProjectedCases(device, context, vr, current, baseline.get(), run);
		current.feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", 1u);
		if (baseline)
			baseline->feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", 1u);
		run({ MakeLight(0.25f), MakeLight(2, 1u << 11), MakeLight(3, 1u << 13) }, 3, 8, 2);
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
