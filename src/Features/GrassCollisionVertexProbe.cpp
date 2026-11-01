#include "GrassCollisionVertexProbe.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Utils/ComputeState.h"
#	include "Utils/D3D.h"
#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <climits>
#	include <d3d11shader.h>
#	include <d3dcompiler.h>
#	include <limits>
#	include <mutex>
#	include <vector>
#	include <winrt/base.h>

namespace
{
	using json = nlohmann::json;
	constexpr UINT kPairsPerPass = 8, kPairLimit = kPairsPerPass * 2;
	constexpr UINT kIndexLimit = 96, kInstanceLimit = 128, kVertexLimit = kIndexLimit * kInstanceLimit;
	constexpr uint32_t kReadbackFrames = 180;
	struct Vertex
	{
		std::array<float, 3> world;
		std::array<float, 4> clip;
	};
	static_assert(sizeof(Vertex) == 28);

	struct ReplayState
	{
		ID3D11DeviceContext1* context;
		winrt::com_ptr<ID3D11ShaderResourceView> field;
		explicit ReplayState(ID3D11DeviceContext1* a_context) : context(a_context)
		{
			context->VSGetShaderResources(100, 1, field.put());
		}
		~ReplayState()
		{
			std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> empty{};
			std::array<UINT, D3D11_SO_BUFFER_SLOT_COUNT> offsets{};
			context->SOSetTargets(UINT(empty.size()), empty.data(), offsets.data());
			context->GSSetShader(nullptr, nullptr, 0);
			auto* restored = field.get();
			context->VSSetShaderResources(100, 1, &restored);
		}
	};

	bool SupportedPipeline(ID3D11DeviceContext1* a_context)
	{
		if (!a_context || a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return false;
		D3D11_PRIMITIVE_TOPOLOGY topology{};
		a_context->IAGetPrimitiveTopology(&topology);
		winrt::com_ptr<ID3D11HullShader> hull;
		winrt::com_ptr<ID3D11DomainShader> domain;
		winrt::com_ptr<ID3D11GeometryShader> geometry;
		winrt::com_ptr<ID3D11Predicate> predicate;
		BOOL predicateValue = FALSE;
		a_context->HSGetShader(hull.put(), nullptr, nullptr);
		a_context->DSGetShader(domain.put(), nullptr, nullptr);
		a_context->GSGetShader(geometry.put(), nullptr, nullptr);
		a_context->GetPredication(predicate.put(), &predicateValue);
		std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> streams{};
		a_context->SOGetTargets(UINT(streams.size()), streams.data());
		bool occupied = false;
		for (auto* stream : streams)
			if (stream) {
				occupied = true;
				stream->Release();
			}
		return topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST && !hull && !domain && !geometry && !predicate && !occupied;
	}

	template <size_t N>
	double Distance(const std::array<float, N>& a_left, const std::array<float, N>& a_right)
	{
		double squared = 0;
		for (size_t i = 0; i < N; ++i)
			squared += std::pow(double(a_left[i]) - a_right[i], 2);
		return std::sqrt(squared);
	}
}

struct GrassCollisionVertexProbe::Impl
{
	struct Pair
	{
		uint64_t generation = 0;
		uint32_t frame = 0;
		bool pending = false;
		std::array<winrt::com_ptr<ID3D11Buffer>, 2> outputs, staging;
		std::array<winrt::com_ptr<ID3D11Query>, 2> statistics;
		winrt::com_ptr<ID3D11Buffer> arguments;
		winrt::com_ptr<ID3D11UnorderedAccessView> argumentUAV;
	};
	struct StreamShader
	{
		winrt::com_ptr<ID3D11VertexShader> vertex;
		winrt::com_ptr<ID3D11GeometryShader> stream;
	};
	mutable std::mutex mutex;
	std::atomic<uint64_t> requested{ 0 };
	uint32_t requestedFrame = 0, pollFrame = UINT32_MAX;
	uint64_t renderGeneration = 0;
	std::array<UINT, 2> passCounts{};
	std::array<Pair, kPairLimit> pairs;
	std::vector<StreamShader> shaders;
	json records = json::array();
	size_t skippedPipelines = 0;
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11ShaderResourceView> zeroField;
	winrt::com_ptr<ID3D11ComputeShader> clampArguments;
	bool capturing = false;

