#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

struct ID3D11ShaderResourceView
{};
struct ID3D11VertexShader
{};
struct ID3D11PixelShader
{};

namespace RE
{
	struct NiAlphaProperty
	{
		bool blending = false, testing = false;
		bool GetAlphaBlending() const { return blending; }
		bool GetAlphaTesting() const { return testing; }
	};
	struct NiAVObject
	{
		struct World
		{
			float scale = 1.0f;
		} world;
	};
	struct NiSkinPartition
	{};
	struct NiSkinInstance
	{
		std::shared_ptr<NiSkinPartition> skinPartition = std::make_shared<NiSkinPartition>();
		NiAVObject* rootParent = nullptr;
	};
	struct Texture
	{
		struct RendererTexture
		{
			ID3D11ShaderResourceView* resourceView = nullptr;
		};
		std::shared_ptr<RendererTexture> rendererTexture = std::make_shared<RendererTexture>();
	};
	struct BSLightingShaderMaterialBase
	{
		float materialAlpha = 1.0f;
		std::shared_ptr<Texture> normalTexture;
	};
	struct BSShaderProperty
	{
		float alpha = 1.0f;
		BSLightingShaderMaterialBase* material = nullptr;
	};
	struct BSGeometry : NiAVObject
	{
		enum class Type
		{
			kTriShape,
			kMeshLODTriShape,
			kDynamicTriShape,
			kParticles
		};
		struct GeometryType
		{
			Type value = Type::kTriShape;
			Type get() const { return value; }
		} type;
		struct RuntimeData
		{
			std::shared_ptr<NiAlphaProperty> alphaProperty;
			std::shared_ptr<NiSkinInstance> skinInstance;
		} data;
		struct Point
		{
			float distance = 10.0f;
			float GetDistance(Point) const { return distance; }
		};
		struct Bound
		{
			Point center;
			float radius = 1.0f;
		} worldBound;
		const RuntimeData& GetGeometryRuntimeData() const { return data; }
		GeometryType GetType() const { return type; }
	};
	struct BSShader
	{};
	struct BSRenderPass
	{
		BSShaderProperty* shaderProperty = nullptr;
		BSGeometry* geometry = nullptr;
	};
	namespace BSGraphics
	{
		enum class DepthStencilDepthMode
		{
			kTestEqual,
			kTestWrite,
			kTest
		};
	}
}

namespace
{
	constexpr uint32_t ReflectionsFlag = 1, ModelSpaceNormalsFlag = 2, FurFlag = 4, FurDepthFlag = 12;
	constexpr uint32_t MaxShells = 32;
	constexpr size_t MaxDeferredPasses = 8192;
	constexpr float PixelsPerShell = 1.0f;
	bool IsFirstPersonPass() { return false; }
	bool IsFurTechnique(uint32_t) { return true; }
	bool IsSkinTechnique(uint32_t) { return false; }
	unsigned projectionCalls = 0;
	unsigned checked = 0;
	void Require(bool condition, const char* message)
	{
		++checked;
		if (!condition)
			throw std::runtime_error(message);
	}

#include "fur_geometry_under_test.h"
}

