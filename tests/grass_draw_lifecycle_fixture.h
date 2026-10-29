#pragma once

#include "Features/GrassOptimizations/GrassPassCache.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using UINT = unsigned;
struct D3D11_BUFFER_DESC
{
	UINT ByteWidth = 1024;
};
struct D3D11_VIEWPORT
{
	float Height = 720;
};
struct ID3D11Buffer
{
	void GetDesc(D3D11_BUFFER_DESC* description) const { *description = {}; }
};
namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* pointer = nullptr;
		T** put() { return &pointer; }
		T* get() const { return pointer; }
		T* operator->() const { return pointer; }
		explicit operator bool() const { return pointer != nullptr; }
	};
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F callback;
		explicit scope_exit(F function) : callback(std::move(function)) {}
		~scope_exit() { callback(); }
	};
}
namespace RE
{
	struct BSShader
	{
		enum class Type
		{
			Grass,
			DistantTree
		};
		Type shaderType = Type::Grass;
	};
	template <class T>
	struct NiPointer
	{
		std::shared_ptr<T> value;
		NiPointer() = default;
		explicit NiPointer(T* pointer) { reset(pointer); }
		explicit operator bool() const { return value != nullptr; }
		T* operator->() const { return value.get(); }
		void reset(T* pointer)
		{
			value = std::shared_ptr<T>(pointer, [](T*) {});
		}
		T* get() const { return value.get(); }
		NiPointer& operator=(const std::shared_ptr<T>& pointer)
		{
			value = pointer;
			return *this;
		}
	};
	struct BSLight
	{};
	struct NiRTTI
	{};
	inline NiRTTI grassRTTI;
	inline const NiRTTI* NiRTTI_BSGrassShaderProperty = &grassRTTI;
	struct BSShaderProperty
	{
		const NiRTTI* GetRTTI() const { return &grassRTTI; }
		enum class EShaderPropertyFlag
		{
			kEffectLighting
		};
		struct Flags
		{
			bool any(EShaderPropertyFlag) const { return false; }
		} flags;
		void* material = nullptr;
	};
	struct BSGrassShaderProperty : BSShaderProperty
	{
		uint32_t windTimer = std::bit_cast<uint32_t>(9.f);
		struct
		{
			std::vector<BSLight*> lights;
		} lightData;
	};
	namespace BSGraphics
	{
		enum class TextureAddressMode
		{
			kClampSClampT
		};
		enum class TextureFilterMode
		{
			kAnisotropic
		};
		struct ShadowState
		{
			uint32_t addressUpdates = 0, filterUpdates = 0;
			void SetPSTextureAddressMode(uint32_t slot, TextureAddressMode mode)
			{
				if (slot != 0 || mode != TextureAddressMode::kClampSClampT)
					throw std::runtime_error("Unexpected grass texture addressing");
				++addressUpdates;
			}
			void SetPSTextureFilterMode(uint32_t slot, TextureFilterMode mode)
			{
				if (slot != 0 || mode != TextureFilterMode::kAnisotropic)
					throw std::runtime_error("Unexpected grass texture filtering");
				++filterUpdates;
			}
		};
		struct Renderer
		{};
		struct TriShape
		{};
		struct VertexDesc
		{
			uint64_t value = 1;
		};
		struct VertexBuffer
		{
			ID3D11Buffer* buffer = nullptr;
		};
		struct VertexShader
		{
			struct Constants
			{
				void* data = nullptr;
			};
			Constants constantBuffers[3];
		};
	}
	struct BSMultiStreamInstanceTriShape
	{
		struct Transform
		{
			std::array<float, 3> translate{};
		} world;
		struct Geometry
		{
			std::shared_ptr<BSShaderProperty> shaderProperty;
		} geometry;
		Geometry& GetGeometryRuntimeData() { return geometry; }
	};
	struct BSRenderPass
	{
		BSMultiStreamInstanceTriShape* geometry;
		uint32_t passEnum = 1;
		BSShaderProperty* shaderProperty = nullptr;
		BSLight** sceneLights = nullptr;
		uint8_t numLights = 0;
		BSShader* shader = nullptr;
	};
}
namespace GrassRuntime
{
	struct GroupReadLock
	{
		GroupReadLock() {}
		~GroupReadLock() {}
	};
	using SetupGeometry = void (*)(RE::BSShader*, RE::BSRenderPass*, uint32_t);
	inline auto& GetPropertyData(RE::BSGrassShaderProperty* property, bool) { return *property; }
}
namespace REL
{
	struct RelocationID
	{
		RelocationID(int, int) {}
	};
	template <class T>
	struct Relocation
	{
		using Pointer = std::conditional_t<std::is_function_v<T>, std::add_pointer_t<T>, T>;
		Pointer target = nullptr;
		static inline bool throwNext = false;
		Relocation() = default;
		Relocation(RelocationID) {}
		Relocation(const RE::NiRTTI* value)
			requires std::is_same_v<T, const RE::NiRTTI*>
			: target(value)
		{}
		Pointer get() const { return target; }
		template <class... Args>
		void operator()(Args&&... args) const
		{
			if (throwNext) {
				throwNext = false;
				throw std::runtime_error("Injected state flush failure");
			}
			if (target)
				target(std::forward<Args>(args)...);
		}
	};
}
namespace logger
{
	template <class... T>
	void error(const char*, T&&...)
	{}
}
using PassKey = GrassPassCache::Key;
using Outcome = GrassPassCache::Outcome;
struct Admission
{
	size_t bucket, source;
};
struct BufferReference
{
	ID3D11Buffer* buffer = nullptr;
	ID3D11Buffer* get() const { return buffer; }
};
struct Group
{
	RE::BSGraphics::VertexBuffer* identity;
	bool bytes = false;
	BufferReference buffer;
	uint32_t count = 4;
};
struct QueuedGroup;
struct Lifetime
{
	std::atomic_bool alive{ true };
	std::atomic_uint64_t generation{ 0 };
	std::weak_ptr<const QueuedGroup> queued;
};
struct Queue
{
	GrassPolicy::WindHistory windHistory;
	uint32_t queuedFrame = 1;
	uint32_t instances = 4, slices = 1;
	std::atomic<RE::BSMultiStreamInstanceTriShape*> representative = nullptr;
	std::vector<RE::BSMultiStreamInstanceTriShape*> members;
};
using QueueGroup = Queue;
struct QueuedShape
{
	RE::NiPointer<RE::BSMultiStreamInstanceTriShape> shape;
	std::shared_ptr<Lifetime> lifetime;
};
struct QueuedGroup
{
	uint32_t frame;
	RE::BSMultiStreamInstanceTriShape* representative;
	std::shared_ptr<QueueGroup> queue;
	std::vector<QueuedShape> members;
};
struct Source
{
	RE::BSMultiStreamInstanceTriShape* identity;
	RE::BSGraphics::TriShape* mesh;
	uint32_t triangles = 2;
	uint64_t descriptor = 1;
	std::vector<Group> groups;
	std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
	uint64_t generation = 0, cpuRecordBytes = 0;
	bool groupsCurrent = true;
	bool forbidNativeGeometry = false;
	std::shared_ptr<Queue> queue = std::make_shared<Queue>();
	std::shared_ptr<RE::BSShaderProperty> property;
	std::vector<RE::BSLight*> lights;
	std::array<float, 3> origin{};
};
struct Bucket
{
	std::vector<Source> sources;
	GrassPassCache::Cache outcomes;
	uint32_t instances = 4;
	struct Plan
	{
		bool prepared = false;
		bool empty = false;
		std::array<float, 152> geometry{};
		void Retire() { prepared = empty = false; }
	} plan;
};
struct FakeContext
{
	ID3D11Buffer stereo;
	void VSSetConstantBuffers(int, int, ID3D11Buffer* const*) {}
	void VSGetConstantBuffers1(int, int, ID3D11Buffer** buffer, UINT* first, UINT* count)
	{
		*buffer = &stereo;
		*first = 0;
		*count = 3;
	}
	void RSGetViewports(UINT* count, D3D11_VIEWPORT* viewport)
	{
		*count = 1;
		*viewport = {};
	}
};
struct FakeBuffer
{
	BufferReference resource;
};
class GrassBucketRenderer;
namespace globals
{
	inline struct State
	{
		enum class ExtraShaderDescriptors
		{
			GrassSphereNormal = 1,
			PBRGrass = 2,
			PBRGrassShading = 4
		};
		uint32_t frameCount = 1, modifiedVertexDescriptor = 2, modifiedPixelDescriptor = 3;
		RE::BSShader* currentShader = nullptr;
		std::atomic_uint32_t frameCountAtomic{ 1 };
		struct Permutation
		{
			uint32_t ExtraShaderDescriptor = 0;
		} permutationData;
	} stateStorage;
	inline State* state = &stateStorage;
	namespace game
	{
		inline bool isVR = false;
		inline RE::BSGraphics::VertexShader* shader = nullptr;
		inline RE::BSGraphics::VertexShader** currentVertexShader = &shader;
		inline RE::BSGraphics::ShadowState shadowStateStorage;
		inline RE::BSGraphics::ShadowState* shadowState = &shadowStateStorage;
	}
	namespace features
	{
		inline GrassBucketRenderer* renderer = nullptr;
		inline struct GrassFeature
		{
			bool loaded = true, enabled = true;
			bool IsEnabled() const { return enabled; }
			GrassPolicy::Settings GetSettings() const { return {}; }
			GrassBucketRenderer& GetRenderer() { return *renderer; }
			void PrepareGeometry(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry, std::optional<uint32_t>);
		} grassOptimizations;
		inline struct PBR
		{
			bool IsPBRGrassMaterial(void*) const { return false; }
			bool IsPBRGrassEnabled() const { return false; }
		} truePBR;
		inline struct Collision
		{
			void ObserveDrawBindings(bool) {}
		} grassCollision;
	}
}
using State = globals::State;
inline bool CurrentGeometry(const Source& source)
{
	if (source.forbidNativeGeometry)
		throw std::runtime_error("Stale preparation read native geometry before lifetime validation");
	return source.identity->GetGeometryRuntimeData().shaderProperty.get() == source.property.get();
}
inline bool CurrentContract(const Source&) { return true; }
inline bool CurrentGroups(const Source& source) { return source.groupsCurrent; }
enum class DrawRejection
{
	QueueMembership
};
class GrassBucketRenderer
{
public:
	struct Impl
	{
		RE::BSMultiStreamInstanceTriShape* current = nullptr;
		FakeContext* context = nullptr;
		FakeBuffer* disabled = nullptr;
		uint32_t nativeFallbackDepth = 0, currentPass = 0;
		bool currentMainPass = false, currentDrawObserved = false;
		std::array<float, 3> currentOrigin{};
		Admission currentAdmission{};
		std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Admission> admitted;
		std::unordered_map<RE::BSMultiStreamInstanceTriShape*, Source> residents;
		std::unordered_map<RE::BSMultiStreamInstanceTriShape*, std::pair<std::shared_ptr<Lifetime>, std::string>> identities;
		uint64_t residentRevision = 0, cpuRecordBytes = 0;
		std::unordered_map<unsigned, std::weak_ptr<Queue>> queues;
		std::vector<std::shared_ptr<const QueuedGroup>> queuedGroups;
		GrassPolicy::Settings frameSettings;
		bool geometryCPUValid = false, failed = false, ready = false, cullingPrepared = false, throwOnPrepare = false;
		bool allCulled = false, throwOnUpload = false;
		bool shaderAllowed = true, depthAllowed = true;
		bool throwOnDepth = false;
		bool drawShadersReady = false, pairedShadersAvailable = true;
		std::shared_mutex captureMutex;
		float frameHeight = 0;
		std::vector<int> frameSlices;
		D3D11_VIEWPORT telemetryViewport;
		UINT telemetryViewportCount = 0;
		std::array<float, 152> geometryCPU{};
		std::vector<Bucket> buckets{ 1 };
		std::vector<Bucket> dormantBuckets;
		RE::BSRenderPass* currentSetupPass = nullptr;
		RE::BSShader* currentNativeShader = nullptr;
		GrassRuntime::SetupGeometry currentSetup = nullptr;
		uint32_t currentSetupFlags = 0;
		std::optional<uint32_t> currentNativeWindTimer;
		std::atomic_bool diagnostics{ false };
		uint32_t frame = 1, submissions = 0, batches = 0, reusedCullingDraws = 0;
		uint64_t combinedSources = 0, combinedInstances = 0, cullingAttempts = 0, fallbacks = 0, nativeDraws = 0;
		void PrepareFrame() {}
		void ClearDrawContext() noexcept;
		bool HasActiveDrawContext() const noexcept;
		void UpdateLODDemand(const GrassPolicy::Settings&) {}
		void PrepareCulling(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry);
		void CopyGeometryConstants();
		void RestoreCurrentGeometry();
		void MakeNativeVisible(RE::BSMultiStreamInstanceTriShape*);
		void MakeNativeVisibleLocked(RE::BSMultiStreamInstanceTriShape*) {}
		void EraseResident(RE::BSMultiStreamInstanceTriShape*);
		void UnassignQueue(Source&);
		std::shared_ptr<const QueuedGroup> QueuedFor(RE::BSMultiStreamInstanceTriShape*, uint32_t) const;
		bool MatchesQueuedSources(const Bucket&) const;
		std::function<void()> beforeBucketWork;
		void RejectDraw(DrawRejection) {}
		void UploadCulling()
		{
			if (throwOnUpload)
				throw std::runtime_error("Injected upload failure");
		}
		bool PrepareBucket(Bucket& bucket)
		{
			if (beforeBucketWork)
				beforeBucketWork();
			++cullingAttempts;
			if (throwOnPrepare)
				throw std::runtime_error("Injected preparation failure");
			if (!geometryCPUValid)
				return false;
			const auto wind = GrassPolicy::WindVectorOffset(globals::game::isVR);
			geometryCPU[wind + 7] = bucket.sources.front().queue->windHistory.Previous(frame, geometryCPU[wind + 3]);
			bucket.plan.prepared = true;
			bucket.plan.empty = allCulled;
			bucket.plan.geometry = geometryCPU;
			return true;
		}
		bool HasBatchShader() const { return ready && shaderAllowed; }
		bool MainDepthBound() const
		{
			if (throwOnDepth)
				throw std::runtime_error("Injected depth-state failure");
			return depthAllowed;
		}
		bool PrepareDrawShaders() { return drawShadersReady = pairedShadersAvailable; }
		bool HasBatchDrawState() const;
		bool DrawBucket(Bucket&, Outcome&);
		bool DrawMainPass(Bucket&, const PassKey&, bool);
		void EmitDraws(const Bucket&)
		{
			++submissions;
		}
	} storage;
	Impl* impl = &storage;
	using NativeDraw = void (*)(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t,
		uint32_t, RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
	static inline uint32_t nativeCalls = 0;
	static inline NativeDraw originalDraw = [](RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t,
												uint32_t, uint32_t, RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*) { ++nativeCalls; };
	void PrepareGeometry(RE::BSRenderPass*, RE::BSShader*, uint32_t, GrassRuntime::SetupGeometry, std::optional<uint32_t>);
	void FinishGeometry(RE::BSRenderPass*);
	void EndFrame();
	void RemoveShape(RE::BSMultiStreamInstanceTriShape*);
	using RenderPass = void (*)(RE::BSRenderPass*, uint32_t, bool, uint32_t);
	void DrawRenderPass(RE::BSRenderPass*, uint32_t, bool, uint32_t, RenderPass);
	static void DrawGroup(RE::BSGraphics::Renderer*, RE::BSGraphics::TriShape*, uint32_t, uint32_t,
		uint32_t, RE::BSGraphics::VertexDesc, RE::BSGraphics::VertexBuffer*);
};

inline void globals::features::GrassFeature::PrepareGeometry(RE::BSRenderPass* pass, RE::BSShader* shader, uint32_t flags,
	GrassRuntime::SetupGeometry setup, std::optional<uint32_t> windTimer)
{
	renderer->PrepareGeometry(pass, shader, flags, setup, windTimer);
}

struct Fixture
{
	GrassBucketRenderer renderer;
	FakeContext context;
	ID3D11Buffer buffer;
	FakeBuffer parameters{ { &buffer } };
	RE::BSMultiStreamInstanceTriShape shape;
	RE::BSShader shader;
	RE::BSRenderPass pass{ &shape };
	RE::BSGraphics::TriShape mesh, unrelatedMesh;
	RE::BSGraphics::VertexBuffer instances{ &buffer };
	std::array<float, 152> constants{};
	RE::BSGraphics::VertexShader vertex;
	std::shared_ptr<RE::BSGrassShaderProperty> property = std::make_shared<RE::BSGrassShaderProperty>();
	std::shared_ptr<RE::BSGrassShaderProperty> replacementProperty = std::make_shared<RE::BSGrassShaderProperty>();
	bool replacePropertyOnSetup = false;
	bool initialSnapshotAvailable = true, recoveredSnapshotAvailable = true;
	bool inOriginalSetup = false;
	std::function<void(RE::BSRenderPass*)> duringNativeSetup;
	uint32_t nativeSetupCalls = 0, nativeRenderPassCalls = 0;
	static inline Fixture* active = nullptr;
	Fixture()
	{
		active = this;
		replacementProperty->windTimer = std::bit_cast<uint32_t>(3.f);
		globals::features::renderer = &renderer;
		globals::features::grassOptimizations.loaded = true;
		globals::features::grassOptimizations.enabled = true;
		globals::game::shadowStateStorage = {};
		globals::state->currentShader = nullptr;
		globals::state->frameCount = 1;
		globals::state->frameCountAtomic = 1;
		globals::state->modifiedVertexDescriptor = 2;
		globals::state->modifiedPixelDescriptor = 3;
		GrassBucketRenderer::nativeCalls = 0;
		vertex.constantBuffers[2].data = constants.data();
		globals::game::shader = &vertex;
		renderer.impl->context = &context;
		renderer.impl->disabled = &parameters;
		renderer.impl->admitted.emplace(&shape, Admission{ 0, 0 });
		renderer.impl->buckets.front().sources.push_back({ &shape, &mesh, 2, 1, { { &instances, false, { &buffer }, 4 } } });
		shape.geometry.shaderProperty = property;
		pass.shaderProperty = property.get();
		pass.shader = &shader;
		auto& source = renderer.impl->buckets.front().sources.front();
		source.property = property;
		source.queue->members = { &shape };
		source.queue->representative = &shape;
		renderer.impl->residents.emplace(&shape, source);
		renderer.impl->queues.emplace(0, source.queue);
		QueueSources();
	}
	void QueueSources()
	{
		auto& bucket = renderer.impl->buckets.front();
		auto queued = std::make_shared<QueuedGroup>(1u, bucket.sources.front().queue->representative.load(), bucket.sources.front().queue);
		for (auto& source : bucket.sources) {
			renderer.impl->identities[source.identity].first = source.lifetime;
			source.lifetime->queued = queued;
			queued->members.push_back({ RE::NiPointer<RE::BSMultiStreamInstanceTriShape>{ source.identity }, source.lifetime });
		}
		renderer.impl->queuedGroups = { queued };
	}
	void Setup(uint32_t flags = 0);
	void Restore();
	static void NativeSetup(RE::BSShader*, RE::BSRenderPass*, uint32_t);
	void Draw(bool same = true) { renderer.DrawGroup(nullptr, same ? &mesh : &unrelatedMesh, 0, 2, 4, {}, &instances); }
};
