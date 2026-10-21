#include "GrassBucketRenderer.h"

#include "Buffer.h"
#include "Features/GrassCollision.h"
#include "Features/GrassOptimizations.h"
#include "Globals.h"
#include "GpuPass.h"
#include "GrassD3DState.h"
#include "GrassFadeHistory.h"
#include "GrassFrustum.h"
#include "GrassHiZ.h"
#include "GrassLODCache.h"
#include "GrassPassCache.h"
#include "GrassRuntime.h"
#include "GrassTelemetry.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"
#include "Utils/LazyShader.h"
#include <DirectXPackedVector.h>

#include <bit>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <tuple>
#include <unordered_map>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <chrono>
#	include <unordered_set>
#endif

namespace
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr UINT kCullUAVCount = 4;
#else
	inline constexpr UINT kCullUAVCount = 3;
#endif
	struct QueueGroup : GrassPolicy::FrameQueue<RE::BSMultiStreamInstanceTriShape>
	{
		std::vector<RE::BSMultiStreamInstanceTriShape*> members;
		uint32_t instances = 0, slices = 0;
		bool batching = true;
		std::atomic_bool compatible{ true };
		GrassPolicy::WindHistory windHistory;  // Render-thread owned; visibility only selects the representative.
	};
	struct Lifetime
	{
		std::atomic_bool alive{ true };
		std::atomic_uint64_t generation{ 0 };
		std::atomic_uint64_t observedFrame{ UINT64_MAX };
		GrassPolicy::FadeHistory<RE::BSGraphics::VertexBuffer*> fadeHistory;
	};
	struct Group
	{
		RE::BSGraphics::VertexBuffer* identity;
		winrt::com_ptr<ID3D11Buffer> buffer;
		uint32_t count;
		float fade;
		bool visible;
		std::shared_ptr<const std::vector<uint8_t>> bytes;
		std::array<float, 3> lo{}, hi{};
		float minVariance = 0, maxVariance = 0, maxBasisNorm = 0;
		bool boundsKnown = false;
		float birthTime = 0;
	};
	struct Source
	{
		RE::BSMultiStreamInstanceTriShape* identity;
		std::shared_ptr<Lifetime> lifetime;
		RE::NiPointer<RE::BSShaderProperty> property;
		RE::NiPointer<RE::NiSourceTexture> texture;
		GrassRuntime::GroupLayout groupLayout;
		RE::BSShaderMaterial* material;
		RE::BSGraphics::TriShape* mesh;
		winrt::com_ptr<ID3D11Buffer> vertices, indices;
		uint64_t descriptor, flags;
		uint32_t triangles, vertexCount, vertexBytes, indexBytes, capturedFrame;
		GrassPolicy::Residency residency;
		uint64_t generation = 0;
		uint64_t nativeVisibleFrame = UINT64_MAX;
		uint64_t cpuRecordBytes = 0;
		float wavePeriod, renderDistance;
		RE::NiPoint3 origin;
		std::vector<RE::BSLight*> lights;
		uint32_t lightMask;
		RE::NiBound bound;
		std::string model;
		std::vector<Group> groups;
		std::shared_ptr<QueueGroup> queue;
	};
	struct Slice
	{
		uint32_t first, count, end, padding = 0;
		std::array<float, 4> originFade;
	};
	static_assert(sizeof(Slice) == 32);
	struct FrustumParameters
	{
		std::array<std::array<float, 4>, 12> planes{};
		uint32_t valid = 0;
		std::array<uint32_t, 3> padding{};
	};
	static_assert(sizeof(FrustumParameters) == 208);
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
		float edgeFadeStart, simpleShadingPixelSize, qualityRadius, padding;
		uint32_t overrideDistance;
		float collisionDistance;
		uint32_t sliceOffset = 0;
		float collisionStrength = GrassCollision::kDefaultCollisionStrength;
		std::array<uint32_t, 4> indexCounts{};
		std::array<float, 4> meshWeights{};
	};
	static_assert(sizeof(Parameters) == 208 && offsetof(Parameters, collisionStrength) == 172 &&
				  offsetof(Parameters, indexCounts) == 176 && offsetof(Parameters, meshWeights) == 192);
	using LODLookup = std::tuple<std::string_view, uint64_t, uintptr_t, uint32_t>;
	struct LODKey
	{
		std::string model;
		uint64_t descriptor;
		RE::NiPointer<RE::NiSourceTexture> texture;
		uint32_t tier;
		LODLookup Lookup() const
		{
			return { model, descriptor, reinterpret_cast<uintptr_t>(texture.get()), tier };
		}
		bool operator<(const LODKey& other) const { return Lookup() < other.Lookup(); }
		bool operator<(const LODLookup& other) const { return Lookup() < other; }
		friend bool operator<(const LODLookup& left, const LODKey& right) { return left < right.Lookup(); }
	};
	struct DrawConstants
	{
		uint32_t enabled, eye, base;
		float collisionDistance;
		std::array<float, 4> origin;
	};
	static_assert(sizeof(DrawConstants) == 32 && offsetof(DrawConstants, collisionDistance) == 12);
	struct Mesh
	{
		RE::NiPointer<RE::NiNode> owner;
		RE::NiPointer<RE::NiSourceTexture> texture;
		winrt::com_ptr<ID3D11Buffer> vertices, indices;
		uint64_t descriptor = 0;
		uint32_t indexCount = 0;
		float radius = 0;
	};
	using Outcome = GrassPassCache::Outcome;
	using PassKey = GrassPassCache::Key;
#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr uint32_t kDiagnosticCounterCount = 19;
	enum class CaptureRejection : size_t
	{
		Unavailable,
		Geometry,
		Source,
		Mesh,
		Fade,
		GroupCapacity,
		GroupBuffer,
		Empty,
		FrameCapacity,
		Allocation,
		Stale,
		Destroyed,
		Count
	};
	enum class DrawRejection : size_t
	{
		Shader,
		Capacity,
		DepthTarget,
		Destroyed,
		Regenerated,
		Resources,
		Layout,
		GeometryConstants,
		StereoConstants,
		Viewport,
		QueueMembership,
		Count
	};
