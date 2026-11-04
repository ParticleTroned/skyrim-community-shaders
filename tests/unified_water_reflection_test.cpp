#ifdef NDEBUG
#	undef NDEBUG
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static std::unordered_set<const void*> liveMaterials;
static unsigned destroyedReflections = 0;
static std::unordered_set<const void*> nativeMaterialCache;
namespace RE
{
	struct NiRefObject
	{
		unsigned references = 0;
		virtual ~NiRefObject() = default;
		void Acquire() { ++references; }
		void Release()
		{
			if (--references == 0)
				delete this;
		}
		unsigned GetRefCount() const { return references; }
	};
	// Intrusive ownership exercises destruction order and native reference-count gates.
	template <class T>
	struct NiPointer
	{
		T* ptr = nullptr;
		NiPointer() = default;
		explicit NiPointer(T* value) : ptr(value)
		{
			if (ptr)
				ptr->Acquire();
		}
		NiPointer(const NiPointer& other) : NiPointer(other.ptr) {}
		NiPointer(NiPointer&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}
		~NiPointer()
		{
			if (ptr)
				ptr->Release();
		}
		NiPointer& operator=(NiPointer other) noexcept
		{
			std::swap(ptr, other.ptr);
			return *this;
		}
		T* get() const { return ptr; }
		T* operator->() const { return ptr; }
		explicit operator bool() const { return ptr != nullptr; }
	};
	struct BSShaderMaterial : NiRefObject
	{
		enum class Type
		{
			kWater
		};
		Type GetType() const { return Type::kWater; }
		void IncRef() { Acquire(); }
	};
	struct BSWaterShaderMaterial : BSShaderMaterial
	{
		BSWaterShaderMaterial()
		{
			liveMaterials.insert(this);
			nativeMaterialCache.insert(this);
		}
		~BSWaterShaderMaterial() override
		{
			assert(!nativeMaterialCache.contains(this));
			liveMaterials.erase(this);
		}
	};
	struct TESWaterReflections : NiRefObject
	{
		enum class Flags
		{
			kDirty = 1,
			kStaticCubemap = 2,
			kDynamicCubemap = 4,
			kInterior = 8
		};
		struct FlagSet
		{
			unsigned value = 4;
			template <class... F>
			bool any(F... flag) const
			{
				return ((value & static_cast<unsigned>(flag)) || ...);
			}
		} flags;
		BSWaterShaderMaterial* waterMaterial = nullptr;
		~TESWaterReflections() override
		{
			assert(liveMaterials.contains(waterMaterial));
			++destroyedReflections;
		}
	};
	struct TESWaterNormals : NiRefObject
	{};
	struct BSTriShape;
	struct BSMultiBoundNode;
	struct NiObject : NiRefObject
	{
		virtual BSTriShape* AsTriShape() { return nullptr; }
		virtual const void* GetRTTI() const { return nullptr; }
	};
	struct NiCloningProcess
	{};
	struct NiUpdateData
	{};
	static void ReleaseNativeMaterial(BSShaderMaterial* material)
	{
		if (!material)
			return;
		if (material->GetRefCount() == 1)
			nativeMaterialCache.erase(material);
		material->Release();
	}
	struct BSWaterShaderProperty : NiObject
	{
		BSShaderMaterial* material = nullptr;
		static inline bool cloneFails = false;
		static inline bool cloneSharesProperty = false;
		BSWaterShaderProperty() { SetMaterial(new BSWaterShaderMaterial, false); }
		~BSWaterShaderProperty() override { ReleaseNativeMaterial(material); }
		const void* GetRTTI() const override;
		void SetMaterial(BSShaderMaterial* next, bool)
		{
			if (next)
				next->IncRef();
			ReleaseNativeMaterial(material);
			material = next;
		}
		NiObject* CreateClone(NiCloningProcess&)
		{
			if (cloneFails)
				return nullptr;
			if (cloneSharesProperty)
				return this;
			return new BSWaterShaderProperty;  // Unique clones must still pin the exact original cached material.
		}
	};
	struct BSTriShape : NiObject
	{
		BSMultiBoundNode* parent = nullptr;
		struct RuntimeData
		{
			NiPointer<BSWaterShaderProperty> shaderProperty{ new BSWaterShaderProperty };
		} data;
		struct
		{
			struct
			{
				float z = 0;
			} translate;
		} local;
		static inline bool cloneFails = false;
		BSTriShape* AsTriShape() override { return this; }
		RuntimeData& GetGeometryRuntimeData() { return data; }
		NiObject* CreateClone(NiCloningProcess&) { return cloneFails ? nullptr : new BSTriShape; }
		bool worldUpdated = false;
		void UpdateWorldData(NiUpdateData*) { worldUpdated = true; }
	};
	struct TESWaterObject : NiRefObject
	{
		NiPointer<BSTriShape> shape;
		NiPointer<TESWaterReflections> reflections;
		NiPointer<TESWaterNormals> normals;
	};
	struct TESWaterForm
	{};
	struct TESWorldSpace
	{};
	struct BSMultiBoundNode : NiRefObject
	{
		BSMultiBoundNode() { references = 1; }  // The fixture owns the stack node.
		void* parent = this;
		struct Bound
		{
			bool data = true;
		} bound;
		struct RuntimeData
		{
			Bound* multiBound;
		} runtime{ &bound };
		std::vector<NiPointer<BSTriShape>> children;
		RuntimeData& GetRuntimeData() { return runtime; }
		void AttachChild(BSTriShape* shape, bool)
		{
			shape->parent = this;
			children.emplace_back(shape);
		}
		void DetachChild2(BSTriShape* shape)
		{
			shape->parent = nullptr;
			std::erase_if(children, [&](const auto& child) { return child.get() == shape; });
		}
	};
	struct BSSpinLockGuard
	{
		explicit BSSpinLockGuard(int&) {}
	};
	struct TESWaterSystem
	{
		std::vector<NiPointer<TESWaterObject>> waterObjects;
		std::vector<NiPointer<TESWaterReflections>> waterReflections;
		std::vector<NiPointer<TESWaterNormals>> waterNormals;
		int lock = 0;
		unsigned addCalls = 0;
		unsigned enableCalls = 0;
		bool enabled = false;
		unsigned creationFlags = 4;
		bool addFails = false;
		void Enable()
		{
			enabled = true;
			++enableCalls;
		}
		void AddWater(BSTriShape* shape, TESWaterForm*, float, void*, bool, bool)
		{
			assert(shape->parent && shape->parent->GetRuntimeData().multiBound && shape->parent->GetRuntimeData().multiBound->data);
			assert(shape->worldUpdated);
			++addCalls;
			if (addFails)
				return;
			if (waterReflections.empty()) {
				NiPointer<TESWaterReflections> reflection{ new TESWaterReflections };
				reflection->waterMaterial = static_cast<BSWaterShaderMaterial*>(shape->data.shaderProperty->material);
				reflection->flags.value = creationFlags;
				waterReflections.push_back(reflection);
			}
			NiPointer<TESWaterObject> object{ new TESWaterObject };
			object->shape = NiPointer<BSTriShape>{ shape };
			object->reflections = waterReflections.front();
			object->normals = NiPointer<TESWaterNormals>{ new TESWaterNormals };
			waterNormals.push_back(object->normals);
			waterObjects.push_back(object);
			Enable();
		}
	};
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F callback;
		explicit scope_exit(F f) : callback(std::move(f)) {}
		~scope_exit() { callback(); }
	};
}
struct TestState
{
	bool loading = false;
	bool IsSaveLoadSafeModeActive() const { return loading; }
};
static TestState testState;
namespace globals
{
	static TestState* state = &testState;
}
namespace globals::game
{
	static RE::TESWaterSystem* waterSystem;
}
namespace globals::rtti
{
	static struct
	{
		const void* get() const { return this; }
	} BSWaterShaderPropertyRTTI;
}
const void* RE::BSWaterShaderProperty::GetRTTI() const { return globals::rtti::BSWaterShaderPropertyRTTI.get(); }
static bool interior = false;
static bool IsInteriorCellActive() { return interior; }
static const RE::TESWorldSpace* ResolveLODDataWorldSpace(const RE::TESWorldSpace* world) { return world; }
static std::vector<RE::NiPointer<RE::NiRefObject>> deferred;
static void QueueDeferredRelease(RE::NiPointer<RE::NiRefObject>& object) { deferred.push_back(object); }
template <class T>
static void RemoveOrphanedEntry(std::vector<RE::NiPointer<T>>& entries, const T* entry)
{
	std::erase_if(entries, [&](const auto& candidate) { return candidate.get() == entry && candidate->GetRefCount() == 1; });
}

