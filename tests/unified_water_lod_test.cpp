#ifdef NDEBUG
#	undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RE
{
	struct NiNode;
	struct BSMultiBoundNode;
	struct BSTriShape;
	struct NiCloningProcess
	{};
	struct NiUpdateData
	{};
	struct NiObject
	{
		static inline int live = 0;
		unsigned refs = 0;
		NiObject() { ++live; }
		virtual ~NiObject() { --live; }
		virtual BSTriShape* AsTriShape() { return nullptr; }
		virtual BSMultiBoundNode* AsMultiBoundNode() { return nullptr; }
		virtual NiObject* CreateClone(NiCloningProcess&) = 0;
		void IncRef() { ++refs; }
		void DecRef()
		{
			if (--refs == 0)
				delete this;
		}
	};
	template <class T>
	struct NiPointer
	{
		T* ptr = nullptr;
		NiPointer() = default;
		explicit NiPointer(T* p) : ptr(p)
		{
			if (ptr)
				ptr->IncRef();
		}
		NiPointer(const NiPointer& other) : NiPointer(other.ptr) {}
		NiPointer(NiPointer&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}
		~NiPointer()
		{
			if (ptr)
				ptr->DecRef();
		}
		NiPointer& operator=(NiPointer other)
		{
			std::swap(ptr, other.ptr);
			return *this;
		}
		T* get() const { return ptr; }
		T* operator->() const { return ptr; }
		explicit operator bool() const { return ptr != nullptr; }
	};
	struct Point
	{
		float x = 0, y = 0, z = 0;
	};
	struct NiAVObject : NiObject
	{
		NiNode* parent = nullptr;
		struct
		{
			Point translate;
			float scale = 1;
		} local, world;
		bool culled = false;
		bool worldUpdated = false;
		int cullWrites = 0;
		virtual NiNode* AsNode() { return nullptr; }
		bool GetAppCulled() const { return culled; }
		void SetAppCulled(bool v)
		{
			culled = v;
			++cullWrites;
		}
		void Update(NiUpdateData&);
	};
	struct BSWaterShaderProperty
	{
		enum class WaterFlag
		{
			kUseCubemapReflections = 1,
			kUseReflections = 2,
			kEnableFlowmap = 4,
			kBlendNormals = 8
		};
		const void* GetRTTI() const { return nullptr; }
		WaterFlag waterFlags{};
	};
	inline BSWaterShaderProperty::WaterFlag& operator|=(BSWaterShaderProperty::WaterFlag& a, BSWaterShaderProperty::WaterFlag b)
	{
		a = static_cast<BSWaterShaderProperty::WaterFlag>(static_cast<int>(a) | static_cast<int>(b));
		return a;
	}
	struct BSTriShape : NiAVObject
	{
		static inline int clonesBeforeFailure = -1;
		BSTriShape* AsTriShape() override { return this; }
		NiObject* CreateClone(NiCloningProcess&) override
		{
			if (clonesBeforeFailure == 0)
				return nullptr;
			if (clonesBeforeFailure > 0)
				--clonesBeforeFailure;
			return new BSTriShape;
		}
		struct
		{
			std::shared_ptr<BSWaterShaderProperty> shaderProperty;
		} data;
		auto& GetGeometryRuntimeData() { return data; }
	};
	struct NiNode : NiAVObject
	{
		std::vector<NiPointer<NiAVObject>> children;
		NiNode* AsNode() override { return this; }
		auto& GetChildren() { return children; }
		void AttachChild(NiAVObject* child, bool)
		{
			assert(child && !child->parent);
			children.emplace_back(child);
			child->parent = this;
		}
		void DetachChildAt(std::size_t i)
		{
			children[i]->parent = nullptr;
			children.erase(children.begin() + i);
		}
		void DetachChild2(NiAVObject* child)
		{
			for (auto i = children.size(); i-- > 0;)
				if (children[i].get() == child)
					DetachChildAt(i);
		}
		NiObject* CreateClone(NiCloningProcess&) override { return new NiNode; }
	};
	struct BSMultiBoundNode : NiNode
	{
		static inline bool cloneFails = false;
		BSMultiBoundNode* AsMultiBoundNode() override { return this; }
		NiObject* CreateClone(NiCloningProcess& cloning) override
		{
			if (cloneFails)
				return nullptr;
			auto copy = new BSMultiBoundNode;
			for (const auto& child : children) copy->AttachChild(static_cast<NiAVObject*>(child->CreateClone(cloning)), true);
			return copy;
		}
	};
	void NiAVObject::Update(NiUpdateData& data)
	{
		world = local;
		if (parent) {
			assert(parent->worldUpdated);
			world.translate.x += parent->world.translate.x;
			world.translate.y += parent->world.translate.y;
			world.translate.z += parent->world.translate.z;
		}
		worldUpdated = true;
		if (auto node = AsNode())
			for (const auto& child : node->GetChildren()) child->Update(data);
	}
	struct TESWorldSpace
	{};
	struct TES
	{
		bool gridCells = false;
	};
	struct TESWaterForm
	{
		enum class Flag
		{
			kEnableFlowmap,
			kBlendNormals
		};
		struct
		{
			bool any(Flag) const { return false; }
		} flags;
	};
	struct Manager
	{
		TESWorldSpace* worldSpace;
	};
	struct BGSTerrainNode
	{
		Manager* manager;
		int baseCellX = 0, baseCellY = 0;
		int level = 4;
		int GetLODLevel() const { return level; }
	};
	struct BGSTerrainBlock
	{
		BGSTerrainNode* node;
		BSMultiBoundNode* chunk;
		BSMultiBoundNode* water;
		bool loaded = true, attached = false, waterAttached = false;
	};
	struct BSSpinLockGuard
	{
		static inline int acquisitions = 0;
		int& depth;
		explicit BSSpinLockGuard(int& v) : depth(v)
		{
			++depth;
			++acquisitions;
		}
		~BSSpinLockGuard() { --depth; }
	};
	struct TESWaterSystem
	{
		int lock = 0, removals = 0;
		std::vector<int> waterObjects;
		void RemoveWater(NiAVObject*) { ++removals; }
		void Enable() {}
	};
	namespace BSVisit
	{
		enum class BSVisitControl
		{
			kContinue
		};
		template <class F>
		void TraverseScenegraphObjects(NiAVObject* object, F callback)
		{
			callback(object);
			if (auto node = object->AsNode())
				for (const auto& child : node->GetChildren()) TraverseScenegraphObjects(child.get(), callback);
		}
	}
}
namespace REX
{
	template <class T>
	struct EnumSet
	{
		T value;
		EnumSet(T v) : value(v) {}
		operator T() const { return value; }
		EnumSet& operator|=(T v)
		{
			value |= v;
			return *this;
		}
	};
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F callback;
		explicit scope_exit(F f) : callback(f) {}
		~scope_exit() { callback(); }
	};
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}
namespace globals::game
{
	RE::TESWaterSystem* waterSystem;
}
namespace globals::rtti
{
	struct
	{
		const void* get() const { return this; }
	} BSWaterShaderPropertyRTTI;
}
namespace REL
{
	static std::array<std::uint8_t, 0x300> code;
	static bool ae = false;
	static unsigned writes = 0;
	constexpr std::uint8_t NOP = 0x90;
	struct RelocationID
	{
		RelocationID(int se, int aeID) { assert(se == 35561 && aeID == 36560); }
		std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(code.data()); }
	};
	static int Relocate(int se, int aeOffset) { return ae ? aeOffset : se; }
	static void safe_fill(std::uintptr_t address, std::uint8_t value, int count)
	{
		assert(count == 5);
		++writes;
		std::fill_n(reinterpret_cast<std::uint8_t*>(address), count, value);
	}
}
#include "water_flowmap_patch_under_test.h"
#include "water_lod_ownership_under_test.h"
#include "water_node_cleanup_under_test.h"

