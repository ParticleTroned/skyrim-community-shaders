#define NOMINMAX
#include <Windows.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

namespace
{
	bool vrRuntime = true;
	int nativeCalls = 0;
	int particleCalls = 0;
	int terrainCalls = 0;
	int interiorCalls = 0;
	std::atomic<int> warnings = 0;
	int failures = 0;
	DWORD injectedShaderException = 0;
	DWORD injectedNativeException = 0;
	bool particleAccepted = true;
	bool invalidateAfterNativeDraw = false;
	bool argumentsPreserved = true;
	void* expectedPass = nullptr;
	constexpr uint32_t technique = 0x4800002D;
	uint32_t expectedTechnique = technique;
	constexpr uint32_t renderFlags = 0x2468;

	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::fprintf(stderr, "%s\n", message);
			++failures;
		}
	}

	void ResetCalls()
	{
		nativeCalls = particleCalls = terrainCalls = interiorCalls = 0;
		argumentsPreserved = true;
	}
}

namespace REL
{
	struct Module
	{
		static bool IsVR() { return vrRuntime; }
	};
	template <class Signature>
	struct Relocation
	{
		Signature* callback = nullptr;
		template <class... Args>
		auto operator()(Args&&... args) const
		{
			return callback(std::forward<Args>(args)...);
		}
	};
}

namespace RE
{
	struct RENDER_TARGETS
	{
		static constexpr int kTOTAL = 114;
		static constexpr int kVRTOTAL = 125;
	};
	struct alignas(8) BSShaderMaterial
	{
		std::array<std::byte, 0x38> baseBytes{};
	};
	struct BSLightingShaderMaterialBase : BSShaderMaterial
	{
		std::array<std::byte, 0x18> textureBytes{};
		int32_t diffuseRenderTargetSourceIndex = -1;
		std::array<std::byte, 0x4C> remainingBytes{};
	};
	static_assert(sizeof(BSLightingShaderMaterialBase) == 0xA0);
	struct alignas(8) BSShader
	{
		enum class Type
		{
			Lighting = 6,
			Effect = 7
		};
		std::array<std::byte, 0x20> shaderBaseBytes{};
		struct
		{
			Type value = Type::Lighting;
			Type get() const
			{
				if (injectedShaderException)
					RaiseException(injectedShaderException, 0, 0, nullptr);
				return value;
			}
		} shaderType;
		std::array<std::byte, 0x6C> shaderRemainingBytes{};
	};
	struct alignas(8) BSShaderProperty
	{
		std::array<std::byte, 0x78> propertyBaseBytes{};
		BSShaderMaterial* material = nullptr;
		std::array<std::byte, 8> propertyRemainingBytes{};
	};
	struct alignas(8) BSRenderPass
	{
		BSShader* shader = nullptr;
		BSShaderProperty* shaderProperty = nullptr;
		std::array<std::byte, 0x38> passRemainingBytes{};
	};
}

#include "native_lighting_material_types_under_test.h"

struct TruePBR
{
	bool loaded = true;
	struct
	{
		uint32_t Enabled = true;
	} settings;
	bool UsesCustomMaterialSetup(uint32_t rawTechnique) const;
};

#include "native_lighting_material_pbr_under_test.h"

namespace Util
{
#include "native_lighting_material_utilities_under_test.h"
}

namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
}

struct TerrainBlending
{
	enum class RenderPassImmediatelyAction
	{
		Draw,
		Skip,
		DrawTwice
	};
	bool loaded = true;
	RenderPassImmediatelyAction action = RenderPassImmediatelyAction::Draw;
	void (*onRenderPass)(RE::BSRenderPass*) = nullptr;
	RenderPassImmediatelyAction OnRenderPassImmediately(RE::BSRenderPass* pass, uint32_t, bool, uint32_t)
	{
		++terrainCalls;
		if (onRenderPass)
			onRenderPass(pass);
		return action;
	}
};

