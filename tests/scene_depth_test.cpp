#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

// Engine and D3D boundaries are fakes; routing, copy publication and hooks below
// are extracted from production so regressions exercise the implemented policy.
struct ID3D11ShaderResourceView
{};
struct Texture
{
	int content = 0;
};
struct View
{
	ID3D11ShaderResourceView* value = nullptr;
	ID3D11ShaderResourceView* get() const { return value; }
};
struct BlendedTexture
{
	View srv;
};
struct DepthStencil
{
	Texture* texture = nullptr;
	ID3D11ShaderResourceView* depthSRV = nullptr;
};
namespace RE
{
	enum RENDER_TARGETS_DEPTHSTENCIL
	{
		kMAIN,
		kPOST_ZPREPASS_COPY
	};
	struct BSBatchRenderer
	{};
	struct BSShaderAccumulator
	{};
	namespace BSGraphics::ShaderFlags
	{
		constexpr int DIRTY_RENDERTARGET = 1;
	}
}
struct Renderer
{
	std::array<DepthStencil, 2> depthStencils;
	Renderer& GetDepthStencilData() { return *this; }
};
struct Context
{
	int copies = 0;
	int unbinds = 0;
	void CopyResource(Texture* destination, Texture* source)
	{
		++copies;
		*destination = *source;
	}
	void OMSetRenderTargets(int, void*, void*) { ++unbinds; }
};
struct State
{
	uint32_t frameCount = 1;
	bool inWorld = true;
	void UpdateSharedData(bool, bool) {}
};
struct ShaderCache
{
	bool enabled = true;
	bool IsEnabled() const { return enabled; }
};
struct TerrainBlending
{
	bool loaded = true;
	struct Settings
	{
		bool Enabled = true;
	} settings;
	BlendedTexture* blendedDepthTexture = nullptr;
	BlendedTexture* blendedDepthTexture16 = nullptr;
	ID3D11ShaderResourceView* depthSRVBackup = nullptr;
	ID3D11ShaderResourceView* prepassSRVBackup = nullptr;
	std::function<void()> render = [] {};
	void RenderTerrainBlendingPasses() { render(); }
};
struct Flags
{
	void set(int) {}
};
class Deferred
{
public:
	bool IsSceneDepthFinal() const;
	bool CopySceneDepth();
	void EarlyPrepasses();
	void CopyShadowLightData() {}
	void StartDeferred() { deferredPass = true; }
	void EndDeferred() { end(); }
	bool deferredPass = false;
	std::function<void()> end = [] {};
	struct Hooks
	{
		struct Main_RenderWorld_Start
		{
			static void thunk(RE::BSBatchRenderer*, uint32_t, uint32_t, uint32_t, int);
			static inline std::function<void(RE::BSBatchRenderer*, uint32_t, uint32_t, uint32_t, int)> func;
		};
		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator*, uint32_t);
			static inline std::function<void(RE::BSShaderAccumulator*, uint32_t)> func;
		};
	};

private:
	std::optional<uint32_t> finalSceneDepthFrame;
};
namespace globals
{
	inline State* state;
	inline ShaderCache* shaderCache;
	inline Deferred* deferred;
	namespace game
	{
		inline Renderer* renderer;
		inline Flags* stateUpdateFlags;
	}
	namespace d3d
	{
		inline Context* context;
	}
	namespace features
	{
		inline TerrainBlending terrainBlending;
	}
}
struct Feature
{
	void EarlyPrepass() {}
	template <class Callback>
	static void ForEachLoadedFeature(const char*, Callback, bool)
	{}
};
namespace logger
{
	template <class... Args>
	void info(const char*, Args...)
	{}
}
#define CS_GPU_PASS(...)
#include "scene_depth_under_test.h"

