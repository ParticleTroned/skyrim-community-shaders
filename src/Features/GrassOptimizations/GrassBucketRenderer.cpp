#include "GrassBucketRenderer.h"

#include "Buffer.h"
#include "Features/GrassCollision.h"
#include "Features/GrassOptimizations.h"
#include "Globals.h"
#include "GpuPass.h"
#include "GrassD3DState.h"
#include "GrassHiZ.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/LazyShader.h"

#include <bit>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace
{
	struct Lifetime
	{
		std::atomic_bool alive{ true };
		std::unordered_map<RE::BSGraphics::VertexBuffer*, float> birthTimes;
	};
	struct Group
	{
		RE::BSGraphics::VertexBuffer* identity;
		winrt::com_ptr<ID3D11Buffer> buffer;
		uint32_t count;
		float fade;
		bool visible;
		std::vector<uint8_t> bytes;
	};
	struct Source
	{
		RE::BSMultiStreamInstanceTriShape* identity;
		std::shared_ptr<Lifetime> lifetime;
		RE::NiPointer<RE::BSShaderProperty> property;
		RE::BSShaderMaterial* material;
		RE::BSGraphics::TriShape* mesh;
		winrt::com_ptr<ID3D11Buffer> vertices, indices;
		uint64_t descriptor, flags;
		uint32_t triangles, windTimer, capturedFrame;
		float wavePeriod, renderDistance;
		RE::NiPoint3 origin;
		std::vector<RE::BSLight*> lights;
		uint32_t lightMask;
		RE::NiBound bound;
		std::string model;
		std::vector<Group> groups;
	};
	struct Slice
	{
		uint32_t first, count, end, padding = 0;
		std::array<float, 4> originFade;
	};
	static_assert(sizeof(Slice) == 32);
	struct Parameters
	{
		uint32_t instances, slices, capacity, eyes;
		uint32_t frustum, density, midEnabled, farEnabled;
		float minPixels, fullPixels, minDensity, bandPixels;
		float midPixels, farPixels, bias, height;
		std::array<float, 4> origin, bound;
		uint32_t depthWidth, depthHeight, depthMips, collision;
		std::array<float, 4> depthScale;
		float meshCostBias, costBiasStartDistance, invisibleFadeCull, renderDistance;
		float edgeFadeStart, simpleShadingPixelSize, qualityRadius, meshWeight;
		uint32_t overrideDistance, padding0 = 0, padding1 = 0, padding2 = 0;
	};
	struct DrawConstants
	{
		uint32_t enabled, eye, base, padding = 0;
		std::array<float, 4> origin;
	};
	struct Mesh
	{
		RE::NiPointer<RE::NiNode> owner;
		RE::NiPointer<RE::NiSourceTexture> texture;
		winrt::com_ptr<ID3D11Buffer> vertices, indices;
		uint64_t descriptor = 0;
		uint32_t indexCount = 0;
		float radius = 0;
	};
	enum class Outcome
	{
		Native,
		Batched
	};
	struct PassKey
	{
		uint32_t pass, vertex, pixel, extra;
		bool operator==(const PassKey&) const = default;
	};
	struct PassHash
	{
		size_t operator()(const PassKey& key) const { return key.pass ^ (size_t(key.vertex) << 16) ^ (size_t(key.pixel) << 32) ^ (size_t(key.extra) << 8); }
	};
	struct Bucket
	{
		std::vector<Source> sources;
		uint32_t instances = 0;
		std::unordered_map<PassKey, Outcome, PassHash> outcomes;
	};
	bool Finite(const RE::NiPoint3& point) { return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z); }
	bool Identity(const RE::NiTransform& transform)
	{
		if (transform.scale != 1 || !Finite(transform.translate))
			return false;
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 3; ++col)
				if (transform.rotate.entry[row][col] != (row == col ? 1.0f : 0.0f))
					return false;
		return true;
	}
	bool Compatible(const Source& a, const Source& b)
	{
		return a.vertices == b.vertices && a.indices == b.indices && a.material == b.material &&
		       a.descriptor == b.descriptor && a.flags == b.flags && a.triangles == b.triangles &&
		       a.lights == b.lights && a.lightMask == b.lightMask && a.renderDistance == b.renderDistance && a.wavePeriod == b.wavePeriod && a.windTimer == b.windTimer && a.model == b.model &&
		       a.bound.radius == b.bound.radius && a.bound.center == b.bound.center;
	}
	size_t CompatibilityHash(const Source& source)
	{
		size_t value = 0;
		const auto combine = [&]<class T>(const T& item) {
			value ^= std::hash<T>{}(item) + 0x9e3779b9 + (value << 6) + (value >> 2);
		};
		combine(source.vertices.get());
		combine(source.indices.get());
		combine(source.material);
		combine(source.descriptor);
		combine(source.flags);
		combine(source.triangles);
		combine(source.model);
		return value;
	}
	std::unique_ptr<Buffer> RawBuffer(uint32_t bytes, UINT bind, UINT misc, const char* name)
	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = bind;
		desc.MiscFlags = misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		auto result = std::make_unique<Buffer>(desc, nullptr, name);
		if (bind & D3D11_BIND_SHADER_RESOURCE) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R32_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			srv.BufferEx.NumElements = bytes / 4;
			srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			result->CreateSRV(srv);
		}
		if (bind & D3D11_BIND_UNORDERED_ACCESS) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = DXGI_FORMAT_R32_TYPELESS;
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = bytes / 4;
			uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			result->CreateUAV(uav);
		}
		return result;
	}
	template <class T>
	std::unique_ptr<Buffer> Structured(uint32_t count, bool output, bool dynamic, const char* name)
	{
		auto result = std::make_unique<Buffer>(StructuredBufferDesc<T>(uint64_t(count), output, dynamic), nullptr, name);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = count;
		result->CreateSRV(srv);
		if (output) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = count;
			result->CreateUAV(uav);
		}
		return result;
	}
	void Upload(ID3D11DeviceContext* ctx, Buffer& buffer, const void* bytes, size_t size)
	{
		if (size > buffer.desc.ByteWidth)
			throw std::length_error("Grass upload exceeds resource capacity");
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(ctx->Map(buffer.resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		std::memcpy(mapped.pData, bytes, size);
		ctx->Unmap(buffer.resource.get(), 0);
	}
	RE::BSTriShape* SingleShape(RE::NiAVObject* object, unsigned depth = 0)
	{
		if (!object || depth > 16 || !Identity(object->local) || object->local.translate != RE::NiPoint3{})
			return nullptr;
		if (auto shape = object->AsTriShape())
			return shape;
		auto node = object->AsNode();
		if (!node)
			return nullptr;
		RE::BSTriShape* result = nullptr;
		for (auto& child : node->GetChildren()) {
			if (!child)
				continue;
			auto found = SingleShape(child.get(), depth + 1);
			if (!found || result)
				return nullptr;
			result = found;
		}
		return result;
	}
	struct VisibleHook
	{
		static void thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::NiCullingProcess* process, int32_t index)
		{
			original(shape, process, index);
			if (globals::features::grassOptimizations.GetRenderer().CaptureVisible(shape)) {
				const auto& groups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
				if (std::none_of(groups.begin(), groups.end(), [](auto group) { return group && group->isVisible && group->instanceCount; }))
					process->AppendVirtual(*shape, index);
			}
		}
		static inline REL::Relocation<decltype(thunk)> original;
	};
	struct DestroyHook
	{
		static void* thunk(RE::BSMultiStreamInstanceTriShape* shape, uint32_t flags)
		{
			globals::features::grassOptimizations.GetRenderer().RemoveShape(shape);
			// Preserve the deleting destructor's allocation flags across the hook.
			return original(shape, flags);
		}
		static inline REL::Relocation<decltype(thunk)> original;
	};
	template <unsigned Index>
	struct ModelHook
	{
		static RE::BSMultiStreamInstanceTriShape* thunk(RE::BGSGrassManager* manager, RE::GrassParam* param,
			uint32_t x, uint32_t y, uint64_t* key, RE::BSFixedString* path)
		{
			auto shape = original(manager, param, x, y, key, path);
			if (shape && path)
				globals::features::grassOptimizations.GetRenderer().RecordModel(shape, path->c_str());
			return shape;
		}
		static inline REL::Relocation<decltype(thunk)> original;
	};
}