#endif
	struct Admission
	{
		size_t bucket, source;
	};
	struct SliceRun
	{
		size_t source, firstGroup, lastGroup;
		uint32_t record, count;
		std::array<float, 3> lo{}, hi{};
		float minVariance, maxVariance, basis;
		bool known;
	};
	struct CullResult
	{
		std::unique_ptr<Buffer> output, extras, arguments;
		uint32_t capacity = 0, slots = 0;
	};
	struct CullPlan
	{
		std::array<float, 152> geometry{};
		FrustumParameters frustum;
		Parameters params{};
		std::array<Mesh, 3> meshes;
		ID3D11InputLayout* layout = nullptr;
		uint32_t slot = 0;
		bool prepared = false, empty = false;
		/** @brief Release draw owners; constants are overwritten before the plan is prepared again. */
		void Retire()
		{
			meshes = {};
			layout = nullptr;
			prepared = empty = false;
		}
	};
	inline constexpr uint32_t kFrameSlotBytes = 3072, kSlotsPerPage = 21;
	inline constexpr uint32_t kParametersOffset = 1024, kFrustumOffset = 1280, kDrawOffset = 1536;
	struct Bucket
	{
		std::vector<Source> sources;
		std::vector<SliceRun> runs;
		uint32_t instances = 0;
		GrassPassCache::Cache outcomes;
		std::unique_ptr<Buffer> records;
		CullResult result;
		CullPlan plan;
		uint32_t recordsCapacity = 0;
		bool recordsDirty = true;
		uint32_t dirtyFirstInstance = 0;
	};

	void CacheRuns(Bucket& bucket)
	{
		uint32_t record = 0;
		for (size_t source = 0; source < bucket.sources.size(); ++source) {
			const auto& groups = bucket.sources[source].groups;
			for (size_t index = 0; index < groups.size(); ++index) {
				const auto& group = groups[index];
				if (!bucket.runs.empty() && bucket.runs.back().source == source && bucket.runs.back().known && group.boundsKnown &&
					groups[bucket.runs.back().firstGroup].birthTime == group.birthTime) {
					auto& run = bucket.runs.back();
					run.lastGroup = index;
					run.count += group.count;
					for (size_t axis = 0; axis < 3; ++axis) {
						run.lo[axis] = std::min(run.lo[axis], group.lo[axis]);
						run.hi[axis] = std::max(run.hi[axis], group.hi[axis]);
					}
					run.minVariance = std::min(run.minVariance, group.minVariance);
					run.maxVariance = std::max(run.maxVariance, group.maxVariance);
					run.basis = std::max(run.basis, group.maxBasisNorm);
				} else
					bucket.runs.push_back({ source, index, index, record, group.count, group.lo, group.hi, group.minVariance, group.maxVariance, group.maxBasisNorm, group.boundsKnown });
				record += group.count;
			}
		}
	}

	size_t RecordLayoutHash(const Bucket& bucket)
	{
		size_t value = bucket.instances;
		const auto combine = [&](size_t item) {
			value ^= item + 0x9e3779b9 + (value << 6) + (value >> 2);
		};
		combine(bucket.sources.size());
		for (const auto& source : bucket.sources) {
			combine(std::hash<RE::BSMultiStreamInstanceTriShape*>{}(source.identity));
			combine(std::hash<uint64_t>{}(source.generation));
			combine(source.groups.size());
			for (const auto& group : source.groups) {
				combine(std::hash<RE::BSGraphics::VertexBuffer*>{}(group.identity));
				combine(group.count);
				combine(std::hash<ID3D11Buffer*>{}(group.buffer.get()));
				combine(std::hash<const std::vector<uint8_t>*>{}(group.bytes.get()));
			}
		}
		return value;
	}
	bool SameRecords(const Bucket& current, const Bucket& previous)
	{
		if (current.sources.size() != previous.sources.size() || current.instances != previous.instances)
			return false;
		for (size_t i = 0; i < current.sources.size(); ++i) {
			const auto& a = current.sources[i];
			const auto& b = previous.sources[i];
			if (a.identity != b.identity || a.generation != b.generation || a.groups.size() != b.groups.size())
				return false;
			for (size_t j = 0; j < a.groups.size(); ++j) {
				const auto& x = a.groups[j];
				const auto& y = b.groups[j];
				if (x.identity != y.identity || x.count != y.count || x.buffer != y.buffer || x.bytes != y.bytes)
					return false;
			}
		}
		return true;
	}
	uint32_t SharedRecordPrefix(const Bucket& current, const Bucket& previous)
	{
		uint32_t prefix = 0;
		for (size_t i = 0; i < std::min(current.sources.size(), previous.sources.size()); ++i) {
			const auto& a = current.sources[i];
			const auto& b = previous.sources[i];
			if (a.identity != b.identity || a.generation != b.generation)
				break;
			for (size_t j = 0; j < std::min(a.groups.size(), b.groups.size()); ++j) {
				const auto& x = a.groups[j];
				const auto& y = b.groups[j];
				if (x.identity != y.identity || x.count != y.count || x.buffer != y.buffer || x.bytes != y.bytes)
					return prefix;
				prefix += x.count;
			}
			if (a.groups.size() != b.groups.size())
				break;
		}
		return prefix;
	}
	void CacheRootBounds(Group& group, const void* bytes)
	{
		if (!bytes || !group.count)
			return;
		group.lo.fill(std::numeric_limits<float>::max());
		group.hi.fill(std::numeric_limits<float>::lowest());
		group.minVariance = std::numeric_limits<float>::max();
		group.maxVariance = std::numeric_limits<float>::lowest();
		for (uint32_t instance = 0; instance < group.count; ++instance) {
			std::array<uint16_t, 16> packed{};
			std::memcpy(packed.data(), static_cast<const uint8_t*>(bytes) + instance * GrassPolicy::kRecordBytes, sizeof(packed));
			std::array<float, 16> record{};
			for (size_t i = 0; i < record.size(); ++i) {
				record[i] = DirectX::PackedVector::XMConvertHalfToFloat(packed[i]);
				if (!std::isfinite(record[i]))
					return;
			}
			for (size_t axis = 0; axis < group.lo.size(); ++axis) {
				group.lo[axis] = std::min(group.lo[axis], record[axis]);
				group.hi[axis] = std::max(group.hi[axis], record[axis]);
			}
			group.minVariance = std::min(group.minVariance, record[13]);
			group.maxVariance = std::max(group.maxVariance, record[13]);
			const std::array<std::array<float, 3>, 3> rows{ std::array{ record[4], record[5], record[6] },
				std::array{ record[8], record[9], record[10] }, std::array{ record[12], record[7], record[11] } };
			group.maxBasisNorm = std::max(group.maxBasisNorm, GrassPolicy::BasisNorm(rows));
		}
		group.boundsKnown = true;
	}
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
	bool CurrentContract(const Source& source)
	{
		if (!source.property || source.property->material != source.material || source.property->flags.underlying() != source.flags ||
			source.property->GetBaseTexture() != source.texture.get())
			return false;
		const auto& data = GrassRuntime::GetPropertyData(static_cast<const RE::BSGrassShaderProperty*>(source.property.get()), globals::game::isVR);
		return GrassRuntime::MatchesPersistentProperty(data, source.wavePeriod, source.lightMask, source.lights);
	}
	bool CurrentGroups(const Source& source)
	{
		return source.groupLayout == GrassRuntime::CaptureGroupLayout(source.identity->GetMultiStreamTrishapeRuntimeData());
	}
	// The capture lock and live lifetime token keep the native shape valid during these reads.
	bool CurrentGeometry(const Source& source)
	{
		auto& shape = *source.identity;
		const auto& geometry = shape.GetGeometryRuntimeData();
		const auto& runtime = shape.GetMultiStreamTrishapeRuntimeData();
		const auto& mesh = shape.GetTrishapeRuntimeData();
		const auto& bound = shape.GetModelData().modelBound;
		return geometry.shaderProperty.get() == source.property.get() &&
		       geometry.rendererData && geometry.rendererData == source.mesh &&
		       source.vertices.get() == reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->vertexBuffer) &&
		       source.indices.get() == reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->indexBuffer) &&
		       source.descriptor == std::bit_cast<uint64_t>(geometry.vertexDesc) &&
		       source.triangles == mesh.triangleCount && source.vertexCount == mesh.vertexCount &&
		       Identity(shape.world) && source.origin == shape.world.translate &&
		       source.bound.center == bound.center && source.bound.radius == bound.radius &&
		       source.renderDistance == runtime.renderDistance;
	}
	bool SameMaterial(const Source& a, const Source& b)
	{
		return a.material == b.material ||
		       (!globals::features::truePBR.IsPBRGrassMaterial(a.material) &&
				   !globals::features::truePBR.IsPBRGrassMaterial(b.material) &&
				   a.property->GetBaseTexture() == b.property->GetBaseTexture() &&
				   a.material->DoIsCopy(b.material) && b.material->DoIsCopy(a.material));
	}
	bool Compatible(const Source& a, const Source& b)
	{
		if (!CurrentContract(a) || !CurrentContract(b))
			return false;
		const bool sameBuffers = a.vertices == b.vertices && a.indices == b.indices;
		const bool sameModelMesh = !a.model.empty() && a.model == b.model &&
		                           a.vertexCount == b.vertexCount && a.vertexBytes == b.vertexBytes && a.indexBytes == b.indexBytes;
		return (sameBuffers || sameModelMesh) && SameMaterial(a, b) &&
		       a.descriptor == b.descriptor && a.flags == b.flags && a.triangles == b.triangles &&
		       a.lights == b.lights && a.lightMask == b.lightMask && a.renderDistance == b.renderDistance && a.wavePeriod == b.wavePeriod && a.model == b.model &&
		       a.bound.radius == b.bound.radius && a.bound.center == b.bound.center;
	}
	size_t CompatibilityHash(const Source& source)
	{
		size_t value = 0;
		const auto combine = [&]<class T>(const T& item) {
			value ^= std::hash<T>{}(item) + 0x9e3779b9 + (value << 6) + (value >> 2);
		};
		if (source.model.empty()) {
			combine(source.vertices.get());
			combine(source.indices.get());
		} else {
			combine(source.vertexCount);
			combine(source.vertexBytes);
			combine(source.indexBytes);
		}
		if (source.model.empty())
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
			auto& feature = globals::features::grassOptimizations;
			auto& renderer = feature.GetRenderer();
			try {
				if (process && feature.loaded && feature.IsEnabled() && renderer.IsRenderingAvailable() &&
					renderer.QueueVisible(shape, process, index))
					return;
			} catch (const std::exception& error) {
				logger::warn("Grass queue unavailable; retaining native rendering: {}", error.what());
			}
			original(shape, process, index);
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
	struct RemovedGroupHook
	{
		static void thunk(RE::BSMultiStreamInstanceTriShape* shape, uint32_t group)
		{
			globals::features::grassOptimizations.GetRenderer().MarkGroupsChanged(shape, group);
			original(shape, group);
		}
		static inline REL::Relocation<decltype(thunk)> original;
	};
	struct AddedGroupHook
	{
		static uint32_t thunk(RE::BSMultiStreamInstanceTriShape* shape, uint32_t count, uint16_t& data, uint32_t arg, float fade)
		{
			globals::features::grassOptimizations.GetRenderer().MarkGroupsChanged(shape);
			const auto group = original(shape, count, data, arg, fade);
			globals::features::grassOptimizations.GetRenderer().RecordGeneratedGroups(shape, { &group, 1 }, true);
			return group;
		}
		static inline REL::Relocation<decltype(thunk)> original;
	};
	struct GeneratedHook
	{
		static void thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSTArray<uint32_t>& instances)
		{
			const auto previousCount = instances.size();
			original(shape, instances);
			globals::features::grassOptimizations.GetRenderer().MarkGenerated(shape);
			if (instances.size() > previousCount)
				globals::features::grassOptimizations.GetRenderer().RecordGeneratedGroups(shape,
					{ instances.data() + previousCount, instances.size() - previousCount });
			globals::features::grassOptimizations.GetRenderer().CaptureVisible(shape, false);
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
	std::shared_mutex captureMutex;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Source> residents;
	std::unordered_multimap<size_t, std::weak_ptr<QueueGroup>> queues;
	uint32_t nativeFallbackDepth = 0;
	uint64_t residentRevision = 0, preparedRevision = UINT64_MAX;
	uint64_t cpuRecordBytes = 0;
	bool preparedBatching = false;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::pair<std::shared_ptr<Lifetime>, std::string>> identities;
	std::vector<Bucket> buckets;
	std::vector<Bucket> dormantBuckets;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Admission> admitted;
	std::vector<Slice> cullingSlices, frameSlices;
	std::vector<std::array<std::array<float, 3>, 2>> runBounds;
	std::vector<std::unique_ptr<Buffer>> frameConstants;
	std::vector<Bucket*> cullingBuckets;
	bool cullingPrepared = false;
	uint64_t resultBytes = 0;
	GrassLOD::Cache<LODKey, Mesh> lodMeshes;
	uint64_t lodRevision = UINT64_MAX;
	uint32_t lodMode = 0;
	std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> layouts;
	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3D11DeviceContext1> context;
	Util::LazyShader<ID3D11ComputeShader> cullShader;
	GrassHiZ hiZ;
	std::unique_ptr<Buffer> table, disabled;
	std::array<float, 152> geometryCPU{};
	bool geometryCPUValid = false;
	FrustumParameters frustumData;
	float nativeRenderDistance = 8000.0f;
	uint32_t frame = UINT32_MAX;
	uint64_t residentRecordBytes = 0;
	bool installed = false;
	std::atomic_bool failed{ false };
	float fadeInSeconds = 0;
	RE::BSMultiStreamInstanceTriShape* current = nullptr;
	RE::BSRenderPass* currentSetupPass = nullptr;
	RE::BSShader* currentNativeShader = nullptr;
	GrassRuntime::SetupGeometry currentSetup = nullptr;
	uint32_t currentSetupFlags = 0;
	std::optional<uint32_t> currentNativeWindTimer;
	Admission currentAdmission{};
	uint32_t currentPass = 0;
	RE::NiPoint3 currentOrigin;
	GrassPolicy::Settings frameSettings;
	float frameCollisionStrength = GrassCollision::kDefaultCollisionStrength;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool diagnostics{ false };
	GrassTelemetry::Channel telemetry;
	GrassTelemetry::Work frameWork;
	D3D11_VIEWPORT telemetryViewport{};
	uint32_t telemetryViewportCount = 0;
	std::atomic_uint64_t publishedResultBytes{ 0 };
	std::atomic_bool telemetryFailed{ false };
	void PublishTelemetry();
	Util::LazyShader<ID3D11ComputeShader> diagnosticShader;
	std::unique_ptr<Buffer> gpuCounters;
	std::array<winrt::com_ptr<ID3D11Buffer>, 4> readbacks;
	std::array<bool, 4> pendingReadbacks{};
	uint32_t counterFrame = UINT32_MAX;
	bool countersActive = false;
	std::array<std::atomic_uint64_t, kDiagnosticCounterCount> totals{};
	std::atomic_uint64_t hiZBatches{ 0 }, hiZUnavailable{ 0 };
	std::atomic_uint64_t nativeDraws{ 0 }, batches{ 0 }, combinedSources{ 0 }, combinedInstances{ 0 }, fallbacks{ 0 }, samples{ 0 }, droppedSamples{ 0 };
	std::atomic_uint64_t reusedRecordBuckets{ 0 }, uploadedRecordBuckets{ 0 }, uploadedRecordBytes{ 0 };
	std::atomic_uint64_t persistentBucketFrames{ 0 }, bucketRebuilds{ 0 }, cachedSources{ 0 };
	std::atomic_uint64_t pressureEvictions{ 0 };
	std::atomic_uint64_t nativeVisibilityBypassed{ 0 }, queuedRepresentatives{ 0 }, queueDuplicates{ 0 };
	std::atomic_uint64_t cullingDispatches{ 0 }, cullingUploadMaps{ 0 }, reusedCullingDraws{ 0 };
	std::atomic_uint64_t coarseRejectedSlices{ 0 }, coarseRejectedInstances{ 0 };
	std::atomic_uint64_t captureAttempts{ 0 }, capturedSources{ 0 }, admittedSources{ 0 }, drawAttempts{ 0 };
	std::atomic_uint64_t unidentifiedModels{ 0 }, sameModelPeers{ 0 }, sameModelCompatible{ 0 };
	std::array<std::atomic_uint64_t, 7> sameModelMismatches{};
	std::array<std::atomic_uint64_t, size_t(CaptureRejection::Count)> captureRejections{};
	std::array<std::atomic_uint64_t, size_t(DrawRejection::Count)> drawRejections{};
	void RejectCapture(CaptureRejection reason) { ++captureRejections[size_t(reason)]; }
	void RejectDraw(DrawRejection reason) { ++drawRejections[size_t(reason)]; }
	void PollCounters();
#endif
	bool RefreshSource(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible);
	void EraseResident(RE::BSMultiStreamInstanceTriShape* shape);
	void AssignQueue(Source& source);
	void MakeNativeVisible(RE::BSMultiStreamInstanceTriShape* shape);
	bool MakeResidentRoom(const Source& source);
	void PrepareFrame();
	void RefreshFades();
	void ReuseRecordBuffers(std::vector<Bucket>& previousBuckets);
	bool EnsureResult(Bucket& bucket, uint32_t slots);
	bool EnsureRecords(Bucket& bucket);
	bool MakeRecordRoom(uint64_t replacementBytes);
	void PrepareCulling(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry);
	void CopyGeometryConstants();
	void RestoreCurrentGeometry();
	bool PrepareBucket(Bucket& bucket);
	void UploadCulling();
	ID3D11InputLayout* Layout(uint64_t descriptor);
	void CompileSignature();
	void UpdateLODDemand(const GrassPolicy::Settings& settings);
	std::array<const Mesh*, 2> LODMeshes(const Source& source);
	static Mesh LoadLODMesh(const LODKey& key);
	bool HasBatchShader() const;
	void CullSlices(const Bucket& bucket, float radius, uint32_t eyes, uint32_t& instances);
	bool DrawBucket(Bucket& bucket, Outcome& outcome);
	bool MainDepthBound() const;
	bool HasBatchDrawState() const;
	float frameHeight = 0;
	void UploadRecords(Bucket& bucket);
	void DispatchBucket(Bucket& bucket);
	void EmitDraws(const Bucket& bucket);
};

GrassBucketRenderer::GrassBucketRenderer() : impl(std::make_unique<Impl>()) {}
GrassBucketRenderer::~GrassBucketRenderer() = default;
bool GrassBucketRenderer::IsHookInstalled() const { return impl->installed; }
bool GrassBucketRenderer::IsRenderingAvailable() const { return impl->installed && !impl->failed.load(std::memory_order_relaxed); }

void GrassBucketRenderer::RecordModel(RE::BSMultiStreamInstanceTriShape* shape, const char* path)
{
	if (!shape || !path)
		return;
	try {
		std::scoped_lock lock(impl->captureMutex);
		auto& identity = impl->identities[shape];
		if (!identity.first)
			identity.first = std::make_shared<Lifetime>();
		if (identity.second != path) {
			identity.second = path;
			if (auto found = impl->residents.find(shape); found != impl->residents.end()) {
				found->second.model = identity.second;
				++impl->residentRevision;
			}
		}
	} catch (const std::bad_alloc&) {
		logger::warn("Grass model tracking allocation failed; retaining full meshes");
	}
}
void GrassBucketRenderer::MarkGenerated(RE::BSMultiStreamInstanceTriShape* shape)
{
	MarkGroupsChanged(shape);
}
void GrassBucketRenderer::MarkGroupsChanged(RE::BSMultiStreamInstanceTriShape* shape, std::optional<uint32_t> removedGroup)
{
	std::scoped_lock lock(impl->captureMutex);
	impl->EraseResident(shape);
	++impl->residentRevision;
	if (auto found = impl->identities.find(shape); found != impl->identities.end() && found->second.first) {
		++found->second.first->generation;
		if (removedGroup) {
			const auto& groups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
			if (*removedGroup < groups.size() && groups[*removedGroup])
				found->second.first->fadeHistory.Removed(groups[*removedGroup]->vertexBuffer);
		}
	}
}
void GrassBucketRenderer::RecordGeneratedGroups(RE::BSMultiStreamInstanceTriShape* shape,
	std::span<const uint32_t> groupIndices, bool replace)
{
	if (!shape || groupIndices.empty())
		return;
	const auto property = shape->GetGeometryRuntimeData().shaderProperty.get();
	if (!property || property->GetRTTI() != REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSGrassShaderProperty }.get())
		return;
	try {
		std::scoped_lock lock(impl->captureMutex);
		auto& lifetime = impl->identities[shape].first;
		if (!lifetime)
			lifetime = std::make_shared<Lifetime>();
		const auto& groups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
		const auto now = globals::state->timer;
		for (auto index : groupIndices) {
			if (index >= groups.size() || !groups[index] || !groups[index]->vertexBuffer || !groups[index]->instanceCount)
				continue;
			lifetime->fadeHistory.Generated(groups[index]->vertexBuffer, now, replace);
		}
	} catch (const std::exception& error) {
		logger::warn("Grass spawn fade history unavailable; retaining visible grass: {}", error.what());
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
	impl->EraseResident(shape);
	++impl->residentRevision;
}

void GrassBucketRenderer::Impl::EraseResident(RE::BSMultiStreamInstanceTriShape* shape)
{
	if (auto found = residents.find(shape); found != residents.end()) {
		cpuRecordBytes -= found->second.cpuRecordBytes;
		if (auto queue = found->second.queue) {
			std::erase(queue->members, shape);
			for (const auto& group : found->second.groups)
				queue->instances -= group.count;
			queue->slices -= uint32_t(found->second.groups.size());
		}
		residents.erase(found);
	}
}

void GrassBucketRenderer::Impl::AssignQueue(Source& source)
{
	const bool batching = globals::features::grassOptimizations.IsCrossCellBatchingEnabled();
	uint32_t count = 0;
	for (const auto& group : source.groups) count += group.count;
	const auto hash = CompatibilityHash(source);
	const auto [begin, end] = queues.equal_range(hash);
	if (batching) {
		for (auto candidate = begin; candidate != end; ++candidate) {
			auto queue = candidate->second.lock();
			if (!queue || !queue->compatible.load(std::memory_order_relaxed) || !queue->batching || queue->members.empty() || queue->instances > GrassPolicy::kMaxBatchInstances - count ||
				queue->slices > GrassPolicy::kMaxBatchSlices - source.groups.size())
				continue;
			const auto representative = residents.find(queue->members.front());
			if (representative != residents.end() && Compatible(representative->second, source)) {
				source.queue = std::move(queue);
				break;
			}
		}
	}
	if (!source.queue) {
		source.queue = std::make_shared<QueueGroup>();
		source.queue->batching = batching;
		queues.emplace(hash, source.queue);
	}
	source.queue->members.push_back(source.identity);
	source.queue->instances += count;
	source.queue->slices += uint32_t(source.groups.size());
}

void GrassBucketRenderer::Impl::MakeNativeVisible(RE::BSMultiStreamInstanceTriShape* shape)
{
	std::scoped_lock lock(captureMutex);
	const auto found = residents.find(shape);
	if (found == residents.end())
		return;
	auto& source = found->second;
	const auto now = globals::state->frameCount;
	if (source.nativeVisibleFrame == now)
		return;
	auto& runtime = shape->GetMultiStreamTrishapeRuntimeData();
	auto property = shape->GetGeometryRuntimeData().shaderProperty.get();
	if (!property || property->GetRTTI() != REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSGrassShaderProperty }.get())
		return;
	auto& data = GrassRuntime::GetPropertyData(static_cast<RE::BSGrassShaderProperty*>(property), globals::game::isVR);
	data.fadeAlphas.resize(runtime.instanceGroups.size());
	for (uint32_t group = 0; group < runtime.instanceGroups.size(); ++group) {
		auto live = runtime.instanceGroups[group];
		if (!live || !live->instanceCount)
			continue;
		const auto time = source.lifetime->fadeHistory.Start(live->vertexBuffer);
		live->isVisible = true;
		data.fadeAlphas[group] = fadeInSeconds > 0 ? std::clamp((globals::state->timer - time) / fadeInSeconds, 0.0f, 1.0f) : 1.0f;
	}
	source.nativeVisibleFrame = now;
}