	void Publish(UINT a_index, uint64_t a_generation, json a_record)
	{
		std::scoped_lock lock(mutex);
		if (requested.load(std::memory_order_acquire) != a_generation)
			return;
		while (records.size() <= a_index)
			records.push_back(nullptr);
		records[a_index] = std::move(a_record);
	}

	HRESULT EnsureResources(Pair& a_pair)
	{
		if (!zeroField) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 512;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const std::vector<uint16_t> zeros(512 * 512 * 4, 0);
			const D3D11_SUBRESOURCE_DATA initial{ zeros.data(), 512 * 8, 0 };
			winrt::com_ptr<ID3D11Texture2D> texture;
			auto result = device->CreateTexture2D(&desc, &initial, texture.put());
			if (FAILED(result))
				return result;
			Util::SetResourceName(texture.get(), "GrassCollision::VertexProbeZeroField");
			result = device->CreateShaderResourceView(texture.get(), nullptr, zeroField.put());
			if (FAILED(result))
				return result;
			Util::SetResourceName(zeroField.get(), "GrassCollision::VertexProbeZeroField SRV");
		}
		for (UINT i = 0; i < 2; ++i) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = kVertexLimit * sizeof(Vertex);
			desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
			if (!a_pair.outputs[i]) {
				const auto result = device->CreateBuffer(&desc, nullptr, a_pair.outputs[i].put());
				if (FAILED(result))
					return result;
				Util::SetResourceName(a_pair.outputs[i].get(), "GrassCollision::VertexProbeOutput%u", i);
			}
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (!a_pair.staging[i]) {
				const auto result = device->CreateBuffer(&desc, nullptr, a_pair.staging[i].put());
				if (FAILED(result))
					return result;
				Util::SetResourceName(a_pair.staging[i].get(), "GrassCollision::VertexProbeReadback%u", i);
			}
			if (!a_pair.statistics[i]) {
				const D3D11_QUERY_DESC query{ D3D11_QUERY_SO_STATISTICS, 0 };
				const auto result = device->CreateQuery(&query, a_pair.statistics[i].put());
				if (FAILED(result))
					return result;
				Util::SetResourceName(a_pair.statistics[i].get(), "GrassCollision::VertexProbeStatistics%u", i);
			}
		}
		return S_OK;
	}

	HRESULT StreamOutput(ID3D11VertexShader* a_vertex, const void* a_bytes, size_t a_size, ID3D11GeometryShader*& a_result)
	{
		const auto found = std::find_if(shaders.begin(), shaders.end(), [&](const auto& item) { return item.vertex.get() == a_vertex; });
		if (found != shaders.end()) {
			a_result = found->stream.get();
			return S_OK;
		}
		if (!a_bytes || !a_size || a_size > 16 * 1024 * 1024 || shaders.size() >= 16)
			return E_INVALIDARG;
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		auto result = D3DReflect(a_bytes, a_size, IID_PPV_ARGS(reflection.put()));
		if (FAILED(result))
			return result;
		D3D11_SHADER_DESC desc{};
		result = reflection->GetDesc(&desc);
		if (FAILED(result))
			return result;
		for (UINT i = 0; i < desc.BoundResources; ++i) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			result = reflection->GetResourceBindingDesc(i, &binding);
			if (FAILED(result))
				return result;
			switch (binding.Type) {
			case D3D_SIT_UAV_RWTYPED:
			case D3D_SIT_UAV_RWSTRUCTURED:
			case D3D_SIT_UAV_RWBYTEADDRESS:
			case D3D_SIT_UAV_APPEND_STRUCTURED:
			case D3D_SIT_UAV_CONSUME_STRUCTURED:
			case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
				return E_INVALIDARG;
			default:
				break;
			}
		}
		const std::array<D3D11_SO_DECLARATION_ENTRY, 2> declaration{ { { 0, "POSITION", 1, 0, 3, 0 }, { 0, "SV_Position", 0, 0, 4, 0 } } };
		const UINT stride = sizeof(Vertex);
		StreamShader shader;
		shader.vertex.copy_from(a_vertex);
		result = device->CreateGeometryShaderWithStreamOutput(a_bytes, a_size, declaration.data(), UINT(declaration.size()),
			&stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, shader.stream.put());
		if (FAILED(result))
			return result;
		Util::SetResourceName(shader.stream.get(), "GrassCollision::VertexProbeStreamOutput");
		a_result = shader.stream.get();
		shaders.push_back(std::move(shader));
		return S_OK;
	}

	HRESULT PrepareArguments(ID3D11DeviceContext1* a_context, Pair& a_pair, const Draw& a_draw)
	{
		D3D11_BUFFER_DESC source{};
		a_draw.arguments->GetDesc(&source);
		if ((a_draw.argumentOffset & 3) || uint64_t(a_draw.argumentOffset) + 20 > source.ByteWidth)
			return E_INVALIDARG;
		if (!clampArguments) {
			constexpr char text[] =
				"RWByteAddressBuffer args:register(u0);[numthreads(1,1,1)]void main(){"
				"args.Store(0,min(args.Load(0),96u));args.Store(4,min(args.Load(4),128u));}";
			winrt::com_ptr<ID3DBlob> blob, error;
			auto result = D3DCompile(text, sizeof(text) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS, 0, blob.put(), error.put());
			if (FAILED(result))
				return result;
			result = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, clampArguments.put());
			if (FAILED(result))
				return result;
			Util::SetResourceName(clampArguments.get(), "GrassCollision::VertexProbeClampArguments");
		}
		if (!a_pair.arguments) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 32;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			auto result = device->CreateBuffer(&desc, nullptr, a_pair.arguments.put());
			if (FAILED(result))
				return result;
			Util::SetResourceName(a_pair.arguments.get(), "GrassCollision::VertexProbeArguments");
		}
		if (!a_pair.argumentUAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
			desc.Format = DXGI_FORMAT_R32_TYPELESS;
			desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			desc.Buffer.NumElements = 8;
			desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			const auto result = device->CreateUnorderedAccessView(a_pair.arguments.get(), &desc, a_pair.argumentUAV.put());
			if (FAILED(result))
				return result;
			Util::SetResourceName(a_pair.argumentUAV.get(), "GrassCollision::VertexProbeArguments UAV");
		}
		const D3D11_BOX box{ a_draw.argumentOffset, 0, 0, a_draw.argumentOffset + 20, 1, 1 };
		a_context->CopySubresourceRegion(a_pair.arguments.get(), 0, 0, 0, 0, a_draw.arguments, 0, &box);
		D3DState::ComputeState restore(a_context, 1);
		auto* uav = a_pair.argumentUAV.get();
		a_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		a_context->CSSetShader(clampArguments.get(), nullptr, 0);
		a_context->Dispatch(1, 1, 1);
		return S_OK;
	}

	json Compare(const Vertex* a_on, const Vertex* a_off, size_t a_count)
	{
		size_t moved = 0, nearVertices = 0, nearMoved = 0, nonFinite = 0;
		double maximum = 0, maxClip = 0, minimumDistance = std::numeric_limits<double>::infinity();
		size_t example = 0;
		for (size_t i = 0; i < a_count; ++i) {
			const auto finite = [](const auto& a_values) {
				return std::all_of(a_values.begin(), a_values.end(), [](float a_value) { return std::isfinite(a_value); });
			};
			if (!finite(a_on[i].world) || !finite(a_off[i].world) || !finite(a_on[i].clip) || !finite(a_off[i].clip)) {
				++nonFinite;
				continue;
			}
			const double delta = Distance(a_on[i].world, a_off[i].world);
			const double cameraDistance = Distance(a_off[i].world, std::array<float, 3>{});
			const bool displaced = delta > 0.0001;
			moved += displaced;
			nearVertices += cameraDistance < 512;
			nearMoved += cameraDistance < 512 && displaced;
			if (delta > maximum || (maximum == 0 && cameraDistance < minimumDistance))
				example = i;
			maximum = std::max(maximum, delta);
			maxClip = std::max(maxClip, Distance(a_on[i].clip, a_off[i].clip));
			minimumDistance = std::min(minimumDistance, cameraDistance);
		}
		json result = {
			{ "sampledVertices", a_count }, { "movedVertices", moved }, { "nearCameraVertices", nearVertices }, { "nearCameraMovedVertices", nearMoved },
			{ "nonFiniteVertices", nonFinite }, { "maxWorldDisplacement", maximum }, { "maxClipDisplacement", maxClip },
			{ "minimumCameraDistance", std::isfinite(minimumDistance) ? json(minimumDistance) : json(nullptr) }
		};
		if (a_count && nonFinite < a_count)
			result["example"] = { { "collisionOnWorld", a_on[example].world }, { "collisionOffWorld", a_off[example].world },
				{ "collisionOnClip", a_on[example].clip }, { "collisionOffClip", a_off[example].clip } };
		return result;
	}
};