struct GrassBucketRenderer::Impl
{
	std::mutex captureMutex;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Source> pending;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::pair<std::shared_ptr<Lifetime>, std::string>> identities;
	std::vector<Bucket> buckets;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, size_t> admitted;
	std::unordered_map<std::string, std::unordered_map<uint64_t, std::array<Mesh, 2>>> lodMeshes;
	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> layouts;
	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3D11DeviceContext1> context;
	Util::LazyShader<ID3D11ComputeShader> cullShader;
	GrassHiZ hiZ;
	std::unique_ptr<Buffer> input, output, extras, arguments, table, parameters, drawConstants, disabled;
	float nativeRenderDistance = 8000.0f;
	uint32_t capacity = 0, frame = UINT32_MAX;
	bool installed = false;
	std::atomic_bool failed{ false };
	float fadeInSeconds = 0;
	RE::BSMultiStreamInstanceTriShape* current = nullptr;
	uint32_t currentPass = 0;
	RE::NiPoint3 currentOrigin;
	GrassPolicy::Settings frameSettings;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool diagnostics{ false };
	Util::LazyShader<ID3D11ComputeShader> diagnosticShader;
	std::unique_ptr<Buffer> gpuCounters;
	std::array<winrt::com_ptr<ID3D11Buffer>, 4> readbacks;
	std::array<bool, 4> pendingReadbacks{};
	uint32_t counterFrame = UINT32_MAX;
	bool countersActive = false;
	std::array<std::atomic_uint64_t, 10> totals{};
	std::atomic_uint64_t hiZBatches{ 0 }, hiZUnavailable{ 0 };
	std::atomic_uint64_t nativeDraws{ 0 }, batches{ 0 }, combinedSources{ 0 }, combinedInstances{ 0 }, fallbacks{ 0 }, samples{ 0 }, droppedSamples{ 0 };
	void PollCounters();
#endif
	void PrepareFrame();
	void EnsureCapacity(uint32_t instances, uint32_t slices);
	ID3D11InputLayout* Layout(uint64_t descriptor);
	void CompileSignature();
	const std::array<Mesh, 2>& LODMeshes(const Source& source);
	bool HasBatchShader() const;
	bool DrawBucket(Bucket& bucket, const PassKey& key);
	void DispatchBucket(const Bucket& bucket, ID3D11ComputeShader* shader, const Parameters& params, ID3D11Buffer* nativeGeometry, UINT first, UINT count);
	void EmitDraws(const std::array<Mesh, 3>& meshes, ID3D11InputLayout* layout, UINT eyes);
};

GrassBucketRenderer::GrassBucketRenderer() : impl(std::make_unique<Impl>()) {}
GrassBucketRenderer::~GrassBucketRenderer() = default;
bool GrassBucketRenderer::IsHookInstalled() const { return impl->installed; }

void GrassBucketRenderer::RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path)
{
	if (!shape || !path)
		return;
	try {
		std::scoped_lock lock(impl->captureMutex);
		auto& identity = impl->identities[shape];
		if (!identity.first)
			identity.first = std::make_shared<Lifetime>();
		identity.second = path;
	} catch (const std::bad_alloc&) {
		logger::warn("Grass model tracking allocation failed; retaining full meshes");
	}
}
void GrassBucketRenderer::RemoveShape(RE::BSMultiStreamInstanceTriShape* shape)
{
	std::scoped_lock lock(impl->captureMutex);
	if (auto found = impl->identities.find(shape); found != impl->identities.end()) {
		if (found->second.first)
			found->second.first->alive.store(false, std::memory_order_release);
		impl->identities.erase(found);
	}
	impl->pending.erase(shape);
}