namespace REX::W32
{
	template <class T>
	T* AsReal(T* value)
	{
		return value;
	}
}
namespace FurInertia
{
	float GetWorldLength(float length, float scale) { return length * (std::isfinite(scale) ? std::clamp(scale, 0.1f, 10.0f) : 1.0f); }
}
namespace Util
{
	RE::BSGeometry::Point GetEyePosition(int) { return {}; }
	struct Camera
	{
		struct Matrix
		{
			float m[4][4]{};
		} projMat;
	} camera;
	const Camera& GetCameraData(int)
	{
		++projectionCalls;
		return camera;
	}
	struct Size
	{
		float x, y;
	};
	float dynamicScale = 1.0f;
	Size ConvertToDynamic(Size size) { return { size.x * dynamicScale, size.y * dynamicScale }; }
}
namespace globals
{
	struct State
	{
		struct Permutation
		{
			uint32_t ExtraShaderDescriptor = 0;
		} permutationData;
		uint32_t currentPixelDescriptor = 0, modifiedVertexDescriptor = 0, modifiedPixelDescriptor = 0;
		bool inWorld = true;
		bool IsMainOrLoadingMenuOpen(void* = nullptr) const { return false; }
	}* state;
	struct ShaderCache
	{
		struct Shader
		{
			void* shader = nullptr;
		} shader;
		unsigned lookups = 0, failedLookup = 0;
		bool IsEnabled() const { return true; }
		Shader* GetVertexShader(RE::BSShader&, uint32_t)
		{
			++lookups;
			return lookups == failedLookup ? nullptr : &shader;
		}
		Shader* GetPixelShader(RE::BSShader&, uint32_t)
		{
			++lookups;
			return lookups == failedLookup ? nullptr : &shader;
		}
	}* shaderCache;
	namespace game
	{
		struct GraphicsState
		{
			uint32_t screenHeight = 1440;
		} graphics;
		auto* graphicsState = &graphics;
		struct ShadowState
		{
			RE::BSGraphics::DepthStencilDepthMode depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestWrite;
		}* shadowState;
		void* ui = nullptr;
	}
}
#define GET_INSTANCE_MEMBER(member, object) const auto member = object->member;

namespace
{
#include "fur_projection_under_test.h"
}

struct FurShells
{
	struct ShellOverride
	{
		bool Enabled = true;
		uint32_t ShellCount = 12;
		float Length = 1.2f, Droop = 0.15f, RootThreshold = 0.0f, TipThreshold = 1.0f, RootDarkening = 0.6f, ShellColor = 1.0f, FadeStart = 700.0f, FadeEnd = 2500.0f;
	};
	struct Settings : ShellOverride
	{
		bool HideCoveredFur = false, OverlayFur = true, DistanceShellBudget = true;
		float MinFurPixels = 0.0f;
	} settings;
	struct ShellData
	{
		ShellOverride values;
		bool overridden = false, skin = false;
	} shellData;
	struct PerPass
	{
		float Length, ShellCount, Droop, RootThreshold, TipThreshold, RootDarkening, ShellColor, RootTest, Density, TipWeight;
	};
	PerPass boundPass{};
	struct View
	{
		ID3D11ShaderResourceView* value = nullptr;
		ID3D11ShaderResourceView* get() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
	struct FrameBody
	{
		RE::NiSkinPartition* partition;
		RE::NiAVObject* rootParent;
		RE::BSGeometry* geometry;
		View shellView, normalView;
		PerPass perPass;
		bool deferred;
	};
	struct DeferredPass
	{
		RE::BSRenderPass* pass = nullptr;
	} currentPass;
	struct ShellLookup
	{
		RE::Texture* shell;
		ShellData* data;
		bool resolved = true, emptyOverlay = false;
	};
	std::vector<FrameBody> frameBodies;
	std::vector<DeferredPass> deferredPasses, deferredOverlayPasses;
	std::atomic<bool> shellAssetsPresent = true;
	bool perPassCB = true, noColorWrite = true, replaying = false, deferralClosed = false, rootTest = false, overlayPass = false;
	uint32_t instanceCount = 0;
	float overrideFadeEnd = 0.0f;
	ID3D11VertexShader* furVertexShader = nullptr;
	ID3D11PixelShader* furPixelShader = nullptr;
	ID3D11PixelShader* depthPixelShader = nullptr;
	ID3D11ShaderResourceView testShellView;
	RE::Texture shell;
	unsigned textureLookups = 0, bindings = 0, dynamics = 0, ends = 0;
	FurShells() { shell.rendererTexture->resourceView = &testShellView; }
	void EndPass()
	{
		++ends;
		instanceCount = 0;
		overlayPass = false;
	}
	void StartAssetScan() {}
	ShellLookup FindShell(RE::BSRenderPass*)
	{
		++textureLookups;
		return { &shell, &shellData };
	}
	ShellOverride GetGlobalValues(bool) const { return settings; }
	void FillDynamics(PerPass&, RE::BSGeometry*) { ++dynamics; }
	void BindPass(const PerPass& pass, ID3D11ShaderResourceView*, ID3D11ShaderResourceView*)
	{
		++bindings;
		boundPass = pass;
		instanceCount = uint32_t(pass.ShellCount);
	}
	void RecordFrameBody(RE::NiSkinInstance* skin, RE::BSGeometry* geometry, const PerPass& pass, ID3D11ShaderResourceView* shellResource, ID3D11ShaderResourceView* normalResource, bool deferred)
	{
		frameBodies.push_back({ skin->skinPartition.get(), skin->rootParent, geometry, { shellResource }, { normalResource }, pass, deferred });
	}
	const FrameBody* FindFrameBody(RE::BSGeometry*) const;
	void BeginPass(RE::BSShader*, RE::BSRenderPass*);
	void BeginOverlayPass(RE::BSShader*, RE::BSRenderPass*, const FrameBody&);
};

#include "fur_body_under_test.h"
#include "fur_overlay_under_test.h"
#include "fur_pass_under_test.h"

namespace
{
	struct Fixture
	{
		globals::State state;
		globals::ShaderCache cache;
		globals::game::ShadowState shadow;
		FurShells fur;
		RE::BSShader shader;
		RE::BSLightingShaderMaterialBase material;
		RE::BSShaderProperty property{ 1.0f, &material };
		RE::BSGeometry geometry;
		RE::BSRenderPass pass{ &property, &geometry };
		Fixture()
		{
			Util::camera.projMat.m[1][1] = 1.0f;
			Util::dynamicScale = 1.0f;
			projectionCalls = 0;
			globals::state = &state;
			globals::shaderCache = &cache;
			globals::game::shadowState = &shadow;
			fur.currentPass.pass = &pass;
		}
		void Begin() { fur.BeginPass(&shader, &pass); }
		void RequireNative()
		{
			Require(fur.instanceCount == 0 && fur.bindings == 0 && fur.deferredPasses.empty() && fur.deferredOverlayPasses.empty(), "Rejected geometry scheduled or bound shell draws");
			Require(fur.textureLookups == 0 && fur.dynamics == 0 && cache.lookups == 0, "Rejected geometry performed shell setup work");
		}
	};