bool GrassBucketRenderer::QueueVisible(RE::BSMultiStreamInstanceTriShape* shape, RE::NiCullingProcess* process, int32_t index)
{
	if (!shape || !process)
		return false;
	const auto now = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	const bool batching = globals::features::grassOptimizations.IsCrossCellBatchingEnabled();
	std::shared_ptr<QueueGroup> queue;
	{
		std::shared_lock lock(impl->captureMutex);
		const auto found = impl->residents.find(shape);
		if (found != impl->residents.end()) {
			const auto& source = found->second;
			if (source.queue && source.queue->compatible.load(std::memory_order_relaxed) && source.queue->batching == batching && source.lifetime->alive.load(std::memory_order_acquire) &&
				source.generation == source.lifetime->generation.load(std::memory_order_acquire) && CurrentGeometry(source) && CurrentContract(source) && CurrentGroups(source)) {
				queue = source.queue;
				source.lifetime->observedFrame.store(now, std::memory_order_relaxed);
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (impl->diagnostics.load(std::memory_order_relaxed))
					++impl->cachedSources;
#endif
			}
		}
	}
	if (!queue) {
		if (!CaptureVisible(shape, false))
			return false;
		std::scoped_lock lock(impl->captureMutex);
		const auto found = impl->residents.find(shape);
		if (found == impl->residents.end())
			return false;
		auto& source = found->second;
		if (!source.queue || !source.queue->compatible.load(std::memory_order_relaxed) || source.queue->batching != batching) {
			if (source.queue) {
				std::erase(source.queue->members, shape);
				for (const auto& group : source.groups) source.queue->instances -= group.count;
				source.queue->slices -= uint32_t(source.groups.size());
			}
			source.queue.reset();
			impl->AssignQueue(source);
			++impl->residentRevision;
		}
		source.lifetime->observedFrame.store(now, std::memory_order_relaxed);
		queue = source.queue;
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (impl->diagnostics.load(std::memory_order_relaxed))
		++impl->nativeVisibilityBypassed;
#endif
	// Rendering consumes the representative after all visibility workers finish.
	if (queue->Claim(now, shape)) {
		process->AppendVirtual(*shape, index);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (impl->diagnostics.load(std::memory_order_relaxed))
			++impl->queuedRepresentatives;
#endif
		return true;
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (impl->diagnostics.load(std::memory_order_relaxed))
		++impl->queueDuplicates;
#endif
	return true;
}

// The caller holds captureMutex; currently visible sources take priority over dormant snapshots.
bool GrassBucketRenderer::Impl::MakeResidentRoom(const Source& source)
{
	if (source.cpuRecordBytes > GrassPolicy::kMaxCpuRecordBytes)
		return false;
	const auto existing = residents.find(source.identity);
	const bool replacing = existing != residents.end();
	const uint64_t previousBytes = replacing ? existing->second.cpuRecordBytes : 0;
	const auto fits = [&]() {
		return (replacing || residents.size() < GrassPolicy::kMaxFrameSources) &&
		       GrassPolicy::SnapshotBudgetValid(cpuRecordBytes, previousBytes, source.cpuRecordBytes);
	};
	while (!fits()) {
		if (!source.residency.VisibleInFrame(source.capturedFrame) && source.lifetime->observedFrame.load(std::memory_order_relaxed) != source.capturedFrame)
			return false;
		auto oldest = residents.end();
		for (auto it = residents.begin(); it != residents.end(); ++it) {
			if (it->first == source.identity || it->second.residency.VisibleInFrame(source.capturedFrame) || it->second.lifetime->observedFrame.load(std::memory_order_relaxed) == source.capturedFrame)
				continue;
			if (oldest == residents.end() ||
				it->second.residency.IdleFrames(source.capturedFrame, it->second.lifetime->observedFrame.load(std::memory_order_relaxed)) >
					oldest->second.residency.IdleFrames(source.capturedFrame, oldest->second.lifetime->observedFrame.load(std::memory_order_relaxed)))
				oldest = it;
		}
		if (oldest == residents.end())
			return false;
		++oldest->second.lifetime->generation;
		EraseResident(oldest->first);
		++residentRevision;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed))
			++pressureEvictions;
#endif
	}
	return true;
}

bool GrassBucketRenderer::Impl::RefreshSource(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible)
{
	auto& runtime = shape->GetMultiStreamTrishapeRuntimeData();
	auto& geometry = shape->GetGeometryRuntimeData();
	auto property = static_cast<RE::BSGrassShaderProperty*>(geometry.shaderProperty.get());
	auto& grassData = GrassRuntime::GetPropertyData(property, globals::game::isVR);
	std::scoped_lock lock(captureMutex);
	const auto found = residents.find(shape);
	if (found != residents.end()) {
		auto& cached = found->second;
		bool unchanged = cached.lifetime->alive.load(std::memory_order_acquire) &&
		                 cached.generation == cached.lifetime->generation.load(std::memory_order_acquire) &&
		                 CurrentGeometry(cached) && CurrentContract(cached) && CurrentGroups(cached);
		size_t groupIndex = 0;
		for (auto group : runtime.instanceGroups) {
			if (!group || !group->instanceCount)
				continue;
			if (groupIndex >= cached.groups.size()) {
				unchanged = false;
				break;
			}
			const auto& snapshot = cached.groups[groupIndex++];
			unchanged &= group->vertexBuffer == snapshot.identity && group->instanceCount == snapshot.count &&
			             group->vertexBuffer && (!snapshot.buffer || reinterpret_cast<ID3D11Buffer*>(group->vertexBuffer->buffer) == snapshot.buffer.get());
		}
		unchanged &= groupIndex == cached.groups.size();
		if (unchanged) {
			if (nativeVisible)
				cached.nativeVisibleFrame = UINT64_MAX;
			groupIndex = 0;
			for (uint32_t index = 0; index < runtime.instanceGroups.size(); ++index) {
				auto group = runtime.instanceGroups[index];
				if (!group || !group->instanceCount)
					continue;
				auto& snapshot = cached.groups[groupIndex++];
				const float fade = fadeInSeconds > 0 ? std::clamp((globals::state->timer - snapshot.birthTime) / fadeInSeconds, 0.0f, 1.0f) : 1.0f;
				snapshot.visible = nativeVisible && group->isVisible;
				snapshot.fade = snapshot.visible && index < grassData.fadeAlphas.size() ? grassData.fadeAlphas[uint32_t(index)] : fade;
				if (!std::isfinite(snapshot.fade))
					return false;
			}
			cached.capturedFrame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
			cached.residency.Observe(cached.capturedFrame, nativeVisible);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics.load(std::memory_order_relaxed))
				++cachedSources;
#endif
			return true;
		}
	}
	return false;
}

bool GrassBucketRenderer::CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape, bool nativeVisible)
{
	auto& feature = globals::features::grassOptimizations;
	if (!feature.loaded || !feature.IsEnabled())
		return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (impl->diagnostics.load(std::memory_order_relaxed))
		++impl->captureAttempts;
#	define GRASS_CAPTURE_REJECT(reason) (impl->diagnostics.load(std::memory_order_relaxed) ? impl->RejectCapture(CaptureRejection::reason) : void(), false)
#else
#	define GRASS_CAPTURE_REJECT(reason) false
#endif
	if (!impl->installed || impl->failed || !impl->context || !shape)
		return GRASS_CAPTURE_REJECT(Unavailable);
	bool captured = false;
	const SKSE::stl::scope_exit retireRejected([&]() noexcept {
		if (captured)
			return;
		std::scoped_lock lock(impl->captureMutex);
		if (auto found = impl->residents.find(shape); found != impl->residents.end()) {
			// Reject prepared buckets as well as the resident snapshot on capture failure.
			++found->second.lifetime->generation;
			impl->EraseResident(shape);
			++impl->residentRevision;
		}
	});
	try {
		auto& geometry = shape->GetGeometryRuntimeData();
		auto& runtime = shape->GetMultiStreamTrishapeRuntimeData();
		if (!geometry.shaderProperty || geometry.shaderProperty->GetRTTI() != REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSGrassShaderProperty }.get() ||
			!geometry.rendererData || !Identity(shape->world))
			return GRASS_CAPTURE_REJECT(Geometry);
		auto property = static_cast<RE::BSGrassShaderProperty*>(geometry.shaderProperty.get());
		auto& grassData = GrassRuntime::GetPropertyData(property, globals::game::isVR);
		if (impl->RefreshSource(shape, nativeVisible)) {
			captured = true;
			return true;
		}
		Source source{};
		source.groupLayout = GrassRuntime::CaptureGroupLayout(runtime);
		source.capturedFrame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		source.residency.createdFrame = source.capturedFrame;
		source.identity = shape;
		{
			std::scoped_lock lock(impl->captureMutex);
			auto& identity = impl->identities[shape];
			if (!identity.first)
				identity.first = std::make_shared<Lifetime>();
			source.lifetime = identity.first;
			source.generation = source.lifetime->generation.load(std::memory_order_acquire);
			source.model = identity.second;
			if (auto found = impl->residents.find(shape); found != impl->residents.end())
				source.residency = found->second.residency;
		}
		source.residency.Observe(source.capturedFrame, true);
		source.property = geometry.shaderProperty;
		source.texture.reset(source.property->GetBaseTexture());
		source.material = property->material;
		source.mesh = geometry.rendererData;
		source.vertices.copy_from(reinterpret_cast<ID3D11Buffer*>(source.mesh->vertexBuffer));
		source.indices.copy_from(reinterpret_cast<ID3D11Buffer*>(source.mesh->indexBuffer));
		source.descriptor = std::bit_cast<uint64_t>(geometry.vertexDesc);
		source.lights.assign(grassData.lightData.lights.begin(), grassData.lightData.lights.end());
		source.lightMask = grassData.lightData.activeLightMask;
		source.renderDistance = runtime.renderDistance;
		source.flags = property->flags.underlying();
		source.wavePeriod = grassData.wavePeriod;
		source.triangles = shape->GetTrishapeRuntimeData().triangleCount;
		source.vertexCount = shape->GetTrishapeRuntimeData().vertexCount;
		source.origin = shape->world.translate;
		source.bound = shape->GetModelData().modelBound;
		if (!source.material || !source.vertices || !source.indices || !source.triangles || !Finite(source.origin) ||
			!std::isfinite(source.renderDistance) || !std::isfinite(source.wavePeriod) ||
			!Finite(source.bound.center) || !std::isfinite(source.bound.radius) || source.bound.radius <= 0)
			return GRASS_CAPTURE_REJECT(Source);
		D3D11_BUFFER_DESC vertices{}, indices{};
		source.vertices->GetDesc(&vertices);
		source.indices->GetDesc(&indices);
		source.vertexBytes = vertices.ByteWidth;
		source.indexBytes = indices.ByteWidth;
		const auto stride = GrassPolicy::MeshStride(source.descriptor);
		if (!stride || !(vertices.BindFlags & D3D11_BIND_VERTEX_BUFFER) || !(indices.BindFlags & D3D11_BIND_INDEX_BUFFER) ||
			uint64_t(stride) * shape->GetTrishapeRuntimeData().vertexCount > vertices.ByteWidth ||
			uint64_t(source.triangles) * 6 > indices.ByteWidth)
			return GRASS_CAPTURE_REJECT(Mesh);
		uint64_t instances = 0;
		for (uint32_t index = 0; index < runtime.instanceGroups.size(); ++index) {
			auto group = runtime.instanceGroups[index];
			if (!group || !group->instanceCount)
				continue;
			if (!group->vertexBuffer ||
				(nativeVisible && group->isVisible && (index >= grassData.fadeAlphas.size() || !std::isfinite(grassData.fadeAlphas[index]))))
				return GRASS_CAPTURE_REJECT(Fade);
			if (source.groups.size() >= GrassPolicy::kMaxBatchSlices)
				return GRASS_CAPTURE_REJECT(GroupCapacity);
			Group snapshot{ group->vertexBuffer, {}, group->instanceCount,
				nativeVisible && group->isVisible ? grassData.fadeAlphas[index] : 1.0f, nativeVisible && group->isVisible, {} };
			instances += snapshot.count;
			const auto bytes = uint64_t(snapshot.count) * GrassPolicy::kRecordBytes;
			if (instances > GrassPolicy::kMaxBatchInstances)
				return GRASS_CAPTURE_REJECT(GroupCapacity);
			if (!GrassRuntime::MatchesInstanceBytes(*group->vertexBuffer, bytes))
				return GRASS_CAPTURE_REJECT(GroupBuffer);
			if (group->vertexBuffer->buffer) {
				snapshot.buffer.copy_from(reinterpret_cast<ID3D11Buffer*>(group->vertexBuffer->buffer));
				D3D11_BUFFER_DESC desc{};
				snapshot.buffer->GetDesc(&desc);
				if (bytes > desc.ByteWidth || desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
					return GRASS_CAPTURE_REJECT(GroupBuffer);
			} else {
				if (!GrassRuntime::HasInstanceData(*group->vertexBuffer, bytes))
					return GRASS_CAPTURE_REJECT(GroupBuffer);
				const auto data = static_cast<const uint8_t*>(group->vertexBuffer->m_data);
				std::shared_ptr<const std::vector<uint8_t>> cached;
				{
					std::scoped_lock lock(impl->captureMutex);
					if (auto found = impl->residents.find(shape); found != impl->residents.end() && found->second.generation == source.generation &&
																  found->second.groupLayout == source.groupLayout)
						for (const auto& previous : found->second.groups)
							if (previous.identity == snapshot.identity && previous.count == snapshot.count) {
								cached = previous.bytes;
								break;
							}
				}
				// Instance generation invalidates this snapshot before new records can be drawn.
				if (cached && cached->size() == bytes)
					snapshot.bytes = std::move(cached);
				else
					snapshot.bytes = std::make_shared<const std::vector<uint8_t>>(data, data + bytes);
				source.cpuRecordBytes += bytes;
			}
			if (GrassRuntime::HasInstanceData(*group->vertexBuffer, bytes))
				CacheRootBounds(snapshot, group->vertexBuffer->m_data);
			else if (snapshot.bytes)
				CacheRootBounds(snapshot, snapshot.bytes->data());
			source.groups.push_back(std::move(snapshot));
		}
		if (!instances)
			return GRASS_CAPTURE_REJECT(Empty);
		std::scoped_lock lock(impl->captureMutex);
		if (!source.lifetime->alive.load(std::memory_order_acquire) ||
			impl->identities.find(shape) == impl->identities.end() ||
			impl->identities.at(shape).first != source.lifetime)
			return GRASS_CAPTURE_REJECT(Destroyed);
		if (source.lifetime->generation.load(std::memory_order_acquire) != source.generation || !CurrentGroups(source))
			return GRASS_CAPTURE_REJECT(Stale);
		if (!impl->MakeResidentRoom(source))
			return GRASS_CAPTURE_REJECT(FrameCapacity);
		const auto existing = impl->residents.find(shape);
		const uint64_t previousBytes = existing != impl->residents.end() ? existing->second.cpuRecordBytes : 0;
		if (existing != impl->residents.end())
			source.generation = ++source.lifetime->generation;
		const auto now = globals::state->timer;
		for (auto& group : source.groups) {
			const auto born = source.lifetime->fadeHistory.Capture(group.identity);
			group.birthTime = born;
			if (!group.visible)
				group.fade = impl->fadeInSeconds > 0 ? std::clamp((now - born) / impl->fadeInSeconds, 0.0f, 1.0f) : 1.0f;
		}
		const uint64_t nextCpuBytes = impl->cpuRecordBytes - previousBytes + source.cpuRecordBytes;
		impl->EraseResident(shape);
		impl->AssignQueue(source);
		impl->residents.insert_or_assign(shape, std::move(source));
		impl->cpuRecordBytes = nextCpuBytes;
		++impl->residentRevision;
		captured = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (impl->diagnostics.load(std::memory_order_relaxed))
			++impl->capturedSources;
#endif
		return true;
	} catch (const std::exception& error) {
		logger::warn("Grass snapshot unavailable; retaining native rendering: {}", error.what());
	}
	return GRASS_CAPTURE_REJECT(Allocation);
#undef GRASS_CAPTURE_REJECT
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
		D3D11_SUBRESOURCE_DATA data{};
		D3D11_BUFFER_DESC desc = ConstantBufferDesc(64, false);
		std::array<uint8_t, 64> zeros{};
		data.pSysMem = zeros.data();
		impl->disabled = std::make_unique<Buffer>(desc, &data, "GrassOptimizations::NativeParameters");
		std::vector<std::pair<const char*, const char*>> defines;
		if (!impl->cullShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::CullCS"))
			throw std::runtime_error("Grass culling shader unavailable");
		impl->hiZ.SetupResources();
		impl->CompileSignature();
#ifdef DEVBENCH_BRIDGE_ENABLED
		defines.emplace_back("GRASS_DIAGNOSTICS", "1");
		impl->diagnosticShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::DiagnosticCullCS");
		impl->gpuCounters = RawBuffer(kDiagnosticCounterCount * sizeof(uint32_t), D3D11_BIND_UNORDERED_ACCESS, 0, "GrassOptimizations::Counters");
		D3D11_BUFFER_DESC staging{};
		staging.ByteWidth = kDiagnosticCounterCount * sizeof(uint32_t);
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
	impl->cullingPrepared = false;
	for (auto& bucket : impl->buckets) {
		bucket.plan.Retire();
		bucket.outcomes.Clear();
	}
	for (auto& bucket : impl->dormantBuckets)
		bucket.plan.Retire();
	impl->cullShader.Reset();
	impl->signature = nullptr;
	impl->layouts.clear();
	impl->hiZ.Reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
	impl->diagnosticShader.Reset();
#endif
	impl->failed = false;
}

void GrassBucketRenderer::EndFrame()
{
	for (auto& bucket : impl->buckets)
		bucket.plan.Retire();
	for (auto& bucket : impl->dormantBuckets)
		bucket.plan.Retire();
	impl->UpdateLODDemand(globals::features::grassOptimizations.GetSettings());
}

void GrassBucketRenderer::Impl::PrepareFrame()
{
	const auto now = globals::state->frameCount;
	if (frame == now)
		return;
#ifdef DEVBENCH_BRIDGE_ENABLED
	try {
		PublishTelemetry();
	} catch (const std::exception&) {
		// Reporting failures must not disable rendering or allocate while handling low memory.
		telemetryFailed.store(true, std::memory_order_relaxed);
	}
	frameWork = {};
	telemetryViewportCount = 0;
#endif
	frameSettings = globals::features::grassOptimizations.GetSettings();
	const auto& collision = globals::features::grassCollision;
	frameCollisionStrength = collision.settings.EnableGrassCollision ? collision.GetCollisionStrength() : GrassCollision::kDefaultCollisionStrength;
	frameSettings.Enabled &= globals::features::grassOptimizations.loaded;
	current = nullptr;
	frame = now;
	cullingPrepared = false;
	for (auto& bucket : buckets) {
		bucket.outcomes.Clear();
		bucket.plan.Retire();
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	PollCounters();
#endif
	std::unique_lock lock(captureMutex);
	std::erase_if(queues, [](const auto& entry) { return entry.second.expired(); });
	for (auto it = residents.begin(); it != residents.end();) {
		if (!it->second.lifetime->alive.load(std::memory_order_acquire) || it->second.generation != it->second.lifetime->generation.load(std::memory_order_acquire) ||
			!CurrentGeometry(it->second) || !CurrentContract(it->second)) {
			++it->second.lifetime->generation;
			auto identity = it->first;
			++it;
			EraseResident(identity);
			++residentRevision;
		} else
			++it;
	}
	lock.unlock();
	UpdateLODDemand(frameSettings);
	lock.lock();
	if (!frameSettings.Enabled || failed)
		return;
	if (preparedRevision == residentRevision) {
		// Captured geometry is already validated; distinct materials can still change in place.
		for (const auto& bucket : buckets)
			for (size_t i = 1; i < bucket.sources.size(); ++i)
				if (!SameMaterial(bucket.sources.front(), bucket.sources[i])) {
					bucket.sources.front().queue->compatible.store(false, std::memory_order_relaxed);
					preparedRevision = UINT64_MAX;
					break;
				}
	}
	if (preparedRevision == residentRevision && preparedBatching == frameSettings.CrossCellBatching) {
		RefreshFades();
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed))
			++persistentBucketFrames;
#endif
		return;
	}
	preparedRevision = residentRevision;
	preparedBatching = frameSettings.CrossCellBatching;
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed))
		++bucketRebuilds;