namespace globals::features
{
	TruePBR truePBR;
	struct
	{
		bool loaded = true;
		void (*onCheck)(RE::BSRenderPass*) = nullptr;
		bool CheckParticleLights(RE::BSRenderPass* pass, uint32_t)
		{
			++particleCalls;
			if (onCheck)
				onCheck(pass);
			return particleAccepted;
		}
	} lightLimitFix;
	TerrainBlending terrainBlending;
	struct
	{
		bool loaded = true;
		void UpdateRasterStateCullMode(RE::BSRenderPass*, uint32_t) { ++interiorCalls; }
	} interiorSun;
}

namespace Hooks
{
	struct BSBatchRenderer_RenderPassImmediately1
	{
		static void thunk(RE::BSRenderPass*, uint32_t, bool, uint32_t);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	void DrawRenderPassImmediately(RE::BSRenderPass*, uint32_t, bool, uint32_t);

#include "native_lighting_material_guard_under_test.h"
}

void NativeDraw(RE::BSRenderPass* pass, uint32_t currentTechnique, bool alphaTest, uint32_t currentFlags)
{
	if (injectedNativeException)
		RaiseException(injectedNativeException, 0, 0, nullptr);
	++nativeCalls;
	argumentsPreserved &= pass == expectedPass && currentTechnique == expectedTechnique && alphaTest && currentFlags == renderFlags;
	if (invalidateAfterNativeDraw)
		static_cast<RE::BSLightingShaderMaterialBase*>(pass->shaderProperty->material)->diffuseRenderTargetSourceIndex = 1861746551;
}

static_assert(RE::BSLightingShader::kTechniqueIDBase == technique);
static_assert(offsetof(RE::BSLightingShaderMaterialBase, diffuseRenderTargetSourceIndex) == 0x50);
static_assert(offsetof(RE::BSLightingShader, currentRawTechnique) == 0x94);
static_assert(offsetof(RE::BSShader, shaderType) == 0x20);
static_assert(sizeof(RE::BSShader) == 0x90);
static_assert(offsetof(RE::BSShaderProperty, material) == 0x78);
static_assert(sizeof(RE::BSShaderProperty) == 0x88);
static_assert(offsetof(RE::BSRenderPass, shaderProperty) == 8);
static_assert(sizeof(RE::BSRenderPass) == 0x48);

bool ProbeExceptionReachesCaller(RE::BSRenderPass* pass, DWORD exceptionCode)
{
	__try {
		Hooks::ShouldSkipInvalidVRLightingMaterial(pass, technique);
	} __except (GetExceptionCode() == exceptionCode ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		return true;
	}
	return false;
}

bool NativeExceptionReachesCaller(RE::BSRenderPass* pass, DWORD exceptionCode)
{
	__try {
		Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(pass, technique, true, renderFlags);
	} __except (GetExceptionCode() == exceptionCode ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		return true;
	}
	return false;
}

int main()
{
	Hooks::BSBatchRenderer_RenderPassImmediately1::func.callback = NativeDraw;
	Hooks::BSBatchRenderer_RenderPassImmediately2::func.callback = NativeDraw;
	Hooks::BSBatchRenderer_RenderPassImmediately3::func.callback = NativeDraw;
	constexpr std::array entries{
		Hooks::BSBatchRenderer_RenderPassImmediately1::thunk,
		Hooks::BSBatchRenderer_RenderPassImmediately2::thunk,
		Hooks::BSBatchRenderer_RenderPassImmediately3::thunk,
		Hooks::DrawRenderPassImmediately
	};
	RE::BSLightingShaderMaterialBase material;
	RE::BSLightingShader shader;
	RE::BSShaderProperty property;
	property.material = &material;
	RE::BSRenderPass pass{ &shader, &property };

	auto run = [&](RE::BSRenderPass* currentPass, bool allowed, uint32_t drawTechnique = technique) {
		expectedTechnique = drawTechnique;
		for (std::size_t entry = 0; entry < entries.size(); ++entry) {
			ResetCalls();
			expectedPass = currentPass;
			entries[entry](currentPass, drawTechnique, true, renderFlags);
			Check(nativeCalls == (allowed ? 1 : 0), "Each entry must reject a malformed draw or forward a valid draw exactly once");
			Check(particleCalls == (allowed && entry != 3 ? 1 : 0), "Material rejection must precede the particle callback");
			Check(terrainCalls == (allowed && entry == 1 ? 1 : 0), "Material rejection must precede terrain routing");
			Check(interiorCalls == (allowed && (entry == 1 || entry == 3) ? 1 : 0), "Material rejection must precede raster-state mutation");
			Check(argumentsPreserved, "Accepted draws must preserve all native call arguments");
		}
	};
	auto runIndex = [&](int32_t index, bool allowed, uint32_t drawTechnique = technique) {
		material.diffuseRenderTargetSourceIndex = index;
		std::array<std::byte, sizeof(material)> before;
		std::memcpy(before.data(), &material, sizeof(material));
		run(&pass, allowed, drawTechnique);
		Check(std::memcmp(before.data(), &material, sizeof(material)) == 0, "The guard must never write or repair material bytes");
	};

	for (const auto index : { -1, 0, 113, 114, 115, 124 })
		runIndex(index, true);
	Check(warnings == 0, "Valid draws must not enter warning logging");
	{
		constexpr std::size_t callerCount = 8;
		std::barrier start{ static_cast<std::ptrdiff_t>(callerCount) };
		std::atomic<int> admittedInvalidPasses = 0;
		{
			std::array<std::jthread, callerCount> callers;
			for (auto& caller : callers) {
				caller = std::jthread([&] {
					start.arrive_and_wait();
					for (int repeat = 0; repeat < 32; ++repeat) {
						if (!Hooks::ShouldSkipInvalidVRLightingMaterial(nullptr, technique))
							++admittedInvalidPasses;
					}
				});
			}
		}
		Check(admittedInvalidPasses == 0, "Concurrent malformed draws must remain rejected");
		Check(warnings == 1, "Concurrent rejections must emit only one warning for their reason");
	}
	for (const auto index : { -2, 125, 1861746551, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max() }) {
		runIndex(index, false);
		runIndex(index, false);
		runIndex(-1, true);
	}
	Check(warnings == 2, "Repeated invalid indices must add only one bounded reason warning");

	vrRuntime = false;
	for (const auto index : { -2, 114, 115, 125, 1861746551 })
		runIndex(index, true);
	run(nullptr, true);
	vrRuntime = true;

	shader.shaderType.value = RE::BSShader::Type::Effect;
	property.material = nullptr;
	run(&pass, true);
	pass.shaderProperty = nullptr;
	run(&pass, true);
	shader.shaderType.value = RE::BSShader::Type::Lighting;
	pass.shaderProperty = &property;

	property.material = nullptr;
	run(&pass, false);
	property.material = reinterpret_cast<RE::BSShaderMaterial*>(reinterpret_cast<std::uintptr_t>(&material) + 1);
	run(&pass, false);
	property.material = reinterpret_cast<RE::BSShaderMaterial*>(0xFFF8);
	run(&pass, false);
	property.material = &material;
	pass.shaderProperty = nullptr;
	run(&pass, false);
	pass.shaderProperty = &property;
	pass.shader = nullptr;
	run(&pass, false);
	pass.shader = &shader;
	run(nullptr, false);
	run(reinterpret_cast<RE::BSRenderPass*>(reinterpret_cast<std::uintptr_t>(&pass) + 1), false);

	void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
	Check(inaccessible != nullptr, "The unreadable-pointer regression must allocate its guard page");
	if (inaccessible) {
		property.material = static_cast<RE::BSShaderMaterial*>(inaccessible);
		run(&pass, false);
		property.material = &material;
		pass.shaderProperty = static_cast<RE::BSShaderProperty*>(inaccessible);
		run(&pass, false);
		pass.shaderProperty = &property;
		pass.shader = static_cast<RE::BSShader*>(inaccessible);
		run(&pass, false);
		pass.shader = &shader;
		run(static_cast<RE::BSRenderPass*>(inaccessible), false);
		Check(VirtualFree(inaccessible, 0, MEM_RELEASE) != 0, "The test must release its guard page");
	}
	Check(warnings == 6, "Warnings must be bounded once per rejection reason");
	runIndex(-1, true);

	ResetCalls();
	expectedPass = &pass;
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::DrawTwice;
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 2 && interiorCalls == 2 && terrainCalls == 1 && particleCalls == 1,
		"Valid terrain double draws must retain both native submissions");
	invalidateAfterNativeDraw = true;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 1 && interiorCalls == 1,
		"A second terrain draw must reject material changes made during the first draw");
	invalidateAfterNativeDraw = false;
	material.diffuseRenderTargetSourceIndex = -1;

	const auto invalidateMaterial = +[](RE::BSRenderPass* currentPass) {
		static_cast<RE::BSLightingShaderMaterialBase*>(currentPass->shaderProperty->material)->diffuseRenderTargetSourceIndex = 1861746551;
	};
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Draw;
	globals::features::lightLimitFix.onCheck = invalidateMaterial;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 0 && interiorCalls == 0 && particleCalls == 1,
		"Material changes in a particle callback must not inherit earlier admission");
	globals::features::lightLimitFix.onCheck = nullptr;
	material.diffuseRenderTargetSourceIndex = -1;
	globals::features::terrainBlending.onRenderPass = invalidateMaterial;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 0 && interiorCalls == 0 && terrainCalls == 1,
		"Material changes in a terrain callback must not inherit earlier admission");
	globals::features::terrainBlending.onRenderPass = nullptr;
	material.diffuseRenderTargetSourceIndex = -1;
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Skip;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 0 && interiorCalls == 0 && terrainCalls == 1 && particleCalls == 1,
		"Valid terrain skip routing must remain intact");
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Draw;
	particleAccepted = false;
	for (std::size_t entry = 0; entry < 3; ++entry) {
		ResetCalls();
		entries[entry](&pass, technique, true, renderFlags);
		Check(nativeCalls == 0 && particleCalls == 1 && terrainCalls == 0 && interiorCalls == 0,
			"Valid particle rejection must retain its existing routing");
	}

	particleAccepted = true;
	constexpr auto pbrFlag = static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr);
	using LightingTechnique = SIE::ShaderCache::LightingShaderTechniques;
	for (const auto type : { LightingTechnique::None, LightingTechnique::MTLand, LightingTechnique::TreeAnim, LightingTechnique::LODLand, LightingTechnique::LODLandNoise }) {
		const auto rawType = static_cast<uint32_t>(type) << 24;
		const bool customType = type != LightingTechnique::LODLand && type != LightingTechnique::LODLandNoise;
		for (const bool loaded : { false, true }) {
			globals::features::truePBR.loaded = loaded;
			for (const uint32_t enabled : { 0u, 1u }) {
				globals::features::truePBR.settings.Enabled = enabled;
				for (const bool flagged : { false, true }) {
					const auto rawTechnique = rawType | (flagged ? pbrFlag : 0u);
					const auto drawTechnique = technique + rawTechnique;
					const bool custom = customType && loaded && enabled && flagged;
					Check(globals::features::truePBR.UsesCustomMaterialSetup(rawTechnique) == custom, "The shared PBR policy must preserve setup ownership for each technique and feature state");
					shader.currentRawTechnique = flagged ? 0u : pbrFlag;
					for (const auto index : { -2, 125, 1861746551 })
						runIndex(index, custom, drawTechnique);
					runIndex(-1, true, drawTechnique);
				}
			}
		}
	}
	globals::features::truePBR.loaded = true;
	globals::features::truePBR.settings.Enabled = 1;
	shader.currentRawTechnique = pbrFlag;
	runIndex(1861746551, false, technique - 1);
	runIndex(1861746551, false, 0);
	shader.currentRawTechnique = 0;
	runIndex(1861746551, true, technique + pbrFlag);
	property.material = nullptr;
	run(&pass, false, technique + pbrFlag);
	property.material = reinterpret_cast<RE::BSShaderMaterial*>(reinterpret_cast<std::uintptr_t>(&material) + 1);
	run(&pass, false, technique + pbrFlag);
	property.material = &material;
	runIndex(-1, true);

	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Skip;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, technique, true, renderFlags);
	Check(nativeCalls == 0 && terrainCalls == 1, "A valid stored terrain pass must first be deferred");
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Draw;
	material.diffuseRenderTargetSourceIndex = 1861746551;
	ResetCalls();
	Hooks::DrawRenderPassImmediately(&pass, technique, true, renderFlags);
	Check(nativeCalls == 0 && interiorCalls == 0, "Terrain replay must revalidate a material that changed while queued");
	material.diffuseRenderTargetSourceIndex = -1;
	ResetCalls();
	Hooks::DrawRenderPassImmediately(&pass, technique, true, renderFlags);
	Check(nativeCalls == 1 && interiorCalls == 1, "A later valid replay must remain drawable");
	const auto queuedPbrTechnique = technique + pbrFlag;
	expectedTechnique = queuedPbrTechnique;
	material.diffuseRenderTargetSourceIndex = 1861746551;
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Skip;
	ResetCalls();
	Hooks::BSBatchRenderer_RenderPassImmediately2::thunk(&pass, queuedPbrTechnique, true, renderFlags);
	Check(nativeCalls == 0 && terrainCalls == 1, "Safe custom PBR fallback must remain eligible for terrain queuing");
	globals::features::terrainBlending.action = TerrainBlending::RenderPassImmediatelyAction::Draw;
	globals::features::truePBR.settings.Enabled = 0;
	ResetCalls();
	Hooks::DrawRenderPassImmediately(&pass, queuedPbrTechnique, true, renderFlags);
	Check(nativeCalls == 0 && interiorCalls == 0, "Disabling PBR while queued must restore native index rejection");
	globals::features::truePBR.settings.Enabled = 1;
	ResetCalls();
	Hooks::DrawRenderPassImmediately(&pass, queuedPbrTechnique, true, renderFlags);
	Check(nativeCalls == 1 && interiorCalls == 1 && argumentsPreserved, "Restoring custom ownership must allow the unchanged queued technique");
	material.diffuseRenderTargetSourceIndex = -1;
	particleAccepted = true;
	injectedShaderException = EXCEPTION_INT_DIVIDE_BY_ZERO;
	Check(ProbeExceptionReachesCaller(&pass, injectedShaderException), "Non-AV snapshot exceptions must propagate");
	injectedShaderException = 0;
	injectedNativeException = EXCEPTION_ACCESS_VIOLATION;
	Check(NativeExceptionReachesCaller(&pass, injectedNativeException), "Native AVs must propagate outside narrow guard");
	injectedNativeException = 0;

	SYSTEM_INFO info{};
	GetSystemInfo(&info);
	auto* boundary = static_cast<std::byte*>(VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	Check(boundary != nullptr, "Split-page fixture allocation");
	if (boundary) {
		DWORD previous = 0;
		Check(VirtualProtect(boundary + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous) != 0, "Split-page fixture protection");
		auto* end = boundary + info.dwPageSize;
		pass.shader = reinterpret_cast<RE::BSShader*>(end - 0x20);
		run(&pass, false);
		pass.shader = &shader;
		pass.shaderProperty = reinterpret_cast<RE::BSShaderProperty*>(end - 0x78);
		run(&pass, false);
		pass.shaderProperty = &property;
		property.material = reinterpret_cast<RE::BSShaderMaterial*>(end - 0x50);
		run(&pass, false);
		run(&pass, false, technique + pbrFlag);
		property.material = &material;
		auto* splitPass = reinterpret_cast<RE::BSRenderPass*>(end - 8);
		splitPass->shader = &shader;
		run(splitPass, false);
		Check(VirtualFree(boundary, 0, MEM_RELEASE) != 0, "Split-page fixture release");
	}

	std::printf("Native lighting material guard: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