	void TestGeometryAdmission()
	{
		using enum RE::BSGeometry::Type;
		for (auto type : { kTriShape, kMeshLODTriShape, kDynamicTriShape, kParticles }) {
			for (bool skinned : { false, true }) {
				Fixture test;
				test.geometry.type.value = type;
				if (skinned)
					test.geometry.data.skinInstance = std::make_shared<RE::NiSkinInstance>();
				const bool supported = skinned || type == kTriShape || type == kMeshLODTriShape;
				Require(CanDrawShells(test.pass) == supported, "Opaque geometry admission changed");
				for (bool testing : { false, true }) {
					auto alpha = std::make_shared<RE::NiAlphaProperty>();
					alpha->testing = testing;
					test.geometry.data.alphaProperty = alpha;
					Require(CanDrawShells(test.pass) == supported, "Non-blended alpha-tested fur must remain eligible");
					alpha->blending = true;
					Require(!CanDrawShells(test.pass), "Blend flags must retain native rendering, including alpha-tested and skinned cards");
				}
			}
		}
	}

	void TestPassRouting()
	{
		for (bool deferred : { false, true }) {
			for (bool replaying : { false, true }) {
				for (bool blending : { false, true }) {
					for (bool testing : { false, true }) {
						Fixture test;
						test.fur.settings.HideCoveredFur = deferred;
						test.fur.replaying = replaying;
						test.fur.instanceCount = 12;
						test.geometry.data.alphaProperty = std::make_shared<RE::NiAlphaProperty>(blending, testing);
						test.Begin();
						Require(test.fur.ends == 1, "Pass routing retained the previous shell state");
						if (blending) {
							test.RequireNative();
						} else {
							const bool queued = deferred && !replaying;
							Require(test.fur.deferredPasses.size() == unsigned(queued) && test.fur.bindings == unsigned(!queued), "Opaque or cutout fur did not retain its immediate/deferred route");
						}
					}
				}
			}
		}
		for (bool materialAlpha : { false, true }) {
			for (float alpha : { -1.0f, 0.0f, 0.5f, std::nextafter(1.0f, 0.0f), 1.0f, 2.0f,
					 std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
				Fixture test;
				(materialAlpha ? test.material.materialAlpha : test.property.alpha) = alpha;
				test.Begin();
				if (std::isfinite(alpha) && alpha >= 1.0f)
					Require(test.fur.bindings == 1, "Opaque scalar alpha was rejected");
				else
					test.RequireNative();
			}
		}
		for (int missing = 0; missing < 4; ++missing) {
			Fixture test;
			if (missing == 0)
				test.pass.geometry = nullptr;
			if (missing == 1)
				test.pass.shaderProperty = nullptr;
			if (missing == 2)
				test.property.material = nullptr;
			if (missing == 3)
				test.fur.settings.Enabled = false;
			test.Begin();
			test.RequireNative();
		}
	}

	void TestOverlayRouting()
	{
		for (bool deferred : { false, true }) {
			for (bool samePartition : { false, true }) {
				for (bool sameRoot : { false, true }) {
					Fixture test;
					RE::NiAVObject root, otherRoot;
					test.fur.settings.HideCoveredFur = deferred;
					test.geometry.data.skinInstance = std::make_shared<RE::NiSkinInstance>();
					test.geometry.data.skinInstance->rootParent = &root;
					test.Begin();
					Require(test.fur.frameBodies.size() == 1, "Body pass was not available for overlays");
					test.fur.EndPass();
					test.fur.deferredPasses.clear();
					test.fur.bindings = test.fur.textureLookups = test.fur.dynamics = test.cache.lookups = 0;
					RE::BSGeometry overlay;
					overlay.data.skinInstance = std::make_shared<RE::NiSkinInstance>();
					if (samePartition)
						overlay.data.skinInstance->skinPartition = test.geometry.data.skinInstance->skinPartition;
					overlay.data.skinInstance->rootParent = sameRoot ? &root : &otherRoot;
					overlay.data.alphaProperty = std::make_shared<RE::NiAlphaProperty>(true, false);
					test.pass.geometry = &overlay;
					test.material.materialAlpha = test.property.alpha = 0.5f;
					test.Begin();
					if (samePartition && sameRoot) {
						Require(test.fur.deferredOverlayPasses.size() == unsigned(deferred) && test.fur.bindings == unsigned(!deferred), "Matched blended body overlay was lost");
						Require(deferred || test.fur.overlayPass, "Overlay entered the solid shell depth path");
						if (deferred) {
							test.fur.replaying = true;
							test.Begin();
							Require(test.fur.bindings == 1 && test.fur.overlayPass, "Deferred overlay did not retain its blended path");
						}
					} else {
						test.RequireNative();
					}
				}
			}
		}
	}

	void TestProjectedBudget()
	{
		for (bool distanceBudget : { false, true })
			for (uint32_t budget : { 1u, 2u, 12u, 32u })
				for (float distance : { 0.0f, 700.0f, std::nextafter(2275.0f, 0.0f), 2275.0f, std::nextafter(2275.0f, 2500.0f), 2400.0f, 2499.0f, 2500.0f })
					for (float minimum : { 0.0f, 0.1f, 16.0f })
						for (float projection : { 0.0f, 0.001f, 1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
							for (bool skinned : { false, true }) {
								Fixture test;
								auto& settings = test.fur.settings;
								settings.ShellCount = budget;
								settings.DistanceShellBudget = distanceBudget;
								settings.MinFurPixels = minimum;
								test.geometry.worldBound.center.distance = distance + test.geometry.worldBound.radius;
								RE::NiAVObject root;
								if (skinned) {
									test.geometry.data.skinInstance = std::make_shared<RE::NiSkinInstance>();
									test.geometry.data.skinInstance->rootParent = &root;
								}
								root.world.scale = std::numeric_limits<float>::quiet_NaN();
								test.geometry.world.scale = 0.25f;
								Util::camera.projMat.m[1][1] = projection;
								Util::dynamicScale = 0.5f;
								const float fade = std::clamp((settings.FadeEnd - distance) / (settings.FadeEnd - settings.FadeStart), 0.0f, 1.0f);
								const auto selected = distanceBudget ? uint32_t(std::lround(float(budget) * fade)) : budget;
								const bool admitted = fade > 0.0f && (!distanceBudget || selected != 0);
								const float pixels = GetFurPixels(FurInertia::GetWorldLength(settings.Length, skinned ? root.world.scale : test.geometry.world.scale), distance);
								const float density = (distanceBudget ? 1.0f : fade) * (minimum > 0.0f ? std::clamp(pixels / minimum, 0.0f, 1.0f) : 1.0f);
								projectionCalls = 0;
								test.Begin();
								Require(projectionCalls == unsigned(admitted && (selected != 1 || minimum > 0.0f)), "Projection must be skipped only for one layer without size thinning");
								Require(test.fur.bindings == unsigned(admitted && density > 0.0f), "Projected density changed pass admission");
								if (!test.fur.bindings)
									continue;
								const float layers = std::clamp(pixels / PixelsPerShell, 1.0f, float(selected));
								const float shells = std::ceil(layers);
								const FurShells::PerPass expected{ settings.Length, shells, settings.Droop, settings.RootThreshold, settings.TipThreshold,
									settings.RootDarkening, settings.ShellColor, 0.0f, density, layers - (shells - 1.0f) };
								auto actual = test.fur.boundPass;
								const auto expectedDensity = std::bit_cast<uint32_t>(expected.Density), actualDensity = std::bit_cast<uint32_t>(actual.Density);
								// Fast math can reassociate the independent density oracle by two ULPs.
								Require(std::max(expectedDensity, actualDensity) - std::min(expectedDensity, actualDensity) <= 2, "Projection changed density beyond its rounding tolerance");
								actual.Density = expected.Density;
								Require(std::memcmp(&expected, &actual, sizeof(expected)) == 0, "Projection shortcut changed the shape payload");
								Require(test.fur.dynamics == 1 && test.cache.lookups == 3, "Projection shortcut bypassed shader readiness or dynamics");
							}
	}

	void TestOneLayerRouting()
	{
		for (bool deferred : { false, true })
			for (bool replaying : { false, true })
				for (unsigned failed = 0; failed <= 3; ++failed) {
					Fixture test;
					test.cache.failedLookup = failed;
					test.fur.settings.ShellCount = 1;
					test.fur.settings.DistanceShellBudget = false;
					test.fur.settings.HideCoveredFur = deferred;
					test.fur.replaying = replaying;
					test.fur.rootTest = true;
					test.geometry.worldBound.center.distance = 2401.0f;
					test.geometry.data.skinInstance = std::make_shared<RE::NiSkinInstance>();
					test.Begin();
					Require(projectionCalls == 0 && test.cache.lookups == 3, "One layer must skip projection while retaining every shader readiness check");
					if (failed) {
						Require(test.fur.bindings == 0 && test.fur.dynamics == 0 && test.fur.frameBodies.empty() && test.fur.deferredPasses.empty(), "Unavailable one-layer shaders must retain native fallback without publishing overlay data");
						continue;
					}
					const bool queued = deferred && !replaying;
					Require(test.fur.bindings == unsigned(!queued) && test.fur.deferredPasses.size() == unsigned(queued), "One-layer fur must preserve immediate, deferred and replay routing");
					Require(test.fur.dynamics == 1 && test.fur.frameBodies.size() == 1, "One-layer fur must prepare dynamics and the overlay body");
					const auto& body = test.fur.frameBodies[0];
					const float density = (2500.0f - 2400.0f) / (2500.0f - 700.0f);
					Require(body.perPass.ShellCount == 1.0f && body.perPass.TipWeight == 1.0f && body.perPass.Density == density && body.deferred == (queued || replaying), "Overlay data must retain one-layer density and deferral");
					if (!queued)
						Require(test.fur.boundPass.RootTest == float(replaying), "One-layer replay must retain covered-root testing");
				}
	}
}

int main()
{
	try {
		TestGeometryAdmission();
		TestPassRouting();
		TestOverlayRouting();
		TestProjectedBudget();
		TestOneLayerRouting();
		std::cout << checked << " fur admission, native fallback and overlay routing checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