#endif
	auto previousBuckets = std::move(buckets);
	for (auto& dormant : dormantBuckets)
		previousBuckets.push_back(std::move(dormant));
	dormantBuckets.clear();
	buckets.clear();
	residentRecordBytes = 0;
	admitted.clear();
	current = nullptr;
	std::vector<Source> sources;
	sources.reserve(residents.size());
	for (const auto& [identity, source] : residents)
		sources.push_back(source);
	std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) {
		return std::less<RE::BSMultiStreamInstanceTriShape*>{}(a.identity, b.identity);
	});
	std::unordered_multimap<size_t, size_t> compatibleBuckets;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::unordered_map<std::string, size_t> modelRepresentatives;
#endif
	for (auto& source : sources) {
		auto identity = source.identity;
		if (!source.lifetime->alive.load(std::memory_order_acquire)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics.load(std::memory_order_relaxed))
				RejectCapture(CaptureRejection::Destroyed);
#endif
			continue;
		}
		if (source.generation != source.lifetime->generation.load(std::memory_order_acquire)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics.load(std::memory_order_relaxed))
				RejectCapture(CaptureRejection::Stale);
#endif
			continue;
		}
		uint32_t count = 0;
		for (const auto& group : source.groups) count += group.count;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed)) {
			if (source.model.empty())
				++unidentifiedModels;
			else if (auto found = modelRepresentatives.find(source.model); found != modelRepresentatives.end()) {
				const auto& first = buckets[found->second].sources.front();
				++sameModelPeers;
				if (Compatible(first, source))
					++sameModelCompatible;
				if (first.vertices != source.vertices)
					++sameModelMismatches[0];
				if (first.indices != source.indices)
					++sameModelMismatches[1];
				if (first.material != source.material)
					++sameModelMismatches[2];
				if (first.descriptor != source.descriptor || first.triangles != source.triangles)
					++sameModelMismatches[3];
				if (first.flags != source.flags)
					++sameModelMismatches[4];
				if (first.lights != source.lights || first.lightMask != source.lightMask)
					++sameModelMismatches[5];
				if (first.renderDistance != source.renderDistance || first.wavePeriod != source.wavePeriod ||
					first.bound.radius != source.bound.radius || first.bound.center != source.bound.center)
					++sameModelMismatches[6];
			}
		}
#endif
		size_t index = buckets.size();
		const auto hash = CompatibilityHash(source);
		if (source.queue && source.queue->batching) {
			const auto [rangeBegin, rangeEnd] = compatibleBuckets.equal_range(hash);
			for (auto candidate = rangeBegin; candidate != rangeEnd; ++candidate) {
				const auto i = candidate->second;
				if (buckets[i].sources.front().queue == source.queue && Compatible(buckets[i].sources.front(), source) && buckets[i].instances <= GrassPolicy::kMaxBatchInstances - count) {
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed) && !buckets[index].sources.back().model.empty())
			modelRepresentatives.try_emplace(buckets[index].sources.back().model, index);
#endif
		admitted.emplace(identity, Admission{ index, buckets[index].sources.size() - 1 });
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed))
			++admittedSources;
#endif
	}
	for (auto& bucket : buckets) {
		CacheRuns(bucket);
		// A split queue must render natively until visibility queues its new representatives.
		if (bucket.sources.size() != bucket.sources.front().queue->members.size())
			bucket.sources.front().queue->compatible.store(false, std::memory_order_relaxed);
	}
	RefreshFades();
	ReuseRecordBuffers(previousBuckets);
	resultBytes = 0;
	for (const auto& bucket : buckets)
		if (bucket.result.output)
			resultBytes += uint64_t(bucket.result.capacity) * bucket.result.slots * GrassPolicy::kSurvivorBytes + 192;
	for (auto& bucket : dormantBuckets) bucket.result = {};
#ifdef DEVBENCH_BRIDGE_ENABLED
	publishedResultBytes.store(resultBytes, std::memory_order_relaxed);
#endif
}

// The caller holds captureMutex so native fades and generation snapshots agree.
void GrassBucketRenderer::Impl::RefreshFades()
{
	for (auto& bucket : buckets) {
		if (bucket.sources.front().queue->queuedFrame != frame)
			continue;
		for (const auto& run : bucket.runs) {
			auto& source = bucket.sources[run.source];
			const auto& live = residents.at(source.identity);
			auto& group = source.groups[run.firstGroup];
			group.fade = live.capturedFrame == frame ? live.groups[run.firstGroup].fade :
			                                           (fadeInSeconds > 0 ? std::clamp((globals::state->timer - group.birthTime) / fadeInSeconds, 0.0f, 1.0f) : 1.0f);
		}
	}
}

void GrassBucketRenderer::Impl::ReuseRecordBuffers(std::vector<Bucket>& previousBuckets)
{
	std::unordered_multimap<size_t, size_t> previousByRecords;
	for (size_t i = 0; i < previousBuckets.size(); ++i)
		if (previousBuckets[i].records)
			previousByRecords.emplace(RecordLayoutHash(previousBuckets[i]), i);
	for (auto& bucket : buckets) {
		const auto [begin, end] = previousByRecords.equal_range(RecordLayoutHash(bucket));
		for (auto candidate = begin; candidate != end; ++candidate) {
			auto& previous = previousBuckets[candidate->second];
			if (!previous.records || previous.recordsCapacity < bucket.instances || !SameRecords(bucket, previous))
				continue;
			const uint64_t bytes = uint64_t(previous.recordsCapacity) * GrassPolicy::kRecordBytes;
			if (bytes > GrassPolicy::kMaxResidentRecordBytes - residentRecordBytes)
				continue;
			bucket.result = std::move(previous.result);
			bucket.records = std::move(previous.records);
			bucket.recordsCapacity = previous.recordsCapacity;
			bucket.recordsDirty = previous.recordsDirty;
			bucket.dirtyFirstInstance = previous.dirtyFirstInstance;
			residentRecordBytes += bytes;
			break;
		}
	}
	for (auto& bucket : buckets) {
		if (bucket.records)
			continue;
		for (auto& previous : previousBuckets) {
			if (!previous.records || previous.recordsCapacity < bucket.instances || previous.sources.empty() ||
				!Compatible(bucket.sources.front(), previous.sources.front()))
				continue;
			const uint64_t bytes = uint64_t(previous.recordsCapacity) * GrassPolicy::kRecordBytes;
			if (bytes > GrassPolicy::kMaxResidentRecordBytes - residentRecordBytes)
				continue;
			const auto shared = SharedRecordPrefix(bucket, previous);
			bucket.dirtyFirstInstance = previous.recordsDirty ? std::min(shared, previous.dirtyFirstInstance) : shared;
			bucket.recordsDirty = bucket.dirtyFirstInstance < bucket.instances;
			bucket.result = std::move(previous.result);
			bucket.records = std::move(previous.records);
			bucket.recordsCapacity = previous.recordsCapacity;
			residentRecordBytes += bytes;
			break;
		}
	}
	uint64_t dormantBytes = 0;
	for (auto& previous : previousBuckets) {
		if (!previous.records ||
			!std::all_of(previous.sources.begin(), previous.sources.end(), [](const Source& source) {
				return source.lifetime->alive.load(std::memory_order_acquire);
			}))
			continue;
		const uint64_t bytes = uint64_t(previous.recordsCapacity) * GrassPolicy::kRecordBytes;
		if (bytes > GrassPolicy::kMaxDormantRecordBytes - dormantBytes ||
			bytes > GrassPolicy::kMaxResidentRecordBytes - residentRecordBytes)
			continue;
		previous.outcomes.Clear();
		dormantBuckets.push_back(std::move(previous));
		dormantBytes += bytes;
		residentRecordBytes += bytes;
	}
}