struct WaterCache
{
	struct Instruction
	{
		struct
		{
			RE::TESWaterForm* ptr;
		} form;
		int x = 0, y = 0, size = 1;
		float waterHeight = 0;
	};
	std::vector<Instruction> instructions;
	bool available = false;
	struct InstructionResult
	{
		const std::vector<Instruction>* instructions;
	};
	InstructionResult GetInstructions(RE::TESWorldSpace*, int, int, int) const { return { available ? &instructions : nullptr }; }
};
static const RE::TESWorldSpace* ResolveLODDataWorldSpace(const RE::TESWorldSpace* p) { return p; }
static bool IsChildWorldSpace(const RE::TESWorldSpace*) { return false; }
static bool ShouldCullAtCell(RE::TES*, int, int) { return false; }
static int cullCalls = 0;
static void CullWaterParentByGridCells(RE::BSMultiBoundNode*) { ++cullCalls; }
static void QueueDeferredRelease() {}

struct UnifiedWater
{
	struct TESWaterSystem_AddLODWater
	{
		static void thunk(RE::TESWaterSystem*, RE::NiAVObject*, RE::TESWorldSpace*, RE::NiNode*, RE::BSMultiBoundNode*, bool);
		static inline decltype(&thunk) func;
	};
	struct BGSTerrainBlock_Attach
	{
		static void thunk(RE::BGSTerrainBlock*);
		static inline decltype(&thunk) func;
	};
	struct BGSTerrainBlock_Detach
	{
		static void thunk(RE::BGSTerrainBlock*);
		static inline decltype(&thunk) func;
	};
	struct BGSTerrainNode_UpdateWaterMeshSubVisibility
	{
		static void thunk(const RE::BGSTerrainNode*, RE::BSMultiBoundNode*);
		static inline decltype(&thunk) func;
	};
	bool ready = true, exterior = true;
	std::atomic_bool mapMenuOpen{ false };
	WaterCache* waterCache = nullptr;
	RE::NiPointer<RE::BSTriShape> waterMesh{ new RE::BSTriShape }, optimisedWaterMesh{ new RE::BSTriShape };
	RE::NiNode** gWaterLOD = nullptr;
	UnifiedWaterLOD waterLOD;
	std::atomic<RE::TES*> cachedTes{ nullptr };
	std::atomic<RE::TESWorldSpace*> currentPlayerWorldSpace{ nullptr };
	int reflectionRetryFrames = 0, materialInitializations = 0;
	int materialsBeforeFailure = -1;
	struct
	{
		bool UseOptimisedMeshes = false;
	} settings;
	struct Capture
	{
		int sources = 0;
		void RegisterSource(RE::BSMultiBoundNode*, const RE::TESWorldSpace*, RE::TESWaterForm*, float) { ++sources; }
		void RemoveSource(RE::BSMultiBoundNode*) { --sources; }
		bool HasSources() const { return sources != 0; }
		void RetireUnused(RE::TESWaterSystem&, void*, void (*)()) {}
	} reflectionCapture;
	bool IsWaterDataReady() const { return ready; }
	bool IsExteriorWorldspaceActive() const { return exterior; }
	bool IsGeneratedWater(const RE::NiAVObject*) const;
	void UpdateWaterLODCull();
	void TryCompleteDeferredChildWorldspaceCull(RE::TES*) {}
	bool InitializeWaterMaterial(RE::TESWaterSystem& system, RE::BSTriShape* shape, RE::TESWaterForm*, float)
	{
		assert(system.lock == 1 && shape->worldUpdated && !waterLOD.Contains(shape));
		assert(shape->world.translate.x == shape->local.translate.x + 8192.0f);
		assert(shape->world.translate.y == shape->local.translate.y - 4096.0f);
		if (materialsBeforeFailure == 0)
			return false;
		if (materialsBeforeFailure > 0)
			--materialsBeforeFailure;
		++materialInitializations;
		return true;
	}
};
namespace globals::features
{
	static UnifiedWater unifiedWater;
}
#include "water_lod_hooks_under_test.h"
#include "water_lod_visibility_under_test.h"