bool GrassBucketRenderer::CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape)
{
	auto& feature = globals::features::grassOptimizations;
	if (!feature.loaded || !feature.IsEnabled() || !impl->installed || impl->failed || !impl->context || !shape)
		return false;
	try {
		auto& geometry = shape->GetGeometryRuntimeData();
		auto& runtime = shape->GetMultiStreamTrishapeRuntimeData();
		if (!geometry.shaderProperty || geometry.shaderProperty->GetRTTI() != REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSGrassShaderProperty }.get() ||
			!geometry.rendererData || runtime.instanceSize != 16 || !Identity(shape->world))
			return false;
		auto property = static_cast<RE::BSGrassShaderProperty*>(geometry.shaderProperty.get());
		Source source{};
		source.capturedFrame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		source.identity = shape;
		source.property = geometry.shaderProperty;
		source.material = property->material;
		source.mesh = geometry.rendererData;
		source.vertices.copy_from(reinterpret_cast<ID3D11Buffer*>(source.mesh->vertexBuffer));
		source.indices.copy_from(reinterpret_cast<ID3D11Buffer*>(source.mesh->indexBuffer));
		source.descriptor = std::bit_cast<uint64_t>(geometry.vertexDesc);
		source.lights.assign(property->grassLightData.lights.begin(), property->grassLightData.lights.end());
		source.lightMask = property->grassLightData.activeLightMask;
		source.renderDistance = runtime.renderDistance;
		source.flags = property->flags.underlying();
		source.wavePeriod = property->wavePeriod;
		source.windTimer = property->windTimer;
		source.triangles = shape->GetTrishapeRuntimeData().triangleCount;
		source.origin = shape->world.translate;
		source.bound = shape->GetModelData().modelBound;
		if (!source.material || !source.vertices || !source.indices || !source.triangles || !Finite(source.origin) ||
			!std::isfinite(source.renderDistance) || !std::isfinite(source.wavePeriod) ||
			!Finite(source.bound.center) || !std::isfinite(source.bound.radius) || source.bound.radius <= 0)
			return false;
		D3D11_BUFFER_DESC vertices{}, indices{};
		source.vertices->GetDesc(&vertices);
		source.indices->GetDesc(&indices);
		const auto stride = GrassPolicy::MeshStride(source.descriptor);
		if (!stride || !(vertices.BindFlags & D3D11_BIND_VERTEX_BUFFER) || !(indices.BindFlags & D3D11_BIND_INDEX_BUFFER) ||
			uint64_t(stride) * shape->GetTrishapeRuntimeData().vertexCount > vertices.ByteWidth ||
			uint64_t(source.triangles) * 6 > indices.ByteWidth)
			return false;
		uint64_t instances = 0;
		for (uint32_t index = 0; index < runtime.instanceGroups.size(); ++index) {
			auto group = runtime.instanceGroups[index];
			if (!group || !group->instanceCount)
				continue;
			if (!group->vertexBuffer ||
				(group->isVisible && (index >= property->fadeAlphas.size() || !std::isfinite(property->fadeAlphas[index]))))
				return false;
			Group snapshot{ group->vertexBuffer, {}, group->instanceCount,
				group->isVisible ? property->fadeAlphas[index] : 1.0f, group->isVisible, {} };
			instances += snapshot.count;
			const auto bytes = uint64_t(snapshot.count) * GrassPolicy::kRecordBytes;
			if (instances > GrassPolicy::kMaxBatchInstances)
				return false;
			if (group->vertexBuffer->buffer) {
				snapshot.buffer.copy_from(reinterpret_cast<ID3D11Buffer*>(group->vertexBuffer->buffer));
				D3D11_BUFFER_DESC desc{};
				snapshot.buffer->GetDesc(&desc);
				if (bytes > desc.ByteWidth || desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
					return false;
			} else {
				if (!group->vertexBuffer->m_data || bytes > group->vertexBuffer->byteWidth)
					return false;
				const auto data = static_cast<const uint8_t*>(group->vertexBuffer->m_data);
				snapshot.bytes.assign(data, data + bytes);
			}
			source.groups.push_back(std::move(snapshot));
		}
		if (!instances)
			return false;
		std::scoped_lock lock(impl->captureMutex);
		if (impl->pending.size() >= GrassPolicy::kMaxFrameSources && !impl->pending.contains(shape))
			return false;
		auto& identity = impl->identities[shape];
		if (!identity.first)
			identity.first = std::make_shared<Lifetime>();
		source.lifetime = identity.first;
		source.model = identity.second;
		const auto now = globals::state->timer;
		std::unordered_map<RE::BSGraphics::VertexBuffer*, float> activeBirthTimes;
		for (auto& group : source.groups) {
			auto found = source.lifetime->birthTimes.find(group.identity);
			const auto born = found == source.lifetime->birthTimes.end() ? now : found->second;
			activeBirthTimes.emplace(group.identity, born);
			if (!group.visible)
				group.fade = impl->fadeInSeconds > 0 ? std::clamp((now - born) / impl->fadeInSeconds, 0.0f, 1.0f) : 1.0f;
		}
		source.lifetime->birthTimes = std::move(activeBirthTimes);
		impl->pending.insert_or_assign(shape, std::move(source));
		return true;
	} catch (const std::bad_alloc&) {
		logger::warn("Grass snapshot allocation failed; retaining native rendering");
	}
	return false;
}

void GrassBucketRenderer::SetupResources()
{
	try {
		const auto iniFloat = [](const char* key, float fallback) {
			auto setting = RE::GetINISetting(key);
			const auto value = setting ? setting->GetFloat() : fallback;
			return std::isfinite(value) && value >= 0 && value <= 100000 ? value : fallback;
		};
		impl->nativeRenderDistance = std::max(1.0f, iniFloat("fGrassStartFadeDistance:Grass", 6000) + iniFloat("fGrassFadeRange:Grass", 2000));
		if (auto setting = RE::GetINISetting("fGrassFadeInTime:Grass")) {
			const auto value = setting->GetFloat();
			impl->fadeInSeconds = std::isfinite(value) && value > 0 ? value : 0;
		}
		DX::ThrowIfFailed(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), impl->context.put_void()));
		impl->parameters = std::make_unique<Buffer>(ConstantBufferDesc(sizeof(Parameters)), nullptr, "GrassOptimizations::Parameters");
		impl->drawConstants = std::make_unique<Buffer>(ConstantBufferDesc(6 * 256), nullptr, "GrassOptimizations::DrawParameters");
		D3D11_SUBRESOURCE_DATA data{};
		D3D11_BUFFER_DESC desc = ConstantBufferDesc(64, false);
		std::array<uint8_t, 64> zeros{};
		data.pSysMem = zeros.data();
		impl->disabled = std::make_unique<Buffer>(desc, &data, "GrassOptimizations::NativeParameters");
		std::vector<std::pair<const char*, const char*>> defines;
		impl->cullShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::CullCS");
		impl->hiZ.SetupResources();
		impl->CompileSignature();