GrassCollisionVertexProbe::GrassCollisionVertexProbe() : impl(std::make_unique<Impl>()) {}
GrassCollisionVertexProbe::~GrassCollisionVertexProbe() = default;

void GrassCollisionVertexProbe::Request(uint64_t a_generation, uint32_t a_frame)
{
	std::scoped_lock lock(impl->mutex);
	impl->records = json::array();
	impl->skippedPipelines = 0;
	impl->requestedFrame = a_frame;
	impl->requested.store(a_generation, std::memory_order_release);
}

nlohmann::json GrassCollisionVertexProbe::Read() const
{
	std::scoped_lock lock(impl->mutex);
	json samples = json::array();
	for (const auto& record : impl->records)
		if (!record.is_null())
			samples.push_back(record);
	return { { "schemaVersion", 1 }, { "requestGeneration", impl->requested.load(std::memory_order_acquire) },
		{ "maximumPairs", kPairLimit }, { "maximumIndices", kIndexLimit }, { "maximumInstances", kInstanceLimit },
		{ "skippedPipelines", impl->skippedPipelines }, { "samples", std::move(samples) } };
}

bool GrassCollisionVertexProbe::IsCapturing() const { return impl->capturing; }

void GrassCollisionVertexProbe::Capture(ID3D11DeviceContext1* a_context, ID3D11VertexShader* a_vertex,
	const void* a_bytecode, size_t a_bytes, uint32_t a_descriptor, uint32_t a_frame,
	const Draw& a_draw, DirectDraw a_direct, IndirectDraw a_indirect)
{
	if (impl->capturing)
		return;
	uint64_t generation;
	{
		std::scoped_lock lock(impl->mutex);
		generation = impl->requested.load(std::memory_order_acquire);
		if (!generation || uint32_t(a_frame - impl->requestedFrame) >= 3)
			return;
	}
	if (impl->renderGeneration != generation) {
		impl->renderGeneration = generation;
		impl->passCounts = {};
		for (auto& pair : impl->pairs)
			pair.pending = false;
	}
	const UINT pass = (a_descriptor & 15) == 8 ? 0 : 1;
	if (impl->passCounts[pass] >= kPairsPerPass)
		return;
	if (!SupportedPipeline(a_context)) {
		std::scoped_lock lock(impl->mutex);
		if (impl->requested.load(std::memory_order_acquire) == generation)
			++impl->skippedPipelines;
		return;
	}
	const UINT index = pass * kPairsPerPass + impl->passCounts[pass]++;
	auto& pair = impl->pairs[index];
	pair.generation = generation;
	pair.frame = a_frame;
	json receipt = { { "status", "pending" }, { "capturedFrame", a_frame }, { "shaderDescriptor", a_descriptor },
		{ "pass", pass ? "color" : "depth" }, { "drawKind", a_draw.arguments ? "indirect" : "native" } };
	winrt::com_ptr<ID3D11Device> device;
	a_context->GetDevice(device.put());
	HRESULT result = S_OK;
	if (!impl->device)
		impl->device = device;
	if (impl->device != device || !a_vertex || (a_draw.arguments ? !a_indirect : !a_direct))
		result = E_INVALIDARG;
	if (SUCCEEDED(result))
		result = impl->EnsureResources(pair);
	ID3D11GeometryShader* stream = nullptr;
	if (SUCCEEDED(result))
		result = impl->StreamOutput(a_vertex, a_bytecode, a_bytes, stream);
	if (SUCCEEDED(result) && a_draw.arguments)
		result = impl->PrepareArguments(a_context, pair, a_draw);
	if (SUCCEEDED(result) && !a_draw.arguments &&
		(a_draw.firstIndex > UINT_MAX - std::min(a_draw.indices, kIndexLimit) ||
			a_draw.firstInstance > UINT_MAX - std::min(a_draw.instances, kInstanceLimit)))
		result = E_INVALIDARG;
	if (FAILED(result)) {
		receipt["status"] = "failed";
		receipt["hresult"] = static_cast<uint32_t>(result);
		impl->Publish(index, generation, std::move(receipt));
		return;
	}
	impl->capturing = true;
	struct CapturingGuard
	{
		bool& flag;
		~CapturingGuard() { flag = false; }
	} capturing{ impl->capturing };
	{
		ReplayState restore(a_context);
		a_context->GSSetShader(stream, nullptr, 0);
		for (UINT i = 0; i < 2; ++i) {
			auto* field = i == 0 ? restore.field.get() : impl->zeroField.get();
			a_context->VSSetShaderResources(100, 1, &field);
			auto* output = pair.outputs[i].get();
			const UINT offset = 0;
			a_context->SOSetTargets(1, &output, &offset);
			a_context->Begin(pair.statistics[i].get());
			if (a_draw.arguments)
				a_indirect(a_context, pair.arguments.get(), 0);
			else
				a_direct(a_context, std::min(a_draw.indices, kIndexLimit), std::min(a_draw.instances, kInstanceLimit),
					a_draw.firstIndex, a_draw.baseVertex, a_draw.firstInstance);
			a_context->End(pair.statistics[i].get());
			ID3D11Buffer* empty = nullptr;
			a_context->SOSetTargets(1, &empty, &offset);
			a_context->CopyResource(pair.staging[i].get(), pair.outputs[i].get());
		}
	}
	pair.pending = true;
	impl->Publish(index, generation, std::move(receipt));
}