void GrassBucketRenderer::PrepareGeometry(RE::BSRenderPass* pass, RE::BSShader* nativeShader, uint32_t flags, GrassRuntime::SetupGeometry setup, std::optional<uint32_t> nativeWindTimer)
{
	impl->current = nullptr;
	impl->currentSetupPass = nullptr;
	if (!pass || !pass->geometry || !impl->context || !impl->disabled)
		return;
	auto cb = impl->disabled->resource.get();
	impl->context->VSSetConstantBuffers(9, 1, &cb);
	if (impl->nativeFallbackDepth)
		return;
	try {
		impl->PrepareFrame();
		if (!impl->frameSettings.Enabled)
			return;
		auto found = impl->admitted.find(static_cast<RE::BSMultiStreamInstanceTriShape*>(pass->geometry));
		if (found == impl->admitted.end())
			return;
		impl->current = found->first;
		impl->currentAdmission = found->second;
		impl->currentPass = pass->passEnum;
		// The native pass owns these inputs through the synchronous group draws.
		impl->currentSetupPass = pass;
		impl->currentNativeShader = nativeShader;
		impl->currentSetup = setup;
		impl->currentSetupFlags = flags;
		impl->currentNativeWindTimer = nativeWindTimer;
		// The representative translation is the one used by the native geometry constants.
		impl->currentOrigin = pass->geometry->world.translate;
		impl->CopyGeometryConstants();
		static REL::Relocation<void (*)(uint32_t)> setDirtyStates{ REL::RelocationID(75580, 77386) };
		setDirtyStates(0);
	} catch (const std::exception& error) {
		impl->failed = true;
		logger::error("Grass frame preparation failed; retaining native rendering: {}", error.what());
	}
}

bool GrassBucketRenderer::Impl::MakeRecordRoom(uint64_t replacementBytes)
{
	if (replacementBytes > GrassPolicy::kMaxResidentRecordBytes || residentRecordBytes > GrassPolicy::kMaxResidentRecordBytes)
		return false;
	while (!GrassPolicy::ReplacementFitsBudget(residentRecordBytes, replacementBytes, GrassPolicy::kMaxResidentRecordBytes)) {
		if (dormantBuckets.empty())
			return false;
		const auto& dormant = dormantBuckets.back();
		const uint64_t bytes = dormant.records ? uint64_t(dormant.recordsCapacity) * GrassPolicy::kRecordBytes : 0;
		if (bytes > residentRecordBytes)
			return false;
		// Inactive reuse must not prevent active growth; leave every active buffer intact.
		dormantBuckets.pop_back();
		residentRecordBytes -= bytes;
	}
	return true;
}

bool GrassBucketRenderer::Impl::EnsureRecords(Bucket& bucket)
{
	if (bucket.records && bucket.recordsCapacity >= bucket.instances)
		return true;
	const auto next = std::bit_ceil(bucket.instances);
	const uint64_t bytes = uint64_t(next) * GrassPolicy::kRecordBytes;
	const uint64_t previous = bucket.records ? uint64_t(bucket.recordsCapacity) * GrassPolicy::kRecordBytes : 0;
	if (previous > residentRecordBytes || !MakeRecordRoom(bytes))
		return false;
	bucket.records = RawBuffer(uint32_t(bytes), D3D11_BIND_SHADER_RESOURCE, 0, "GrassOptimizations::BucketRecords");
	bucket.recordsCapacity = next;
	bucket.recordsDirty = true;
	bucket.dirtyFirstInstance = 0;
	residentRecordBytes = residentRecordBytes - previous + bytes;
	return true;
}

bool GrassBucketRenderer::Impl::EnsureResult(Bucket& bucket, uint32_t slots)
{
	auto& result = bucket.result;
	if (result.capacity >= bucket.instances && result.slots >= slots && result.arguments)
		return true;
	const auto planned = GrassPolicy::PlanCullCapacity({ result.capacity, result.slots }, bucket.instances, slots);
	if (!planned)
		return false;
	const uint32_t capacity = planned->instances;
	slots = planned->slots;
	const uint64_t previous = uint64_t(result.capacity) * result.slots * GrassPolicy::kSurvivorBytes + (result.arguments ? 192 : 0);
	const uint64_t next = uint64_t(capacity) * slots * GrassPolicy::kSurvivorBytes + 192;
	if (previous > resultBytes || !GrassPolicy::ReplacementFitsBudget(resultBytes, next, GrassPolicy::kMaxCullResultBytes))
		return false;
	CullResult replacement;
	replacement.output = RawBuffer(capacity * slots * GrassPolicy::kSurvivorIndexBytes, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS, 0, "GrassOptimizations::Compacted");
	replacement.extras = Structured<std::array<float, 6>>(capacity * slots, true, false, "GrassOptimizations::Extras");
	replacement.arguments = RawBuffer(192, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, "GrassOptimizations::IndirectArgs");
	replacement.capacity = capacity;
	replacement.slots = slots;
	result = std::move(replacement);
	resultBytes = resultBytes - previous + next;
#ifdef DEVBENCH_BRIDGE_ENABLED
	publishedResultBytes.store(resultBytes, std::memory_order_relaxed);
#endif
	return true;
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
	auto desc = std::bit_cast<RE::BSGraphics::VertexDesc>(descriptor);
	using Vertex = RE::BSGraphics::Vertex;
	if (!desc.HasFlag(Vertex::VF_VERTEX) || !desc.HasFlag(Vertex::VF_UV) || !desc.HasFlag(Vertex::VF_NORMAL) || !desc.HasFlag(Vertex::VF_COLORS))
		return nullptr;
	const auto stride = GrassPolicy::MeshStride(descriptor);
	const bool fullPosition = desc.HasFlag(Vertex::VF_FULLPREC) || desc.GetAttributeOffset(Vertex::VA_TEXCOORD0) >= 16;
	if (stride < (fullPosition ? 16u : 8u) ||
		desc.GetAttributeOffset(Vertex::VA_TEXCOORD0) + 4 > stride ||
		desc.GetAttributeOffset(Vertex::VA_NORMAL) + 4 > stride ||
		desc.GetAttributeOffset(Vertex::VA_COLOR) + 4 > stride)
		return nullptr;
	CompileSignature();
	std::array<D3D11_INPUT_ELEMENT_DESC, 8> elements{};
	elements[0] = { "POSITION", 0, fullPosition ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[1] = { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, desc.GetAttributeOffset(Vertex::VA_TEXCOORD0), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[2] = { "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, desc.GetAttributeOffset(Vertex::VA_NORMAL), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	elements[3] = { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, desc.GetAttributeOffset(Vertex::VA_COLOR), D3D11_INPUT_PER_VERTEX_DATA, 0 };
	for (UINT i = 0; i < 4; ++i) elements[4 + i] = { "TEXCOORD", 4 + i, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
	winrt::com_ptr<ID3D11InputLayout> result;
	DX::ThrowIfFailed(globals::d3d::device->CreateInputLayout(elements.data(), UINT(elements.size()), signature->GetBufferPointer(), signature->GetBufferSize(), result.put()));
	Util::SetResourceName(result.get(), "GrassOptimizations::InputLayout");
	auto raw = result.get();
	layouts.emplace(descriptor, std::move(result));
	return raw;
}

void GrassBucketRenderer::Impl::UpdateLODDemand(const GrassPolicy::Settings& settings)
{
	try {
		const uint32_t mode = settings.Enabled && settings.EnableMeshLOD && !failed && globals::features::grassOptimizations.loaded ?
		                          (settings.EnableMidLOD ? 1u : 0u) | (settings.EnableFarLOD ? 2u : 0u) :
		                          0u;
		std::shared_lock lock(captureMutex);
		const auto revision = residentRevision;
		if (lodRevision == revision && lodMode == mode)
			return;
		std::set<LODKey, std::less<>> demand;
		if (mode)
			for (const auto& [identity, source] : residents) {
				if (source.model.empty() || !source.texture || !source.lifetime->alive.load(std::memory_order_acquire) ||
					source.generation != source.lifetime->generation.load(std::memory_order_acquire))
					continue;
				demand.insert({ source.model, source.descriptor, source.texture, 0 });
				if (mode & 2u)
					demand.insert({ source.model, source.descriptor, source.texture, 1 });
			}
		lock.unlock();
		lodMeshes.SetDemand(std::move(demand));
		lodRevision = revision;
		lodMode = mode;
	} catch (const std::bad_alloc&) {
		// Optional LOD demand must not disable culling when its allocation fails.
		lodMeshes.Clear();
		lodRevision = UINT64_MAX;
		logger::warn("Grass LOD demand allocation failed; released cached meshes");
	}
}

std::array<const Mesh*, 2> GrassBucketRenderer::Impl::LODMeshes(const Source& source)
{
	const auto find = [&](uint32_t tier) -> const Mesh* {
		const LODLookup key{ source.model, source.descriptor, reinterpret_cast<uintptr_t>(source.texture.get()), tier };
		return lodMeshes.FindOrLoad(key, [&](const LODKey& requested) { return LoadLODMesh(requested); });
	};
	const auto compatible = [&](const Mesh* mesh) {
		return mesh && mesh->vertices && mesh->indices && mesh->descriptor == source.descriptor &&
		       mesh->texture && mesh->texture.get() == source.property->GetBaseTexture();
	};
	const Mesh* midMesh = frameSettings.EnableMidLOD ? find(0) : nullptr;
	const Mesh* farMesh = frameSettings.EnableFarLOD ? find(1) : nullptr;
	bool farValid = compatible(farMesh);
	if (frameSettings.EnableFarLOD && !farValid && !frameSettings.EnableMidLOD) {
		midMesh = find(0);
		farValid = compatible(farMesh);
	}
	if (!compatible(midMesh))
		midMesh = nullptr;
	if (!farValid)
		farMesh = midMesh;
	// Cache entries remain stable throughout render-thread plan preparation.
	return { frameSettings.EnableMidLOD ? midMesh : nullptr, frameSettings.EnableFarLOD ? farMesh : nullptr };
}

Mesh GrassBucketRenderer::Impl::LoadLODMesh(const LODKey& key)
{
	Mesh mesh;
	const auto path = "LOD\\Grass\\" + std::filesystem::path(key.model).stem().string() + (key.tier ? "_LOD1.nif" : "_LOD0.nif");
	RE::BSModelDB::DBTraits::ArgsType args{};
	args.unk8 = false;
	args.unkA = false;
	args.postProcess = false;
	if (RE::BSModelDB::Demand(path.c_str(), mesh.owner, args) != RE::BSResource::ErrorCode::kNone || !mesh.owner)
		return {};
	auto shape = SingleShape(mesh.owner.get());
	if (!shape)
		return {};
	auto& geometry = shape->GetGeometryRuntimeData();
	if (!geometry.rendererData || !geometry.shaderProperty)
		return {};
	mesh.texture.reset(geometry.shaderProperty->GetBaseTexture());
	mesh.descriptor = std::bit_cast<uint64_t>(geometry.vertexDesc) | 0x8000000000000080ull;
	if (mesh.descriptor != key.descriptor || !mesh.texture || mesh.texture.get() != key.texture.get())
		return {};
	mesh.indexCount = shape->GetTrishapeRuntimeData().triangleCount * 3;
	const auto bound = shape->GetModelData().modelBound;
	if (!mesh.indexCount || !Finite(bound.center) || !std::isfinite(bound.radius) || bound.radius <= 0)
		return {};
	mesh.radius = bound.center.Length() + bound.radius;
	if (!std::isfinite(mesh.radius))
		return {};
	mesh.vertices.copy_from(reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->vertexBuffer));
	mesh.indices.copy_from(reinterpret_cast<ID3D11Buffer*>(geometry.rendererData->indexBuffer));
	if (!mesh.vertices || !mesh.indices)
		return {};
	D3D11_BUFFER_DESC vertices{}, indices{};
	mesh.vertices->GetDesc(&vertices);
	mesh.indices->GetDesc(&indices);
	const auto stride = GrassPolicy::MeshStride(mesh.descriptor);
	if (!stride || !(vertices.BindFlags & D3D11_BIND_VERTEX_BUFFER) || !(indices.BindFlags & D3D11_BIND_INDEX_BUFFER) ||
		uint64_t(stride) * shape->GetTrishapeRuntimeData().vertexCount > vertices.ByteWidth ||
		uint64_t(mesh.indexCount) * 2 > indices.ByteWidth)
		return {};
	return mesh;
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

void GrassBucketRenderer::Impl::CullSlices(const Bucket& bucket, float radius, uint32_t eyes, uint32_t& instances)
{
	frustumData = {};
	std::array<GrassFrustum::Planes, 2> planeSets{};
	bool planesValid = geometryCPUValid;
	for (uint32_t eye = 0; eye < eyes && planesValid; ++eye) {
		GrassFrustum::Matrix matrix{};
		std::copy_n(geometryCPU.begin() + eye * 16, 16, matrix.begin());
		GrassFrustum::Matrix world{}, camera{}, unjittered{};
		std::copy_n(geometryCPU.begin() + (eyes == 2 ? 64 : 32) + eye * 16, 16, world.begin());
		std::memcpy(camera.data(), &globals::game::frameBufferCached.GetCameraViewProj(eye), sizeof(camera));
		std::memcpy(unjittered.data(), &globals::game::frameBufferCached.GetCameraViewProjUnjittered(eye), sizeof(unjittered));
		matrix = GrassFrustum::SelectProjection(matrix, world, camera, unjittered);
		const auto planes = GrassFrustum::Extract(matrix);
		if (!planes) {
			planesValid = false;
			break;
		}
		planeSets[eye] = *planes;
		std::copy(planes->begin(), planes->end(), frustumData.planes.begin() + eye * 6);
	}
	frustumData.valid = planesValid;

	cullingSlices.clear();
	instances = 0;
	runBounds.resize(bucket.runs.size());
	std::array<float, 3> bucketLo, bucketHi;
	bucketLo.fill(std::numeric_limits<float>::max());
	bucketHi.fill(std::numeric_limits<float>::lowest());
	bool bucketKnown = !bucket.runs.empty();
	const size_t windOffset = GrassPolicy::WindVectorOffset(eyes == 2), scaleOffset = eyes == 2 ? 148 : 84;
	const float wind = .4f * std::abs(geometryCPU[windOffset + 2]) * std::hypot(geometryCPU[windOffset], geometryCPU[windOffset + 1]);
	std::array<GrassFrustum::Matrix, 2> world{};
	for (uint32_t eye = 0; eye < eyes; ++eye)
		std::copy_n(geometryCPU.begin() + (eyes == 2 ? 64 : 32) + eye * 16, 16, world[eye].begin());
	for (size_t index = 0; index < bucket.runs.size(); ++index) {
		const auto& run = bucket.runs[index];
		const auto& source = bucket.sources[run.source];
		float scale = 0;
		for (size_t axis = 0; axis < 3; ++axis)
			scale = std::max({ scale, std::abs(1 + run.minVariance * geometryCPU[scaleOffset + axis]), std::abs(1 + run.maxVariance * geometryCPU[scaleOffset + axis]) });
		const float reach = (source.bound.center.Length() + radius) * scale * run.basis;
		const std::array<float, 3> origin{ source.origin.x - currentOrigin.x, source.origin.y - currentOrigin.y, source.origin.z - currentOrigin.z };
		std::array<float, 3> rootsLo{}, rootsHi{};
		for (size_t axis = 0; axis < 3; ++axis) {
			rootsLo[axis] = run.lo[axis] + origin[axis];
			rootsHi[axis] = run.hi[axis] + origin[axis];
		}
		const float padding = GrassFrustum::DeformationPadding(world[0], rootsLo, rootsHi, reach, wind,
			globals::features::grassCollision.loaded ? frameSettings.CollisionDistance : 0,
			frameCollisionStrength);
		const bool known = run.known && std::isfinite(padding) && padding >= 0;
		bucketKnown &= known;
		for (size_t axis = 0; axis < 3; ++axis) {
			runBounds[index][0][axis] = known ? rootsLo[axis] - padding : NAN;
			runBounds[index][1][axis] = known ? rootsHi[axis] + padding : NAN;
			bucketLo[axis] = std::min(bucketLo[axis], runBounds[index][0][axis]);
			bucketHi[axis] = std::max(bucketHi[axis], runBounds[index][1][axis]);
		}
	}
	const auto visible = [&](const auto& lo, const auto& hi) {
		if (!planesValid || !frameSettings.FrustumCulling)
			return true;
		for (uint32_t eye = 0; eye < eyes; ++eye)
			if (GrassFrustum::Visible(planeSets[eye], lo, hi))
				return true;
		return false;
	};
	const auto reject = [&](const SliceRun& run) {
		(void)run;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed)) {
			coarseRejectedSlices += run.lastGroup - run.firstGroup + 1;
			coarseRejectedInstances += run.count;
		}
#endif
	};
	if (bucketKnown && !visible(bucketLo, bucketHi)) {
		for (const auto& run : bucket.runs) reject(run);
		return;
	}
	const float distance = frameSettings.RenderDistanceOverride > 0 ? frameSettings.RenderDistanceOverride : nativeRenderDistance;
	for (size_t index = 0; index < bucket.runs.size(); ++index) {
		const auto& run = bucket.runs[index];
		const auto& box = runBounds[index];
		bool withinDistance = false;
		for (uint32_t eye = 0; eye < eyes; ++eye) withinDistance |= GrassFrustum::WithinDistance(world[eye], box[0], box[1], distance);
		if (!withinDistance || !visible(box[0], box[1])) {
			reject(run);
			continue;
		}
		const auto& source = bucket.sources[run.source];
		const auto fade = source.groups[run.firstGroup].fade;
		cullingSlices.push_back({ run.record, run.count, instances + run.count, 0, { source.origin.x, source.origin.y, source.origin.z, fade } });
		instances += run.count;
	}
}

bool GrassBucketRenderer::Impl::MainDepthBound() const
{
	winrt::com_ptr<ID3D11DepthStencilView> depth;
	context->OMGetRenderTargets(0, nullptr, depth.put());
	if (!depth)
		return false;
	winrt::com_ptr<ID3D11Resource> target;
	depth->GetResource(target.put());
	const auto main = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
	return target.get() == reinterpret_cast<ID3D11Resource*>(main);
}

bool GrassBucketRenderer::Impl::HasBatchDrawState() const
{
	return !failed && frameSettings.Enabled && HasBatchShader() && MainDepthBound();
}

void GrassBucketRenderer::Impl::CopyGeometryConstants()
{
	geometryCPUValid = false;
	if (!globals::game::currentVertexShader)
		return;
	if (auto shader = *globals::game::currentVertexShader; shader && shader->constantBuffers[2].data) {
		std::memcpy(geometryCPU.data(), shader->constantBuffers[2].data, globals::game::isVR ? 608 : 352);
		geometryCPUValid = true;
	}
}

void GrassBucketRenderer::Impl::RestoreCurrentGeometry()
{
	if (!currentSetupPass || !currentSetup || !currentNativeWindTimer)
		return;
	GrassRuntime::PrepareGeometryWithoutAdvancingWind(currentNativeShader, currentSetupPass,
		currentSetupFlags, currentSetup, globals::game::isVR, currentNativeWindTimer);
	CopyGeometryConstants();
}

void GrassBucketRenderer::Impl::PrepareCulling(RE::BSRenderPass* pass, RE::BSShader* shader, uint32_t flags, GrassRuntime::SetupGeometry setup)
{
	if (cullingPrepared || !setup || !currentNativeWindTimer || !HasBatchDrawState())
		return;
	D3D11_VIEWPORT viewport{};
	UINT count = 1;
	context->RSGetViewports(&count, &viewport);
	if (count != 1 || !std::isfinite(viewport.Height) || viewport.Height <= 0)
		return;
	frameHeight = viewport.Height;
	if (globals::game::isVR) {
		winrt::com_ptr<ID3D11Buffer> stereo;
		UINT first = 0, constants = 0;
		context->VSGetConstantBuffers1(13, 1, stereo.put(), &first, &constants);
		D3D11_BUFFER_DESC desc{};
		if (stereo)
			stereo->GetDesc(&desc);
		if (!stereo || constants * 16 < 48 || uint64_t(first) * 16 + 48 > desc.ByteWidth)
			return;
	}
	frameSlices.clear();
	const auto savedOrigin = currentOrigin;
	const auto savedGeometry = geometryCPU;
	const bool savedGeometryValid = geometryCPUValid;
	bool nativeGeometryChanged = false;
	{
		const SKSE::stl::scope_exit restore([&]() noexcept {
			if (nativeGeometryChanged)
				RestoreCurrentGeometry();
			else {
				geometryCPU = savedGeometry;
				geometryCPUValid = savedGeometryValid;
			}
			currentOrigin = savedOrigin;
		});
		for (auto& bucket : buckets) {
			bucket.plan.Retire();
			if (bucket.sources.empty())
				continue;
			auto& source = bucket.sources.front();
			if (!source.queue || source.queue->queuedFrame != frame)
				continue;
			if (source.queue->members.size() != bucket.sources.size()) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (diagnostics.load(std::memory_order_relaxed))
					RejectDraw(DrawRejection::QueueMembership);
#endif
				continue;
			}
			RE::BSMultiStreamInstanceTriShape* representative = source.identity;
			if (source.queue && source.queue->queuedFrame == frame)
				representative = source.queue->representative.load(std::memory_order_acquire);
			const auto member = std::find_if(bucket.sources.begin(), bucket.sources.end(), [&](const Source& item) {
				return item.identity == representative && item.lifetime->alive.load(std::memory_order_acquire) && CurrentGeometry(item) && CurrentContract(item);
			});
			if (member == bucket.sources.end() || member->lights.size() > UINT8_MAX)
				continue;
			auto prototype = *pass;
			prototype.geometry = representative;
			prototype.shaderProperty = member->property.get();
			prototype.sceneLights = member->lights.empty() ? nullptr : member->lights.data();
			prototype.numLights = uint8_t(member->lights.size());
			MakeNativeVisible(representative);
			const bool reuseCurrent = savedGeometryValid && pass == currentSetupPass && shader == currentNativeShader && flags == currentSetupFlags &&
			                          pass->geometry == representative && pass->shaderProperty == member->property.get() &&
			                          pass->numLights == member->lights.size() &&
			                          (member->lights.empty() || (pass->sceneLights && std::equal(member->lights.begin(), member->lights.end(), pass->sceneLights)));
			if (reuseCurrent) {
				geometryCPU = savedGeometry;
				geometryCPUValid = true;
			} else {
				nativeGeometryChanged = true;
				GrassRuntime::PrepareGeometryWithoutAdvancingWind(shader, &prototype, flags, setup, globals::game::isVR);
				CopyGeometryConstants();
			}
			currentOrigin = member->origin;
			PrepareBucket(bucket);
		}
	}
	UploadCulling();
	cullingPrepared = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
	telemetryViewport = viewport;
	telemetryViewportCount = count;
#endif
}

bool GrassBucketRenderer::Impl::DrawBucket(Bucket& bucket, Outcome& outcome)
{
	if (!bucket.plan.prepared || !geometryCPUValid || !HasBatchDrawState())
		return false;
	const auto eyes = globals::game::isVR ? 2u : 1u;
	const auto& saved = bucket.plan.geometry;
	const size_t wind = GrassPolicy::WindVectorOffset(eyes == 2), scale = eyes == 2 ? 148 : 84;
	// Previous wind is batch-owned; native setup exposes pass-dependent history.
	if (!std::equal(geometryCPU.begin(), geometryCPU.begin() + eyes * 48, saved.begin()) ||
		!std::equal(geometryCPU.begin() + wind, geometryCPU.begin() + wind + 4, saved.begin() + wind) ||
		!std::equal(geometryCPU.begin() + scale, geometryCPU.begin() + scale + 3, saved.begin() + scale) ||
		geometryCPU[wind + 11] != saved[wind + 11] || geometryCPU[wind + 15] != saved[wind + 15])
		return false;
	for (const auto& source : bucket.sources)
		if (!source.lifetime->alive.load(std::memory_order_acquire) ||
			source.generation != source.lifetime->generation.load(std::memory_order_acquire) || !CurrentGroups(source))
			return false;
	outcome = Outcome::Batched;
	if (!bucket.plan.empty) {
		EmitDraws(bucket);
		bucket.sources.front().queue->windHistory.Commit(frame, saved[wind + 3]);
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed)) {
		++batches;
		++reusedCullingDraws;
		combinedSources += bucket.sources.size();
		combinedInstances += bucket.instances;
	}
#endif
	return true;
}