#ifdef DEVBENCH_BRIDGE_ENABLED
		defines.emplace_back("GRASS_DIAGNOSTICS", "1");
		impl->diagnosticShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::DiagnosticCullCS");
		impl->gpuCounters = RawBuffer(40, D3D11_BIND_UNORDERED_ACCESS, 0, "GrassOptimizations::Counters");
		D3D11_BUFFER_DESC staging{};
		staging.ByteWidth = 40;
		staging.Usage = D3D11_USAGE_STAGING;
		staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		for (size_t i = 0; i < impl->readbacks.size(); ++i) {
			DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&staging, nullptr, impl->readbacks[i].put()));
			Util::SetResourceName(impl->readbacks[i].get(), "GrassOptimizations::CounterReadback%zu", i);
		}
#endif
		impl->failed = false;
	} catch (const std::exception& error) {
		impl->failed = true;
		logger::error("Grass resources unavailable; retaining native rendering: {}", error.what());
	}
}

void GrassBucketRenderer::ClearShaderCache()
{
	impl->cullShader.Reset();
	impl->signature = nullptr;
	impl->layouts.clear();
	impl->hiZ.Reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
	impl->diagnosticShader.Reset();
#endif
	impl->failed = false;
}

void GrassBucketRenderer::Impl::PrepareFrame()
{
	const auto now = globals::state->frameCount;
	if (frame == now)
		return;
	frameSettings = globals::features::grassOptimizations.GetSettings();
	frameSettings.Enabled &= globals::features::grassOptimizations.loaded;
	frameSettings.EnableOcclusionCulling &= globals::features::grassOptimizations.IsGrassHiZAvailable();
	buckets.clear();
	admitted.clear();
	current = nullptr;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Source> sources;
	{
		std::scoped_lock lock(captureMutex);
		sources.swap(pending);
	}
	frame = now;
#ifdef DEVBENCH_BRIDGE_ENABLED
	PollCounters();
#endif
	if (!frameSettings.Enabled || failed)
		return;
	std::unordered_multimap<size_t, size_t> compatibleBuckets;
	for (auto& [identity, source] : sources) {
		if (source.capturedFrame != now || !source.lifetime->alive.load(std::memory_order_acquire))
			continue;
		uint32_t count = 0;
		for (const auto& group : source.groups) count += group.count;
		size_t index = buckets.size();
		const auto hash = CompatibilityHash(source);
		if (frameSettings.CrossCellBatching) {
			const auto [rangeBegin, rangeEnd] = compatibleBuckets.equal_range(hash);
			for (auto candidate = rangeBegin; candidate != rangeEnd; ++candidate) {
				const auto i = candidate->second;
				if (Compatible(buckets[i].sources.front(), source) && buckets[i].instances <= GrassPolicy::kMaxBatchInstances - count) {
					index = i;
					break;
				}
			}
		}
		if (index == buckets.size()) {
			buckets.emplace_back();
			compatibleBuckets.emplace(hash, index);
		}
		buckets[index].instances += count;
		buckets[index].sources.push_back(std::move(source));
		admitted.emplace(identity, index);
	}
}

void GrassBucketRenderer::PrepareGeometry(RE::BSRenderPass* pass)
{
	impl->current = nullptr;
	if (!pass || !pass->geometry || !impl->context || !impl->disabled)
		return;
	auto cb = impl->disabled->resource.get();
	impl->context->VSSetConstantBuffers(9, 1, &cb);
	try {
		impl->PrepareFrame();
		auto found = impl->admitted.find(static_cast<RE::BSMultiStreamInstanceTriShape*>(pass->geometry));
		if (found == impl->admitted.end())
			return;
		impl->current = found->first;
		impl->currentPass = pass->passEnum;
		// The representative translation is the one used by the native geometry constants.
		impl->currentOrigin = pass->geometry->world.translate;
		static REL::Relocation<void (*)(uint32_t)> setDirtyStates{ REL::RelocationID(75580, 77386) };
		setDirtyStates(0);
		auto& bucket = impl->buckets[found->second];
		const PassKey key{ impl->currentPass, globals::state->modifiedVertexDescriptor,
			globals::state->modifiedPixelDescriptor, globals::state->permutationData.ExtraShaderDescriptor };
		if (bucket.outcomes.try_emplace(key, Outcome::Native).second)
			impl->DrawBucket(bucket, key);
	} catch (const std::exception& error) {
		impl->failed = true;
		logger::error("Grass frame preparation failed; retaining native rendering: {}", error.what());
	}
}