#include "reflection_capture_under_test.h"

struct UnifiedWater
{
	bool loaded = true;
	bool ready = true;
	bool exterior = true;
	std::atomic<RE::TESWorldSpace*> currentPlayerWorldSpace{ nullptr };
	RE::NiPointer<RE::BSTriShape> waterMesh{ new RE::BSTriShape };
	UnifiedWaterReflectionCapture reflectionCapture;
	std::uint32_t reflectionRetryFrames = 0;
	bool IsWaterDataReady() const { return ready; }
	bool IsExteriorWorldspaceActive() const { return exterior; }
	void InitializeWaterMaterial(RE::TESWaterSystem&, RE::BSTriShape*, RE::TESWaterForm*, float);
	void RetireReflectionCapture(bool);
	void MaintainReflectionCapture();
};
#include "reflection_maintenance_under_test.h"

struct Scene
{
	RE::TESWorldSpace world;
	RE::TESWaterForm water;
	RE::BSMultiBoundNode block;
	RE::TESWaterSystem system;
	UnifiedWater uw;
	Scene()
	{
		globals::game::waterSystem = &system;
		uw.currentPlayerWorldSpace = &world;
		uw.reflectionCapture.RegisterSource(&block, &world, &water, 100.0f);
	}
	RE::NiPointer<RE::BSTriShape> MakeTile()
	{
		RE::NiPointer<RE::BSTriShape> tile{ new RE::BSTriShape };
		block.AttachChild(tile.get(), true);
		RE::NiUpdateData updateData;
		tile->UpdateWorldData(&updateData);
		return tile;
	}
	~Scene()
	{
		system.waterObjects.clear();
		uw.RetireReflectionCapture(true);
		deferred.clear();
		assert(system.waterReflections.empty());
	}
};