static int nativeAttach = 0, nativeDetach = 0, nativeVisibility = 0, registrations = 0;
static bool rejectNativeAttach = false;
static void NativeAttach(RE::BGSTerrainBlock* block)
{
	++nativeAttach;
	if (!block || block->attached || !block->loaded || !block->chunk || rejectNativeAttach)
		return;
	block->attached = true;
	if (block->water) {
		++registrations;
		block->waterAttached = true;
	}
}
static void NativeDetach(RE::BGSTerrainBlock* block)
{
	++nativeDetach;
	if (block->water) {
		assert(block->waterAttached);
		block->waterAttached = false;
	}
	block->attached = false;
}
static void NativeVisibility(const RE::BGSTerrainNode*, RE::BSMultiBoundNode*) { ++nativeVisibility; }
static int nativeLODRegistrations = 0;
static void NativeAddLODWater(RE::TESWaterSystem* system, RE::NiAVObject* water, RE::TESWorldSpace* world, RE::NiNode* root, RE::BSMultiBoundNode* bound, bool enableHeight)
{
	assert(system == globals::game::waterSystem && water && world && root && bound);
	assert(water->parent == bound && enableHeight);
	++nativeLODRegistrations;
}

struct Scene
{
	RE::TESWaterSystem system;
	RE::TESWorldSpace world;
	RE::TESWaterForm form;
	RE::Manager manager{ &world };
	RE::BGSTerrainNode node{ &manager };
	RE::NiPointer<RE::BSMultiBoundNode> chunk{ new RE::BSMultiBoundNode }, original{ new RE::BSMultiBoundNode }, lod{ new RE::BSMultiBoundNode };
	RE::NiPointer<RE::BSTriShape> nativeTile{ new RE::BSTriShape };
	RE::NiNode* lodPtr = lod.get();
	RE::BGSTerrainBlock block{ &node, chunk.get(), original.get() };
	WaterCache cache;
	UnifiedWater& uw = globals::features::unifiedWater;
	Scene()
	{
		globals::game::waterSystem = &system;
		uw.ready = true;
		uw.exterior = true;
		uw.mapMenuOpen = false;
		uw.waterCache = &cache;
		uw.gWaterLOD = &lodPtr;
		uw.materialInitializations = 0;
		uw.materialsBeforeFailure = -1;
		chunk->local.translate = { 8192.0f, -4096.0f, 0 };
		original->AttachChild(nativeTile.get(), true);
		chunk->AttachChild(original.get(), true);
	}
	void Attach() { UnifiedWater::BGSTerrainBlock_Attach::thunk(&block); }
	void Detach() { UnifiedWater::BGSTerrainBlock_Detach::thunk(&block); }
	void Ready()
	{
		cache.available = true;
		cache.instructions.push_back({ { &form } });
	}
	void CheckNative() const
	{
		assert(block.water == original.get() && original->parent == chunk.get());
		assert(original->children.size() == 1 && original->children.front().get() == nativeTile.get());
		assert(!uw.IsGeneratedWater(original.get()) && !uw.IsGeneratedWater(nativeTile.get()));
	}
	~Scene()
	{
		if (block.attached)
			Detach();
		assert(uw.reflectionCapture.sources == 0 && system.lock == 0);
		uw.gWaterLOD = nullptr;
		uw.waterCache = nullptr;
		globals::game::waterSystem = nullptr;
	}
};

