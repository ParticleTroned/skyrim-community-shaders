#ifdef NDEBUG
#	undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Utils/StringUtils.h"

namespace fs = std::filesystem;
constexpr int D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION = 16384;
enum D3D11_RESOURCE_DIMENSION
{
	D3D11_RESOURCE_DIMENSION_TEXTURE2D
};
struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width;
	uint32_t Height;
	uint32_t ArraySize = 1;
	struct
	{
		uint32_t Count = 1;
	} SampleDesc;
};
constexpr int D3D11_SRV_DIMENSION_TEXTURE2D = 4;
struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
	int ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	struct
	{
		uint32_t MostDetailedMip = 0;
	} Texture2D;
};
struct TestView
{
	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	void GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* out) { *out = desc; }
};
struct TestTexture
{
	D3D11_TEXTURE2D_DESC desc{ 128, 192 };
	void GetDesc(D3D11_TEXTURE2D_DESC* out) { *out = desc; }
	void GetType(D3D11_RESOURCE_DIMENSION* out) { *out = D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
};
using ID3D11Texture2D = TestTexture;
namespace globals::rtti
{
	struct TextureRTTI
	{
		const void* get() const { return this; }
	} NiSourceTextureRTTI;
}
namespace RE
{
	template <class T>
	struct NiPointer
	{
		T* value = nullptr;
		NiPointer() = default;
		NiPointer(T* ptr) : value(ptr) {}
		T* get() const { return value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
	struct NiTexture
	{
		const void* GetRTTI() const { return globals::rtti::NiSourceTextureRTTI.get(); }
	};
	struct NiSourceTexture : NiTexture
	{
		struct RendererTexture
		{
			TestTexture* texture;
			TestView* resourceView;
		};
		RendererTexture* rendererTexture;
	};
	struct BSTriShape
	{
		BSTriShape* AsTriShape() { return this; }
	};
	struct NiNode : BSTriShape
	{
		std::vector<NiPointer<NiNode>> children;
		NiNode* AsNode() { return this; }
		auto& GetChildren() { return children; }
	};
	namespace BSResource
	{
		enum class ErrorCode
		{
			kNone
		};
	}
	namespace BSModelDB
	{
		struct DBTraits
		{
			struct ArgsType
			{
				bool unk8;
				bool unkA;
				bool postProcess;
			};
		};
		inline BSResource::ErrorCode Demand(const char*, NiPointer<NiNode>& out, DBTraits::ArgsType)
		{
			static NiNode leaf;
			static NiNode inner{ {}, { &leaf } };
			static NiNode outer{ {}, { &inner } };
			out = &outer;
			return BSResource::ErrorCode::kNone;
		}
	}
	struct BSShaderManager
	{
		static void GetTexture(const char* path, bool, NiPointer<NiTexture>& out, bool);
	};
}
namespace logger
{
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

struct Fixture
{
	fs::path root = fs::temp_directory_path() / ("csx-flowmap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	TestTexture texture;
	TestView view;
	RE::NiSourceTexture::RendererTexture renderer{ &texture, &view };
	RE::NiSourceTexture source{ {}, &renderer };
	std::map<std::string, RE::NiSourceTexture*> textures;
	bool generationSucceeds = false;
	bool generatedTextureLoads = false;
	bool generatedFileVisible = true;
	bool loadOrderChanged = true;
	bool patchesApplied = false;
	bool patchSucceeds = true;
	bool cacheSucceeds = true;
	bool cacheStarts = true;
	RE::NiSourceTexture generatedSource{ {}, &renderer };
	int generationCalls = 0;
	int textureLoads = 0;
	int hashPublishes = 0;
	static inline Fixture* active;
	Fixture()
	{
		active = this;
		fs::create_directories(Directory());
	}
	~Fixture() { fs::remove_all(root); }
	fs::path Directory() const { return root / "textures" / "water" / "flowmaps"; }
	void Add(std::string name, bool visible = true)
	{
		textures[Util::ToLowerAscii(name)] = &source;
		if (visible) {
			std::ofstream output(Directory() / name);
			output << "simulated texture";
			assert(output.good());
		}
	}
};
namespace Util::PathHelpers
{
	fs::path GetDataPath() { return Fixture::active->root; }
}
void RE::BSShaderManager::GetTexture(const char* path, bool, NiPointer<NiTexture>& out, bool)
{
	auto& fixture = *Fixture::active;
	++fixture.textureLoads;
	const auto name = Util::ToLowerAscii(fs::path(path).filename().string());
	const auto found = fixture.textures.find(name);
	out = found == fixture.textures.end() ? nullptr : found->second;
}

#include "Features/UnifiedWater/Flowmap.h"

bool Flowmap::GenerateFlowmap(bool, fs::path& generatedPath)
{
	auto& fixture = *Fixture::active;
	++fixture.generationCalls;
	if (!fixture.generationSucceeds)
		return false;
	const auto name = std::format("Tamriel-Flowmap.2.3.-4.5.{:016X}.dds", fixture.generationCalls);
	generatedPath = fixture.Directory() / name;
	if (fixture.generatedTextureLoads) {
		fixture.Add(name, fixture.generatedFileVisible);
		fixture.textures[Util::ToLowerAscii(name)] = &fixture.generatedSource;
	}
	return true;
}

struct WaterCache
{
	bool RegenerateCaches(const auto& completion)
	{
		if (Fixture::active->cacheStarts)
			completion(Fixture::active->cacheSucceeds);
		return Fixture::active->cacheStarts;
	}
	void LoadOrGenerateCaches() {}
};
bool DisableVanillaWaterLOD()
{
	Fixture::active->patchesApplied = Fixture::active->patchSucceeds;
	return Fixture::active->patchSucceeds;
}
void LogMeshLoadFailure(RE::BSResource::ErrorCode, const char*) {}
bool PersistLoadOrderHash(uint64_t)
{
	++Fixture::active->hashPublishes;
	return true;
}
struct UnifiedWater
{
	RE::NiPointer<RE::BSTriShape> waterMesh;
	RE::NiPointer<RE::BSTriShape> optimisedWaterMesh;
	Flowmap* flowmap = nullptr;
	WaterCache* waterCache = nullptr;
	RE::NiPointer<RE::NiSourceTexture> boundTexture;
	RE::NiPointer<RE::NiSourceTexture>* gFlowMapSourceTex = &boundTexture;
	int32_t boundWidth = 0;
	int32_t* gFlowMapSize = &boundWidth;
	int32_t* gDisplacementMeshFlowCellOffset = &boundWidth;
	int32_t* gDisplacementCellTexCoordOffset = &boundWidth;
	int32_t* gDisplacementMeshPos = &boundWidth;
	std::string failedLoadedMessage;
	bool loaded = true;
	std::atomic_bool waterDataReady{ false };
	~UnifiedWater()
	{
		delete flowmap;
		delete waterCache;
	}
	static bool LoadOrderChanged(uint64_t& hash)
	{
		hash = 123;
		return Fixture::active->loadOrderChanged;
	}
	void DataLoaded();
	void SetFlowmapTex() const;
	bool IsWaterDataReady() const;
	bool RequiresVanillaWaterShaders() const;
};

#include "unified_water_flowmap_under_test.h"

void TestInvisibleGeneratedFile()
{
	Fixture fixture;
	fixture.generationSucceeds = true;
	fixture.generatedTextureLoads = true;
	fixture.generatedFileVisible = false;
	Flowmap map;
	assert(map.RegenerateAndLoadFlowmap());
	assert(fs::is_empty(fixture.Directory()));
	assert(map.IsValid() && map.GetWidth() == 2 && map.GetHeight() == 3);
	assert(map.GetOffsetX() == -4 && map.GetOffsetY() == 5);
}

void TestCaseInsensitiveDiscovery()
{
	Fixture fixture;
	fixture.Add("tamriel-flowmap.2.3.-4.5.DDS");
	fixture.Add("Tamriel-Flowmap.2.3.0.0.dds.tmp");
	Flowmap map;
	assert(map.LoadOrGenerateFlowmap());
	assert(map.IsValid() && fixture.generationCalls == 0);
	assert(map.GetWidth() == 2 && map.GetOffsetX() == -4);
}

void TestFailedReloadRetainsMap()
{
	Fixture fixture;
	fixture.Add("Tamriel-Flowmap.2.3.7.8.dds");
	Flowmap map;
	assert(map.LoadOrGenerateFlowmap());
	assert(!map.RegenerateAndLoadFlowmap());
	fixture.generationSucceeds = true;
	assert(!map.RegenerateAndLoadFlowmap());
	assert(map.IsValid() && map.GetWidth() == 2 && map.GetHeight() == 3);
	assert(map.GetOffsetX() == 7 && map.GetOffsetY() == 8);
	assert(map.GetInverseWidth() == 0.5f);
	assert(fs::exists(fixture.Directory() / "Tamriel-Flowmap.2.3.7.8.dds"));
	RE::NiPointer<RE::NiSourceTexture> texture;
	assert(map.TryGetFlowmap(texture) && texture.get() == &fixture.source);
}

void TestInvalidMetadataAndTexture()
{
	for (const auto* name : { "Tamriel-Flowmap.0.3.0.0.dds", "Tamriel-Flowmap.-2.3.0.0.dds", "Tamriel-Flowmap.257.3.0.0.dds", "Tamriel-Flowmap.999999999999.3.0.0.dds", "Tamriel-Flowmap.2.3.no.0.dds", "Tamriel-Flowmap.2.3.0.dds", "Tamriel-Flowmap.4.3.0.0.dds", "Tamriel-Flowmap.2.3.0.0..dds", "Tamriel-Flowmap.2.3.0.0.bad-id.dds" }) {
		Fixture fixture;
		fixture.Add(name);
		Flowmap map;
		assert(!map.LoadOrGenerateFlowmap());
		assert(!map.IsValid() && map.GetWidth() == 0 && map.GetHeight() == 0);
	}
	for (int defect = 0; defect < 4; ++defect) {
		Fixture fixture;
		fixture.Add("Tamriel-Flowmap.2.3.0.0.dds");
		switch (defect) {
		case 0:
			fixture.renderer.resourceView = nullptr;
			break;
		case 1:
			fixture.texture.desc.ArraySize = 2;
			break;
		case 2:
			fixture.texture.desc.SampleDesc.Count = 4;
			break;
		case 3:
			fixture.view.desc.ViewDimension = 9;
			break;
		}
		Flowmap map;
		assert(!map.LoadOrGenerateFlowmap() && !map.IsValid());
	}
}

void TestStartupFailureAndRecovery()
{
	Fixture fixture;
	fixture.generationSucceeds = true;
	UnifiedWater water;
	water.DataLoaded();
	assert(water.RequiresVanillaWaterShaders() && !water.IsWaterDataReady());
	assert(!fixture.patchesApplied && !water.flowmap && !water.waterCache);
	assert(!water.failedLoadedMessage.empty() && fixture.hashPublishes == 0);
	fixture.generatedTextureLoads = true;
	water.DataLoaded();
	assert(water.IsWaterDataReady() && !water.RequiresVanillaWaterShaders());
	assert(fixture.patchesApplied && fixture.hashPublishes == 1);
	assert(water.boundWidth == 2 && water.boundTexture.get() == &fixture.generatedSource);
	const int calls = fixture.generationCalls;
	water.DataLoaded();
	assert(fixture.generationCalls == calls);
}

void TestCachedStartup()
{
	Fixture fixture;
	fixture.loadOrderChanged = false;
	fixture.Add("tamriel-flowmap.2.3.0.0.dds");
	UnifiedWater water;
	water.DataLoaded();
	assert(water.IsWaterDataReady() && !water.RequiresVanillaWaterShaders());
	assert(fixture.patchesApplied && fixture.generationCalls == 0 && fixture.hashPublishes == 0);
}

void TestReducedResolutionMap()
{
	Fixture fixture;
	fixture.Add("Tamriel-Flowmap.2.3.0.0.dds");
	fixture.texture.desc.Width = 64;
	fixture.texture.desc.Height = 96;
	fixture.view.desc.Texture2D.MostDetailedMip = 1;
	Flowmap map;
	assert(map.LoadOrGenerateFlowmap() && map.IsValid());
	assert(map.GetWidth() == 2 && map.GetHeight() == 3);
}

void TestReplacementAndAmbiguousCaches()
{
	Fixture fixture;
	fixture.Add("Tamriel-Flowmap.2.3.-4.5.dds");
	Flowmap map;
	assert(map.LoadOrGenerateFlowmap());
	fixture.generationSucceeds = true;
	fixture.generatedTextureLoads = true;
	assert(map.RegenerateAndLoadFlowmap());
	RE::NiPointer<RE::NiSourceTexture> texture;
	assert(map.TryGetFlowmap(texture) && texture.get() == &fixture.generatedSource);
	assert(!fs::exists(fixture.Directory() / "Tamriel-Flowmap.2.3.-4.5.dds"));
	assert(std::distance(fs::directory_iterator(fixture.Directory()), fs::directory_iterator{}) == 1);
	fixture.Add("Tamriel-Flowmap.2.3.7.8.dds");
	const auto calls = fixture.generationCalls;
	Flowmap nextMap;
	assert(nextMap.LoadOrGenerateFlowmap());
	assert(fixture.generationCalls == calls + 1 && nextMap.GetOffsetX() == -4);
}

void TestReadinessAndLateFailures()
{
	Fixture fixture;
	fixture.generationSucceeds = true;
	fixture.generatedTextureLoads = true;
	UnifiedWater water;
	assert(water.RequiresVanillaWaterShaders() && !water.IsWaterDataReady());
	water.loaded = false;
	assert(!water.RequiresVanillaWaterShaders());
	water.loaded = true;
	water.gDisplacementMeshPos = nullptr;
	water.DataLoaded();
	assert(water.RequiresVanillaWaterShaders() && !water.IsWaterDataReady());
	assert(!fixture.patchesApplied && !water.flowmap && !water.waterCache);
	water.gDisplacementMeshPos = &water.boundWidth;
	fixture.patchSucceeds = false;
	water.DataLoaded();
	assert(water.RequiresVanillaWaterShaders() && !water.IsWaterDataReady());
	assert(!fixture.patchesApplied && !water.boundTexture && fixture.hashPublishes == 0);
}

void TestCacheFailureRetainsHash()
{
	for (bool starts : { false, true }) {
		Fixture fixture;
		fixture.generationSucceeds = true;
		fixture.generatedTextureLoads = true;
		fixture.cacheStarts = starts;
		fixture.cacheSucceeds = false;
		UnifiedWater water;
		water.DataLoaded();
		assert(water.IsWaterDataReady() && fixture.hashPublishes == 0);
	}
}

int main()
{
	TestInvisibleGeneratedFile();
	TestCaseInsensitiveDiscovery();
	TestFailedReloadRetainsMap();
	TestInvalidMetadataAndTexture();
	TestStartupFailureAndRecovery();
	TestCachedStartup();
	TestReducedResolutionMap();
	TestReplacementAndAmbiguousCaches();
	TestReadinessAndLateFailures();
	TestCacheFailureRetainsHash();
	std::cout << "Unified Water flowmap: 10 regression scenarios passed\n";
}
