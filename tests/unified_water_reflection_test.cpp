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
			kWater,
			kOther
		};
		Type type = Type::kWater;
		Type GetType() const { return type; }
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
		// Only retained captures promise material lifetime through deferred destruction.
		bool requireLiveMaterialOnDestruction = false;
		~TESWaterReflections() override
		{
			assert(!requireLiveMaterialOnDestruction || liveMaterials.contains(waterMaterial));
			++destroyedReflections;
		}
	};
	struct TESWaterNormals : NiRefObject
	{
		BSWaterShaderMaterial* waterMaterial = nullptr;
	};
	template <class T>
	using BSTArray = std::vector<T>;
	struct BSTriShape;
	using NiAVObject = BSTriShape;
	struct BSMultiBoundAABB : NiRefObject
	{};
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
		bool validRTTI = true;
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
		static inline unsigned geometryReads = 0;
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
		RuntimeData& GetGeometryRuntimeData()
		{
			++geometryReads;
			return data;
		}
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
		int& depth;
		explicit BSSpinLockGuard(int& lock) : depth(lock) { ++depth; }
		~BSSpinLockGuard() { --depth; }
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
		void AddWater(BSTriShape* shape, TESWaterForm* type, float height, const BSTArray<NiPointer<BSMultiBoundAABB>>* bounds, bool noDisplacement, bool procedural);
		void CheckSharedMaterials() const
		{
			assert(lock > 0);
			for (const auto& entry : waterReflections)
				assert(entry && liveMaterials.contains(entry->waterMaterial));
			for (const auto& entry : waterNormals)
				assert(entry && liveMaterials.contains(entry->waterMaterial));
		}
		void AddWaterNative(BSTriShape* shape, TESWaterForm*, float, const BSTArray<NiPointer<BSMultiBoundAABB>>*, bool, bool)
		{
			BSSpinLockGuard guard(lock);
			CheckSharedMaterials();
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
			object->normals->waterMaterial = static_cast<BSWaterShaderMaterial*>(shape->data.shaderProperty->material);
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
const void* RE::BSWaterShaderProperty::GetRTTI() const { return validRTTI ? globals::rtti::BSWaterShaderPropertyRTTI.get() : nullptr; }
static bool interior = false;
static bool IsInteriorCellActive() { return interior; }
static const RE::TESWorldSpace* ResolveLODDataWorldSpace(const RE::TESWorldSpace* world) { return world; }
static std::vector<RE::NiPointer<RE::NiRefObject>> deferred;
static void QueueDeferredRelease(RE::NiPointer<RE::NiRefObject>& object) { deferred.push_back(object); }
#include "water_entry_cleanup_under_test.h"

#include "reflection_capture_under_test.h"

namespace globals::features
{
	static struct
	{
		UnifiedWaterReflectionCapture reflectionCapture;
		struct
		{
			const RE::NiAVObject* generated = nullptr;
			bool Contains(const RE::NiAVObject* water) const { return water && water == generated; }
		} waterLOD;
	} unifiedWater;
}

struct UnifiedWater
{
	struct TESWaterSystem_AddWater
	{
		static void thunk(RE::TESWaterSystem*, RE::NiAVObject*, RE::TESWaterForm*, float, const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>*, bool, bool);
		static void InitializeLocked(RE::TESWaterSystem*, RE::NiAVObject*, RE::TESWaterForm*, float, const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>*, bool, bool);
		static inline decltype(&thunk) func;
	};
	bool loaded = true;
	bool ready = true;
	bool exterior = true;
	std::atomic<RE::TESWorldSpace*> currentPlayerWorldSpace{ nullptr };
	RE::NiPointer<RE::BSTriShape> waterMesh{ new RE::BSTriShape };
	UnifiedWaterReflectionCapture& reflectionCapture = globals::features::unifiedWater.reflectionCapture;
	std::uint32_t reflectionRetryFrames = 0;
	bool IsWaterDataReady() const { return ready; }
	bool IsExteriorWorldspaceActive() const { return exterior; }
	bool InitializeWaterMaterial(RE::TESWaterSystem&, RE::BSTriShape*, RE::TESWaterForm*, float);
	void RetireReflectionCapture(bool);
	void MaintainReflectionCapture();
};
#include "reflection_maintenance_under_test.h"

void RE::TESWaterSystem::AddWater(BSTriShape* shape, TESWaterForm* type, float height, const BSTArray<NiPointer<BSMultiBoundAABB>>* bounds, bool noDisplacement, bool procedural)
{
	UnifiedWater::TESWaterSystem_AddWater::thunk(this, shape, type, height, bounds, noDisplacement, procedural);
}

static void NativeAddWater(RE::TESWaterSystem* system, RE::NiAVObject* shape, RE::TESWaterForm* type, float height,
	const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* bounds, bool noDisplacement, bool procedural)
{
	system->AddWaterNative(shape, type, height, bounds, noDisplacement, procedural);
}

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

static unsigned checkedNativeCalls = 0;
struct WaterCallArguments
{
	RE::NiAVObject* water = nullptr;
	RE::TESWaterForm* type = nullptr;
	float height = 123.5f;
	const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* bounds = nullptr;
	bool noDisplacement = true;
	bool procedural = false;
};
static WaterCallArguments expectedWaterCall;
static void CheckNativeAddWater(RE::TESWaterSystem* system, RE::NiAVObject* water, RE::TESWaterForm* type, float height,
	const RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* bounds, bool noDisplacement, bool procedural)
{
	assert(system->lock > 0);
	assert(water == expectedWaterCall.water && type == expectedWaterCall.type && height == expectedWaterCall.height);
	assert(bounds == expectedWaterCall.bounds && noDisplacement == expectedWaterCall.noDisplacement && procedural == expectedWaterCall.procedural);
	system->CheckSharedMaterials();
	++checkedNativeCalls;
}

static void CheckSharedMaterials(RE::TESWaterSystem& system, const WaterCallArguments& arguments = {})
{
	const auto depth = system.lock;
	const auto calls = checkedNativeCalls;
	const auto original = std::exchange(UnifiedWater::TESWaterSystem_AddWater::func, CheckNativeAddWater);
	const auto previousArguments = std::exchange(expectedWaterCall, arguments);
	const SKSE::stl::scope_exit restore([&] {
		UnifiedWater::TESWaterSystem_AddWater::func = original;
		expectedWaterCall = previousArguments;
	});
	UnifiedWater::TESWaterSystem_AddWater::thunk(&system, arguments.water, arguments.type, arguments.height, arguments.bounds, arguments.noDisplacement, arguments.procedural);
	assert(system.lock == depth && checkedNativeCalls == calls + 1);
}

static RE::TESWaterObject* AddNativeOwner(RE::TESWaterSystem& system, RE::TESWaterReflections* reflection, RE::TESWaterNormals* normals)
{
	RE::NiPointer<RE::TESWaterObject> object{ new RE::TESWaterObject };
	object->shape = RE::NiPointer<RE::BSTriShape>{ new RE::BSTriShape };
	object->reflections = RE::NiPointer<RE::TESWaterReflections>{ reflection };
	object->normals = RE::NiPointer<RE::TESWaterNormals>{ normals };
	system.waterObjects.push_back(object);
	return object.get();
}

static void TestSharedMaterialRepair()
{
	auto& captures = globals::features::unifiedWater.reflectionCapture;
	{
		RE::TESWaterSystem system;
		RE::NiPointer<RE::BSTriShape> shape{ new RE::BSTriShape };
		RE::TESWaterForm type;
		RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>> bounds;
		for (const auto noDisplacement : { false, true }) {
			for (const auto procedural : { false, true })
				CheckSharedMaterials(system, { shape.get(), &type, -256.0f, &bounds, noDisplacement, procedural });
		}
	}
	for (const auto flags : { 4u, 2u, 12u }) {
		RE::TESWaterSystem system;
		system.waterReflections.emplace_back(new RE::TESWaterReflections);
		system.waterNormals.emplace_back(new RE::TESWaterNormals);
		const auto reflection = system.waterReflections.front().get();
		reflection->flags.value = flags;
		const auto normals = system.waterNormals.front().get();
		const auto original = AddNativeOwner(system, reflection, normals);
		const auto survivor = AddNativeOwner(system, reflection, normals);
		const auto originalMaterial = static_cast<RE::BSWaterShaderMaterial*>(original->shape->data.shaderProperty->material);
		const auto survivorMaterial = static_cast<RE::BSWaterShaderMaterial*>(survivor->shape->data.shaderProperty->material);
		reflection->waterMaterial = survivorMaterial;
		normals->waterMaterial = survivorMaterial;
		CheckSharedMaterials(system);
		assert(reflection->waterMaterial == survivorMaterial && normals->waterMaterial == survivorMaterial);

		reflection->waterMaterial = originalMaterial;
		normals->waterMaterial = originalMaterial;
		const auto reads = RE::BSTriShape::geometryReads;
		CheckSharedMaterials(system);
		assert(RE::BSTriShape::geometryReads == reads + 2);
		system.waterObjects.erase(system.waterObjects.begin());
		assert(!liveMaterials.contains(originalMaterial));
		{
			RE::BSSpinLockGuard outer(system.lock);
			CheckSharedMaterials(system);
		}
		assert(reflection->waterMaterial == survivorMaterial && normals->waterMaterial == survivorMaterial);
		assert(deferred.empty());
		reflection->waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(1);
		normals->waterMaterial = nullptr;
		CheckSharedMaterials(system);
		assert(reflection->waterMaterial == survivorMaterial && normals->waterMaterial == survivorMaterial);
	}
	{
		RE::TESWaterSystem system;
		system.waterReflections.emplace_back(new RE::TESWaterReflections);
		system.waterNormals.emplace_back(new RE::TESWaterNormals);
		const auto reflection = system.waterReflections.front().get();
		const auto normals = system.waterNormals.front().get();
		const auto creator = AddNativeOwner(system, reflection, normals);
		const auto borrower = AddNativeOwner(system, reflection, normals);
		const auto oldMaterial = static_cast<RE::BSWaterShaderMaterial*>(creator->shape->data.shaderProperty->material);
		reflection->waterMaterial = oldMaterial;
		normals->waterMaterial = oldMaterial;
		{
			RE::NiPointer<RE::NiRefObject> pruned{ creator };
			QueueDeferredRelease(pruned);
		}
		system.waterObjects.erase(system.waterObjects.begin());
		CheckSharedMaterials(system);
		assert(liveMaterials.contains(oldMaterial));
		assert(reflection->waterMaterial == borrower->shape->data.shaderProperty->material);
		assert(normals->waterMaterial == reflection->waterMaterial);
		deferred.clear();
		assert(!liveMaterials.contains(oldMaterial));
		CheckSharedMaterials(system);
	}
	{
		RE::TESWaterSystem system;
		RE::NiPointer<RE::TESWaterReflections> reflection{ new RE::TESWaterReflections };
		RE::NiPointer<RE::TESWaterNormals> normals{ new RE::TESWaterNormals };
		reflection->waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(1);
		normals->waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(1);
		system.waterReflections = { reflection, {}, RE::NiPointer<RE::TESWaterReflections>{ new RE::TESWaterReflections } };
		system.waterNormals = { normals, {}, RE::NiPointer<RE::TESWaterNormals>{ new RE::TESWaterNormals } };
		CheckSharedMaterials(system);
		assert(system.waterReflections.empty() && system.waterNormals.empty());
		assert(deferred.size() == 4 && reflection->GetRefCount() == 2 && normals->GetRefCount() == 2);
		deferred.clear();
		assert(reflection->GetRefCount() == 1 && normals->GetRefCount() == 1);
	}
	for (unsigned invalidOwner = 0; invalidOwner < 5; ++invalidOwner) {
		RE::TESWaterSystem system;
		system.waterObjects.emplace_back();
		system.waterReflections.emplace_back(new RE::TESWaterReflections);
		system.waterNormals.emplace_back(new RE::TESWaterNormals);
		const auto owner = AddNativeOwner(system, system.waterReflections.front().get(), system.waterNormals.front().get());
		switch (invalidOwner) {
		case 0:
			owner->shape = {};
			break;
		case 1:
			owner->shape->data.shaderProperty = {};
			break;
		case 2:
			owner->shape->data.shaderProperty->validRTTI = false;
			break;
		case 3:
			owner->shape->data.shaderProperty->material->type = RE::BSShaderMaterial::Type::kOther;
			break;
		case 4:
			owner->shape->data.shaderProperty->SetMaterial(nullptr, false);
			break;
		}
		CheckSharedMaterials(system);
		assert(system.waterReflections.empty() && system.waterNormals.empty() && deferred.size() == 2);
		deferred.clear();
	}
	{
		RE::TESWaterSystem system;
		system.waterReflections.emplace_back(new RE::TESWaterReflections);
		const auto reflection = system.waterReflections.front().get();
		const auto owner = AddNativeOwner(system, reflection, nullptr);
		const auto material = static_cast<RE::BSWaterShaderMaterial*>(owner->shape->data.shaderProperty->material);
		reflection->waterMaterial = material;
		reflection->requireLiveMaterialOnDestruction = true;
		assert(captures.Retain(reflection, owner->shape->data.shaderProperty.get()));
		system.waterObjects.clear();
		assert(liveMaterials.contains(material));
		CheckSharedMaterials(system);
		assert(system.waterReflections.size() == 1 && reflection->waterMaterial == material && deferred.empty());
		AddNativeOwner(system, reflection, nullptr);
		const auto reads = RE::BSTriShape::geometryReads;
		CheckSharedMaterials(system);
		assert(reflection->waterMaterial == material && RE::BSTriShape::geometryReads == reads);
		system.waterObjects.clear();
		// A changed raw field must not detach the capture from its actual lifetime guard.
		reflection->waterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(1);
		CheckSharedMaterials(system);
		assert(reflection->waterMaterial == captures.GetRetainedMaterial(reflection));
		captures.RetireUnused(system, nullptr, QueueDeferredRelease);
		assert(system.waterReflections.empty() && liveMaterials.contains(material));
		deferred.clear();
		assert(!liveMaterials.contains(material));
	}
}

static void TestGeneratedRegistrationGate()
{
	Scene scene;
	const auto tile = scene.MakeTile();
	const auto previous = std::exchange(UnifiedWater::TESWaterSystem_AddWater::func, CheckNativeAddWater);
	expectedWaterCall.water = tile.get();
	globals::features::unifiedWater.waterLOD.generated = tile.get();
	const auto calls = checkedNativeCalls;
	UnifiedWater::TESWaterSystem_AddWater::thunk(&scene.system, tile.get(), nullptr, 123.5f, nullptr, true, false);
	assert(checkedNativeCalls == calls && scene.system.lock == 0);
	{
		RE::BSSpinLockGuard guard(scene.system.lock);
		UnifiedWater::TESWaterSystem_AddWater::InitializeLocked(&scene.system, tile.get(), nullptr, 123.5f, nullptr, true, false);
	}
	assert(checkedNativeCalls == calls + 1 && scene.system.lock == 0);
	globals::features::unifiedWater.waterLOD.generated = nullptr;
	UnifiedWater::TESWaterSystem_AddWater::func = previous;
}

static void TestUnpublishedMaterialCleanup()
{
	Scene scene;
	scene.uw.reflectionCapture.ClearSources();
	const auto tile = scene.MakeTile();
	{
		RE::BSSpinLockGuard guard(scene.system.lock);
		assert(scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100));
		scene.system.addFails = true;
		assert(!scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100));
	}
	assert(scene.system.waterObjects.empty() && scene.system.waterNormals.empty());
	const auto previouslyDeferred = deferred.size();
	scene.uw.MaintainReflectionCapture();
	assert(scene.system.waterReflections.empty() && deferred.size() == previouslyDeferred + 1);
}