void GrassCollisionVertexProbe::Poll(ID3D11DeviceContext1* a_context, uint32_t a_frame)
{
	if (!a_context || impl->capturing || impl->pollFrame == a_frame)
		return;
	impl->pollFrame = a_frame;
	for (UINT index = 0; index < kPairLimit; ++index) {
		auto& pair = impl->pairs[index];
		if (!pair.pending)
			continue;
		if (pair.generation != impl->requested.load(std::memory_order_acquire)) {
			pair.pending = false;
			continue;
		}
		json receipt;
		{
			std::scoped_lock lock(impl->mutex);
			if (index >= impl->records.size() || impl->records[index].is_null())
				continue;
			receipt = impl->records[index];
		}
		const auto finish = [&](const char* a_status, HRESULT a_result) {
			pair.pending = false;
			receipt["status"] = a_status;
			receipt["completedFrame"] = a_frame;
			receipt["hresult"] = static_cast<uint32_t>(a_result);
			impl->Publish(index, pair.generation, receipt);
		};
		if (uint32_t(a_frame - pair.frame) > kReadbackFrames) {
			finish("timed_out", DXGI_ERROR_WAS_STILL_DRAWING);
			continue;
		}
		std::array<D3D11_QUERY_DATA_SO_STATISTICS, 2> statistics{};
		bool ready = true;
		for (UINT i = 0; i < 2; ++i) {
			const auto result = a_context->GetData(pair.statistics[i].get(), &statistics[i], sizeof(statistics[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_FALSE)
				ready = false;
			else if (FAILED(result)) {
				finish("failed", result);
				ready = false;
				break;
			}
		}
		if (!ready)
			continue;
		if (statistics[0].NumPrimitivesWritten != statistics[1].NumPrimitivesWritten ||
			statistics[0].NumPrimitivesWritten != statistics[0].PrimitivesStorageNeeded ||
			statistics[1].NumPrimitivesWritten != statistics[1].PrimitivesStorageNeeded ||
			statistics[0].NumPrimitivesWritten > kVertexLimit / 3) {
			finish("output_count_mismatch", E_FAIL);
			continue;
		}
		std::array<D3D11_MAPPED_SUBRESOURCE, 2> mapped{};
		std::array<bool, 2> active{};
		struct Unmap
		{
			ID3D11DeviceContext1* context;
			Impl::Pair& pair;
			std::array<bool, 2>& active;
			~Unmap()
			{
				for (UINT i = 0; i < 2; ++i)
					if (active[i])
						context->Unmap(pair.staging[i].get(), 0);
			}
		} unmap{ a_context, pair, active };
		for (UINT i = 0; i < 2; ++i) {
			const auto result = a_context->Map(pair.staging[i].get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped[i]);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING)
				ready = false;
			else if (FAILED(result)) {
				finish("failed", result);
				ready = false;
			} else
				active[i] = true;
			if (!ready)
				break;
		}
		if (!ready)
			continue;
		const auto count = size_t(statistics[0].NumPrimitivesWritten) * 3;
		receipt.update(impl->Compare(static_cast<const Vertex*>(mapped[0].pData), static_cast<const Vertex*>(mapped[1].pData), count));
		const bool invalid = receipt["nonFiniteVertices"].get<size_t>() != 0;
		finish(invalid ? "invalid_outputs" : count ? "completed" :
													 "empty",
			S_OK);
	}
}

#endif