void Check(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

struct Fixture
{
	Texture main{ 42 }, copy{ 7 };
	ID3D11ShaderResourceView mainSrv, copySrv, blendSrv, blend16Srv, alternateSrv;
	BlendedTexture blend{ { &blendSrv } }, blend16{ { &blend16Srv } };
	Renderer renderer{ { DepthStencil{ &main, &mainSrv }, DepthStencil{ &copy, &copySrv } } };
	Context context;
	State state;
	ShaderCache shaderCache;
	Deferred deferred;
	Flags flags;
	Fixture()
	{
		globals::state = &state;
		globals::shaderCache = &shaderCache;
		globals::deferred = &deferred;
		globals::game::renderer = &renderer;
		globals::game::stateUpdateFlags = &flags;
		globals::d3d::context = &context;
		globals::features::terrainBlending = {};
		auto& terrain = globals::features::terrainBlending;
		terrain.blendedDepthTexture = &blend;
		terrain.blendedDepthTexture16 = &blend16;
		terrain.depthSRVBackup = &mainSrv;
		terrain.prepassSRVBackup = &copySrv;
		Deferred::Hooks::Main_RenderWorld_Start::func = [](auto...) {};
		Deferred::Hooks::Main_RenderWorld_BlendedDecals::func = [](auto...) {};
	}
	void Start() { Deferred::Hooks::Main_RenderWorld_Start::thunk(nullptr, 0, 0, 0, 0); }
	void Decals() { Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk(nullptr, 0); }
};

void TestSources()
{
	Fixture f;
	Check(!f.deferred.IsSceneDepthFinal(), "A new frame must start with prepass depth");
	Check(Util::GetCurrentSceneDepthSRV(false) == &f.blendSrv, "Compute prepass depth must retain terrain blending");
	Check(Util::GetCurrentSceneDepthSRV(true) == &f.blend16Srv, "Pixel prepass depth must retain 16-bit selection");
	auto& terrain = globals::features::terrainBlending;
	terrain.blendedDepthTexture16 = nullptr;
	Check(Util::GetCurrentSceneDepthSRV(true) == &f.copySrv, "Missing blended depth must fall back");
	terrain.settings.Enabled = false;
	Check(Util::GetCurrentSceneDepthSRV(false) == &f.copySrv, "Disabled terrain must use native depth");
	terrain.settings.Enabled = true;
	terrain.loaded = false;
	Check(Util::GetCurrentSceneDepthSRV(false) == &f.copySrv, "Unloaded terrain must use native depth");
	terrain.loaded = true;
	Check(f.deferred.CopySceneDepth(), "Completed opaque depth must be copied");
	Check(f.copy.content == 42, "The published texture must contain current world geometry");
	Check(Util::GetCurrentSceneDepthSRV(false) == &f.copySrv && Util::GetCurrentSceneDepthSRV(true) == &f.copySrv,
		"Final depth must override both blended formats");
	Check(f.context.unbinds == 0, "Copying must preserve existing output bindings");
}

void TestInvalidation()
{
	Fixture f;
	Check(f.deferred.CopySceneDepth(), "Initial copy failed");
	++f.state.frameCount;
	Check(!f.deferred.IsSceneDepthFinal() && Util::GetCurrentSceneDepthSRV(false) == &f.blendSrv,
		"A previous frame's final depth must not reach early consumers");
	f.state.frameCount = std::numeric_limits<uint32_t>::max();
	Check(f.deferred.CopySceneDepth(), "Rollover copy failed");
	++f.state.frameCount;
	Check(!f.deferred.IsSceneDepthFinal(), "Frame counter rollover must invalidate final depth");
	Check(f.deferred.CopySceneDepth(), "Same-frame copy failed");
	f.shaderCache.enabled = false;
	f.deferred.EarlyPrepasses();
	Check(!f.deferred.IsSceneDepthFinal(), "Disabled early prepasses must still invalidate depth");
	Check(f.deferred.CopySceneDepth(), "Repeated world copy failed");
	f.Start();
	Check(!f.deferred.IsSceneDepthFinal(), "A disabled repeated world pass must invalidate depth in the same frame");
	Check(f.deferred.CopySceneDepth(), "Outside-world copy failed");
	f.state.inWorld = false;
	f.Start();
	Check(!f.deferred.IsSceneDepthFinal(), "A skipped outside-world pass must invalidate depth");
}

void TestFallbacks()
{
	for (int mode = 0; mode < 5; ++mode) {
		Fixture f;
		if (mode == 1)
			f.shaderCache.enabled = false;
		if (mode == 3)
			f.state.inWorld = false;
		f.Start();
		globals::features::terrainBlending.render = [&] { f.main.content = 60; };
		Deferred::Hooks::Main_RenderWorld_BlendedDecals::func = [&](auto...) { f.main.content += 1; };
		if (mode == 0)
			f.deferred.end = [&] { Check(f.deferred.CopySceneDepth(), "Early depth copy failed"); };
		else if (mode == 2)
			f.deferred.deferredPass = false;
		else if (mode == 4)
			f.deferred.end = [&] {
				f.renderer.depthStencils[1].texture = nullptr;
				Check(!f.deferred.CopySceneDepth(), "An unavailable early copy must fail");
				f.renderer.depthStencils[1].texture = &f.copy;
			};
		f.Decals();
		Check(f.context.copies == 1, "An early copy or fallback must produce exactly one depth copy");
		Check(f.copy.content == (mode == 0 || mode == 2 || mode == 4 ? 61 : 43), "Copy must follow terrain and native decals");
		Check(f.deferred.IsSceneDepthFinal(), "Fallback depth must be published for water");
	}
}

void TestRedirection()
{
	Fixture f;
	f.renderer.depthStencils[0].depthSRV = &f.blendSrv;
	f.renderer.depthStencils[1].depthSRV = &f.blendSrv;
	f.shaderCache.enabled = false;
	f.Decals();
	Check(f.renderer.depthStencils[0].depthSRV == &f.mainSrv, "Skipped terrain must restore the main depth SRV");
	Check(Util::GetCurrentSceneDepthSRV(false) == &f.copySrv, "Skipped terrain must restore the physical copy SRV");
	f.renderer.depthStencils[0].depthSRV = &f.alternateSrv;
	f.renderer.depthStencils[1].depthSRV = &f.alternateSrv;
	Check(f.deferred.CopySceneDepth(), "Unredirected copy failed");
	Check(f.renderer.depthStencils[0].depthSRV == &f.alternateSrv && f.renderer.depthStencils[1].depthSRV == &f.alternateSrv,
		"Restoration must only replace terrain-owned redirections");
}

void TestUnavailableResources()
{
	for (int missing = 0; missing < 7; ++missing) {
		Fixture f;
		switch (missing) {
		case 0:
			globals::game::renderer = nullptr;
			break;
		case 1:
			globals::d3d::context = nullptr;
			break;
		case 2:
			globals::state = nullptr;
			break;
		case 3:
			f.renderer.depthStencils[0].texture = nullptr;
			break;
		case 4:
			f.renderer.depthStencils[1].texture = nullptr;
			break;
		case 5:
			f.renderer.depthStencils[1].depthSRV = nullptr;
			break;
		case 6:
			f.renderer.depthStencils[1].depthSRV = &f.blendSrv;
			globals::features::terrainBlending.prepassSRVBackup = nullptr;
			break;
		}
		Check(!f.deferred.CopySceneDepth(), "Unavailable resources must reject the copy");
		Check(!f.deferred.IsSceneDepthFinal() && f.context.copies == 0, "Rejected copies must not publish final depth");
		if (missing == 0)
			Check(Util::GetCurrentSceneDepthSRV(false) == nullptr, "Missing renderer must return no view");
	}
}

int main()
{
	try {
		TestSources();
		TestInvalidation();
		TestFallbacks();
		TestRedirection();
		TestUnavailableResources();
		std::cout << "Scene depth: source selection, invalidation, fallback ordering, restoration and missing resources passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
