#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Utils/ResidentShaderCache.h"
#include "Utils/ShaderLifetime.h"

struct ID3D11Device
{};
struct ID3D11PixelShader
{};
struct ID3D11VertexShader
{};
namespace RE
{
	namespace BSGraphics
	{
		struct Renderer
		{};
		enum
		{
			DIRTY_VERTEX_DESC
		};
		struct VertexShader
		{
			ID3D11VertexShader* shader = nullptr;
		};
		struct PixelShader
		{
			ID3D11PixelShader* shader = nullptr;
			uint32_t id = 0;
			struct ConstantBuffer
			{
				void* buffer = nullptr;
			};
			std::array<ConstantBuffer, 3> constantBuffers{};
			std::array<int, 3> constantTable{ 0, 1, 4 };
		};
	}
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Grass,
			DistantTree,
			Sky,
			Effect,
			ImageSpace,
			Total
		};
		struct TypeValue
		{
			Type value = Type::Lighting;
			Type get() const { return value; }
			bool operator==(Type other) const { return value == other; }
			unsigned underlying() const { return static_cast<unsigned>(value); }
		} shaderType;
		const char* fxpFilename = "Lighting";
	};
}
namespace REL
{
	struct Module
	{
		static Module get() { return {}; }
		uintptr_t base() const { return 0; }
	};
	template <class T>
	struct Relocation
	{};
}
namespace logger
{
	unsigned warnings = 0;
	template <class... T>
	void debug(const char*, T&&...)
	{}
	template <class... T>
	void warn(const char*, T&&...)
	{
		++warnings;
	}
}
struct TestState
{
	bool developer = false, enablePShaders = true, settingCustomShader = false;
	bool deferredPixelShaderMissing = false;
	RE::BSShader* currentShader = nullptr;
	uint32_t modifiedPixelDescriptor = 0;
	uint32_t currentVertexDescriptor = 0, currentPixelDescriptor = 0, modifiedVertexDescriptor = 0;
	uint32_t lastModifiedVertexDescriptor = 0, lastModifiedPixelDescriptor = 0, frameCount = 0;
	bool updateShader = false;
	struct
	{
		uint32_t VertexShaderDescriptor = 0, PixelShaderDescriptor = 0;
	} permutationData;
	void ModifyShaderLookup(const RE::BSShader&, uint32_t&, uint32_t&) {}
	bool IsDeveloperMode() const { return developer; }
	bool IsShaderEnabled(const RE::BSShader&) const { return true; }
	bool ShaderEnabled(RE::BSShader::Type) const { return true; }
};
struct TestDeferred
{
	bool deferredPass = true, categories = true, captureReady = true;
	unsigned invalidations = 0;
	bool IsCharacterCategoryEnabled() const { return categories; }
	void InvalidateCharacterCategoryCapture()
	{
		captureReady = false;
		++invalidations;
	}
};
struct TestContext
{
	ID3D11PixelShader* bound = nullptr;
	unsigned binds = 0;
	ID3D11VertexShader* vertex = nullptr;
	void VSSetShader(ID3D11VertexShader* shader, void*, unsigned) { vertex = shader; }
	void PSSetShader(ID3D11PixelShader* shader, void*, unsigned)
	{
		bound = shader;
		++binds;
	}
};
namespace SIE
{
	struct ShaderCache;
}
namespace globals
{
	TestState* state;
	TestDeferred* deferred;
	SIE::ShaderCache* shaderCache;
	namespace game
	{
		bool isVR = true;
		RE::BSGraphics::PixelShader** currentPixelShader;
		RE::BSGraphics::VertexShader* vertex = nullptr;
		RE::BSGraphics::VertexShader** currentVertexShader = &vertex;
		struct Flags
		{
			void set(int) {}
		} flags;
		auto* stateUpdateFlags = &flags;
	}
	namespace d3d
	{
		TestContext* context;
		ID3D11Device* device = nullptr;
	}
	namespace features
	{
		struct Upscaling
		{
			bool IsNeuralRenderingEnabled() const { return false; }
			bool NeedsCharacterCategoryShaders() const { return globals::deferred->categories; }
		} upscaling;
		struct TerrainBlending
		{
			void OnBeginTechnique(RE::BSShader*, uint32_t, uint32_t) {}
		} terrainBlending;
	}
}
namespace NeuralRendering::Color
{
	struct ExposureCapture
	{
		static ExposureCapture& Instance()
		{
			static ExposureCapture value;
			return value;
		}
		template <class... T>
		void ObservePixelShaderSelection(T&&...)
		{}
	};
}
bool UseNativeWaterShaders(const RE::BSShader&) { return false; }
namespace SIE
{
	enum class ShaderClass
	{
		Pixel
	};
	namespace SShaderCache
	{
		bool ResolveImageSpaceDescriptor(const RE::BSShader&, uint32_t&) { return true; }
		std::string GetShaderString(ShaderClass, const RE::BSShader&, uint32_t descriptor, bool)
		{
			return std::to_string(descriptor);
		}
	}
	struct ShaderCache
	{
#include "category_flag.h"
#include "lighting_flags.h"
		std::array<std::unordered_map<uint32_t, std::shared_ptr<RE::BSGraphics::PixelShader>>,
			static_cast<unsigned>(RE::BSShader::Type::Total)>
			pixelShaders;
		std::mutex pixelShadersMutex;
		std::atomic<uint64_t> pixelShaderEvictionRevision{ 0 };
		const std::shared_ptr<const uint8_t> residentShaderCacheOwner = std::make_shared<const uint8_t>();
		std::atomic<unsigned> activeShaderCaptureFramesRemaining{ 0 };
		int blockedKeyIndex = -1;
		std::string blockedKey;
		std::vector<uint32_t> blockedIDs;
		bool safeLoad = false, async = true, compilationSucceeds = true;
		unsigned compiles = 0, trackedLookups = 0;
		std::shared_ptr<RE::BSGraphics::VertexShader> vertex;
		auto GetVertexShader(const RE::BSShader&, uint32_t) { return vertex; }
		struct Task
		{
			ShaderClass stage;
			const RE::BSShader& shader;
			uint32_t descriptor;
		};
		struct Queue
		{
			std::set<uint32_t> requests;
			void Add(Task task) { requests.insert(task.descriptor); }
		} compilationSet;
		static bool IsSupportedShader(const RE::BSShader&) { return true; }
		bool IsEnabled() const { return true; }
		bool IsSaveLoadSafeModeActive() const { return safeLoad; }
		bool ShouldUseAsyncCompilation() const { return async; }
		void TrackActiveShader(ShaderClass, const RE::BSShader&, uint32_t) { ++trackedLookups; }
		RE::BSGraphics::PixelShader* Add(const RE::BSShader& shader, uint32_t descriptor)
		{
			auto& typeCache = pixelShaders[shader.shaderType.underlying()];
			auto [found, inserted] = typeCache.try_emplace(descriptor);
			if (!inserted)
				return found->second.get();
			auto& entry = found->second;
			entry = std::make_shared<RE::BSGraphics::PixelShader>();
			entry->id = descriptor;
			entry->constantBuffers[1].buffer = entry.get();
			entry->shader = reinterpret_cast<ID3D11PixelShader*>(entry.get());
			return entry.get();
		}
		std::shared_ptr<RE::BSGraphics::PixelShader> MakeAndAddPixelShader(const RE::BSShader& shader, uint32_t descriptor)
		{
			++compiles;
			if (!compilationSucceeds)
				return nullptr;
			Add(shader, descriptor);
			return pixelShaders[shader.shaderType.underlying()].at(descriptor);
		}
		void ClearPixelShaders(size_t type)
		{
			std::lock_guard lock(pixelShadersMutex);
			pixelShaderEvictionRevision.fetch_add(1, std::memory_order_release);
			pixelShaders[type].clear();
		}
		void PrewarmDeferredPixelShaders(const RE::BSShader&, uint32_t, bool);
		std::shared_ptr<RE::BSGraphics::PixelShader> GetPixelShader(const RE::BSShader&, uint32_t, PixelShaderLookup = PixelShaderLookup::Compile);
	};
#include "pixel_lookup.h"
#include "pixel_prewarm.h"
}
namespace Permutations
{
	std::vector<uint32_t> pixelPermutations;
	const auto& GeneratePBRLightingPixelPermutations() { return pixelPermutations; }
}
struct TruePBR
{
	void GenerateShaderPermutations(RE::BSShader*);
};
#include "pbr_prewarm.h"
enum class CSFrameHookPhase
{
	BeginTechniqueTerrainOnBegin,
	BeginTechniqueModifyShaderLookup,
	BeginTechniqueOriginal,
	BeginTechniqueShaderCacheLookup,
	BeginTechniqueBindCustomShader,
	BeginTechniqueTotal
};
bool ShouldRecordCSFramePhaseDiag() { return false; }
uint64_t ReadFrameDiagCounterTicks() { return 0; }
void RecordCSFrameHookPhase(CSFrameHookPhase, uint32_t, uint64_t) {}
namespace
{
	Util::ShaderLifetime::RetainedBinding<RE::BSGraphics::VertexShader> g_retainedVertexShader;
	Util::ShaderLifetime::RetainedBinding<RE::BSGraphics::PixelShader> g_retainedPixelShader;
}
namespace Hooks
{
#include "pixel_binding.h"
	struct BSShader_BeginTechnique
	{
		static bool thunk(RE::BSShader*, uint32_t, uint32_t, bool);
		static inline bool (*func)(RE::BSShader*, uint32_t, uint32_t, bool) = nullptr;
	};
}
#include "begin_technique.h"
namespace NeuralRendering
{
#if defined(DEVBENCH_BRIDGE_ENABLED)
	constexpr bool kDevelopmentDiagnostics = true;
#else
	constexpr bool kDevelopmentDiagnostics = false;
#endif
#include "diagnostic_increment.h"
#include "preparation_failure_type.h"
	struct CharacterMaskPrepareArgs
	{
		uint64_t generation = 1;
		uint32_t frameId = 1, sourceWorldFrame = 1, featureSlot = 0, eyeIndex = 0;
		uint32_t settings = 14;
	};
	uint32_t GetEnabledCharacterCategoryMask(uint32_t settings) { return settings; }
	struct PreparationState
	{
		uint32_t capturedFrame_ = UINT32_MAX, capturedEnabledCategoryMask_ = 0;
		struct Snapshot
		{
			uint64_t preparationFailures = 0;
			CharacterPreparationFailure lastPreparationFailure;
		} snapshot_;
#include "preparation_failure.h"
	};
}
unsigned assertions = 0;
void Require(bool condition, const std::source_location& location = std::source_location::current())
{
	++assertions;
	if (!condition)
		throw std::runtime_error("Category fallback check failed at line " + std::to_string(location.line()));
}
struct Fixture
{
	TestState state;
	TestDeferred deferred;
	TestContext context;
	SIE::ShaderCache cache;
	RE::BSShader shader;
	RE::BSGraphics::PixelShader native;
	RE::BSGraphics::PixelShader* current = nullptr;
	Fixture()
	{
		globals::state = &state;
		globals::deferred = &deferred;
		globals::shaderCache = &cache;
		globals::d3d::context = &context;
		globals::game::currentPixelShader = &current;
		state.currentShader = &shader;
		native.shader = reinterpret_cast<ID3D11PixelShader*>(&native);
	}
	void Bind(uint32_t descriptor)
	{
		state.deferredPixelShaderMissing = false;
		state.modifiedPixelDescriptor = descriptor;
		Hooks::BSShader__BeginTechnique_SetPixelShader::thunk(nullptr, &native);
	}
};
void TestLayoutTransitions(RE::BSShader::Type type, bool categories)
{
	Fixture f;
	f.shader.shaderType.value = type;
	f.deferred.categories = categories;
	const uint32_t flag = SIE::ShaderCache::GetCharacterCategoryFlag(type);
	const uint32_t requested = 0x10001u | (categories ? flag : 0u);
	const uint32_t previous = requested ^ flag;
	auto* fallback = f.cache.Add(f.shader, previous);
	f.Bind(requested);
	Require(f.context.bound == fallback->shader && f.current == &f.native);
	Require(!f.state.deferredPixelShaderMissing && !f.deferred.captureReady);
	Require(f.cache.compilationSet.requests == std::set<uint32_t>{ requested });
	Require(f.cache.compiles == 0);
	auto* ready = f.cache.Add(f.shader, requested);
	f.deferred.captureReady = true;
	const auto invalidations = f.deferred.invalidations;
	for (int frame = 0; frame < 100; ++frame) f.Bind(requested);
	Require(f.context.bound == ready->shader && f.deferred.captureReady);
	Require(f.deferred.invalidations == invalidations && f.cache.compiles == 0);
}
void TestColdAndFailedVariants()
{
	Fixture f;
	const uint32_t requested = 0x10021u, plain = 0x10001u;
	f.Bind(requested);
	Require(f.state.deferredPixelShaderMissing && f.context.binds == 0 && f.current == &f.native);
	Require(f.cache.compilationSet.requests == std::set<uint32_t>{ requested });
	auto* fallback = f.cache.Add(f.shader, plain);
	Require(f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw).get() == fallback);
	Require(!f.state.deferredPixelShaderMissing);
	f.Bind(requested);
	Require(!f.state.deferredPixelShaderMissing && f.context.bound == fallback->shader);
	f.cache.async = false;
	f.cache.compilationSucceeds = false;
	f.Bind(requested);
	Require(!f.state.deferredPixelShaderMissing && f.context.bound == fallback->shader);
	Require(f.cache.compiles == 1);
	f.cache.ClearPixelShaders(0);
	f.context.binds = 0;
	f.Bind(requested);
	Require(f.state.deferredPixelShaderMissing && f.context.binds == 0);
}
void TestExactAndProtectedLookups()
{
	Fixture f;
	constexpr uint32_t requested = 0x10021u, plain = 0x10001u;
	auto* fallback = f.cache.Add(f.shader, plain);
	Require(!f.cache.GetPixelShader(f.shader, requested));
	Require(f.deferred.captureReady && !f.state.deferredPixelShaderMissing);
	f.cache.safeLoad = true;
	f.cache.compilationSet.requests.clear();
	f.Bind(requested);
	Require(f.context.bound == fallback->shader && f.cache.compilationSet.requests.empty());
	f.deferred.deferredPass = false;
	f.Bind(requested);
	Require(f.context.bound == f.native.shader);
	f.deferred.deferredPass = true;
	f.state.enablePShaders = false;
	f.Bind(requested);
	Require(f.context.bound == f.native.shader);
	f.state.enablePShaders = true;
	f.state.developer = true;
	f.cache.blockedKeyIndex = 0;
	f.cache.blockedKey = std::to_string(requested);
	f.Bind(requested);
	Require(f.context.bound == f.native.shader);
	f.cache.blockedKeyIndex = -1;
	f.state.developer = false;
	f.shader.shaderType.value = RE::BSShader::Type::ImageSpace;
	f.Bind(requested);
	Require(f.context.bound == f.native.shader && !f.state.deferredPixelShaderMissing);
}
void TestPositiveSelectionTransitions()
{
	Fixture f;
	constexpr uint32_t requested = 0x10021u;
	auto* ready = f.cache.Add(f.shader, requested);
	auto retained = f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw);
	std::weak_ptr<RE::BSGraphics::PixelShader> retired = retained;
	Require(retained.get() == ready);
	f.cache.activeShaderCaptureFramesRemaining = 1;
	f.state.deferredPixelShaderMissing = true;
	Require(f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw) == retained);
	Require(f.cache.trackedLookups == 1 && !f.state.deferredPixelShaderMissing);
	f.state.enablePShaders = false;
	Require(!f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw));
	f.state.enablePShaders = true;
	f.state.developer = true;
	f.cache.blockedKeyIndex = 0;
	f.cache.blockedKey = std::to_string(requested);
	Require(!f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw));
	Require(f.cache.blockedIDs == std::vector<uint32_t>{ requested });
	f.cache.blockedKeyIndex = -1;
	f.state.developer = false;
	Require(f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw) == retained);
	const auto publishedRevision = f.cache.pixelShaderEvictionRevision.load();
	Require(f.cache.Add(f.shader, requested) == ready);
	Require(f.cache.pixelShaderEvictionRevision.load() == publishedRevision);
	f.cache.ClearPixelShaders(0);
	Require(f.cache.pixelShaderEvictionRevision.load() > publishedRevision);
	Require(retained->shader == ready->shader && retained->id == requested);
	auto* replacement = f.cache.Add(f.shader, requested);
	Require(replacement != ready);
	Require(f.cache.GetPixelShader(f.shader, requested, SIE::ShaderCache::PixelShaderLookup::Draw).get() == replacement);
	Require(retained.get() == ready && retained->id == requested && !retired.expired());
	retained.reset();
	Require(retired.expired());
}
void TestUnusedCategoriesStayCold()
{
	Fixture f;
	f.deferred.categories = false;
	f.Bind(0x10001u);
	Require(f.state.deferredPixelShaderMissing && f.context.binds == 0);
	Require(f.cache.compilationSet.requests == std::set<uint32_t>{ 0x10001u });
	f.cache.compilationSet.requests.clear();
	Require(!f.cache.GetPixelShader(f.shader, 0x10021u, SIE::ShaderCache::PixelShaderLookup::Cached));
	Require(f.cache.compilationSet.requests.empty());
}
namespace ShaderConstants::GrassPS
{
	struct Constants
	{
		unsigned PBRFlags = 0, PBRParams1 = 1, PBRParams2 = 2;
	};
	Constants Get() { return {}; }
}
std::atomic<unsigned> grassShaderFallbackSetups{ 0 };
unsigned materialSetups = 0;
void SetupGrassMaterialConstants(RE::BSShader* shader)
{
	[[maybe_unused]] const bool diagnostics = false;
#include "grass_material_admission.h"
	++materialSetups;
}
void TestBoundGrassVariant()
{
	Fixture f;
	f.shader.shaderType.value = RE::BSShader::Type::Grass;
	const uint32_t plain = 0x10001u;
	const auto requested = plain | SIE::ShaderCache::GetCharacterCategoryFlag(RE::BSShader::Type::Grass);
	auto* fallback = f.cache.Add(f.shader, plain);
	f.current = fallback;
	f.state.modifiedPixelDescriptor = requested;
	materialSetups = 0;
	SetupGrassMaterialConstants(&f.shader);
	Require(materialSetups == 1 && f.cache.compilationSet.requests.empty());
	f.cache.Add(f.shader, requested);
	SetupGrassMaterialConstants(&f.shader);
	Require(materialSetups == 2);
	f.native.id = plain;
	f.current = &f.native;
	SetupGrassMaterialConstants(&f.shader);
	Require(materialSetups == 2);
	f.current = nullptr;
	SetupGrassMaterialConstants(&f.shader);
	Require(materialSetups == 2);
	f.current = fallback;
	fallback->constantTable[0] = 99;
	SetupGrassMaterialConstants(&f.shader);
	Require(materialSetups == 2);
}
bool nativeTechniqueReady = true;
RE::BSGraphics::PixelShader* nativeTechniquePixel = nullptr;
bool NativeTechnique(RE::BSShader*, uint32_t, uint32_t, bool skipPixel)
{
	if (nativeTechniqueReady)
		Hooks::BSShader__BeginTechnique_SetPixelShader::thunk(nullptr, skipPixel ? nullptr : nativeTechniquePixel);
	return nativeTechniqueReady;
}
void TestTechniqueAdmission()
{
	Fixture f;
	constexpr uint32_t requested = 0x10021u, plain = 0x10001u;
	Hooks::BSShader_BeginTechnique::func = NativeTechnique;
	nativeTechniquePixel = &f.native;
	nativeTechniqueReady = true;
	// Native shader availability must not admit a draw with incompatible outputs.
	Require(!Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, false));
	Require(f.context.binds == 0 && f.state.deferredPixelShaderMissing);
	// A new depth-only technique must not inherit the rejected pixel stage.
	Require(Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, true));
	Require(!f.state.deferredPixelShaderMissing && f.context.binds == 1 && f.context.bound == nullptr);
	auto* fallback = f.cache.Add(f.shader, plain);
	Require(Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, false));
	Require(f.context.bound == fallback->shader);
	nativeTechniqueReady = false;
	Require(!Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, false));
	ID3D11VertexShader nativeVS;
	f.cache.vertex = std::make_shared<RE::BSGraphics::VertexShader>();
	f.cache.vertex->shader = &nativeVS;
	Require(Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, false));
	Require(f.current == fallback && f.context.vertex == &nativeVS && !f.state.settingCustomShader);
	// Depth-only fallback requires a vertex stage but never a pixel variant.
	f.cache.ClearPixelShaders(0);
	f.cache.compilationSet.requests.clear();
	Require(Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, true));
	Require(f.current == nullptr && !f.state.deferredPixelShaderMissing);
	Require(f.context.bound == nullptr && f.cache.compilationSet.requests.empty());
	// Effect techniques retain their native admission rules.
	f.shader.shaderType.value = RE::BSShader::Type::Effect;
	Require(!Hooks::BSShader_BeginTechnique::thunk(&f.shader, plain, requested, false));
}
void TestWarningDeduplication()
{
	NeuralRendering::PreparationState state;
	NeuralRendering::CharacterMaskPrepareArgs args;
	logger::warnings = 0;
	for (unsigned frame = 0; frame < 2000; ++frame) {
		args.frameId = frame;
		args.eyeIndex = frame & 1;
		state.RecordPreparationFailure(args, "category capture pending");
	}
	Require(logger::warnings == 1);
	Require(state.snapshot_.lastPreparationFailure.frame == 1999);
	Require(state.snapshot_.lastPreparationFailure.sequence ==
			(NeuralRendering::kDevelopmentDiagnostics ? 2000u : 0u));
	state.RecordPreparationFailure(args, "device changed");
	state.RecordPreparationFailure(args, "device changed");
	Require(logger::warnings == 2);
	state.RecordPreparationFailure(args, "category capture pending");
	Require(logger::warnings == 3);
}
void TestNullPixelBinding()
{
	for (bool cached : { false, true }) {
		Fixture f;
		f.context.bound = f.native.shader;
		f.current = &f.native;
		f.state.modifiedPixelDescriptor = 0x10021u;
		if (cached)
			f.cache.Add(f.shader, f.state.modifiedPixelDescriptor);
		Hooks::BSShader__BeginTechnique_SetPixelShader::thunk(nullptr, nullptr);
		Require(f.context.bound == nullptr && f.current == nullptr);
		Require(!f.state.deferredPixelShaderMissing && f.deferred.captureReady);
		Require(f.cache.compilationSet.requests.empty());
	}
}
void TestPBRPrewarming()
{
	constexpr uint32_t forward = 0x10009u;
	constexpr uint32_t deferred = forward | static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Deferred);
	const auto flag = SIE::ShaderCache::GetCharacterCategoryFlag(RE::BSShader::Type::Lighting);
	Permutations::pixelPermutations = { forward, deferred };
	for (bool categories : { false, true }) {
		Fixture f;
		f.deferred.categories = categories;
		TruePBR feature;
		feature.GenerateShaderPermutations(&f.shader);
		std::set<uint32_t> expected{ forward, deferred };
		if (categories)
			expected.insert(deferred | flag);
		Require(f.cache.compilationSet.requests == expected);
		Require(f.deferred.captureReady && !f.state.deferredPixelShaderMissing);
		f.cache.compilationSet.requests.clear();
		f.shader.shaderType.value = RE::BSShader::Type::Effect;
		feature.GenerateShaderPermutations(&f.shader);
		Require(f.cache.compilationSet.requests.empty());
	}
}
int main()
{
	try {
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			for (auto type : { RE::BSShader::Type::Lighting, RE::BSShader::Type::Grass,
					 RE::BSShader::Type::DistantTree, RE::BSShader::Type::Sky, RE::BSShader::Type::Effect })
				for (bool categories : { false, true }) TestLayoutTransitions(type, categories);
			TestColdAndFailedVariants();
			TestExactAndProtectedLookups();
			TestPositiveSelectionTransitions();
			TestUnusedCategoriesStayCold();
			TestBoundGrassVariant();
			TestNullPixelBinding();
			TestTechniqueAdmission();
			TestPBRPrewarming();
		}
		TestWarningDeduplication();
		std::cout << "Category fallback and warning checks: " << assertions << " passed (flat and VR)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