void GrassBucketRenderer::Impl::EnsureCapacity(uint32_t instances, uint32_t slices)
{
	if (!instances || instances > GrassPolicy::kMaxBatchInstances || !slices || slices > 8192)
		throw std::length_error("Grass batch exceeds bounded scratch capacity");
	if (instances <= capacity && input && table && slices <= table->desc.ByteWidth / sizeof(Slice))
		return;
	const auto next = std::bit_ceil(instances);
	const auto slots = next * (globals::game::isVR ? 6u : 3u);
	auto nextInput = RawBuffer(next * 32, D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::Input");
	auto nextOutput = RawBuffer(slots * 32, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS, 0, "GrassOptimizations::Compacted");
	auto nextExtras = Structured<std::array<float, 4>>(slots, true, false, "GrassOptimizations::Extras");
	auto nextArgs = RawBuffer(192, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, "GrassOptimizations::IndirectArgs");
	auto nextTable = Structured<Slice>(std::bit_ceil(slices), false, true, "GrassOptimizations::Slices");
	input = std::move(nextInput);
	output = std::move(nextOutput);
	extras = std::move(nextExtras);
	arguments = std::move(nextArgs);
	table = std::move(nextTable);
	capacity = next;
}

void GrassBucketRenderer::Impl::CompileSignature()
{
	if (!signature) {
		winrt::com_ptr<ID3DBlob> errors;
		const auto result = D3DCompileFromFile(L"Data\\Shaders\\GrassOptimizations\\GrassInstanceSignatureVS.hlsl", nullptr, nullptr,
			"main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, signature.put(), errors.put());
		if (FAILED(result)) {
			if (errors)
				logger::error("Grass input signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
			DX::ThrowIfFailed(result);
		}
	}
}

ID3D11InputLayout* GrassBucketRenderer::Impl::Layout(uint64_t descriptor)
{
	if (auto found = layouts.find(descriptor); found != layouts.end())
		return found->second.get();
	CompileSignature();
	auto desc = std::bit_cast<RE::BSGraphics::VertexDesc>(descriptor);
	using Vertex = RE::BSGraphics::Vertex;
	if (!desc.HasFlag(Vertex::VF_VERTEX) || !desc.HasFlag(Vertex::VF_UV) || !desc.HasFlag(Vertex::VF_NORMAL) || !desc.HasFlag(Vertex::VF_COLORS))
		throw std::invalid_argument("Grass layout missing native attributes");
	const auto stride = GrassPolicy::MeshStride(descriptor);
	const bool fullPosition = desc.HasFlag(Vertex::VF_FULLPREC) || desc.GetAttributeOffset(Vertex::VA_TEXCOORD0) >= 16;
	if (stride < (fullPosition ? 16u : 8u) ||
		desc.GetAttributeOffset(Vertex::VA_TEXCOORD0) + 4 > stride ||
		desc.GetAttributeOffset(Vertex::VA_NORMAL) + 4 > stride ||
		desc.GetAttributeOffset(Vertex::VA_COLOR) + 4 > stride)
		throw std::invalid_argument("Grass attribute offsets exceed native mesh stride");
	std::array<D3D11_INPUT_ELEMENT_DESC, 8> elements{};
	elements[0] = { "POSITION", 0, fullPosition ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[1] = { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, desc.GetAttributeOffset(Vertex::VA_TEXCOORD0), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[2] = { "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, desc.GetAttributeOffset(Vertex::VA_NORMAL), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[3] = { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, desc.GetAttributeOffset(Vertex::VA_COLOR), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	for (UINT i = 0; i < 4; ++i) elements[4 + i] = { "TEXCOORD", 4 + i, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 8 * i, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
	winrt::com_ptr<ID3D11InputLayout> result;
	DX::ThrowIfFailed(globals::d3d::device->CreateInputLayout(elements.data(), UINT(elements.size()), signature->GetBufferPointer(), signature->GetBufferSize(), result.put()));
	Util::SetResourceName(result.get(), "GrassOptimizations::InputLayout");
	auto raw = result.get();
	layouts.emplace(descriptor, std::move(result));
	return raw;
}

const std::array<Mesh, 2>& GrassBucketRenderer::Impl::LODMeshes(const Source& source)
{
	auto& entries = lodMeshes[source.model];
	auto found = entries.find(source.descriptor);
	if (found != entries.end())
		return found->second;
	std::array<Mesh, 2> result;
	if (!source.model.empty()) {
		const auto normalized = std::filesystem::path(source.model).stem().string();
		for (unsigned tier = 0; tier < 2; ++tier) {
			const auto path = "LOD\\Grass\\" + normalized + (tier ? "_LOD1.nif" : "_LOD0.nif");
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.unk8 = false;
			args.unkA = false;
			args.postProcess = false;
			auto& mesh = result[tier];
			if (RE::BSModelDB::Demand(path.c_str(), mesh.owner, args) != RE::BSResource::ErrorCode::kNone || !mesh.owner)
				continue;
			auto shape = SingleShape(mesh.owner.get());
			if (!shape)
				continue;
			auto& geometry = shape->GetGeometryRuntimeData();
			if (!geometry.rendererData)
				continue;
			if (!geometry.shaderProperty)
				continue;
			mesh.texture.reset(geometry.shaderProperty->GetBaseTexture());
			mesh.descriptor = std::bit_cast<uint64_t>(geometry.vertexDesc) | 0x8000000000000080ull;
			if (mesh.descriptor != source.descriptor)
				continue;
			mesh.indexCount = shape->GetTrishapeRuntimeData().triangleCount * 3;
			auto bound = shape->GetModelData().modelBound;
			if (!mesh.indexCount || !Finite(bound.center) || !std::isfinite(bound.radius) || bound.radius <= 0)
				continue;
			mesh.radius = bound.center.Length() + bound.radius;
			mesh.vertices.copy_from(reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->vertexBuffer));
			mesh.indices.copy_from(reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->indexBuffer));
			if (!mesh.vertices || !mesh.indices)
				continue;
			D3D11_BUFFER_DESC vertices{}, indices{};
			mesh.vertices->GetDesc(&vertices);
			mesh.indices->GetDesc(&indices);
			if (!(vertices.BindFlags & D3D11_BIND_VERTEX_BUFFER) || !(indices.BindFlags & D3D11_BIND_INDEX_BUFFER) ||
				uint64_t(GrassPolicy::MeshStride(mesh.descriptor)) * shape->GetTrishapeRuntimeData().vertexCount > vertices.ByteWidth ||
				uint64_t(mesh.indexCount) * 2 > indices.ByteWidth) {
				mesh.vertices = nullptr;
				mesh.indices = nullptr;
			}
		}
	}
	return entries.emplace(source.descriptor, std::move(result)).first->second;
}

bool GrassBucketRenderer::Impl::HasBatchShader() const
{
	auto state = globals::state;
	if (!globals::features::grassOptimizations.loaded || !globals::shaderCache->IsEnabled() || !globals::shaderCache->IsEnableRequested() ||
		!state->currentShader || state->currentShader->shaderType.get() != RE::BSShader::Type::Grass || !globals::game::currentVertexShader)
		return false;
	const auto shader = globals::shaderCache->GetVertexShaderIfCached(*state->currentShader, state->modifiedVertexDescriptor);
	if (!shader || shader != *globals::game::currentVertexShader)
		return false;
	winrt::com_ptr<ID3D11VertexShader> bound;
	context->VSGetShader(bound.put(), nullptr, nullptr);
	return bound.get() == reinterpret_cast<ID3D11VertexShader*>(shader->shader);
}

bool GrassBucketRenderer::Impl::DrawBucket(Bucket& bucket, const PassKey& key)
{
	if (failed || !frameSettings.Enabled || !HasBatchShader())
		return false;
	{
		winrt::com_ptr<ID3D11DepthStencilView> depth;
		context->OMGetRenderTargets(0, nullptr, depth.put());
		if (!depth)
			return false;
		winrt::com_ptr<ID3D11Resource> target;
		depth->GetResource(target.put());
		const auto main = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
		if (target.get() != reinterpret_cast<ID3D11Resource*>(main))
			return false;
	}
	for (const auto& source : bucket.sources)
		if (!source.lifetime->alive.load(std::memory_order_acquire))
			return false;
	std::vector<std::pair<const char*, const char*>> defines;
	auto shader = cullShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::CullCS");
	if (!shader || !parameters || !drawConstants)
		return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (countersActive && diagnostics.load(std::memory_order_relaxed) && gpuCounters) {
		defines.emplace_back("GRASS_DIAGNOSTICS", "1");
		if (auto diagnostic = diagnosticShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::DiagnosticCullCS"))
			shader = diagnostic;
	}
#endif
	auto& representative = bucket.sources.front();
	std::array<Mesh, 3> meshes;
	meshes[0].vertices = representative.vertices;
	meshes[0].indices = representative.indices;
	meshes[0].descriptor = representative.descriptor;
	meshes[0].indexCount = representative.triangles * 3;
	float radius = representative.bound.radius;
	if (frameSettings.EnableMeshLOD && !representative.model.empty()) {
		const auto& lod = LODMeshes(representative);
		const auto compatibleLOD = [&](const Mesh& mesh) {
			return mesh.vertices && mesh.indices && mesh.descriptor == representative.descriptor &&
			       mesh.texture && mesh.texture.get() == representative.property->GetBaseTexture();
		};
		if (frameSettings.EnableMidLOD && compatibleLOD(lod[0]))
			meshes[1] = lod[0];
		if (frameSettings.EnableFarLOD) {
			if (compatibleLOD(lod[1]))
				meshes[2] = lod[1];
			else if (compatibleLOD(lod[0]))
				meshes[2] = lod[0];
		}
		for (size_t tier = 1; tier < meshes.size(); ++tier)
			if (meshes[tier].vertices && meshes[tier].indices)
				radius = std::max(radius, meshes[tier].radius + representative.bound.center.Length());
	}
	const uint32_t eyes = globals::game::isVR ? 2 : 1;
	auto layout = Layout(representative.descriptor);
	winrt::com_ptr<ID3D11Buffer> nativeGeometry;
	UINT first = 0, count = 0;
	context->VSGetConstantBuffers1(2, 1, nativeGeometry.put(), &first, &count);
	if (!nativeGeometry || count * 16 < (eyes == 2 ? 608u : 352u))
		return false;
	D3D11_BUFFER_DESC nativeDesc{};
	nativeGeometry->GetDesc(&nativeDesc);
	if (uint64_t(first) * 16 + (eyes == 2 ? 608 : 352) > nativeDesc.ByteWidth)
		return false;
	if (eyes == 2) {
		winrt::com_ptr<ID3D11Buffer> stereo;
		UINT stereoFirst = 0, stereoCount = 0;
		context->VSGetConstantBuffers1(13, 1, stereo.put(), &stereoFirst, &stereoCount);
		if (!stereo || stereoCount * 16 < 48)
			return false;
		D3D11_BUFFER_DESC stereoDesc{};
		stereo->GetDesc(&stereoDesc);
		if (uint64_t(stereoFirst) * 16 + 48 > stereoDesc.ByteWidth)
			return false;
	}
	std::vector<Slice> slices;
	uint32_t prefix = 0;
	for (const auto& source : bucket.sources)
		for (const auto& group : source.groups) {
			slices.push_back({ prefix, group.count, prefix + group.count, 0, { source.origin.x, source.origin.y, source.origin.z, group.fade } });
			prefix += group.count;
		}
	EnsureCapacity(bucket.instances, uint32_t(slices.size()));
	D3D11_VIEWPORT viewport{};
	UINT viewports = 1;
	context->RSGetViewports(&viewports, &viewport);
	if (!viewports || !std::isfinite(viewport.Height) || viewport.Height <= 0)
		return false;
	Parameters params{ bucket.instances, uint32_t(slices.size()), capacity, eyes,
		frameSettings.FrustumCulling, frameSettings.DensityReduction,
		meshes[1].vertices && meshes[1].indices ? 1u : 0u, meshes[2].vertices && meshes[2].indices ? 1u : 0u,
		frameSettings.MinPixelSize, frameSettings.FullDetailPixelSize, frameSettings.MinDensity, frameSettings.MeshLODBandPixels,
		frameSettings.MidLODPixelSize, frameSettings.FarLODPixelSize, frameSettings.OcclusionBias, viewport.Height,
		{ currentOrigin.x, currentOrigin.y, currentOrigin.z, 0 },
		{ representative.bound.center.x, representative.bound.center.y, representative.bound.center.z, radius },
		0, 0, 0, globals::features::grassCollision.loaded ? 1u : 0u, {},
		frameSettings.MeshCostBias, frameSettings.CostBiasStartDistance, frameSettings.InvisibleFadeCull,
		frameSettings.RenderDistanceOverride > 0 ? frameSettings.RenderDistanceOverride : nativeRenderDistance,
		frameSettings.EdgeFadeStart, frameSettings.SimpleShadingPixelSize, representative.bound.radius,
		std::sqrt(std::max(1.0f, representative.triangles / 8.0f)), frameSettings.RenderDistanceOverride > 0 ? 1u : 0u };
	if (frameSettings.EnableOcclusionCulling && globals::features::grassOptimizations.IsGrassHiZAvailable() && hiZ.Build(context.get(), frame)) {
		params.depthWidth = hiZ.Width();
		params.depthHeight = hiZ.Height();
		params.depthMips = hiZ.Mips();
		params.depthScale = hiZ.Scale();
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed) && frameSettings.EnableOcclusionCulling) {
		if (params.depthMips)
			++hiZBatches;
		else
			++hiZUnavailable;
	}
#endif
	std::array<uint8_t, 6 * 256> drawBytes{};
	std::array<uint32_t, 48> args{};
	for (uint32_t tier = 0; tier < 3; ++tier)
		for (uint32_t eye = 0; eye < eyes; ++eye) {
			const auto slot = tier * eyes + eye;
			const DrawConstants constants{ 1, eye, slot * capacity, 0, params.origin };
			std::memcpy(drawBytes.data() + slot * 256, &constants, sizeof(constants));
			args[slot * 8 + 3] = meshes[tier].indexCount;
		}
	Upload(context.get(), *drawConstants, drawBytes.data(), drawBytes.size());
	Upload(context.get(), *parameters, &params, sizeof(params));
	Upload(context.get(), *table, slices.data(), slices.size() * sizeof(Slice));
	context->UpdateSubresource(arguments->resource.get(), 0, nullptr, args.data(), 0, 0);
	DispatchBucket(bucket, shader, params, nativeGeometry.get(), first, count);
	// All allocations and uploads precede the first draw; never render native members afterward.
	bucket.outcomes.at(key) = Outcome::Batched;
	EmitDraws(meshes, layout, eyes);
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed)) {
		++batches;
		combinedSources += bucket.sources.size();
		combinedInstances += bucket.instances;
	}
#endif
	return true;
}

void GrassBucketRenderer::Impl::DispatchBucket(const Bucket& bucket, ID3D11ComputeShader* shader, const Parameters& params, ID3D11Buffer* nativeGeometry, UINT first, UINT count)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::PrepareAndCull");
#endif
	uint32_t offset = 0;
	for (const auto& source : bucket.sources)
		for (const auto& group : source.groups) {
			const UINT bytes = group.count * 32;
			const D3D11_BOX box{ 0, 0, 0, bytes, 1, 1 };
			if (group.buffer)
				context->CopySubresourceRegion(input->resource.get(), 0, offset, 0, 0, group.buffer.get(), 0, &box);
			else {
				const D3D11_BOX destination{ offset, 0, 0, offset + bytes, 1, 1 };
				context->UpdateSubresource(input->resource.get(), 0, &destination, group.bytes.data(), 0, 0);
			}
			offset += bytes;
		}
	{
		GrassD3D::ComputeState restore(context.get());
		auto cb = parameters->resource.get();
		auto native = nativeGeometry;
		winrt::com_ptr<ID3D11Buffer> stereo;
		UINT stereoFirst = 0, stereoCount = 0;
		if (globals::game::isVR) {
			context->VSGetConstantBuffers1(13, 1, stereo.put(), &stereoFirst, &stereoCount);
			auto buffer = stereo.get();
			context->CSSetConstantBuffers1(1, 1, &buffer, &stereoFirst, &stereoCount);
		}
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetConstantBuffers1(2, 1, &native, &first, &count);
		std::array<ID3D11ShaderResourceView*, 3> srvs{ input->srv.get(), table->srv.get(), params.depthMips ? hiZ.SRV() : nullptr };
		std::array<ID3D11UnorderedAccessView*, 4> uavs{ output->uav.get(), extras->uav.get(), arguments->uav.get(), nullptr };
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (shader == diagnosticShader.get())
			uavs[3] = gpuCounters->uav.get();
#endif
		context->CSSetShaderResources(0, UINT(srvs.size()), srvs.data());
		context->CSSetUnorderedAccessViews(0, UINT(uavs.size()), uavs.data(), nullptr);
		context->CSSetShader(shader, nullptr, 0);
		context->Dispatch((bucket.instances + 63) / 64, 1, 1);
	}
}

void GrassBucketRenderer::Impl::EmitDraws(const std::array<Mesh, 3>& meshes, ID3D11InputLayout* layout, UINT eyes)
{
	GrassD3D::DrawState restore(context.get());
	context->IASetInputLayout(layout);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto extra = extras->srv.get();
	context->VSSetShaderResources(2, 1, &extra);
	for (UINT tier = 0; tier < 3; ++tier) {
		if (!meshes[tier].vertices || !meshes[tier].indices)
			continue;
		const UINT stride = GrassPolicy::MeshStride(meshes[tier].descriptor);
		auto vertices = meshes[tier].vertices.get();
		UINT meshOffset = 0;
		context->IASetVertexBuffers(0, 1, &vertices, &stride, &meshOffset);
		context->IASetIndexBuffer(meshes[tier].indices.get(), DXGI_FORMAT_R16_UINT, 0);
		for (UINT eye = 0; eye < eyes; ++eye) {
			const auto slot = tier * eyes + eye;
			auto compacted = output->resource.get();
			const UINT instanceStride = 32, instanceOffset = slot * capacity * 32;
			context->IASetVertexBuffers(1, 1, &compacted, &instanceStride, &instanceOffset);
			auto drawCB = drawConstants->resource.get();
			const UINT begin = slot * 16, size = 16;
			context->VSSetConstantBuffers1(9, 1, &drawCB, &begin, &size);
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
			CS_GPU_PASS("GrassOptimizations::DrawIndirect");
#endif
			context->DrawIndexedInstancedIndirect(arguments->resource.get(), slot * 32 + 12);
		}
	}
}

void GrassBucketRenderer::DrawGroup(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* mesh,
	uint32_t firstTriangle, uint32_t triangles, uint32_t instances, RE::BSGraphics::VertexDesc descriptor, RE::BSGraphics::VertexBuffer* buffer)
{
	auto& self = *globals::features::grassOptimizations.GetRenderer().impl;
	try {
		if (self.current && self.admitted.contains(self.current)) {
			static REL::Relocation<void (*)(uint32_t)> setDirtyStates{ REL::RelocationID(75580, 77386) };
			setDirtyStates(0);
			auto& bucket = self.buckets[self.admitted.at(self.current)];
			const PassKey key{ self.currentPass, globals::state->modifiedVertexDescriptor, globals::state->modifiedPixelDescriptor, globals::state->permutationData.ExtraShaderDescriptor };
			auto source = std::find_if(bucket.sources.begin(), bucket.sources.end(), [&](const Source& item) { return item.identity == self.current; });
			const bool same = source != bucket.sources.end() && mesh == source->mesh && firstTriangle == 0 && triangles == source->triangles &&
			                  std::bit_cast<uint64_t>(descriptor) == source->descriptor &&
			                  std::any_of(source->groups.begin(), source->groups.end(), [&](const Group& group) {
								  return group.identity == buffer && buffer && (!group.bytes.empty() || reinterpret_cast<ID3D11Buffer*>(buffer->buffer) == group.buffer.get()) &&
				                         GrassPolicy::NativeCountMatches(globals::game::isVR, group.count, instances);
							  });
			if (auto found = bucket.outcomes.find(key); found != bucket.outcomes.end()) {
				if (found->second == Outcome::Batched && same)
					return;
			} else {
				bucket.outcomes.emplace(key, Outcome::Native);
				if (same) {
					try {
						if (self.DrawBucket(bucket, key))
							return;
					} catch (const std::exception& error) {
						self.failed = true;
						logger::error("Grass batch unavailable; retaining native rendering: {}", error.what());
					}
				}
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (self.diagnostics.load(std::memory_order_relaxed))
					++self.fallbacks;
#endif
			}
		}
	} catch (const std::exception& error) {
		self.failed = true;
		logger::error("Grass draw bookkeeping failed; retaining native rendering: {}", error.what());
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (self.diagnostics.load(std::memory_order_relaxed))
		++self.nativeDraws;
#endif
	originalDraw(renderer, mesh, firstTriangle, triangles, instances, descriptor, buffer);
}

void GrassBucketRenderer::InstallHooks()
{
	if (impl->installed)
		return;
	const auto callsite = REL::RelocationID(100847, 107637).address() + REL::Relocate(0x663, 0x64B);
	if (*reinterpret_cast<const uint8_t*>(callsite) != 0xE8) {
		logger::warn("Grass optimization hook unavailable: native draw instruction differs");
		return;
	}
	int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void*>(callsite + 1), sizeof(displacement));
	if (callsite + 5 + displacement != REL::RelocationID(75479, 77265).address()) {
		logger::warn("Grass optimization hook unavailable: native draw target differs");
		return;
	}
	originalDraw = SKSE::GetTrampoline().write_call<5>(callsite, DrawGroup);
	REL::Relocation<uintptr_t> table{ RE::VTABLE_BSMultiStreamInstanceTriShape[0] };
	VisibleHook::original = table.write_vfunc(REL::Relocate(0x34, 0x34, 0x35), VisibleHook::thunk);
	DestroyHook::original = table.write_vfunc(0, DestroyHook::thunk);
	const std::array<uintptr_t, 3> modelCalls{
		REL::RelocationID(15204, 15372).address() + REL::Relocate(0x2F5, 0x2F5, 0x305),
		REL::RelocationID(15205, 15373).address() + REL::Relocate(0x62B, 0x597, 0x590),
		REL::RelocationID(15206, 15374).address() + REL::Relocate(0x25C, 0x25C)
	};
	bool valid = true;
	uintptr_t target = 0;
	for (auto call : modelCalls) {
		if (*reinterpret_cast<const uint8_t*>(call) != 0xE8) {
			valid = false;
			break;
		}
		int32_t offset = 0;
		std::memcpy(&offset, reinterpret_cast<const void*>(call + 1), sizeof(offset));
		const auto destination = call + 5 + offset;
		if (target && destination != target) {
			valid = false;
			break;
		}
		target = destination;
	}
	if (valid) {
		ModelHook<0>::original = SKSE::GetTrampoline().write_call<5>(modelCalls[0], ModelHook<0>::thunk);
		ModelHook<1>::original = SKSE::GetTrampoline().write_call<5>(modelCalls[1], ModelHook<1>::thunk);
		ModelHook<2>::original = SKSE::GetTrampoline().write_call<5>(modelCalls[2], ModelHook<2>::thunk);
	} else
		logger::warn("Grass model tracking unavailable; retaining full meshes");
	impl->installed = true;
	logger::info("Installed native grass bucket hooks");
}

#ifdef DEVBENCH_BRIDGE_ENABLED
void GrassBucketRenderer::Impl::PollCounters()
{
	if (!gpuCounters)
		return;
	for (size_t i = 0; i < readbacks.size(); ++i) {
		if (!pendingReadbacks[i] || !readbacks[i])
			continue;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto result = context->Map(readbacks[i].get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (result == DXGI_ERROR_WAS_STILL_DRAWING)
			continue;
		if (SUCCEEDED(result)) {
			const auto values = static_cast<const uint32_t*>(mapped.pData);
			for (size_t counter = 0; counter < totals.size(); ++counter) totals[counter] += values[counter];
			context->Unmap(readbacks[i].get(), 0);
			++samples;
		} else {
			++droppedSamples;
			logger::warn("Grass counter readback failed: {:08X}", uint32_t(result));
		}
		pendingReadbacks[i] = false;
	}
	if (countersActive && counterFrame != frame) {
		auto free = std::find(pendingReadbacks.begin(), pendingReadbacks.end(), false);
		if (free != pendingReadbacks.end()) {
			const auto slot = size_t(free - pendingReadbacks.begin());
			if (readbacks[slot]) {
				context->CopyResource(readbacks[slot].get(), gpuCounters->resource.get());
				*free = true;
			}
		} else
			++droppedSamples;
	}
	if (diagnostics.load(std::memory_order_relaxed) && !diagnosticShader.get()) {
		std::vector<std::pair<const char*, const char*>> defines{ { "GRASS_DIAGNOSTICS", "1" } };
		diagnosticShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::DiagnosticCullCS");
	}
	countersActive = diagnostics.load(std::memory_order_relaxed) && diagnosticShader.get();
	if (countersActive) {
		const UINT zeros[4]{};
		context->ClearUnorderedAccessViewUint(gpuCounters->uav.get(), zeros);
		counterFrame = frame;
	}
}
void GrassBucketRenderer::SetDiagnosticsEnabled(bool enabled) { impl->diagnostics.store(enabled, std::memory_order_relaxed); }
json GrassBucketRenderer::GetDiagnostics() const
{
	auto& self = *impl;
	const auto value = [](const std::atomic_uint64_t& counter) { return counter.load(std::memory_order_relaxed); };
	return { { "hookInstalled", self.installed }, { "diagnosticsEnabled", self.diagnostics.load(std::memory_order_relaxed) },
		{ "batchedDraws", value(self.batches) }, { "combinedSources", value(self.combinedSources) }, { "combinedInstances", value(self.combinedInstances) },
		{ "nativeDraws", value(self.nativeDraws) }, { "fallbacks", value(self.fallbacks) },
		{ "hiZBatches", value(self.hiZBatches) }, { "hiZDepthFallbacks", value(self.hiZUnavailable) },
		{ "gpuSamples", value(self.samples) }, { "droppedGpuSamples", value(self.droppedSamples) },
		{ "eyeTests", value(self.totals[0]) }, { "frustumRejected", value(self.totals[1]) }, { "densityRejected", value(self.totals[2]) },
		{ "hiZRejected", value(self.totals[3]) }, { "fullMeshSurvivors", value(self.totals[4]) }, { "middleMeshSurvivors", value(self.totals[5]) },
		{ "farMeshSurvivors", value(self.totals[6]) }, { "invalidBoundsRetained", value(self.totals[7]) },
		{ "distanceRejected", value(self.totals[8]) }, { "fadeRejected", value(self.totals[9]) },
		{ "counterScope", "cumulative while enabled; GPU counters count eye instances and arrive asynchronously" } };
}
#endif