bool GrassBucketRenderer::Impl::PrepareBucket(Bucket& bucket)
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed))
		++drawAttempts;
#	define GRASS_DRAW_REJECT(reason) (diagnostics.load(std::memory_order_relaxed) ? RejectDraw(DrawRejection::reason) : void(), false)
#else
#	define GRASS_DRAW_REJECT(reason) false
#endif
	if (failed || !frameSettings.Enabled || bucket.sources.empty())
		return GRASS_DRAW_REJECT(Shader);
	uint64_t sliceCount = 0;
	for (const auto& source : bucket.sources)
		sliceCount += source.groups.size();
	if (!GrassPolicy::BatchCapacityValid(bucket.instances, sliceCount))
		return GRASS_DRAW_REJECT(Capacity);

	for (const auto& source : bucket.sources) {
		if (!source.lifetime->alive.load(std::memory_order_acquire))
			return GRASS_DRAW_REJECT(Destroyed);
		if (source.generation != source.lifetime->generation.load(std::memory_order_acquire))
			return GRASS_DRAW_REJECT(Regenerated);
	}

	auto& representative = bucket.sources.front();
	std::array<Mesh, 3> meshes;
	meshes[0].vertices = representative.vertices;
	meshes[0].indices = representative.indices;
	meshes[0].descriptor = representative.descriptor;
	meshes[0].indexCount = representative.triangles * 3;
	float radius = representative.bound.radius;
	if (frameSettings.EnableMeshLOD && !representative.model.empty()) {
		const auto lod = LODMeshes(representative);
		for (size_t tier = 1; tier < meshes.size(); ++tier) {
			if (lod[tier - 1]) {
				meshes[tier] = *lod[tier - 1];
				radius = std::max(radius, meshes[tier].radius + representative.bound.center.Length());
			}
		}
	}
	const uint32_t eyes = globals::game::isVR ? 2 : 1;
	auto layout = Layout(representative.descriptor);
	if (!layout)
		return GRASS_DRAW_REJECT(Layout);
	if (!geometryCPUValid)
		return GRASS_DRAW_REJECT(GeometryConstants);
	const size_t wind = GrassPolicy::WindVectorOffset(eyes == 2);
	if (!std::isfinite(geometryCPU[wind + 3]))
		return GRASS_DRAW_REJECT(GeometryConstants);
	// Every pass reuses the last rendered batch phase, never a representative's native history.
	geometryCPU[wind + 7] = representative.queue->windHistory.Previous(frame, geometryCPU[wind + 3]);
	uint32_t prefix = 0;
	CullSlices(bucket, radius, eyes, prefix);
	bucket.plan.geometry = geometryCPU;
	if (cullingSlices.empty()) {
		bucket.plan.empty = true;
		bucket.plan.prepared = true;
		return true;
	}

	const uint32_t tiers = meshes[2].vertices ? 3u : meshes[1].vertices ? 2u :
	                                                                      1u;
	if (!EnsureResult(bucket, eyes * tiers))
		return GRASS_DRAW_REJECT(Capacity);
	if (!EnsureRecords(bucket))
		return GRASS_DRAW_REJECT(Capacity);
	Parameters params{ prefix, uint32_t(cullingSlices.size()), bucket.result.capacity, eyes,
		frameSettings.FrustumCulling, frameSettings.DensityReduction,
		meshes[1].vertices && meshes[1].indices ? 1u : 0u, meshes[2].vertices && meshes[2].indices ? 1u : 0u,
		frameSettings.MinPixelSize, frameSettings.FullDetailPixelSize, frameSettings.MinDensity, frameSettings.MeshLODBandPixels,
		frameSettings.MidLODPixelSize, frameSettings.FarLODPixelSize, frameSettings.OcclusionBias, frameHeight,
		{ currentOrigin.x, currentOrigin.y, currentOrigin.z, 0 },
		{ representative.bound.center.x, representative.bound.center.y, representative.bound.center.z, radius },
		0, 0, 0, globals::features::grassCollision.loaded ? 1u : 0u, {},
		frameSettings.MeshCostBias, frameSettings.CostBiasStartDistance, frameSettings.InvisibleFadeCull,
		frameSettings.RenderDistanceOverride > 0 ? frameSettings.RenderDistanceOverride : nativeRenderDistance,
		frameSettings.EdgeFadeStart, frameSettings.SimpleShadingPixelSize, representative.bound.radius,
		0, frameSettings.RenderDistanceOverride > 0 ? 1u : 0u, frameSettings.CollisionDistance };
	params.indexCounts = { meshes[0].indexCount, meshes[1].indexCount, meshes[2].indexCount, 0 };
	params.meshWeights = { GrassPolicy::MeshWeight(meshes[0].indexCount),
		GrassPolicy::MeshWeight(meshes[1].indexCount), GrassPolicy::MeshWeight(meshes[2].indexCount), 1 };
	params.collisionStrength = frameCollisionStrength;
	if (cullingSlices.size() > GrassPolicy::kMaxFrameSlices - frameSlices.size())
		return GRASS_DRAW_REJECT(Capacity);
	params.sliceOffset = uint32_t(frameSlices.size());
	frameSlices.insert(frameSlices.end(), cullingSlices.begin(), cullingSlices.end());
	bucket.plan.params = params;
	bucket.plan.geometry = geometryCPU;
	bucket.plan.frustum = frustumData;
	bucket.plan.meshes = std::move(meshes);
	bucket.plan.layout = layout;
	bucket.plan.prepared = true;
	return true;
#undef GRASS_DRAW_REJECT
}