int main()
{
	TestGeneratedRegistrationGate();
	UnifiedWater::TESWaterSystem_AddWater::func = NativeAddWater;
	TestUnpublishedMaterialCleanup();
	TestSharedMaterialRepair();
	{
		Scene scene;
		auto tile = scene.MakeTile();
		{
			RE::BSSpinLockGuard guard(scene.system.lock);
			assert(scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100));
		}
		const auto reflection = scene.system.waterReflections.front().get();
		reflection->requireLiveMaterialOnDestruction = true;
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
		RE::TESWaterReflections* retained = nullptr;
		for (unsigned tileIndex = 0; tileIndex < 32; ++tileIndex) {
			auto tile = scene.MakeTile();
			{
				RE::BSSpinLockGuard guard(scene.system.lock);
				assert(scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100));
			}
			assert(scene.system.waterReflections.size() == 1);
			const auto reflection = scene.system.waterReflections.front().get();
			if (!retained)
				retained = reflection;
			assert(reflection == retained && scene.uw.reflectionCapture.Contains(reflection));
			reflection->requireLiveMaterialOnDestruction = true;
			assert(scene.system.waterObjects.empty() && scene.system.waterNormals.empty());
			deferred.clear();
			scene.block.DetachChild2(tile.get());
		}
		assert(scene.system.addCalls == 32 && retained->GetRefCount() == 2);
	}
	{
		Scene scene;
		auto nativeShape = scene.MakeTile();
		scene.system.AddWater(nativeShape.get(), &scene.water, 100, nullptr, true, false);
		const auto reflection = scene.system.waterReflections.front().get();
		auto tile = scene.MakeTile();
		{
			RE::BSSpinLockGuard guard(scene.system.lock);
			assert(scene.uw.InitializeWaterMaterial(scene.system, tile.get(), &scene.water, 100));
		}
		assert(!scene.uw.reflectionCapture.Contains(reflection));  // Reused entry belongs to nativeShape.
		deferred.clear();
		scene.uw.MaintainReflectionCapture();
		assert(scene.uw.reflectionCapture.Contains(reflection));
		reflection->requireLiveMaterialOnDestruction = true;
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