static void TestFallbacks()
{
	for (int mode = 0; mode < 8; ++mode) {
		Scene s;
		if (mode == 1)
			s.uw.ready = false;
		if (mode >= 2)
			s.cache.available = true;
		if (mode == 3)
			s.cache.instructions.push_back({ { nullptr } });
		if (mode >= 4)
			s.Ready();
		if (mode == 4)
			RE::BSMultiBoundNode::cloneFails = true;
		if (mode == 5) {
			s.cache.instructions.push_back({ { &s.form } });
			RE::BSTriShape::clonesBeforeFailure = 2;
		}
		if (mode == 6)
			s.uw.gWaterLOD = nullptr;
		if (mode == 7)
			globals::game::waterSystem = nullptr;
		const int before = registrations;
		s.Attach();
		s.CheckNative();
		assert(registrations == before + 1 && s.uw.materialInitializations == 0);
		UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(&s.node, s.original.get());
		assert(nativeVisibility == mode + 1 && cullCalls == 0);
		s.Detach();
		s.CheckNative();
		assert(s.system.removals == 0);
		RE::BSMultiBoundNode::cloneFails = false;
		RE::BSTriShape::clonesBeforeFailure = -1;
	}
}

static void TestPreviouslyRegisteredWater()
{
	Scene s;
	s.Ready();
	s.chunk->DetachChild2(s.original.get());
	s.lod->AttachChild(s.original.get(), true);
	s.block.waterAttached = true;
	s.Attach();
	assert(s.original->parent == nullptr && s.system.removals == 2);
	assert(s.lod->children.size() == 1 && s.lod->children.front().get() == s.block.water);
	s.Detach();
	s.CheckNative();
	assert(s.lod->children.empty());
}