void GrassBucketRenderer::Impl::UploadCulling()
{
	cullingBuckets.clear();
	for (auto& bucket : buckets)
		if (bucket.plan.prepared && !bucket.plan.empty) {
			bucket.plan.slot = uint32_t(cullingBuckets.size());
			cullingBuckets.push_back(&bucket);
		}
	const auto slots = uint32_t(cullingBuckets.size());
	if (!slots)
		return;
	if (!table || table->desc.ByteWidth < frameSlices.size() * sizeof(Slice))
		table = Structured<Slice>(std::bit_ceil(uint32_t(frameSlices.size())), false, true, "GrassOptimizations::FrameSlices");
	Upload(context.get(), *table, frameSlices.data(), frameSlices.size() * sizeof(Slice));
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed))
		++cullingUploadMaps;
#endif
	const bool useHiZ = frameSettings.EnableOcclusionCulling && hiZ.Build(context.get(), frame);
	const uint32_t pages = (slots + kSlotsPerPage - 1) / kSlotsPerPage;
	while (frameConstants.size() < pages)
		frameConstants.push_back(std::make_unique<Buffer>(ConstantBufferDesc(kSlotsPerPage * kFrameSlotBytes), nullptr, "GrassOptimizations::FrameConstants"));
	for (uint32_t page = 0; page < pages; ++page) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(frameConstants[page]->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));

#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed))
			++cullingUploadMaps;
#endif
		const SKSE::stl::scope_exit unmap([&]() noexcept { context->Unmap(frameConstants[page]->resource.get(), 0); });
		const auto end = std::min(slots, (page + 1) * kSlotsPerPage);
		for (uint32_t slot = page * kSlotsPerPage; slot < end; ++slot) {
			auto& plan = cullingBuckets[slot]->plan;
			auto& params = plan.params;
			if (useHiZ) {
				params.depthWidth = hiZ.Width();
				params.depthHeight = hiZ.Height();
				params.depthMips = hiZ.Mips();
				params.depthScale = hiZ.Scale();
				params.bias += hiZ.DepthViewportSlack();
			}
			auto bytes = static_cast<uint8_t*>(mapped.pData) + (plan.slot % kSlotsPerPage) * kFrameSlotBytes;
			std::memset(bytes, 0, kFrameSlotBytes);
			std::memcpy(bytes, plan.geometry.data(), globals::game::isVR ? 608 : 352);
			std::memcpy(bytes + kParametersOffset, &params, sizeof(params));
			std::memcpy(bytes + kFrustumOffset, &plan.frustum, sizeof(plan.frustum));
			for (uint32_t tier = 0; tier < 3; ++tier)
				for (uint32_t eye = 0; eye < params.eyes; ++eye) {
					const auto index = tier * params.eyes + eye;
					const DrawConstants draw{ 1, eye, index * params.capacity, frameSettings.CollisionDistance, params.origin };
					std::memcpy(bytes + kDrawOffset + index * 256, &draw, sizeof(draw));
				}
		}
	}
	std::vector<std::pair<const char*, const char*>> defines;
	auto shader = cullShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::CullCS");
	if (!shader)
		throw std::runtime_error("Grass culling shader unavailable");
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (countersActive && diagnostics.load(std::memory_order_relaxed) && gpuCounters) {
		defines.emplace_back("GRASS_DIAGNOSTICS", "1");
		if (auto diagnostic = diagnosticShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassCullingCS.hlsl", defines, "cs_5_0", "main", "GrassOptimizations::DiagnosticCullCS"))
			shader = diagnostic;
	}
#endif
	// Bucket inputs and outputs are distinct; finish all uploads before binding culling resources.
	for (auto bucket : cullingBuckets) {
		UploadRecords(*bucket);
		constexpr UINT zeros[4]{};
		context->ClearUnorderedAccessViewUint(bucket->result.arguments->uav.get(), zeros);
	}
	GrassD3D::ComputeState restore(context.get(), kCullUAVCount);
	const std::array<ID3D11ShaderResourceView*, 2> sharedSRVs{ table->srv.get(), useHiZ ? hiZ.SRV() : nullptr };
	context->CSSetShaderResources(1, UINT(sharedSRVs.size()), sharedSRVs.data());
#ifdef DEVBENCH_BRIDGE_ENABLED
	auto counters = shader == diagnosticShader.get() ? gpuCounters->uav.get() : nullptr;
	context->CSSetUnorderedAccessViews(3, 1, &counters, nullptr);
#endif
	winrt::com_ptr<ID3D11Buffer> stereo;
	UINT first = 0, count = 0;
	if (globals::game::isVR) {
		context->VSGetConstantBuffers1(13, 1, stereo.put(), &first, &count);
		auto buffer = stereo.get();
		context->CSSetConstantBuffers1(1, 1, &buffer, &first, &count);
	}
	context->CSSetShader(shader, nullptr, 0);
	for (auto bucket : cullingBuckets)
		DispatchBucket(*bucket);
}

void GrassBucketRenderer::Impl::UploadRecords(Bucket& bucket)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::UploadRecords");
#endif
	auto& records = *bucket.records;
	if (bucket.recordsDirty) {
		uint32_t offset = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		uint32_t uploadedBytes = 0;
#endif
		for (const auto& source : bucket.sources)
			for (const auto& group : source.groups) {
				const UINT bytes = group.count * GrassPolicy::kRecordBytes;
				if (offset + bytes <= bucket.dirtyFirstInstance * GrassPolicy::kRecordBytes) {
					offset += bytes;
					continue;
				}
				const D3D11_BOX box{ 0, 0, 0, bytes, 1, 1 };
				if (group.buffer)
					context->CopySubresourceRegion(records.resource.get(), 0, offset, 0, 0, group.buffer.get(), 0, &box);
				else {
					const D3D11_BOX destination{ offset, 0, 0, offset + bytes, 1, 1 };
					context->UpdateSubresource(records.resource.get(), 0, &destination, group.bytes->data(), 0, 0);
				}
				offset += bytes;
#ifdef DEVBENCH_BRIDGE_ENABLED
				uploadedBytes += bytes;
#endif
			}
		bucket.recordsDirty = false;
		bucket.dirtyFirstInstance = bucket.instances;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed)) {
			++uploadedRecordBuckets;
			uploadedRecordBytes += uploadedBytes;
		}
		frameWork.uploadedRecordBytes += uploadedBytes;
#endif
	} else {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (diagnostics.load(std::memory_order_relaxed))
			++reusedRecordBuckets;
#endif
	}
}

void GrassBucketRenderer::Impl::DispatchBucket(Bucket& bucket)
{
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::PrepareAndCull");
#endif
	const auto& plan = bucket.plan;
	const auto& params = plan.params;
	auto cb = frameConstants[plan.slot / kSlotsPerPage]->resource.get();
	const UINT begin = (plan.slot % kSlotsPerPage) * (kFrameSlotBytes / 16);
	const std::array<UINT, 3> offsets{ begin + kParametersOffset / 16, begin, begin + kFrustumOffset / 16 };
	const std::array<UINT, 3> counts{ 16, 64, 16 };
	for (UINT i = 0; i < 3; ++i) {
		const UINT binding = i == 0 ? 0 : i == 1 ? 2 :
		                                           3;
		context->CSSetConstantBuffers1(binding, 1, &cb, &offsets[i], &counts[i]);
	}
	auto records = bucket.records->srv.get();
	const std::array<ID3D11UnorderedAccessView*, 3> uavs{ bucket.result.output->uav.get(), bucket.result.extras->uav.get(), bucket.result.arguments->uav.get() };
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (diagnostics.load(std::memory_order_relaxed) && frameSettings.EnableOcclusionCulling) {
		if (params.depthMips)
			++hiZBatches;
		else
			++hiZUnavailable;
	}
#endif
	context->CSSetShaderResources(0, 1, &records);
	context->CSSetUnorderedAccessViews(0, UINT(uavs.size()), uavs.data(), nullptr);
	context->Dispatch((params.instances + 63) / 64, 1, 1);
#ifdef DEVBENCH_BRIDGE_ENABLED
	++frameWork.dispatches;
	frameWork.candidateInstances += params.instances;
	frameWork.candidateEyeInstances += uint64_t(params.instances) * params.eyes;
	if (diagnostics.load(std::memory_order_relaxed))
		++cullingDispatches;
#endif
}

void GrassBucketRenderer::Impl::EmitDraws(const Bucket& bucket)
{
	const auto& meshes = bucket.plan.meshes;
	const auto layout = bucket.plan.layout;
	const auto eyes = bucket.plan.params.eyes;
	const auto capacity = bucket.result.capacity;
	GrassD3D::DrawState restore(context.get());
	context->IASetInputLayout(layout);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const std::array<ID3D11ShaderResourceView*, 2> sources{ bucket.result.extras->srv.get(), bucket.records->srv.get() };
	context->VSSetShaderResources(2, UINT(sources.size()), sources.data());
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
			auto compacted = bucket.result.output->resource.get();
			const UINT instanceStride = GrassPolicy::kSurvivorIndexBytes, instanceOffset = slot * capacity * instanceStride;
			context->IASetVertexBuffers(1, 1, &compacted, &instanceStride, &instanceOffset);
			auto drawCB = frameConstants[bucket.plan.slot / kSlotsPerPage]->resource.get();
			const UINT begin = (bucket.plan.slot % kSlotsPerPage) * (kFrameSlotBytes / 16) + kDrawOffset / 16 + slot * 16, size = 16;
			context->VSSetConstantBuffers1(9, 1, &drawCB, &begin, &size);
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
			CS_GPU_PASS("GrassOptimizations::DrawIndirect");
#endif
#ifdef DEVBENCH_BRIDGE_ENABLED
			globals::features::grassCollision.ObserveDrawBindings(true);
#endif
			context->DrawIndexedInstancedIndirect(bucket.result.arguments->resource.get(), slot * 32 + 12);
#ifdef DEVBENCH_BRIDGE_ENABLED
			++frameWork.indirectDraws;
#endif
		}
	}
}

void GrassBucketRenderer::DrawRenderPass(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t flags, RenderPass draw)
{
	if (!pass || !pass->geometry || !pass->shader || pass->shader->shaderType != RE::BSShader::Type::Grass || impl->nativeFallbackDepth) {
		draw(pass, technique, alphaTest, flags);
		return;
	}
	std::shared_ptr<QueueGroup> queue;
	if (pass && pass->geometry && pass->shader && pass->shader->shaderType == RE::BSShader::Type::Grass && !impl->nativeFallbackDepth) {
		std::scoped_lock lock(impl->captureMutex);
		const auto found = impl->residents.find(static_cast<RE::BSMultiStreamInstanceTriShape*>(pass->geometry));
		if (found != impl->residents.end() && found->second.queue &&
			found->second.queue->queuedFrame == globals::state->frameCountAtomic.load(std::memory_order_relaxed))
			queue = found->second.queue;
	}
	if (!queue && pass && pass->geometry && pass->shader && pass->shader->shaderType == RE::BSShader::Type::Grass) {
		std::shared_lock lock(impl->captureMutex);
		for (const auto& entry : impl->queues) {
			auto candidate = entry.second.lock();
			if (candidate && candidate->queuedFrame == globals::state->frameCountAtomic.load(std::memory_order_relaxed) &&
				candidate->representative.load(std::memory_order_acquire) == pass->geometry) {
				queue = std::move(candidate);
				break;
			}
		}
	}
	if (queue)
		impl->MakeNativeVisible(static_cast<RE::BSMultiStreamInstanceTriShape*>(pass->geometry));
	draw(pass, technique, alphaTest, flags);
	if (!queue)
		return;
	if (impl->current == pass->geometry && impl->currentAdmission.bucket < impl->buckets.size()) {
		auto& bucket = impl->buckets.at(impl->currentAdmission.bucket);
		const PassKey key{ pass->passEnum, globals::state->modifiedVertexDescriptor,
			globals::state->modifiedPixelDescriptor, globals::state->permutationData.ExtraShaderDescriptor };
		const auto [outcome, firstPass] = bucket.outcomes.Acquire(key);
		if (!firstPass && *outcome == Outcome::Batched && bucket.sources.front().queue == queue)
			return;
	}
	++impl->nativeFallbackDepth;
	const SKSE::stl::scope_exit restore([&]() noexcept { --impl->nativeFallbackDepth; });
	size_t member = 0;
	for (;;) {
		RE::NiPointer<RE::BSMultiStreamInstanceTriShape> shape;
		RE::NiPointer<RE::BSShaderProperty> property;
		{
			std::scoped_lock lock(impl->captureMutex);
			if (member >= queue->members.size())
				break;
			auto identity = queue->members[member++];
			if (identity == pass->geometry)
				continue;
			const auto found = impl->residents.find(identity);
			if (found == impl->residents.end() || !found->second.lifetime->alive.load(std::memory_order_acquire) ||
				!CurrentGeometry(found->second) || !CurrentContract(found->second))
				continue;
			shape.reset(identity);
			property = found->second.property;
		}
		auto nativePass = *pass;
		nativePass.geometry = shape.get();
		nativePass.shaderProperty = property.get();
		impl->MakeNativeVisible(shape.get());
		draw(&nativePass, technique, alphaTest, flags);
	}
}