int main()
{
	{
		Scene scene;
		auto tile = scene.MakeTile();
		scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100);
		const auto reflection = scene.system.waterReflections.front().get();
		const auto material = reflection->waterMaterial;
		assert(reflection->GetRefCount() == 2);
		assert(scene.system.waterObjects.empty() && scene.system.waterNormals.empty());
		tile->data.shaderProperty->SetMaterial(new RE::BSWaterShaderMaterial, false);
		assert(nativeMaterialCache.contains(material));
		tile->data.shaderProperty = RE::NiPointer<RE::BSWaterShaderProperty>{ new RE::BSWaterShaderProperty };
		assert(liveMaterials.contains(material));  // Replacing a property cannot invalidate the native raw pointer.
		scene.block.DetachChild2(tile.get());
		tile = {};
		assert(liveMaterials.contains(material));
		for (int i = 0; i < 120; ++i) scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 1 && reflection->GetRefCount() == 2);
		scene.system.enabled = false;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.enabled && scene.system.enableCalls == 2);
		assert(reflection->flags.value == 4);
		scene.uw.reflectionCapture.ClearSources();
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty() && liveMaterials.contains(material));
		deferred.clear();
		assert(!liveMaterials.contains(material));
	}
	{
		Scene scene;
		auto nativeShape = scene.MakeTile();
		scene.system.AddWater(nativeShape.get(), &scene.water, 100, nullptr, true, false);
		const auto reflection = scene.system.waterReflections.front().get();
		auto tile = scene.MakeTile();
		scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100);
		assert(!scene.uw.reflectionCapture.Contains(reflection));  // Reused entry belongs to nativeShape.
		scene.uw.MaintainReflectionCapture();
		assert(scene.uw.reflectionCapture.Contains(reflection));
		const auto material = reflection->waterMaterial;
		scene.uw.reflectionCapture.ClearSources();
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.size() == 1 && deferred.empty());
		scene.system.waterObjects.clear();
		scene.block.DetachChild2(nativeShape.get());
		nativeShape = {};
		assert(liveMaterials.contains(material));
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty());
	}
	{
		Scene scene;
		scene.uw.MaintainReflectionCapture();  // Recovery without a new tile attachment.
		assert(scene.system.addCalls == 1 && scene.system.enableCalls == 1);
		assert(scene.system.waterObjects.empty() && scene.system.waterReflections.size() == 1);
		assert(scene.block.children.empty());  // The bootstrap proxy never reaches a render traversal.
		const auto oldMaterial = scene.system.waterReflections.front()->waterMaterial;
		scene.system.waterReflections.clear();  // Engine reload drops its array ownership.
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 2 && scene.system.waterReflections.size() == 1);
		deferred.clear();
		assert(!liveMaterials.contains(oldMaterial));
		interior = true;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty());
		interior = false;
		RE::TESWorldSpace otherWorld;
		scene.uw.currentPlayerWorldSpace = &otherWorld;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 2);  // Old tiles cannot seed another worldspace.
	}
	{
		Scene scene;
		scene.system.addFails = true;
		scene.uw.MaintainReflectionCapture();
		for (int i = 0; i < 60; ++i) scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 1 && scene.system.waterReflections.empty() && scene.block.children.empty());
		scene.system.addFails = false;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 2 && scene.system.waterReflections.size() == 1);
	}
	for (const unsigned flags : { 2u, 12u }) {
		Scene scene;
		scene.system.creationFlags = flags;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty());  // Static and interior captures are not retained.
	}
	{
		Scene scene;
		scene.uw.ready = false;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 0);
		scene.uw.ready = true;
		RE::BSTriShape::cloneFails = true;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 0 && scene.system.waterReflections.empty());
		RE::BSTriShape::cloneFails = false;
		scene.uw.RetireReflectionCapture(true);
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 0);  // A load reset removes the old bootstrap sources.
	}
	{
		Scene scene;
		testState.loading = true;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.addCalls == 0);
		testState.loading = false;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.size() == 1);
		scene.uw.loaded = false;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty());
	}
	{
		Scene scene;
		RE::BSWaterShaderProperty::cloneFails = true;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterObjects.empty() && scene.system.waterReflections.empty() && scene.block.children.empty());
		RE::BSWaterShaderProperty::cloneFails = false;
		scene.uw.reflectionRetryFrames = 0;
		RE::BSWaterShaderProperty::cloneSharesProperty = true;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.empty());
		RE::BSWaterShaderProperty::cloneSharesProperty = false;
		scene.uw.reflectionRetryFrames = 0;
		scene.uw.MaintainReflectionCapture();
		assert(scene.system.waterReflections.size() == 1);
	}
	{
		Scene scene;
		assert(scene.uw.reflectionCapture.FindSource(&scene.world)->parent.get() == &scene.block);
		scene.uw.reflectionCapture.RemoveSource(&scene.block);
		assert(!scene.uw.reflectionCapture.FindSource(&scene.world));
		scene.uw.reflectionCapture.RegisterSource(&scene.block, &scene.world, &scene.water, 200);
		assert(scene.uw.reflectionCapture.FindSource(&scene.world)->waterHeight == 200);
		scene.block.runtime.multiBound = nullptr;
		scene.uw.MaintainReflectionCapture();
		assert(!scene.uw.reflectionCapture.HasSources() && scene.system.addCalls == 0);
	}
	assert(liveMaterials.empty());
	assert(nativeMaterialCache.empty());
	assert(destroyedReflections >= 8);
}