static void TestInitializationFailure()
{
	for (int successfulTiles : { 0, 1 }) {
		Scene s;
		s.Ready();
		s.cache.instructions.push_back({ { &s.form } });
		s.uw.materialsBeforeFailure = successfulTiles;
		const int before = registrations;
		s.Attach();
		s.CheckNative();
		assert(s.uw.materialInitializations == successfulTiles && registrations == before + 1);
		assert(s.system.removals == 0 && s.uw.reflectionCapture.sources == 0 && s.lod->children.empty());
	}
	Scene s;
	s.Ready();
	rejectNativeAttach = true;
	s.Attach();
	s.CheckNative();
	assert(!s.block.attached && !s.block.waterAttached && s.system.removals == 0 && s.lod->children.empty());
	rejectNativeAttach = false;
	s.Attach();
	assert(s.block.water != s.original.get());
}

static void TestVisibilityAndReparentedDetach()
{
	Scene s;
	s.Ready();
	s.uw.exterior = false;
	s.uw.UpdateWaterLODCull();
	s.Attach();
	assert(s.block.water->culled);
	const auto writes = s.block.water->cullWrites;
	const auto acquisitions = RE::BSSpinLockGuard::acquisitions;
	for (int frame = 0; frame < 120; ++frame) s.uw.UpdateWaterLODCull();
	assert(s.block.water->cullWrites == writes && RE::BSSpinLockGuard::acquisitions == acquisitions);
	s.uw.mapMenuOpen = true;
	s.uw.UpdateWaterLODCull();
	assert(!s.block.water->culled && s.block.water->cullWrites == writes + 1);
	RE::NiPointer<RE::BSMultiBoundNode> replacement{ s.block.water }, otherParent{ new RE::BSMultiBoundNode };
	s.lod->DetachChild2(replacement.get());
	otherParent->AttachChild(replacement.get(), true);
	s.Detach();
	assert(!replacement->parent && otherParent->children.empty() && !s.uw.IsGeneratedWater(replacement.get()));
	s.CheckNative();
}