void GrassBucketRenderer::DrawGroup(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* mesh,
	uint32_t firstTriangle, uint32_t triangles, uint32_t instances, RE::BSGraphics::VertexDesc descriptor, RE::BSGraphics::VertexBuffer* buffer)
{
	auto& self = *globals::features::grassOptimizations.GetRenderer().impl;
	try {
		if (self.current) {
			static REL::Relocation<void (*)(uint32_t)> setDirtyStates{ REL::RelocationID(75580, 77386) };
			setDirtyStates(0);
			auto& bucket = self.buckets.at(self.currentAdmission.bucket);
			const PassKey key{ self.currentPass, globals::state->modifiedVertexDescriptor, globals::state->modifiedPixelDescriptor, globals::state->permutationData.ExtraShaderDescriptor };
			const auto& source = bucket.sources.at(self.currentAdmission.source);
			const bool same = source.identity == self.current && mesh == source.mesh && firstTriangle == 0 && triangles == source.triangles &&
			                  std::bit_cast<uint64_t>(descriptor) == source.descriptor &&
			                  std::any_of(source.groups.begin(), source.groups.end(), [&](const Group& group) {
								  return group.identity == buffer && buffer && (group.bytes || reinterpret_cast<ID3D11Buffer*>(buffer->buffer) == group.buffer.get()) &&
				                         GrassPolicy::NativeCountMatches(group.count, instances);
							  });
			auto [outcome, firstPass] = bucket.outcomes.Acquire(key);
			bool batched = !firstPass && *outcome == Outcome::Batched && same;
			if (firstPass) {
				if (same) {
					try {
						// Commit pass outcomes only at the native draw boundary, after validating its inputs.
						if ((!self.cullingPrepared || !self.geometryCPUValid) && self.currentSetupPass && self.currentSetup) {
							if (!self.cullingPrepared)
								self.PrepareCulling(self.currentSetupPass, self.currentNativeShader, self.currentSetupFlags, self.currentSetup);
							else if (self.HasBatchDrawState())
								self.RestoreCurrentGeometry();
							setDirtyStates(0);
						}
						batched = self.DrawBucket(bucket, *outcome);
					} catch (const std::exception& error) {
						self.failed = true;
						setDirtyStates(0);
						logger::error("Grass batch unavailable; retaining native rendering: {}", error.what());
					}
				}
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (!batched && self.diagnostics.load(std::memory_order_relaxed))
					++self.fallbacks;
#endif
			}
			if (batched)
				return;
		}
	} catch (const std::exception& error) {
		self.failed = true;
		logger::error("Grass draw bookkeeping failed; retaining native rendering: {}", error.what());
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (self.diagnostics.load(std::memory_order_relaxed))
		++self.nativeDraws;
#endif
#ifdef DEVBENCH_BRIDGE_ENABLED
	globals::features::grassCollision.ObserveDrawBindings(false);
#endif
	originalDraw(renderer, mesh, firstTriangle, triangles, instances, descriptor, buffer);
}

void GrassBucketRenderer::InstallHooks()
{
	if (impl->installed)
		return;
	const auto callsite = REL::RelocationID(100847, 107637).address() + REL::Relocate(0x663, 0x64B, 0x75B);
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
	AddedGroupHook::original = table.write_vfunc(REL::Relocate(0x3C, 0x3C, 0x3D), AddedGroupHook::thunk);
	RemovedGroupHook::original = table.write_vfunc(REL::Relocate(0x3D, 0x3D, 0x3E), RemovedGroupHook::thunk);
	GeneratedHook::original = table.write_vfunc(REL::Relocate(0x3A, 0x3A, 0x3B), GeneratedHook::thunk);
	DestroyHook::original = table.write_vfunc(0, DestroyHook::thunk);
	const std::array<uintptr_t, 3> modelCalls{
		REL::RelocationID(15204, 15372).address() + REL::Relocate(0x2F5, 0x2F5, 0x2F5),
		REL::RelocationID(15205, 15373).address() + REL::Relocate(0x62B, 0x597, 0x62B),
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
void GrassBucketRenderer::Impl::PublishTelemetry()
{
	if (!telemetry.Requested() || frame == UINT32_MAX)
		return;
	const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	if (!telemetry.BeginSample(now))
		return;
	GrassTelemetry::Snapshot snapshot;
	snapshot.frame = frame;
	snapshot.publishedSteadyMilliseconds = now;
	snapshot.enabled = frameSettings.Enabled;
	snapshot.hiZRequested = frameSettings.EnableOcclusionCulling;
	snapshot.meshLODRequested = frameSettings.EnableMeshLOD;
	snapshot.cullingPrepared = cullingPrepared;
	snapshot.work = frameWork;
	{
		std::scoped_lock lock(captureMutex);
		snapshot.identities = identities.size();
		snapshot.residentSources = residents.size();
		snapshot.cpuSnapshotPayloadBytes = cpuRecordBytes;
		for (const auto& [identity, source] : residents) {
			snapshot.residentGroups += source.groups.size();
			for (const auto& group : source.groups)
				snapshot.residentInstances += group.count;
		}
	}
	const auto bytes = [](const std::unique_ptr<Buffer>& buffer) -> uint64_t { return buffer ? buffer->desc.ByteWidth : 0; };
	snapshot.buckets = buckets.size();
	snapshot.dormantBuckets = dormantBuckets.size();
	for (const auto& bucket : buckets) {
		snapshot.activeRecordBytes += bytes(bucket.records);
		snapshot.cullingBufferBytes += bytes(bucket.result.output) + bytes(bucket.result.extras) + bytes(bucket.result.arguments);
	}
	for (const auto& bucket : dormantBuckets)
		snapshot.dormantRecordBytes += bytes(bucket.records);
	snapshot.scratchBufferBytes = bytes(table) + bytes(disabled);
	for (const auto& buffer : frameConstants)
		snapshot.scratchBufferBytes += bytes(buffer);
	snapshot.diagnosticBufferBytes = bytes(gpuCounters);
	for (const auto& buffer : readbacks)
		if (buffer) {
			D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(&desc);
			snapshot.diagnosticBufferBytes += desc.ByteWidth;
		}
	std::unordered_set<ID3D11Buffer*> referenced;
	std::set<std::string> models;
	std::set<std::tuple<std::string, uint64_t, uintptr_t>> variants;
	for (const auto& [key, mesh] : lodMeshes.Entries()) {
		models.insert(key.model);
		variants.emplace(key.model, key.descriptor, reinterpret_cast<uintptr_t>(key.texture.get()));
		for (auto buffer : { mesh.vertices.get(), mesh.indices.get() })
			if (buffer && referenced.insert(buffer).second) {
				D3D11_BUFFER_DESC desc{};
				buffer->GetDesc(&desc);
				snapshot.lodReferencedBufferBytes += desc.ByteWidth;
			}
	}
	snapshot.lodModels = models.size();
	snapshot.lodVariants = variants.size();
	snapshot.viewportCount = telemetryViewportCount;
	if (telemetryViewportCount == 1) {
		snapshot.viewportWidth = telemetryViewport.Width;
		snapshot.viewportHeight = telemetryViewport.Height;
	}
	snapshot.hiZWidth = hiZ.Width();
	snapshot.hiZHeight = hiZ.Height();
	snapshot.hiZMips = hiZ.Mips();
	telemetry.Publish(snapshot);
	telemetryFailed.store(false, std::memory_order_relaxed);
}
void GrassBucketRenderer::RequestTelemetry() { impl->telemetry.Request(); }
void GrassBucketRenderer::SetDiagnosticsEnabled(bool enabled)
{
	impl->diagnostics.store(enabled, std::memory_order_relaxed);
	impl->hiZ.SetDiagnosticsEnabled(enabled);
}
json GrassBucketRenderer::GetDiagnostics() const
{
	auto& self = *impl;
	const auto value = [](const std::atomic_uint64_t& counter) { return counter.load(std::memory_order_relaxed); };
	static constexpr std::array captureNames{
		"unavailable", "geometry", "source", "mesh", "fade", "groupCapacity",
		"groupBuffer", "empty", "frameCapacity", "allocation", "stale", "destroyed"
	};
	static constexpr std::array drawNames{
		"shader", "capacity", "depthTarget", "destroyed", "regenerated", "resources", "layout",
		"geometryConstants", "stereoConstants", "viewport", "queueMembership"
	};
	static_assert(captureNames.size() == size_t(CaptureRejection::Count));
	static_assert(drawNames.size() == size_t(DrawRejection::Count));
	json captureReasons = json::object(), drawReasons = json::object();
	for (size_t i = 0; i < captureNames.size(); ++i)
		captureReasons[captureNames[i]] = value(self.captureRejections[i]);
	for (size_t i = 0; i < drawNames.size(); ++i)
		drawReasons[drawNames[i]] = value(self.drawRejections[i]);
	static constexpr std::array hiZNames{ "resources", "missingTarget", "targetView", "targetMismatch",
		"targetFormat", "targetLayout", "targetSRV", "sourceTexture", "depthState", "sourceLayout",
		"viewport", "extent" };
	static constexpr std::array mismatchNames{ "vertexBuffer", "indexBuffer", "material", "geometry",
		"flags", "lighting", "parameters" };
	static_assert(hiZNames.size() == size_t(GrassHiZ::Failure::Count));
	static_assert(mismatchNames.size() == self.sameModelMismatches.size());
	json hiZReasons = json::object(), modelMismatches = json::object();
	const auto hiZCounts = self.hiZ.FailureCounts();
	for (size_t i = 0; i < hiZNames.size(); ++i)
		hiZReasons[hiZNames[i]] = hiZCounts[i];
	for (size_t i = 0; i < mismatchNames.size(); ++i)
		modelMismatches[mismatchNames[i]] = value(self.sameModelMismatches[i]);
	const auto observation = self.hiZ.ObservedFailures();
	const auto lastHiZFailure = self.hiZ.LastFailure();
	json telemetry = nullptr;
	if (const auto snapshot = self.telemetry.Read()) {
		const auto& s = *snapshot;
		telemetry = { { "frame", s.frame }, { "publishedSteadyMilliseconds", s.publishedSteadyMilliseconds },
			{ "ageMilliseconds", uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) - s.publishedSteadyMilliseconds },
			{ "enabled", s.enabled }, { "hiZRequested", s.hiZRequested }, { "meshLODRequested", s.meshLODRequested },
			{ "cullingPrepared", s.cullingPrepared }, { "identities", s.identities }, { "residentSources", s.residentSources },
			{ "residentGroups", s.residentGroups }, { "residentInstances", s.residentInstances },
			{ "buckets", s.buckets }, { "dormantBuckets", s.dormantBuckets }, { "lodModels", s.lodModels }, { "lodVariants", s.lodVariants },
			{ "cpuSnapshotPayloadBytes", s.cpuSnapshotPayloadBytes }, { "activeRecordBytes", s.activeRecordBytes },
			{ "dormantRecordBytes", s.dormantRecordBytes }, { "cullingBufferBytes", s.cullingBufferBytes },
			{ "scratchBufferBytes", s.scratchBufferBytes }, { "diagnosticBufferBytes", s.diagnosticBufferBytes },
			{ "lodReferencedBufferBytes", s.lodReferencedBufferBytes },
			{ "hiZWidth", s.hiZWidth }, { "hiZHeight", s.hiZHeight }, { "hiZMips", s.hiZMips },
			{ "viewportCount", s.viewportCount },
			{ "viewportWidth", s.viewportCount == 1 ? json(s.viewportWidth) : json(nullptr) },
			{ "viewportHeight", s.viewportCount == 1 ? json(s.viewportHeight) : json(nullptr) },
			{ "work", { { "dispatches", s.work.dispatches }, { "candidateInstances", s.work.candidateInstances },
						  { "candidateEyeInstances", s.work.candidateEyeInstances }, { "indirectDraws", s.work.indirectDraws },
						  { "uploadedRecordBytes", s.work.uploadedRecordBytes } } } };
	}
	return { { "telemetrySchema", 1 }, { "telemetry", std::move(telemetry) },
		{ "telemetryError", self.telemetryFailed.load(std::memory_order_relaxed) ? json("snapshot_failed") : json(nullptr) },
		{ "telemetryScope", "resources at publication; work and main-depth culling viewport from last completed grass frame; CPU bytes cover resident snapshot payloads; buffer bytes exclude HiZ textures and native asset ownership; LOD references may be shared with native assets" },
		{ "hookInstalled", self.installed }, { "renderingAvailable", IsRenderingAvailable() }, { "diagnosticsEnabled", self.diagnostics.load(std::memory_order_relaxed) },
		{ "captureAttempts", value(self.captureAttempts) }, { "capturedSources", value(self.capturedSources) },
		{ "admittedSources", value(self.admittedSources) }, { "drawAttempts", value(self.drawAttempts) },
		{ "captureRejections", std::move(captureReasons) }, { "drawRejections", std::move(drawReasons) },
		{ "batchedDraws", value(self.batches) }, { "combinedSources", value(self.combinedSources) }, { "combinedInstances", value(self.combinedInstances) },
		{ "persistentBucketFrames", value(self.persistentBucketFrames) }, { "bucketRebuilds", value(self.bucketRebuilds) },
		{ "coarseRejectedSlices", value(self.coarseRejectedSlices) }, { "coarseRejectedInstances", value(self.coarseRejectedInstances) },
		{ "cachedSources", value(self.cachedSources) }, { "nativeVisibilityBypassed", value(self.nativeVisibilityBypassed) },
		{ "queuedRepresentatives", value(self.queuedRepresentatives) }, { "queueDuplicates", value(self.queueDuplicates) },
		{ "cullingDispatches", value(self.cullingDispatches) }, { "cullingUploadMaps", value(self.cullingUploadMaps) },
		{ "drawsUsingFrameCulling", value(self.reusedCullingDraws) }, { "residentCullingBytes", value(self.publishedResultBytes) },
		{ "expiredResidents", 0 }, { "pressureEvictions", value(self.pressureEvictions) },
		{ "reusedRecordBuckets", value(self.reusedRecordBuckets) }, { "uploadedRecordBuckets", value(self.uploadedRecordBuckets) },
		{ "uploadedRecordBytes", value(self.uploadedRecordBytes) }, { "uncachedRecordBuckets", 0 },
		{ "nativeDraws", value(self.nativeDraws) }, { "fallbacks", value(self.fallbacks) },
		{ "hiZBatches", value(self.hiZBatches) }, { "hiZDepthFallbacks", value(self.hiZUnavailable) },
		{ "hiZBuildFailures", std::move(hiZReasons) },
		{ "hiZLastBuildFailure", lastHiZFailure == GrassHiZ::Failure::Count ? json(nullptr) : json(hiZNames[size_t(lastHiZFailure)]) },
		{ "hiZFailureState", { { "depthEnabled", observation.depthEnabled }, { "depthFunction", observation.depthFunction },
								 { "viewportCount", observation.viewportCount }, { "viewportX", observation.viewportX },
								 { "viewportY", observation.viewportY }, { "viewportWidth", observation.viewportWidth },
								 { "viewportHeight", observation.viewportHeight }, { "minDepth", observation.minDepth },
								 { "maxDepth", observation.maxDepth }, { "sourceWidth", observation.sourceWidth },
								 { "sourceHeight", observation.sourceHeight } } },
		{ "unidentifiedModels", value(self.unidentifiedModels) }, { "sameModelPeers", value(self.sameModelPeers) },
		{ "sameModelCompatible", value(self.sameModelCompatible) }, { "sameModelMismatches", std::move(modelMismatches) },
		{ "gpuSamples", value(self.samples) }, { "droppedGpuSamples", value(self.droppedSamples) },
		{ "eyeTests", value(self.totals[0]) }, { "frustumRejected", value(self.totals[1]) }, { "densityRejected", value(self.totals[2]) },
		{ "hiZRejected", value(self.totals[3]) }, { "fullMeshSurvivors", value(self.totals[4]) }, { "middleMeshSurvivors", value(self.totals[5]) },
		{ "farMeshSurvivors", value(self.totals[6]) }, { "invalidBoundsRetained", value(self.totals[7]) },
		{ "distanceRejected", value(self.totals[8]) }, { "fadeRejected", value(self.totals[9]) },
		{ "hiZOutcomes", { { "eligible", value(self.totals[10]) }, { "noPyramid", value(self.totals[11]) },
							 { "projectionFailed", value(self.totals[12]) }, { "footprintOutside", value(self.totals[13]) },
							 { "wideFootprint", value(self.totals[14]) }, { "invalidDepth", value(self.totals[15]) },
							 { "noDepthCoverage", value(self.totals[16]) }, { "depthNotBehind", value(self.totals[17]) },
							 { "sampledCells", value(self.totals[18]) } } },
		{ "counterScope", "cumulative while enabled; GPU counters count eye instances and arrive asynchronously" } };
}
#endif