static void TestLODRegistrationGate()
{
	Scene s;
	UnifiedWater::TESWaterSystem_AddLODWater::func = NativeAddLODWater;
	UnifiedWater::TESWaterSystem_AddLODWater::thunk(&s.system, s.nativeTile.get(), &s.world, s.lod.get(), s.original.get(), true);
	assert(nativeLODRegistrations == 1);
	s.Ready();
	s.Attach();
	auto water = s.block.water;
	UnifiedWater::TESWaterSystem_AddLODWater::thunk(&s.system, water->children.front().get(), &s.world, s.lod.get(), water, true);
	UnifiedWater::TESWaterSystem_AddLODWater::thunk(&s.system, water, &s.world, s.lod.get(), water, false);
	assert(nativeLODRegistrations == 1 && water->parent == s.lod.get());
	s.Detach();
	UnifiedWater::TESWaterSystem_AddLODWater::thunk(&s.system, s.nativeTile.get(), &s.world, s.lod.get(), s.original.get(), true);
	assert(nativeLODRegistrations == 2);
}

static void TestCachePublicationAndReload()
{
	Scene s;
	s.Attach();
	s.Ready();
	const int registered = registrations;
	s.Attach();
	s.CheckNative();  // Publication does not replace an already attached native block.
	assert(registrations == registered);
	s.Detach();
	s.Attach();
	assert(s.block.water != s.original.get() && s.uw.materialInitializations == 1);
	assert(registrations == registered && s.uw.IsGeneratedWater(s.block.water));
	assert(s.original->children.front().get() == s.nativeTile.get());
	assert(s.block.water->parent == s.lod.get() && s.original->parent == nullptr);
	UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::thunk(&s.node, s.block.water);
	assert(cullCalls == 1);
	// A native sibling keeps its own visibility while generated water is hidden.
	RE::NiPointer<RE::BSMultiBoundNode> sibling{ new RE::BSMultiBoundNode };
	s.lod->AttachChild(sibling.get(), true);
	s.uw.exterior = false;
	s.uw.UpdateWaterLODCull();
	assert(s.block.water->culled && !sibling->culled && !s.lod->culled);
	s.uw.mapMenuOpen = true;
	s.uw.UpdateWaterLODCull();
	assert(!s.block.water->culled);
	s.Detach();
	s.CheckNative();
	assert(s.lod->children.size() == 1 && s.system.removals == 3);
	s.cache.available = false;
	s.Attach();
	s.CheckNative();
	assert(registrations == registered + 1);
}

static void TestFlowmapPatchPreflight()
{
	REL::code.fill(0xCC);
	const std::array offsets{ REL::Relocate(0x1B7, 0x1F7), REL::Relocate(0x1EA, 0x22A), REL::Relocate(0x202, 0x242) };
	for (const auto i : offsets) REL::code[i] = 0xE8;
	REL::code[offsets.back()] = 0xCC;
	assert(!DisableVanillaFlowmapUpdates() && REL::writes == 0);
	REL::code[offsets.back()] = 0xE8;
	assert(DisableVanillaFlowmapUpdates() && REL::writes == 3);
	for (std::size_t i = 0; i < REL::code.size(); ++i) {
		const bool patched = std::ranges::any_of(offsets, [&](int offset) { return i >= static_cast<std::size_t>(offset) && i < static_cast<std::size_t>(offset + 5); });
		assert(REL::code[i] == (patched ? REL::NOP : 0xCC));
	}
	assert(DisableVanillaFlowmapUpdates() && REL::writes == 3);
}

int main(int argc, char**)
{
	REL::ae = argc > 1;
	TestFlowmapPatchPreflight();
	UnifiedWater::BGSTerrainBlock_Attach::func = NativeAttach;
	UnifiedWater::BGSTerrainBlock_Detach::func = NativeDetach;
	UnifiedWater::BGSTerrainNode_UpdateWaterMeshSubVisibility::func = NativeVisibility;
	const auto live = RE::NiObject::live;
	TestFallbacks();
	TestCachePublicationAndReload();
	TestPreviouslyRegisteredWater();
	TestInitializationFailure();
	TestVisibilityAndReparentedDetach();
	TestLODRegistrationGate();
	assert(RE::NiObject::live == live);
	std::cout << "Water LOD: native fallback, replacement, publication, reload, visibility and patch preflight passed\n";
}
