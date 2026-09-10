#include "LightLimitFix.h"
#include "Features/InverseSquareLighting/Common.h"
#include "Globals.h"
#include "GpuPass.h"
#include "InverseSquareLighting.h"
#include "LightLimitFix/VRHookPolicy.h"
#include "LinearLighting.h"
#include "LocationContext.h"

#include "Menu/ThemeManager.h"
#include "Shadercache.h"
#include "State.h"
#include "Util.h"
#include "Utils/D3D.h"
#include "Utils/ExternalEmittance.h"
#include "Utils/StringUtils.h"

#include "RE/B/BSMultiBoundRoom.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

// Per-cluster visible-light cap. Must match MAX_CLUSTER_LIGHTS in
// features/Light Limit Fix/Shaders/LightLimitFix/Common.hlsli because this
// sizes the global lightIndexList pool.
static constexpr uint CLUSTER_MAX_LIGHTS = 256;
static constexpr uint CONTACT_SHADOW_MAX_LIGHTS = 8;
static constexpr uint MAX_LIGHTS = 1024;

namespace
{
	constexpr uint kContactShadowFlagPoint = 1u << 0;
	constexpr uint kContactShadowFlagParticle = 1u << 1;
	constexpr uint kLightsVisualisationModeMax = 3;
	constexpr uint kContactShadowQualityMax = 2;
	constexpr int kContactShadowQualityOptionCount = static_cast<int>(kContactShadowQualityMax) + 1;
	constexpr uint kContactShadowClusterBudgetMin = 0;
	constexpr uint kContactShadowClusterBudgetMax = CONTACT_SHADOW_MAX_LIGHTS;
	constexpr uint kParticleContactShadowBudgetMax = 4;
	constexpr uint kStrictContactShadowBudgetMax = CONTACT_SHADOW_MAX_LIGHTS;

	constexpr float kParticleLightsSaturationMin = 1.0f;
	constexpr float kParticleLightsSaturationMax = 2.0f;
	constexpr float kParticleBrightnessMin = 0.0f;
	constexpr float kParticleBrightnessMax = 10.0f;
	constexpr float kParticleRadiusMin = 0.0f;
	constexpr float kParticleRadiusMax = 10.0f;
	constexpr float kBillboardBrightnessMin = 0.0f;
	constexpr float kBillboardBrightnessMax = 10.0f;
	constexpr float kBillboardRadiusMin = 0.0f;
	constexpr float kBillboardRadiusMax = 10.0f;
	constexpr float kParticleClusterThresholdMin = 8.0f;
	constexpr float kParticleClusterThresholdMax = 128.0f;
	constexpr int kMaxParticlesPerEmitterMin = 32;
	constexpr int kMaxParticlesPerEmitterMax = 2048;
	constexpr float kMaxParticleDistanceMin = 0.0f;
	constexpr float kMaxParticleDistanceMax = 20000.0f;
	constexpr float kParticleConfigSaturationMin = 0.0f;
	constexpr float kParticleConfigSaturationMax = 10.0f;
	constexpr float kJsonPlacedLightIntensityMin = 0.0f;
	constexpr float kJsonPlacedLightIntensityMax = 8.0f;
	constexpr std::uint32_t kParticleLightCacheSweepInterval = 120;
	constexpr std::uint32_t kParticleLightCacheMaxIdleFrames = 600;
	constexpr std::size_t kParticleLightCacheMaxEntries = static_cast<std::size_t>(MAX_LIGHTS) * 32u;
	constexpr std::size_t kMaxQueuedParticleLights = static_cast<std::size_t>(MAX_LIGHTS) * 16u;
	// StartGroupingAlphas has no capacity check; leave room for workers already past the guard.
	constexpr std::uint32_t kAlphaGeometryGroupCapacityFlat = 512;
	constexpr std::uint32_t kAlphaGeometryGroupCapacityVR = 1024;
	constexpr std::uint32_t kAlphaGeometryGroupReserve = 64;
	constexpr std::size_t kNiLightEngineReadSize = 0x174;
	constexpr std::size_t kBSLightEngineReadSize = 0x50;
	constexpr int kVRNiAVObjectFlagsOffset = 0x10C;
	constexpr int kVRBSLightNiLightOffset = 0x48;
	constexpr int kVRBSLightCullingProcessOffset = 0x128;
	constexpr int kVRShadowMapDescriptorPrimaryCameraOffset = 0x40;
	constexpr int kVRNiCameraViewFrustumArrayOffset = 0x180;
	constexpr int kMinimumPlausibleRenderPointer = 0x10000;
	constexpr int kLowCanonicalUserAddressBits = 47;
	constexpr int kRenderPointerAlignmentMask = static_cast<int>(alignof(void*)) - 1;
	constexpr std::uintptr_t kMaximumPlausibleRenderPointer = std::uintptr_t{ 1 } << kLowCanonicalUserAddressBits;
	constexpr std::array<std::uintptr_t, 4> kVRBSLightVtableRVAs{
		0x190A1F8,  // BSLight
		0x1908B20,  // BSShadowDirectionalLight
		0x190C090,  // BSShadowFrustumLight
		0x190C1F0   // BSShadowParabolicLight
	};
	constexpr std::array<std::uintptr_t, 5> kVRNiLightVtableRVAs{
		0x17F03B0,  // NiLight
		0x17F2C38,  // NiPointLight
		0x17F3550,  // NiDirectionalLight
		0x17F4E60,  // NiAmbientLight
		0x17F6940   // NiSpotLight
	};
	constexpr std::array<std::uintptr_t, 2> kVRShadowCameraVtableRVAs{
		0x17F0CB8,  // NiCamera
		0x19064C0   // BSCubeMapCamera
	};
	struct VRCullingProcessVtableSpec
	{
		std::uintptr_t vtableRVA;
		std::uintptr_t visibilityTargetRVA;
	};
	constexpr std::array<VRCullingProcessVtableSpec, 3> kVRBSCullingProcessVtableSpecs{
		VRCullingProcessVtableSpec{ 0x1815B40, 0xD9A7B0 },  // BSCullingProcess
		VRCullingProcessVtableSpec{ 0x1621068, 0xD9A7B0 },  // BSGeometryListCullingProcess
		VRCullingProcessVtableSpec{ 0x190BEB0, 0x136F600 }  // BSParabolicCullingProcess
	};
	constexpr std::size_t kVRBSCullingProcessVisibilityVtableOffset = 0xE8;

	std::uint32_t* alphaGeometryGroupCount = nullptr;
	std::uint32_t alphaGeometryGroupLimit = 0;
	std::atomic<std::uint32_t> alphaGeometryGroupPeak{ 0 };
	std::atomic<std::uint64_t> alphaGeometryGroupDrops{ 0 };

	struct StartGroupingAlphas
	{
		static void* thunk(
			RE::BSBatchRenderer* a_this,
			void* a_bound,
			RE::NiCamera* a_camera,
			bool a_sortByClosestPoint)
		{
			if (alphaGeometryGroupCount && a_camera) {
				const auto live = *alphaGeometryGroupCount;
				auto peak = alphaGeometryGroupPeak.load(std::memory_order_relaxed);
				while (live > peak && !alphaGeometryGroupPeak.compare_exchange_weak(
										  peak, live, std::memory_order_relaxed)) {
				}

				if (live >= alphaGeometryGroupLimit) {
					const auto drops = alphaGeometryGroupDrops.fetch_add(1, std::memory_order_relaxed) + 1;
					if (drops == 1 || drops % 10000 == 0) {
						logger::warn(
							"[LLF] Alpha GeometryGroup ceiling reached ({} live, {} refused)",
							live,
							drops);
					}
					return nullptr;
				}
			}

			return func(a_this, a_bound, a_camera, a_sortByClosestPoint);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <std::size_t N>
	bool MatchesInstructions(std::uintptr_t a_address, const std::uint8_t (&a_expected)[N]) noexcept
	{
		return std::equal(std::begin(a_expected), std::end(a_expected), reinterpret_cast<const std::uint8_t*>(a_address));
	}

	bool IsEngineFixesLoaded() noexcept
	{
		return GetModuleHandleW(L"EngineFixes.dll") != nullptr;
	}

	class VRValidatedObjectGuard : public Xbyak::CodeGenerator
	{
	protected:
		void RequirePlausiblePointer(
			const Xbyak::Reg64& a_pointer,
			const Xbyak::Reg64& a_scratch,
			Xbyak::Label& a_invalid)
		{
			test(a_pointer, a_pointer);
			jz(a_invalid, T_NEAR);
			cmp(a_pointer, kMinimumPlausibleRenderPointer);
			jb(a_invalid, T_NEAR);
			test(a_pointer.cvt8(), kRenderPointerAlignmentMask);
			jnz(a_invalid, T_NEAR);
			mov(a_scratch, a_pointer);
			shr(a_scratch, kLowCanonicalUserAddressBits);
			jnz(a_invalid, T_NEAR);
		}

		template <std::size_t N>
		void RequireKnownVtable(
			const Xbyak::Reg64& a_object,
			const Xbyak::Reg64& a_vtable,
			const Xbyak::Reg64& a_scratch,
			const std::array<std::uintptr_t, N>& a_allowedVtables,
			Xbyak::Label& a_invalid)
		{
			Xbyak::Label validVtable;

			RequirePlausiblePointer(a_object, a_scratch, a_invalid);
			mov(a_vtable, qword[a_object]);
			for (const auto vtable : a_allowedVtables) {
				mov(a_scratch, vtable);
				cmp(a_vtable, a_scratch);
				je(validVtable, T_NEAR);
			}
			jmp(a_invalid, T_NEAR);

			L(validVtable);
		}
	};

	class VREffectShaderFirstLightGuard : public VRValidatedObjectGuard
	{
	public:
		VREffectShaderFirstLightGuard(
			const std::array<std::uintptr_t, kVRBSLightVtableRVAs.size()>& a_allowedBSLightVtables,
			const std::array<std::uintptr_t, kVRNiLightVtableRVAs.size()>& a_allowedNiLightVtables,
			std::uintptr_t a_skipLights)
		{
			Xbyak::Label invalidLight;

			// Reproduce sceneLights[0]->light at the final engine read while
			// accepting only concrete BSLight and NiLight engine objects. The
			// later JZ consumes flags from an earlier engine CMP, so preserve
			// them and the scratch register across the checks.
			push(r10);
			pushfq();
			RequirePlausiblePointer(rax, r10, invalidLight);
			mov(rax, qword[rax]);
			RequireKnownVtable(rax, rcx, r10, a_allowedBSLightVtables, invalidLight);
			mov(rcx, qword[rax + kVRBSLightNiLightOffset]);
			RequireKnownVtable(rcx, rax, r10, a_allowedNiLightVtables, invalidLight);
			popfq();
			pop(r10);
			ret();

			L(invalidLight);
			// Discard write_call's return address and take Bethesda's existing
			// no-light path. No light constants have been written at this point.
			popfq();
			pop(r10);
			add(rsp, 8);
			mov(rax, a_skipLights);
			jmp(rax);
		}
	};

	class VREffectShaderAdditionalLightGuard : public VRValidatedObjectGuard
	{
	public:
		VREffectShaderAdditionalLightGuard(
			const std::array<std::uintptr_t, kVRBSLightVtableRVAs.size()>& a_allowedBSLightVtables,
			const std::array<std::uintptr_t, kVRNiLightVtableRVAs.size()>& a_allowedNiLightVtables,
			std::uintptr_t a_finishLights)
		{
			Xbyak::Label invalidLight;

			// Reproduce sceneLights[index]->light at the final engine read while
			// rejecting stale non-null entries of another engine object type.
			RequirePlausiblePointer(rax, r14, invalidLight);
			mov(rcx, qword[rax + rbx]);
			RequireKnownVtable(rcx, rax, r14, a_allowedBSLightVtables, invalidLight);
			mov(r14, qword[rcx + kVRBSLightNiLightOffset]);
			RequireKnownVtable(r14, rax, rcx, a_allowedNiLightVtables, invalidLight);
			mov(rcx, r13);
			ret();

			L(invalidLight);
			// RBX is the byte offset of the current sceneLights entry. Convert
			// it to the number of additional lights already completed, then use
			// Bethesda's existing tail to zero this slot and all following slots.
			mov(r14, rbx);
			shr(r14, 3);
			dec(r14);
			mov(qword[rbp + 0x28], r14);
			add(rsp, 8);
			mov(rax, a_finishLights);
			jmp(rax);
		}
	};

	class VRNonShadowCasterLightFlagsGuard : public Xbyak::CodeGenerator
	{
	public:
		VRNonShadowCasterLightFlagsGuard()
		{
			Xbyak::Label nullLight;

			test(rax, rax);
			jz(nullLight, T_SHORT);
			test(byte[rax + kVRNiAVObjectFlagsOffset], 1);
			ret();

			L(nullLight);
			// Synthesize the exact flags that the original test would produce for
			// a hidden light, without leaving registers changed or dereferencing nullptr.
			push(1);
			test(byte[rsp], 1);
			lea(rsp, ptr[rsp + 8]);
			ret();
		}
	};

	class VRSceneGraphCullingObjectGuard : public VRValidatedObjectGuard
	{
	public:
		explicit VRSceneGraphCullingObjectGuard(std::uintptr_t a_continuation)
		{
			Xbyak::Label invalidObject;

			// RDX is the NiAVObject selected by the scene graph. A stale child can
			// remain readable after its vtable has been cleared during cell teardown.
			// Reject only impossible object/vtable shapes before the native helper
			// reads further object fields. RAX and R10 are volatile and dead here.
			RequirePlausiblePointer(rdx, r10, invalidObject);
			mov(rax, qword[rdx]);
			RequirePlausiblePointer(rax, r10, invalidObject);

			// Reproduce the displaced five-byte prologue instruction exactly,
			// then resume before the native stack frame is established.
			mov(qword[rsp + 0x10], rbx);
			mov(rax, a_continuation);
			jmp(rax);

			L(invalidObject);
			// The helper is void and has not modified the stack yet, so skipping
			// this stale culling candidate preserves its caller's frame.
			ret();
		}
	};

	class VRShadowMapCameraGuard : public VRValidatedObjectGuard
	{
	public:
		VRShadowMapCameraGuard(
			const std::array<std::uintptr_t, kVRShadowCameraVtableRVAs.size()>& a_allowedCameraVtables,
			std::uintptr_t a_continuation)
		{
			Xbyak::Label invalidCamera;

			// ShadowmapDescriptorVR::camera[0] is consumed unconditionally by the
			// shared shadow-map helper. A descriptor can survive cell teardown after
			// its camera storage has been reused by another engine object, so accept
			// only known camera layouts and their required per-eye frustum array.
			// RAX, R10, and R11 are volatile and the original prologue overwrites RAX.
			RequirePlausiblePointer(rdx, r10, invalidCamera);
			mov(rax, qword[rdx + kVRShadowMapDescriptorPrimaryCameraOffset]);
			RequireKnownVtable(rax, r10, r11, a_allowedCameraVtables, invalidCamera);
			mov(rax, qword[rax + kVRNiCameraViewFrustumArrayOffset]);
			RequirePlausiblePointer(rax, r10, invalidCamera);

			// Reproduce the displaced six-byte prologue exactly, then continue
			// before the native helper has released or modified any render state.
			mov(rax, rsp);
			push(rbp);
			push(r12);
			mov(r10, a_continuation);
			jmp(r10);

			L(invalidCamera);
			// The helper is void and no native state has changed yet. Returning here
			// skips only this stale shadow descriptor; its caller advances normally.
			ret();
		}
	};

	class VRShadowMapCameraLateUseGuard : public VRValidatedObjectGuard
	{
	public:
		VRShadowMapCameraLateUseGuard(
			const std::array<std::uintptr_t, kVRShadowCameraVtableRVAs.size()>& a_allowedCameraVtables,
			std::uintptr_t a_epilogue)
		{
			Xbyak::Label invalidCamera;

			// The helper entry guard validates the descriptor supplied in RDX, but
			// live Windhelm/Dragonsreach COC testing reproduced the same helper later
			// consuming RBX->camera[0] after its frustum array had become null. Guard
			// the actual late-use camera immediately before the native MOVUPS reads
			// NiCamera::viewFrustumArray.
			RequireKnownVtable(rdi, rax, r10, a_allowedCameraVtables, invalidCamera);
			mov(rax, qword[rdi + kVRNiCameraViewFrustumArrayOffset]);
			RequirePlausiblePointer(rax, r10, invalidCamera);
			ret();

			L(invalidCamera);
			// Discard write_call's return address and run the helper's own epilogue.
			// At this point the native frame is fully established, so returning
			// directly would corrupt the caller's stack; the verified epilogue restores
			// all saved registers and exits this stale shadow descriptor cleanly.
			add(rsp, 8);
			mov(rax, a_epilogue);
			jmp(rax);
		}
	};

	enum class VRRoomLightCullingUse
	{
		kPortalGraphEntry,
		kVirtualCall,
	};

	class VRRoomLightCullingProcessGuard : public VRValidatedObjectGuard
	{
	public:
		VRRoomLightCullingProcessGuard(
			const std::array<std::uintptr_t, kVRBSCullingProcessVtableSpecs.size()>& a_allowedVtables,
			VRRoomLightCullingUse a_use,
			std::uintptr_t a_recoveryTarget)
		{
			if (a_use == VRRoomLightCullingUse::kPortalGraphEntry) {
				Generate(rax, rcx, a_allowedVtables, a_recoveryTarget);
			} else {
				Generate(rcx, rax, a_allowedVtables, a_recoveryTarget);
			}
		}

	private:
		void Generate(
			const Xbyak::Reg64& a_cullingProcess,
			const Xbyak::Reg64& a_vtable,
			const std::array<std::uintptr_t, kVRBSCullingProcessVtableSpecs.size()>& a_allowedVtables,
			std::uintptr_t a_recoveryTarget)
		{
			Xbyak::Label invalidCullingProcess;

			// Reproduce the displaced BSLight::cullingProcess load in the register
			// expected by its consumer. A stale but readable process can retain the
			// base NiCullingProcess vtable, whose +0xE8 entry is data rather than a
			// callable target. Accept only the concrete BSCullingProcess layouts that
			// provide both the portalGraphEntry field and the required virtual slot.
			// Preserve the registers and flags that the displaced MOV left unchanged,
			// while keeping this hot per-light guard allocation- and logging-free.
			push(a_vtable);
			push(r10);
			pushfq();
			mov(a_cullingProcess, qword[rbx + kVRBSLightCullingProcessOffset]);
			RequireKnownVtable(a_cullingProcess, a_vtable, r10, a_allowedVtables, invalidCullingProcess);
			popfq();
			pop(r10);
			pop(a_vtable);
			ret();

			L(invalidCullingProcess);
			// Discard write_call's return address and use the consumer's narrowest
			// existing Bethesda recovery branch.
			popfq();
			pop(r10);
			pop(a_vtable);
			add(rsp, 8);
			mov(rax, a_recoveryTarget);
			jmp(rax);
		}
	};

	class VRRoomLightEntryGuard : public Xbyak::CodeGenerator
	{
	public:
		VRRoomLightEntryGuard(
			const std::array<std::uintptr_t, 4>& a_allowedVtables,
			const std::array<std::uintptr_t, 4>& a_allowedCallTargets,
			std::size_t a_allowedCallTargetCount,
			std::uintptr_t a_skipLight)
		{
			Xbyak::Label invalidLight;
			Xbyak::Label validVtable;
			Xbyak::Label validCallTarget;

			// RAX is the vtable loaded from the current BSLight entry and RCX is
			// the BSLight itself. A stale but readable entry can retain a
			// heap-shaped vtable, so pointer-shape checks alone are insufficient.
			// Accept only the verified concrete BSLight vtables, rather than any
			// unrelated engine vtable which happens to contain executable data.
			for (const auto vtable : a_allowedVtables) {
				mov(r10, vtable);
				cmp(rax, r10);
				je(validVtable, T_NEAR);
			}
			jmp(invalidLight, T_NEAR);

			L(validVtable);
			mov(r11, qword[rax + 0x18]);
			for (std::size_t i = 0; i < a_allowedCallTargetCount; ++i) {
				mov(r10, a_allowedCallTargets[i]);
				cmp(r11, r10);
				je(validCallTarget, T_NEAR);
			}
			jmp(invalidLight, T_NEAR);

			L(validCallTarget);
			// write_call has already pushed the native continuation. Reserve a
			// fresh Windows x64 shadow area and restore pre-call stack alignment
			// for the nested virtual call.
			sub(rsp, 0x28);
			call(r11);
			add(rsp, 0x28);
			// The native instruction immediately following the displaced call
			// branches on these flags.
			test(al, al);
			ret();

			L(invalidLight);
			// Discard write_call's return address and use Bethesda's existing
			// skip-current-light path. RAX, R10, and R11 are volatile across the
			// original virtual call, so no live native state is lost.
			add(rsp, 8);
			mov(rax, a_skipLight);
			jmp(rax);
		}
	};

	void DrawHeatWarpStrengthSetting()
	{
		ImGui::SliderFloat(
			"Heat Warp Strength",
			&globals::state->refractionScale,
			0.0f,
			2.0f,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Scales ImageSpace refraction (heat shimmer around fire/heat sources).\n"
				"Lower values reduce warping; 0 disables it.");
		}
	}

	bool IsPlausibleRenderPointer(const void* a_ptr)
	{
		const auto value = reinterpret_cast<std::uintptr_t>(a_ptr);
		return value >= kMinimumPlausibleRenderPointer &&
		       value < kMaximumPlausibleRenderPointer &&
		       (value & kRenderPointerAlignmentMask) == 0;
	}

	bool IsReadableRange(const void* a_ptr, std::size_t a_size) noexcept
	{
		if (!a_ptr || a_size == 0) {
			return false;
		}

		MEMORY_BASIC_INFORMATION memoryInfo{};
		if (::VirtualQuery(a_ptr, &memoryInfo, sizeof(memoryInfo)) == 0) {
			return false;
		}
		if (memoryInfo.State != MEM_COMMIT) {
			return false;
		}

		constexpr DWORD kReadableProtection = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		                                      PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((memoryInfo.Protect & kReadableProtection) == 0 || (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
			return false;
		}

		const auto base = reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress);
		const auto ptr = reinterpret_cast<std::uintptr_t>(a_ptr);
		if (ptr < base) {
			return false;
		}

		const auto offset = ptr - base;
		if (offset > memoryInfo.RegionSize) {
			return false;
		}

		const auto available = memoryInfo.RegionSize - offset;
		return a_size <= available;
	}

	bool IsSafeLightRange(const void* a_ptr, std::size_t a_size)
	{
		return IsPlausibleRenderPointer(a_ptr) && IsReadableRange(a_ptr, a_size);
	}

	bool IsExecutableAddress(const void* a_ptr) noexcept
	{
		if (!a_ptr) {
			return false;
		}

		MEMORY_BASIC_INFORMATION memoryInfo{};
		if (::VirtualQuery(a_ptr, &memoryInfo, sizeof(memoryInfo)) == 0 || memoryInfo.State != MEM_COMMIT) {
			return false;
		}

		constexpr DWORD kExecutableProtection =
			PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		return (memoryInfo.Protect & kExecutableProtection) != 0 &&
		       (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
	}

	bool IsSafeDirectionalNiLight(const RE::NiLight* a_light)
	{
		static const RE::NiLight* validated = nullptr;
		static uint32_t validatedFrame = std::numeric_limits<uint32_t>::max();

		const uint32_t frame = globals::state ? globals::state->frameCount : 0;
		if (a_light == validated && frame == validatedFrame) {
			return true;
		}
		if (!IsSafeLightRange(a_light, kNiLightEngineReadSize)) {
			return false;
		}

		validated = a_light;
		validatedFrame = frame;
		return true;
	}

	RE::NiLight* SafeReadNiLight(RE::BSLight* a_light)
	{
#if defined(_MSC_VER)
		__try {
			return a_light->light.get();
		} __except (1) {
			return nullptr;
		}
#else
		return a_light ? a_light->light.get() : nullptr;
#endif
	}

	bool IsDirectionalSceneLightSafe(RE::BSRenderPass* a_pass, uint32_t& a_outNumLights, RE::BSLight*& a_outLight, RE::NiLight*& a_outNiLight)
	{
		a_outNumLights = 0;
		a_outLight = nullptr;
		a_outNiLight = nullptr;

		if (!a_pass) {
			return true;
		}

#if defined(_MSC_VER)
		__try
#endif
		{
			a_outNumLights = a_pass->numLights;
			if (a_outNumLights == 0 || !a_pass->sceneLights) {
				return false;
			}

			a_outLight = a_pass->sceneLights[0];
			if (!IsPlausibleRenderPointer(a_outLight)) {
				return false;
			}

			a_outNiLight = SafeReadNiLight(a_outLight);
			return IsSafeDirectionalNiLight(a_outNiLight);
		}
#if defined(_MSC_VER)
		__except (1) {
			a_outLight = nullptr;
			a_outNiLight = nullptr;
			return false;
		}
#endif
	}

	float ClampFiniteOrDefault(float a_value, float a_min, float a_max, float a_default)
	{
		if (!std::isfinite(a_value)) {
			return a_default;
		}
		return std::clamp(a_value, a_min, a_max);
	}

	void SanitizeSettings(LightLimitFix::Settings& a_settings)
	{
		a_settings.LightsVisualisationMode = std::min(a_settings.LightsVisualisationMode, kLightsVisualisationModeMax);
		a_settings.ContactShadowQuality = std::min(a_settings.ContactShadowQuality, kContactShadowQualityMax);
		a_settings.ContactShadowClusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		a_settings.ParticleContactShadowBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		a_settings.StrictContactShadowBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);
		a_settings.ParticleLightsSaturation =
			ClampFiniteOrDefault(a_settings.ParticleLightsSaturation, kParticleLightsSaturationMin, kParticleLightsSaturationMax, 1.0f);
		a_settings.ParticleBrightness =
			ClampFiniteOrDefault(a_settings.ParticleBrightness, kParticleBrightnessMin, kParticleBrightnessMax, 1.0f);
		a_settings.ParticleRadius =
			ClampFiniteOrDefault(a_settings.ParticleRadius, kParticleRadiusMin, kParticleRadiusMax, 1.0f);
		a_settings.BillboardBrightness =
			ClampFiniteOrDefault(a_settings.BillboardBrightness, kBillboardBrightnessMin, kBillboardBrightnessMax, 1.0f);
		a_settings.BillboardRadius =
			ClampFiniteOrDefault(a_settings.BillboardRadius, kBillboardRadiusMin, kBillboardRadiusMax, 1.0f);
		a_settings.ParticleClusterThreshold =
			ClampFiniteOrDefault(a_settings.ParticleClusterThreshold, kParticleClusterThresholdMin, kParticleClusterThresholdMax, 32.0f);
		a_settings.MaxParticlesPerEmitter = std::clamp(a_settings.MaxParticlesPerEmitter, kMaxParticlesPerEmitterMin, kMaxParticlesPerEmitterMax);
		a_settings.MaxParticleDistance =
			ClampFiniteOrDefault(a_settings.MaxParticleDistance, kMaxParticleDistanceMin, kMaxParticleDistanceMax, 6000.0f);
		a_settings.JsonPlacedLightIntensity =
			ClampFiniteOrDefault(a_settings.JsonPlacedLightIntensity, kJsonPlacedLightIntensityMin, kJsonPlacedLightIntensityMax, 1.0f);
	}

	uint PackContactShadowFlags(const LightLimitFix::Settings& a_settings)
	{
		if (!LocationContext::AllowsInteriorOnly(a_settings.ContactShadowsInteriorsOnly)) {
			return 0;
		}

		const uint clusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		const uint particleBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		const uint strictBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);

		uint flags = 0;
		if (a_settings.EnableContactShadows && (clusterBudget > 0 || strictBudget > 0)) {
			flags |= kContactShadowFlagPoint;
		}
		if (a_settings.EnableParticleContactShadows && clusterBudget > 0 && particleBudget > 0) {
			flags |= kContactShadowFlagParticle;
		}
		return flags;
	}

	uint PackContactShadowParams(const LightLimitFix::Settings& a_settings)
	{
		const uint quality = std::min(a_settings.ContactShadowQuality, kContactShadowQualityMax);
		const uint particleBudget = std::min(a_settings.ParticleContactShadowBudget, kParticleContactShadowBudgetMax);
		const uint clusterBudget = std::clamp(a_settings.ContactShadowClusterBudget, kContactShadowClusterBudgetMin, kContactShadowClusterBudgetMax);
		const uint strictBudget = std::min(a_settings.StrictContactShadowBudget, kStrictContactShadowBudgetMax);

		return (quality & 0xFFu) |
		       ((particleBudget & 0xFFu) << 8) |
		       ((clusterBudget & 0xFFu) << 16) |
		       ((strictBudget & 0xFFu) << 24);
	}

	float HashToUnitFloat(std::uint32_t a_value)
	{
		a_value ^= a_value >> 16;
		a_value *= 0x7feb352du;
		a_value ^= a_value >> 15;
		a_value *= 0x846ca68bu;
		a_value ^= a_value >> 16;
		return static_cast<float>(a_value & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
	}

	// Smooth deterministic legacy flicker without the removed external PerlinNoise header.
	float LegacyFlickerNoiseSigned(std::uint32_t a_seed, float a_time)
	{
		constexpr float tau = 6.28318530717958647692f;
		const float phase = HashToUnitFloat(a_seed) * tau;
		const float frequency = 0.85f + (HashToUnitFloat(a_seed ^ 0x9E3779B9u) * 0.5f);
		const float wave1 = std::sin((a_time * frequency) + phase);
		const float wave2 = std::sin((a_time * frequency * 1.93f) + (phase * 1.67f));
		const float wave3 = std::sin((a_time * frequency * 3.17f) + (phase * 2.31f));
		return std::clamp((wave1 * 0.6f) + (wave2 * 0.3f) + (wave3 * 0.1f), -1.0f, 1.0f);
	}

	float LegacyFlickerNoise01(std::uint32_t a_seed, float a_time)
	{
		return std::clamp((LegacyFlickerNoiseSigned(a_seed, a_time) * 0.5f) + 0.5f, 0.0f, 1.0f);
	}

	void ApplyLegacyParticleLightFlicker(LightLimitFix::LightData& a_light, const LightLimitFix::ResolvedBillboardLight& a_billboardLight, int a_eyeCount)
	{
		if (!a_billboardLight.flicker || !globals::state) {
			return;
		}

		const auto seed = a_billboardLight.flickerSeed;
		const float scaledTimer = globals::state->timer * a_billboardLight.flickerSpeed;
		const RE::NiPoint3 flickerOffset{
			LegacyFlickerNoiseSigned(seed, scaledTimer) * a_billboardLight.flickerMovement,
			LegacyFlickerNoiseSigned(seed + 1u, scaledTimer) * a_billboardLight.flickerMovement,
			LegacyFlickerNoiseSigned(seed + 2u, scaledTimer) * a_billboardLight.flickerMovement
		};

		for (int eyeIndex = 0; eyeIndex < std::clamp(a_eyeCount, 0, 2); eyeIndex++) {
			a_light.positionWS[eyeIndex].data.x += flickerOffset.x;
			a_light.positionWS[eyeIndex].data.y += flickerOffset.y;
			a_light.positionWS[eyeIndex].data.z += flickerOffset.z;
		}

		// Legacy LLF applied flicker after distance dimming. The current path keeps fade
		// separate from color, so convert back into pre-fade color space first.
		const float fadeCompensation = 1.0f / std::max(a_light.fade, 1e-4f);
		const float flickerIntensity = LegacyFlickerNoise01(seed + 3u, scaledTimer) * a_billboardLight.flickerIntensity * fadeCompensation;
		a_light.color.x = std::max(0.0f, a_light.color.x - flickerIntensity);
		a_light.color.y = std::max(0.0f, a_light.color.y - flickerIntensity);
		a_light.color.z = std::max(0.0f, a_light.color.z - flickerIntensity);
	}

	float ResolveParticleSaturation(float a_globalSaturation, float a_configSaturation)
	{
		const float configSaturation = ClampFiniteOrDefault(
			a_configSaturation,
			kParticleConfigSaturationMin,
			kParticleConfigSaturationMax,
			1.0f);
		return a_globalSaturation * configSaturation;
	}

	bool IsParticleEmitterBeyondDistance(
		const RE::BSGeometry* a_geometry,
		const RE::NiPoint3& a_eyePosition,
		float a_maxDistance)
	{
		if (!a_geometry || a_maxDistance <= 0.0f) {
			return false;
		}

		const auto& bound = a_geometry->worldBound;
		const float conservativeDistance = a_maxDistance + std::max(bound.radius, 0.0f);
		const float dx = bound.center.x - a_eyePosition.x;
		const float dy = bound.center.y - a_eyePosition.y;
		const float dz = bound.center.z - a_eyePosition.z;
		return (dx * dx) + (dy * dy) + (dz * dz) > conservativeDistance * conservativeDistance;
	}

	void ClearStrictLightData(LightLimitFix::StrictLightDataCB& a_data, bool a_resetRoomIndex) noexcept
	{
		a_data.NumStrictLights = 0;
		a_data.ShadowBitMask = 0;
		if (a_resetRoomIndex) {
			a_data.RoomIndex = -1;
		}
	}

	bool IsNearWhiteTint(const RE::NiColorA& a_color)
	{
		const float avg = (a_color.red + a_color.green + a_color.blue) / 3.0f;
		return std::abs(a_color.red - avg) < 0.02f &&
		       std::abs(a_color.green - avg) < 0.02f &&
		       std::abs(a_color.blue - avg) < 0.02f &&
		       avg > 0.92f;
	}

	struct EmissiveTintCandidate
	{
		bool valid = false;
		float distanceSq = std::numeric_limits<float>::max();
		float luma = -1.0f;
		RE::NiColorA tint{};
	};

	void UpdateEmissiveTintCandidate(
		EmissiveTintCandidate& a_candidate,
		float a_distanceSq,
		float a_luma,
		const RE::NiColorA& a_tint)
	{
		const bool isCloser = a_distanceSq + 1e-3f < a_candidate.distanceSq;
		const bool sameDistance = std::abs(a_distanceSq - a_candidate.distanceSq) <= 1e-3f;
		if (!a_candidate.valid || isCloser || (sameDistance && a_luma > a_candidate.luma)) {
			a_candidate.valid = true;
			a_candidate.distanceSq = a_distanceSq;
			a_candidate.luma = a_luma;
			a_candidate.tint = a_tint;
		}
	}

	RE::NiColorA BuildBillboardFallbackTint(
		const ParticleLights::Config& a_config,
		bool a_hasGradientConfig,
		const ParticleLights::GradientConfig& a_gradientConfig)
	{
		RE::NiColorA fallback{ 1.0f, 1.0f, 1.0f, 1.0f };

		if (a_hasGradientConfig) {
			fallback.red = a_gradientConfig.color.red;
			fallback.green = a_gradientConfig.color.green;
			fallback.blue = a_gradientConfig.color.blue;
		} else {
			fallback.red = a_config.colorMult.red;
			fallback.green = a_config.colorMult.green;
			fallback.blue = a_config.colorMult.blue;
		}
		return fallback;
	}

	RE::BSLightingShaderProperty* GetLightingShaderProperty(RE::NiProperty* a_property)
	{
		if (!a_property || a_property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get()) {
			return nullptr;
		}
		return static_cast<RE::BSLightingShaderProperty*>(a_property);
	}

	void ConsiderLightingEmissiveTint(
		RE::BSGeometry* a_geometry,
		RE::BSGeometry* a_ignoreGeometry,
		const RE::NiPoint3& a_targetPosition,
		EmissiveTintCandidate& a_bestAnyTint,
		EmissiveTintCandidate& a_bestNonWhiteTint)
	{
		if (!a_geometry || a_geometry == a_ignoreGeometry) {
			return;
		}

		auto* lightingProperty = GetLightingShaderProperty(a_geometry->GetGeometryRuntimeData().shaderProperty.get());

		if (!lightingProperty || !lightingProperty->emissiveColor || lightingProperty->emissiveMult <= 1e-4f) {
			return;
		}

		RE::NiColorA emissiveTint{
			std::max(lightingProperty->emissiveColor->red, 0.0f) * lightingProperty->emissiveMult,
			std::max(lightingProperty->emissiveColor->green, 0.0f) * lightingProperty->emissiveMult,
			std::max(lightingProperty->emissiveColor->blue, 0.0f) * lightingProperty->emissiveMult,
			1.0f
		};

		const float emissiveLuma =
			std::max(emissiveTint.red, 0.0f) +
			std::max(emissiveTint.green, 0.0f) +
			std::max(emissiveTint.blue, 0.0f);
		if (emissiveLuma <= 1e-4f) {
			return;
		}

		const auto& center = a_geometry->worldBound.center;
		const float dx = center.x - a_targetPosition.x;
		const float dy = center.y - a_targetPosition.y;
		const float dz = center.z - a_targetPosition.z;
		const float distanceSq = (dx * dx) + (dy * dy) + (dz * dz);
		UpdateEmissiveTintCandidate(a_bestAnyTint, distanceSq, emissiveLuma, emissiveTint);
		if (!IsNearWhiteTint(emissiveTint)) {
			UpdateEmissiveTintCandidate(a_bestNonWhiteTint, distanceSq, emissiveLuma, emissiveTint);
		}
	}

	void CollectNearbyLightingTint(
		RE::NiNode* a_root,
		RE::BSGeometry* a_ignoreGeometry,
		std::uint32_t a_depthRemaining,
		const RE::NiPoint3& a_targetPosition,
		EmissiveTintCandidate& a_bestAnyTint,
		EmissiveTintCandidate& a_bestNonWhiteTint)
	{
		if (!a_root) {
			return;
		}

		for (const auto& child : a_root->GetChildren()) {
			auto* childObject = child.get();
			if (!childObject) {
				continue;
			}

			if (auto* childGeometry = childObject->AsGeometry()) {
				ConsiderLightingEmissiveTint(childGeometry, a_ignoreGeometry, a_targetPosition, a_bestAnyTint, a_bestNonWhiteTint);
			}

			if (a_depthRemaining > 0) {
				if (auto* childNode = childObject->AsNode()) {
					CollectNearbyLightingTint(childNode, a_ignoreGeometry, a_depthRemaining - 1, a_targetPosition, a_bestAnyTint, a_bestNonWhiteTint);
				}
			}
		}
	}

	bool TryGetBillboardSiblingEmissiveTint(RE::BSGeometry* a_billboardGeometry, RE::NiColorA& a_outTint)
	{
		if (!a_billboardGeometry) {
			return false;
		}

		auto* billboardParentNode = a_billboardGeometry->parent ? a_billboardGeometry->parent->AsNode() : nullptr;
		if (!billboardParentNode) {
			return false;
		}

		RE::NiNode* searchRoot = billboardParentNode;
		if (auto* ownerNode = billboardParentNode->parent ? billboardParentNode->parent->AsNode() : nullptr) {
			searchRoot = ownerNode;
		}

		const RE::NiPoint3 targetPosition = a_billboardGeometry->world.translate;
		EmissiveTintCandidate bestAnyTint{};
		EmissiveTintCandidate bestNonWhiteTint{};
		CollectNearbyLightingTint(searchRoot, a_billboardGeometry, 2u, targetPosition, bestAnyTint, bestNonWhiteTint);
		if (!bestAnyTint.valid) {
			return false;
		}

		// Prefer non-white sibling emissive tint when available; fall back to closest emissive tint otherwise.
		a_outTint = bestNonWhiteTint.valid ? bestNonWhiteTint.tint : bestAnyTint.tint;
		return true;
	}

	RE::NiColorA BuildEffectMaterialEmissiveTint(RE::BSEffectShaderMaterial* a_material, RE::BSEffectShaderProperty* a_shaderProperty)
	{
		RE::NiColorA materialEmissiveTint{
			a_material->baseColor.red * a_material->baseColorScale,
			a_material->baseColor.green * a_material->baseColorScale,
			a_material->baseColor.blue * a_material->baseColorScale,
			1.0f
		};
		if (auto emittance = a_shaderProperty->emittanceColor) {
			materialEmissiveTint.red *= emittance->red;
			materialEmissiveTint.green *= emittance->green;
			materialEmissiveTint.blue *= emittance->blue;
		}
		return materialEmissiveTint;
	}

	float GetEmissiveTintLuma(const RE::NiColorA& a_tint)
	{
		return std::max(a_tint.red, 0.0f) +
		       std::max(a_tint.green, 0.0f) +
		       std::max(a_tint.blue, 0.0f);
	}

	void SetEngineLightFlags(LightLimitFix::LightData& a_light, RE::BSLight* a_bsLight)
	{
		PointLightFlags::SetPointLightTypeFlags(a_light.lightFlags, a_bsLight);
		a_light.lightFlags.reset(
			LightLimitFix::LightFlags::PortalStrict,
			LightLimitFix::LightFlags::Shadow,
			LightLimitFix::LightFlags::AffectWater);
		if (a_bsLight && a_bsLight->affectWater) {
			a_light.lightFlags.set(LightLimitFix::LightFlags::AffectWater);
		}
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableParticleLightsDetection,
	ParticleLightsSaturation,
	EnableParticleLightsOptimization,
	ParticleBrightness,
	ParticleRadius,
	BillboardBrightness,
	BillboardRadius,
	UseParticleLights087LegacyMode,
	ParticleClusterThreshold,  // NEW
	MaxParticlesPerEmitter,    // NEW
	MaxParticleDistance,       // NEW
	JsonPlacedLightIntensity,
	JsonPlacedLightsInteriorsOnly,
	JsonPlacedLightsPortalStrictOnly,
	EnableContactShadows,
	ContactShadowsInteriorsOnly,
	EnableParticleContactShadows,
	ContactShadowQuality,
	ContactShadowClusterBudget,
	ParticleContactShadowBudget,
	StrictContactShadowBudget,
	EnableLightsVisualisation,
	LightsVisualisationMode)
void LightLimitFix::DrawSettings()
{
	{
		// Heat warp / refraction strength (moved from Advanced Settings)
		ImGui::Text("ImageSpace Refraction");
		DrawHeatWarpStrengthSetting();

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::TreeNodeEx("Particle Lights")) {
			ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enables Particle Lights.");
			}

			ImGui::Separator();
			ImGui::TextWrapped("Particle Lights Performance");

			ImGui::Checkbox("Enable Culling", &settings.EnableParticleLightsCulling);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Significantly improves performance by not rendering empty textures. Only disable if you are encountering issues.");
			}

			ImGui::Checkbox("Enable Detection", &settings.EnableParticleLightsDetection);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Adds particle lights to the player light level, so that NPCs can detect them for stealth and gameplay.");
			}

			ImGui::Checkbox("Enable Optimization", &settings.EnableParticleLightsOptimization);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Merges vertices which are close enough to each other to improve performance.");
			}

			// NEW: clustering controls
			ImGui::SliderFloat("Cluster Threshold", &settings.ParticleClusterThreshold, kParticleClusterThresholdMin, kParticleClusterThresholdMax, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Distance+radius similarity threshold for merging particles into one light.\n"
					"Higher = more merging, better performance, blurrier lights.\n"
					"Lower = less merging, more precise, more expensive.");
			}

			ImGui::SliderInt("Max Particles per Emitter", &settings.MaxParticlesPerEmitter, kMaxParticlesPerEmitterMin, kMaxParticlesPerEmitterMax);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Maximum number of particles sampled per emitter per frame.\n"
					"Higher = closer to the real particle system but more CPU work.\n"
					"Lower = faster, especially for very dense effects.");
			}

			// NEW: distance cutoff for particle lights
			ImGui::SliderFloat("Max Particle Distance", &settings.MaxParticleDistance, 1000.0f, kMaxParticleDistanceMax, "%.0f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Particle lights beyond this distance from the camera are skipped entirely.\n"
					"Lower = better performance, but distant effects won't contribute light.\n"
					"Higher = more distant particle lighting, but more cost.");
			}

			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::TextWrapped("Particle Lights Customisation");
			ImGui::SliderFloat("Saturation", &settings.ParticleLightsSaturation, kParticleLightsSaturationMin, kParticleLightsSaturationMax, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Particle light saturation.");
			}
			ImGui::SliderFloat("Particle Brightness", &settings.ParticleBrightness, kParticleBrightnessMin, kParticleBrightnessMax, "%.2f");
			ImGui::SliderFloat("Particle Radius", &settings.ParticleRadius, kParticleRadiusMin, kParticleRadiusMax, "%.2f");
			ImGui::SliderFloat("Billboard Brightness", &settings.BillboardBrightness, kBillboardBrightnessMin, kBillboardBrightnessMax, "%.2f");
			ImGui::SliderFloat("Billboard Radius", &settings.BillboardRadius, kBillboardRadiusMin, kBillboardRadiusMax, "%.2f");
			ImGui::Checkbox("v0.8.7 Particle Lights Legacy", &settings.UseParticleLights087LegacyMode);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Restores the v0.8.7 particle-light alpha model.\n"
					"When enabled, brightness comes from material / shader / vertex alpha and RadiusMult affects radius only.\n"
					"When disabled, the current path uses RadiusMult for both intensity and radius.\n"
					"This is most noticeable on billboard-backed particle lights.");
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Placed Lights (JSON)")) {
			const bool jsonPlacedLightsSupported = globals::features::inverseSquareLighting.loaded;
			ImGui::BeginDisabled(!jsonPlacedLightsSupported);
			ImGui::SliderFloat("Intensity Scale", &settings.JsonPlacedLightIntensity, kJsonPlacedLightIntensityMin, kJsonPlacedLightIntensityMax, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Scales intensity for attached runtime lights generated from light records.\n"
					"This primarily targets Light Placer-style JSON lights.\n"
					"Requires Inverse Square Lighting runtime metadata to identify those lights.");
			}

			ImGui::Checkbox("Interiors Only", &settings.JsonPlacedLightsInteriorsOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only apply the intensity scale while in interiors.");
			}

			ImGui::Checkbox("Portal Strict Only", &settings.JsonPlacedLightsPortalStrictOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only apply the intensity scale to portal-strict lights.");
			}
			ImGui::EndDisabled();

			if (!jsonPlacedLightsSupported) {
				ImGui::TextDisabled("Requires Inverse Square Lighting to identify JSON-placed runtime lights.");
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Contact Shadows")) {
			ImGui::Checkbox("Enable Point Light Contact Shadows", &settings.EnableContactShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Adds short screen-space contact shadows to LLF point lights.\n"
					"Uses a cached per-cluster candidate list to limit the number of ray marches.");
			}

			ImGui::Checkbox("Interiors Only", &settings.ContactShadowsInteriorsOnly);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Only run LLF contact shadows in interior cells.");
			}

			const char* qualityOptions[] = { "Low", "Medium", "High" };
			int contactShadowQuality = static_cast<int>(settings.ContactShadowQuality);
			if (ImGui::Combo("Quality", &contactShadowQuality, qualityOptions, kContactShadowQualityOptionCount)) {
				settings.ContactShadowQuality = static_cast<uint>(std::clamp(contactShadowQuality, 0, static_cast<int>(kContactShadowQualityMax)));
			}

			int contactShadowClusterBudget = static_cast<int>(settings.ContactShadowClusterBudget);
			if (ImGui::SliderInt("Cached Lights per Cluster", &contactShadowClusterBudget, static_cast<int>(kContactShadowClusterBudgetMin), static_cast<int>(kContactShadowClusterBudgetMax))) {
				settings.ContactShadowClusterBudget = static_cast<uint>(contactShadowClusterBudget);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Maximum cached point and particle lights per cluster that can cast contact shadows. Set to 0 to disable clustered contact shadows.");
			}

			int strictContactShadowBudget = static_cast<int>(settings.StrictContactShadowBudget);
			if (ImGui::SliderInt("Strict Light Budget", &strictContactShadowBudget, 0, static_cast<int>(kStrictContactShadowBudgetMax))) {
				settings.StrictContactShadowBudget = static_cast<uint>(strictContactShadowBudget);
			}

			ImGui::Checkbox("Enable Particle Contact Shadows", &settings.EnableParticleContactShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Adds cheaper contact shadows for particle lights.\n"
					"Keep the particle budget low for fire, smoke, and magic-heavy scenes.");
			}

			int particleContactShadowBudget = static_cast<int>(settings.ParticleContactShadowBudget);
			if (ImGui::SliderInt("Particle Budget per Cluster", &particleContactShadowBudget, 0, static_cast<int>(kParticleContactShadowBudgetMax))) {
				settings.ParticleContactShadowBudget = static_cast<uint>(particleContactShadowBudget);
			}

			ImGui::Spacing();
			ImGui::TreePop();
		}
	}
	auto shaderCache = globals::shaderCache;

	if (ImGui::TreeNodeEx("Statistics")) {
		ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
		ImGui::Text(std::format(
			"Particle Lights Count : {}",
			particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed) +
				particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
				.c_str());
		ImGui::Text(std::format(
			"Particle Emitters / Billboards : {} / {}",
			particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed),
			particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
				.c_str());
		ImGui::Text(std::format(
			"Particle Cache Entries : {}",
			particleLightDiagnostics.cacheEntries.load(std::memory_order_relaxed))
				.c_str());

		ImGui::TreePop();
	}

	///////////////////////////////
	ImGui::SeparatorText("Debug");

	if (ImGui::TreeNode("Light Limit Visualization")) {
		ImGui::Checkbox("Enable Lights Visualisation", &settings.EnableLightsVisualisation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables visualization of the light limit\n");
		}

		{
			static const char* comboOptions[] = { "Light Limit", "Strict Lights Count", "Clustered Lights Count", "Shadow Mask" };
			ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, 4);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					" - Visualise the light limit. Red when the \"strict\" light limit is reached (portal-strict lights).\n"
					" - Visualise the number of strict lights.\n"
					" - Visualise the number of clustered lights.\n"
					" - Visualize the Shadow Mask.\n");
			}
		}
		currentEnableLightsVisualisation = settings.EnableLightsVisualisation;
		if (previousEnableLightsVisualisation != currentEnableLightsVisualisation) {
			globals::state->SetDefines(settings.EnableLightsVisualisation ? "LLFDEBUG" : "");
			shaderCache->Clear(RE::BSShader::Type::Lighting);
			previousEnableLightsVisualisation = currentEnableLightsVisualisation;
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void LightLimitFix::DrawPerformanceSettings(bool a_advanced)
{
	ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);

	ImGui::Checkbox("Enable Point Light Contact Shadows", &settings.EnableContactShadows);
	ImGui::Checkbox("Interiors Only", &settings.ContactShadowsInteriorsOnly);

	if (!a_advanced) {
		return;
	}

	ImGui::SeparatorText("Particle Lights");
	ImGui::Checkbox("Enable Culling", &settings.EnableParticleLightsCulling);
	ImGui::Checkbox("Enable Optimization", &settings.EnableParticleLightsOptimization);
	ImGui::SliderFloat("Cluster Threshold", &settings.ParticleClusterThreshold, kParticleClusterThresholdMin, kParticleClusterThresholdMax, "%.1f");
	ImGui::SliderInt("Max Particles per Emitter", &settings.MaxParticlesPerEmitter, kMaxParticlesPerEmitterMin, kMaxParticlesPerEmitterMax);
	ImGui::SliderFloat("Max Particle Distance", &settings.MaxParticleDistance, 1000.0f, kMaxParticleDistanceMax, "%.0f");
	ImGui::Checkbox("Enable Detection", &settings.EnableParticleLightsDetection);

	ImGui::SeparatorText("Contact Shadows");
	const char* qualityOptions[] = { "Low", "Medium", "High" };
	int contactShadowQuality = static_cast<int>(settings.ContactShadowQuality);
	if (ImGui::Combo("Contact Shadow Quality", &contactShadowQuality, qualityOptions, kContactShadowQualityOptionCount)) {
		settings.ContactShadowQuality = static_cast<uint>(std::clamp(contactShadowQuality, 0, static_cast<int>(kContactShadowQualityMax)));
	}

	int contactShadowClusterBudget = static_cast<int>(settings.ContactShadowClusterBudget);
	if (ImGui::SliderInt("Cached Lights per Cluster", &contactShadowClusterBudget, static_cast<int>(kContactShadowClusterBudgetMin), static_cast<int>(kContactShadowClusterBudgetMax))) {
		settings.ContactShadowClusterBudget = static_cast<uint>(contactShadowClusterBudget);
	}

	int strictContactShadowBudget = static_cast<int>(settings.StrictContactShadowBudget);
	if (ImGui::SliderInt("Strict Light Budget", &strictContactShadowBudget, 0, static_cast<int>(kStrictContactShadowBudgetMax))) {
		settings.StrictContactShadowBudget = static_cast<uint>(strictContactShadowBudget);
	}

	ImGui::Checkbox("Enable Particle Contact Shadows", &settings.EnableParticleContactShadows);
	int particleContactShadowBudget = static_cast<int>(settings.ParticleContactShadowBudget);
	if (ImGui::SliderInt("Particle Budget per Cluster", &particleContactShadowBudget, 0, static_cast<int>(kParticleContactShadowBudgetMax))) {
		settings.ParticleContactShadowBudget = static_cast<uint>(particleContactShadowBudget);
	}
}

void LightLimitFix::DrawEssentialSettings()
{
	ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
	ImGui::Checkbox("Enable Point Light Contact Shadows", &settings.EnableContactShadows);
	DrawHeatWarpStrengthSetting();
}

json LightLimitFix::CapturePerformanceSettingsState() const
{
	return settings;
}

void LightLimitFix::DrawOverlay()
{
	if (!settings.EnableLightsVisualisation)
		return;
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Always);
	ImGui::Begin("##LLFDebug", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	Util::Text::Error("DEBUG FEATURE - LIGHT LIMIT VISUALISATION ENABLED");

	if (ImGui::TreeNodeEx("Statistics")) {
		ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
		ImGui::Text(std::format(
			"Particle Lights Count : {}",
			particleLightDiagnostics.currentEmitters.load(std::memory_order_relaxed) +
				particleLightDiagnostics.currentBillboards.load(std::memory_order_relaxed))
				.c_str());
		ImGui::TreePop();
	}

	ImGui::End();
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.ContactShadowFlags = PackContactShadowFlags(settings);
	perFrame.ContactShadowParams = PackContactShadowParams(settings);
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	auto screenSize = globals::state->screenSize;
	if (REL::Module::IsVR())
		screenSize.x *= .5;
	clusterSize[0] = ((uint)screenSize.x + 63) / 64;
	clusterSize[1] = ((uint)screenSize.y + 63) / 64;
	clusterSize[2] = 32;
	uint clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != globals::d3d::device) {
		delete lightBuildingCB;
		lightBuildingCB = nullptr;
		delete lightCullingCB;
		lightCullingCB = nullptr;
		delete strictLightDataCB;
		strictLightDataCB = nullptr;
		clusterBuildingCS.Reset();
		clusterCullingCS.Reset();
		shaderDevice = globals::d3d::device;
	}

	{
		CompileComputeShaders();

		if (!lightBuildingCB)
			lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		if (!lightCullingCB)
			lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = clusterCount;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Clusters");
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexList");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightGrid");
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		contactShadowIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CONTACT_SHADOW_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		contactShadowIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowIndexList");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		contactShadowGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::ContactShadowGrid");
		srvDesc.Buffer.NumElements = numElements;
		contactShadowGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		contactShadowGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Lights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}

	{
		if (!strictLightDataCB)
			strictLightDataCB = new ConstantBuffer(ConstantBufferDesc<StrictLightDataCB>());
	}
}

void LightLimitFix::SetupRenderTargetResources()
{
	SetupResources();
}

void LightLimitFix::Reset()
{
	effectLightValidationCache.clear();

	{
		std::lock_guard<std::mutex> currentLock{ currentParticleLightsMutex };

		// NiPointer releases the retained emitter geometry and particle data.
		currentParticleEmitters.clear();
		currentBillboardLights.clear();

		{
			std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
			std::swap(currentParticleEmitters, queuedParticleEmitters);
			std::swap(currentBillboardLights, queuedBillboardLights);
			queuedEmitterIndices.clear();
			queuedBillboardIndices.clear();
			nextParticleLightSequence = 0;

			particleLightDiagnostics.currentEmitters.store(currentParticleEmitters.size(), std::memory_order_relaxed);
			particleLightDiagnostics.currentBillboards.store(currentBillboardLights.size(), std::memory_order_relaxed);
		}
	}

	const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
	PruneParticleLightCache(frame);
	jsonPlacedLightCache.clear();
}

void LightLimitFix::PruneParticleLightCache(std::uint32_t a_frame)
{
	if (a_frame - lastParticleLightCacheSweepFrame < kParticleLightCacheSweepInterval) {
		return;
	}
	lastParticleLightCacheSweepFrame = a_frame;

	const auto configVersion = globals::features::llf::particleLights.configVersion;
	std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
	for (auto it = particleLightsReferences.begin(); it != particleLightsReferences.end();) {
		const bool configChanged = it->second.reference.configVersion != configVersion;
		const bool entryExpired = a_frame - it->second.lastSeenFrame > kParticleLightCacheMaxIdleFrames;
		if (configChanged || entryExpired) {
			it = particleLightsReferences.erase(it);
		} else {
			++it;
		}
	}

	particleLightDiagnostics.cacheEntries.store(particleLightsReferences.size(), std::memory_order_relaxed);
}

void LightLimitFix::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void LightLimitFix::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings(settings);
}

RE::NiNode* GetParentRoomNode(RE::NiAVObject* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	static const auto* roomRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSMultiBoundRoom }.get();
	static const auto* portalRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSPortalSharedNode }.get();

	const auto* rtti = object->GetRTTI();
	if (rtti == roomRtti || rtti == portalRtti) {
		return static_cast<RE::NiNode*>(object);
	}

	return GetParentRoomNode(object->parent);
}

void LightLimitFix::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	ClearStrictLightData(strictLightDataTemp, true);

	if (!a_pass || !a_pass->geometry) {
		return;
	}
	if (!roomNodes.empty()) {
		if (RE::NiNode* roomNode = GetParentRoomNode(a_pass->geometry)) {
			if (auto it = roomNodes.find(roomNode); it != roomNodes.cend()) {
				strictLightDataTemp.RoomIndex = it->second;
			}
		}
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->sceneLights) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	auto smState = globals::game::smState;
	if (!smState) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	auto& isl = globals::features::inverseSquareLighting;

	auto accumulator = *globals::game::currentAccumulator.get();
	if (!accumulator) {
		ClearStrictLightData(strictLightDataTemp, false);
		return;
	}

	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == smState->shadowSceneNode[0];
	const bool isInterior = LocationContext::Get().inInterior;

	constexpr uint32_t kStrictLightCapacity = 15;
	const uint32_t availableSceneLights = a_pass->numLights > 0 ? (a_pass->numLights - 1) : 0;
	const uint32_t requestedStrictLights = inWorld ? 0u : availableSceneLights;
	const uint32_t strictLightCount = std::min(requestedStrictLights, kStrictLightCapacity);
	const uint32_t shadowLightCount = std::min(static_cast<uint32_t>(a_pass->numShadowLights), availableSceneLights);
	RefreshJsonPlacedLightCacheFrame();

	ClearStrictLightData(strictLightDataTemp, false);

	uint32_t outIndex = 0;
#if defined(_MSC_VER)
	__try
#endif
	{
		for (uint32_t i = 0; i < strictLightCount; i++) {
			auto bsLight = a_pass->sceneLights[i + 1];
			if (!bsLight) {
				continue;
			}
			auto niLight = bsLight->light.get();
			if (!niLight) {
				continue;
			}

			auto& runtimeData = niLight->GetLightRuntimeData();

			LightData light{};
			light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
			light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

			if (isl.loaded) {
				isl.ProcessLight(light, bsLight, niLight);
			} else {
				light.radius = runtimeData.radius.x;
				// light.color *= runtimeData.fade;
				light.fade = runtimeData.fade;
			}

			SetEngineLightFlags(light, bsLight);
			light.fade *= bsLight->lodDimmer;
			const bool isPortalStrict = !IsGlobalLight(bsLight);
			ApplyJsonPlacedLightIntensityScale(light, bsLight, niLight, isPortalStrict, isInterior);

			SetLightPosition(light, niLight->world.translate, inWorld);

			if (i < shadowLightCount && bsLight->IsShadowLight()) {
				auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
				GET_INSTANCE_MEMBER(maskIndex, shadowLight);
				if (maskIndex < 32) {
					light.shadowMaskIndex = maskIndex;
					light.lightFlags.set(LightFlags::Shadow);
				}
			}

			strictLightDataTemp.StrictLights[outIndex++] = light;
		}
		strictLightDataTemp.NumStrictLights = outIndex;

		for (uint32_t i = 0; i < shadowLightCount; i++) {
			auto bsLight = a_pass->sceneLights[i + 1];
			if (!bsLight || !bsLight->IsShadowLight()) {
				continue;
			}
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			GET_INSTANCE_MEMBER(maskIndex, shadowLight);
			if (maskIndex < 32) {
				strictLightDataTemp.ShadowBitMask |= (1u << maskIndex);
			}
		}
	}
#if defined(_MSC_VER)
	__except (1) {
		ClearStrictLightData(strictLightDataTemp, false);
	}
#endif
}

void LightLimitFix::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	if (!shaderCache->IsEnabled())
		return;

	if (!smState || !strictLightDataCB) {
		return;
	}

	auto accumulator = *globals::game::currentAccumulator.get();
	if (!accumulator) {
		return;
	}

	auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == shadowSceneNode;
	const auto roomIndex = strictLightDataTemp.RoomIndex;
	const auto shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex || shadowBitMask != previousShadowBitMask) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
		previousShadowBitMask = shadowBitMask;
	}

	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffer = { strictLightDataCB->CB() };
		context->PSSetConstantBuffers(3, 1, &buffer);
	}
}

void LightLimitFix::SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		RE::NiPoint3 eyePosition;

		if (a_cached) {
			eyePosition = eyePositionCached[eyeIndex];
		} else {
			eyePosition = Util::GetEyePosition(eyeIndex);
		}

		auto worldPos = a_initialPosition - eyePosition;
		a_light.positionWS[eyeIndex].data.x = worldPos.x;
		a_light.positionWS[eyeIndex].data.y = worldPos.y;
		a_light.positionWS[eyeIndex].data.z = worldPos.z;
	}
}

void LightLimitFix::RefreshJsonPlacedLightCacheFrame()
{
	if (jsonPlacedLightCacheFrameChecker.IsNewFrame()) {
		jsonPlacedLightCache.clear();
	}
}

bool LightLimitFix::IsJsonPlacedLight(RE::BSLight* a_bsLight, RE::NiLight* a_niLight)
{
	if (!a_bsLight || !a_niLight || !a_bsLight->pointLight) {
		return false;
	}
	if (!globals::features::inverseSquareLighting.loaded) {
		return false;
	}
	if (const auto it = jsonPlacedLightCache.find(a_niLight); it != jsonPlacedLightCache.end()) {
		return it->second;
	}

	bool isJsonPlacedLight = false;
	const auto ownerRef = a_niLight->GetUserData();
	if (ownerRef) {
		if (const auto ownerBase = ownerRef->GetObjectReference(); ownerBase && ownerBase->GetFormType() != RE::FormType::Light) {
			const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(a_niLight);
			if (runtimeData && runtimeData->lighFormId != 0) {
				const auto lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId);
				isJsonPlacedLight = lighForm && lighForm->GetFormType() == RE::FormType::Light;
			}
		}
	}

	jsonPlacedLightCache.insert_or_assign(a_niLight, isJsonPlacedLight);
	return isJsonPlacedLight;
}

void LightLimitFix::ApplyJsonPlacedLightIntensityScale(
	LightData& a_light,
	RE::BSLight* a_bsLight,
	RE::NiLight* a_niLight,
	bool a_isPortalStrict,
	bool a_isInterior)
{
	if (std::abs(settings.JsonPlacedLightIntensity - 1.0f) <= 1e-4f) {
		return;
	}
	if (!LocationContext::AllowsInteriorOnly(settings.JsonPlacedLightsInteriorsOnly, a_isInterior)) {
		return;
	}
	if (settings.JsonPlacedLightsPortalStrictOnly && !a_isPortalStrict) {
		return;
	}
	if (!IsJsonPlacedLight(a_bsLight, a_niLight)) {
		return;
	}

	a_light.fade *= settings.JsonPlacedLightIntensity;
}

float LightLimitFix::CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point)
{
	// See BSLight::CalculateLuminance_14131D3D0
	// Performs lighting on the CPU which is identical to GPU code

	auto lightDirection = light.position - point;
	float lightDist = lightDirection.Length();
	float intensityFactor = std::clamp(lightDist / light.radius, 0.0f, 1.0f);
	float intensityMultiplier = 1 - intensityFactor * intensityFactor;

	return light.grey * intensityMultiplier;
}

void LightLimitFix::AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel)
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	std::shared_lock<std::shared_mutex> lk{ cachedParticleLightsMutex };
	int particleLightsDetectionHits = 0;
	if (settings.EnableParticleLightsDetection) {
		for (auto& light : cachedParticleLights) {
			auto luminance = CalculateLuminance(light, targetPosition);
			lightLevel += luminance;
			if (luminance > 0.0)
				particleLightsDetectionHits++;
		}
	}
	numHits += particleLightsDetectionHits;
}

void LightLimitFix::Prepass()
{
	auto context = globals::d3d::context;
	auto state = globals::state;
	if (!context || !state || !lights || !lightIndexList || !lightGrid ||
		!contactShadowIndexList || !contactShadowGrid ||
		!lights->srv || !lightIndexList->srv || !lightGrid->srv ||
		!contactShadowIndexList->srv || !contactShadowGrid->srv) {
		return;
	}

	CS_GPU_PASS("LightLimitFix::Prepass");
	UpdateLights();

	ID3D11ShaderResourceView* views[5]{};
	views[0] = lights->srv.get();
	views[1] = lightIndexList->srv.get();
	views[2] = lightGrid->srv.get();
	views[3] = contactShadowIndexList->srv.get();
	views[4] = contactShadowGrid->srv.get();
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);
}

bool LightLimitFix::IsValidLight(RE::BSLight* a_light)
{
	return a_light && a_light->light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool LightLimitFix::IsGlobalLight(RE::BSLight* a_light)
{
	return a_light && !(a_light->portalStrict || !a_light->portalGraph);
}

struct VertexColor
{
	std::uint8_t data[4];
};

struct VertexPosition
{
	std::uint8_t data[3];
};

bool TryGetMaxAlphaVertexColor(const std::uint8_t* a_rawVertexData, std::uint32_t a_vertexSize, std::uint32_t a_colorOffset, std::uint32_t a_vertexCount, VertexColor& a_outVertexColor)
{
	if (!a_rawVertexData || a_vertexSize < sizeof(VertexColor) || a_vertexCount == 0) {
		return false;
	}
	if (a_colorOffset > (a_vertexSize - sizeof(VertexColor))) {
		return false;
	}

	std::uint8_t maxAlpha = 0;
	bool found = false;
	VertexColor bestColor{};

#if defined(_MSC_VER)
	__try
#endif
	{
		for (std::uint32_t v = 0; v < a_vertexCount; ++v) {
			const std::size_t byteOffset = static_cast<std::size_t>(a_vertexSize) * static_cast<std::size_t>(v) + static_cast<std::size_t>(a_colorOffset);
			const auto* vertex = reinterpret_cast<const VertexColor*>(a_rawVertexData + byteOffset);
			const std::uint8_t alpha = vertex->data[3];
			if (alpha > maxAlpha) {
				maxAlpha = alpha;
				bestColor = *vertex;
				found = true;
			}
		}
	}
#if defined(_MSC_VER)
	__except (1) {
		return false;
	}
#endif

	if (found) {
		a_outVertexColor = bestColor;
	}
	return found;
}

void ResolveBillboardTint(
	LightLimitFix::ParticleLightReference& a_reference,
	RE::BSGeometry* a_geometry,
	RE::BSEffectShaderMaterial* a_material,
	RE::BSEffectShaderProperty* a_shaderProperty)
{
	a_reference.applyEffectMaterialTint = true;
	a_reference.baseColor = { 1, 1, 1, 1 };

	bool hasVertexTint = false;
	if (auto rendererData = a_geometry->GetGeometryRuntimeData().rendererData) {
		if (auto triShape = a_geometry->AsTriShape()) {
			const std::uint32_t vertexSize = rendererData->vertexDesc.GetSize();
			if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS) && rendererData->rawVertexData && vertexSize > 0u) {
				const std::uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);
				const std::uint32_t vertexCount = static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount);

				VertexColor maxAlphaVertexColor{};
				if (TryGetMaxAlphaVertexColor(rendererData->rawVertexData, vertexSize, offset, vertexCount, maxAlphaVertexColor)) {
					a_reference.baseColor.red *= maxAlphaVertexColor.data[0] / 255.f;
					a_reference.baseColor.green *= maxAlphaVertexColor.data[1] / 255.f;
					a_reference.baseColor.blue *= maxAlphaVertexColor.data[2] / 255.f;
					hasVertexTint = true;
					if (a_shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)) {
						a_reference.baseColor.alpha *= maxAlphaVertexColor.data[3] / 255.f;
					}
				}
			}
		}
	}

	RE::NiColorA siblingEmissiveTint{};
	bool hasSiblingEmissiveTint = false;
	const bool vertexTintLooksWhite = hasVertexTint && IsNearWhiteTint(a_reference.baseColor);
	if (!hasVertexTint || vertexTintLooksWhite) {
		hasSiblingEmissiveTint = TryGetBillboardSiblingEmissiveTint(a_geometry, siblingEmissiveTint);
		const bool siblingTintIsNonWhite = hasSiblingEmissiveTint && !IsNearWhiteTint(siblingEmissiveTint);

		const RE::NiColorA materialEmissiveTint = BuildEffectMaterialEmissiveTint(a_material, a_shaderProperty);
		const float materialEmissiveLuma = GetEmissiveTintLuma(materialEmissiveTint);
		const bool hasMaterialEmissiveTint = materialEmissiveLuma > 1e-4f;
		const bool materialTintIsNonWhite = hasMaterialEmissiveTint && !IsNearWhiteTint(materialEmissiveTint);

		// Resolve fallback from a single source to avoid mixing color from one source
		// with emissive energy from another. Prefer the billboard's own effect material
		// when it already provides a non-white tint, then fall back to sibling tint.
		if (materialTintIsNonWhite) {
			a_reference.baseColor = materialEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (siblingTintIsNonWhite) {
			a_reference.baseColor = siblingEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (hasMaterialEmissiveTint) {
			a_reference.baseColor = materialEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else if (hasSiblingEmissiveTint) {
			a_reference.baseColor = siblingEmissiveTint;
			a_reference.applyEffectMaterialTint = false;
		} else {
			a_reference.baseColor = BuildBillboardFallbackTint(
				a_reference.config,
				a_reference.hasGradientConfig,
				a_reference.gradientConfig);
			a_reference.applyEffectMaterialTint = true;
		}
	}
}

LightLimitFix::ParticleLightReference LightLimitFix::GetParticleLightConfigs(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return {};
	}

	if (!settings.EnableParticleLights) {
		return {};
	}

	auto& particleLights = globals::features::llf::particleLights;
	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty || shaderProperty->lightData) {
		return {};
	}

	auto material = shaderProperty->GetMaterial();
	if (!material) {
		return {};
	}

	const bool billboard = a_pass->geometry->GetRTTI() != globals::rtti::NiParticleSystemRTTI.get();
	if (billboard) {
		auto parent = a_pass->geometry->parent;
		if (!parent || parent->GetRTTI() != globals::rtti::NiBillboardNodeRTTI.get()) {
			return {};
		}
	}

	auto* node = a_pass->geometry;
	const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
	const ParticleLightKey key{ node, shaderProperty };

	ParticleLightCacheSignature signature{};
	signature.material = material;
	signature.billboard = billboard;
	if (billboard) {
		signature.rendererData = node->GetGeometryRuntimeData().rendererData;
		if (signature.rendererData) {
			signature.rawVertexData = signature.rendererData->rawVertexData;
		}
	}

	std::optional<ParticleLightReference> cachedReference;
	{
		std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
		auto it = particleLightsReferences.find(key);
		if (it != particleLightsReferences.end() &&
			it->second.reference.configVersion == particleLights.configVersion &&
			it->second.signature == signature &&
			it->second.sourceTexturePath == material->sourceTexturePath &&
			it->second.gradientTexturePath == material->greyscaleTexturePath) {
			it->second.lastSeenFrame = frame;
			if (!it->second.reference.valid || !billboard || it->second.tintResolvedFrame == frame) {
				return it->second.reference;
			}
			cachedReference = it->second.reference;
		} else if (it != particleLightsReferences.end()) {
			particleLightsReferences.erase(it);
		}
	}

	auto cacheReference = [&](ParticleLightReference a_reference) {
		ParticleLightCacheEntry entry{};
		entry.reference = a_reference;
		entry.signature = signature;
		// Retain the interned texture names so their identity cannot be recycled
		// while a stale geometry key remains in the bounded cache.
		entry.sourceTexturePath = material->sourceTexturePath;
		entry.gradientTexturePath = material->greyscaleTexturePath;
		entry.lastSeenFrame = frame;
		entry.tintResolvedFrame = billboard ? frame : 0;

		std::lock_guard<std::mutex> cacheLock{ particleLightsCacheMutex };
		if (auto it = particleLightsReferences.find(key); it != particleLightsReferences.end()) {
			it->second = std::move(entry);
		} else if (particleLightsReferences.size() < kParticleLightCacheMaxEntries) {
			particleLightsReferences.emplace(key, std::move(entry));
			particleLightDiagnostics.cacheEntries.store(particleLightsReferences.size(), std::memory_order_relaxed);
		}
		return a_reference;
	};

	if (cachedReference) {
		auto reference = *cachedReference;
		ResolveBillboardTint(reference, node, material, shaderProperty);
		return cacheReference(reference);
	}

	auto cacheInvalidReference = [&]() {
		ParticleLightReference invalidReference{};
		invalidReference.configVersion = particleLights.configVersion;
		return cacheReference(invalidReference);
	};

	// See https://www.nexusmods.com/skyrimspecialedition/articles/1391.
	if (material->sourceTexturePath.empty()) {
		return cacheInvalidReference();
	}

	auto textureName = Util::GetLowercaseStem(material->sourceTexturePath.c_str(), ".dds");
	if (!textureName) {
		return cacheInvalidReference();
	}

	auto configIt = particleLights.particleLightConfigs.find(*textureName);
	if (configIt == particleLights.particleLightConfigs.end()) {
		return cacheInvalidReference();
	}

	ParticleLightReference reference{};
	reference.valid = true;
	reference.billboard = billboard;
	reference.config = configIt->second;
	reference.configVersion = particleLights.configVersion;

	if (!material->greyscaleTexturePath.empty()) {
		const auto gradientName = Util::GetLowercaseStem(material->greyscaleTexturePath.c_str(), ".dds");
		if (!gradientName) {
			return cacheInvalidReference();
		}

		auto gradientIt = particleLights.particleLightGradientConfigs.find(*gradientName);
		if (gradientIt == particleLights.particleLightGradientConfigs.end()) {
			return cacheInvalidReference();
		}
		reference.hasGradientConfig = true;
		reference.gradientConfig = gradientIt->second;
	}

	if (billboard) {
		ResolveBillboardTint(reference, node, material, shaderProperty);
	}
	return cacheReference(reference);
}

bool LightLimitFix::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return true;
	}

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return true;

	auto reference = GetParticleLightConfigs(a_pass);
	if (reference.valid) {
		if (AddParticleLight(a_pass, reference)) {
			return !(settings.EnableParticleLightsCulling && reference.config.cull);
		}
	}
	return true;
}

bool LightLimitFix::AddParticleLight(RE::BSRenderPass* a_pass, const ParticleLightReference& a_reference)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty) {
		return false;
	}

	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty) {
		return false;
	}

	auto material = shaderProperty->GetMaterial();
	if (!material) {
		return false;
	}
	const auto& config = a_reference.config;
	const ParticleLightKey key{ a_pass->geometry, shaderProperty };

	RE::NiColorA color = a_reference.baseColor;
	if (a_reference.applyEffectMaterialTint) {
		color.red *= material->baseColor.red * material->baseColorScale;
		color.green *= material->baseColor.green * material->baseColorScale;
		color.blue *= material->baseColor.blue * material->baseColorScale;

		if (auto emittance = shaderProperty->emittanceColor) {
			color.red *= emittance->red;
			color.green *= emittance->green;
			color.blue *= emittance->blue;
		}
	}

	if (a_reference.hasGradientConfig) {
		auto grey = float3(config.colorMult.red, config.colorMult.green, config.colorMult.blue).Dot(float3(0.3f, 0.59f, 0.11f));
		color.red *= grey * a_reference.gradientConfig.color.red;
		color.green *= grey * a_reference.gradientConfig.color.green;
		color.blue *= grey * a_reference.gradientConfig.color.blue;
	} else {
		color.red *= config.colorMult.red;
		color.green *= config.colorMult.green;
		color.blue *= config.colorMult.blue;
	}

	const float sourceAlpha = std::max(color.alpha * material->baseColor.alpha * shaderProperty->alpha, 0.0f);
	if (settings.UseParticleLights087LegacyMode) {
		color.alpha = sourceAlpha;
	} else {
		// Keep particle light energy stable and config-driven (1.4.6-style behavior).
		color.alpha = std::max(config.radiusMult, 0.0f);
	}

	if (a_reference.billboard) {
		ResolvedBillboardLight resolved{};
		resolved.position = a_pass->geometry->world.translate;
		resolved.radius = a_pass->geometry->worldBound.radius * config.radiusMult * settings.BillboardRadius * 0.5f;
		resolved.color = Saturation(
			float3{ color.red, color.green, color.blue },
			ResolveParticleSaturation(settings.ParticleLightsSaturation, config.saturationMult));
		resolved.color *= color.alpha * settings.BillboardBrightness;
		resolved.flickerSeed = static_cast<std::uint32_t>(std::hash<void*>{}(a_pass->geometry));
		resolved.flicker = config.flicker;
		resolved.flickerSpeed = config.flickerSpeed;
		resolved.flickerIntensity = config.flickerIntensity;
		resolved.flickerMovement = config.flickerMovement;

		std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
		if (auto it = queuedBillboardIndices.find(key); it != queuedBillboardIndices.end()) {
			if (it->second < queuedBillboardLights.size()) {
				resolved.sequence = queuedBillboardLights[it->second].sequence;
				queuedBillboardLights[it->second] = resolved;
				return true;
			}
			queuedBillboardIndices.erase(it);
		}

		if (queuedParticleEmitters.size() + queuedBillboardLights.size() >= kMaxQueuedParticleLights) {
			return false;
		}

		resolved.sequence = nextParticleLightSequence++;
		const std::size_t billboardIndex = queuedBillboardLights.size();
		queuedBillboardLights.push_back(resolved);
		queuedBillboardIndices[key] = billboardIndex;
		return true;
	}

	auto* particleSystem = static_cast<RE::NiParticleSystem*>(a_pass->geometry);
	auto* particleData = particleSystem->GetParticlesRuntimeData().particleData.get();
	auto updateEmitter = [&](ParticleEmitterLight& a_emitter) {
		a_emitter.particleData.reset(particleData);
		a_emitter.color = color;
		a_emitter.radiusMult = config.radiusMult;
		a_emitter.saturationMult = config.saturationMult;
	};

	std::lock_guard<std::mutex> queueLock{ particleLightsQueueMutex };
	if (auto it = queuedEmitterIndices.find(key); it != queuedEmitterIndices.end()) {
		if (it->second < queuedParticleEmitters.size()) {
			auto& emitter = queuedParticleEmitters[it->second];
			updateEmitter(emitter);
			return true;
		}
		queuedEmitterIndices.erase(it);
	}

	if (queuedParticleEmitters.size() + queuedBillboardLights.size() >= kMaxQueuedParticleLights) {
		return false;
	}

	const std::size_t emitterIndex = queuedParticleEmitters.size();
	auto& emitter = queuedParticleEmitters.emplace_back();
	emitter.node.reset(particleSystem);
	updateEmitter(emitter);
	emitter.sequence = nextParticleLightSequence++;
	queuedEmitterIndices[key] = emitterIndex;
	return true;
}

void LightLimitFix::Hooks::InstallAlphaGeometryGroupGuard()
{
	// Decode the counter from ClearAlphaGeometryGroups so the guard stays portable across runtimes.
	constexpr std::uint8_t kMovDwordImmediateOpcode = 0xC7;
	constexpr std::uint8_t kRipRelativeModRM = 0x05;
	constexpr std::uint8_t kReturnOpcode = 0xC3;
	constexpr std::size_t kMovDwordImmediateSize = 10;

	const auto clearFunction = REL::RelocationID(100856, 107646).address();
	const auto* code = reinterpret_cast<const std::uint8_t*>(clearFunction);
	std::uint32_t immediate = 1;
	std::int32_t displacement = 0;
	if (clearFunction) {
		std::memcpy(&displacement, code + 2, sizeof(displacement));
		std::memcpy(&immediate, code + 6, sizeof(immediate));
	}

	const bool shapeMatches = clearFunction && code[0] == kMovDwordImmediateOpcode &&
	                          code[1] == kRipRelativeModRM && immediate == 0 &&
	                          code[kMovDwordImmediateSize] == kReturnOpcode;
	const auto counterAddress =
		shapeMatches ? clearFunction + kMovDwordImmediateSize + displacement : 0;
	const auto dataSegment = REL::Module::get().segment(REL::Segment::data);
	if (counterAddress < dataSegment.address() ||
		counterAddress >= dataSegment.address() + dataSegment.size()) {
		alphaGeometryGroupCount = nullptr;
		logger::error(
			"[LLF] Alpha GeometryGroup guard not installed: "
			"ClearAlphaGeometryGroups did not decode to a counter in .data");
		return;
	}

	alphaGeometryGroupCount = reinterpret_cast<std::uint32_t*>(counterAddress);
	alphaGeometryGroupLimit =
		(globals::game::isVR ? kAlphaGeometryGroupCapacityVR : kAlphaGeometryGroupCapacityFlat) -
		kAlphaGeometryGroupReserve;
	if (const auto result = stl::detour_thunk<StartGroupingAlphas>(REL::RelocationID(100874, 107670));
		result != 0) {
		alphaGeometryGroupCount = nullptr;
		logger::error("[LLF] Failed to install StartGroupingAlphas guard ({})", result);
		return;
	}

	logger::info(
		"[LLF] Installed alpha GeometryGroup guard (limit {}, reserve {})",
		alphaGeometryGroupLimit,
		kAlphaGeometryGroupReserve);
}

void LightLimitFix::Hooks::InstallVRNonShadowCasterLightFlagsGuard()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	// CalculateActiveNonShadowCasterLights reloads BSLight::light into RAX,
	// then tests NiAVObject::flags without checking the refreshed pointer. The
	// earlier ValidLight2 hook cannot close that time-of-check/time-of-use gap.
	constexpr std::uint8_t expectedInstruction[] = { 0xF6, 0x80, 0x0C, 0x01, 0x00, 0x00, 0x01 };
	const auto target = REL::RelocationID(100997, 107784).address() + 0x1F0;
	if (!MatchesInstructions(target, expectedInstruction)) {
		logger::error("[LLF] VR non-shadow caster light flags guard not installed: unexpected instruction at 0x{:x}", target);
		return;
	}

	VRNonShadowCasterLightFlagsGuard code;
	code.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto guard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
	trampoline.write_call<5>(target, guard);
	REL::safe_fill(target + 5, REL::NOP, sizeof(expectedInstruction) - 5);

	logger::info("[LLF] Installed VR non-shadow caster light flags guard");
}

void LightLimitFix::Hooks::InstallVRSceneGraphCullingObjectGuard()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::error("[LLF] VR scene-graph culling-object guard not installed: unsupported Skyrim VR runtime {}", REL::Module::get().version().string());
		return;
	}

	// Skyrim VR 1.4.15 can retain a readable NiNode child after teardown has
	// cleared its vtable. The scene-culling helper then reads that stale object
	// and dispatches through slot 0x1A8 of the null vtable. The helper entry,
	// virtual-call context, internal tail-entry context, and void epilogue were verified
	// against the live, decrypted runtime after the same Windhelm-to-Dragonsreach
	// crash reproduced both before and after the room-light/effect-shader guards.
	constexpr std::uintptr_t helperEntryRVA = 0xCBFC60;
	constexpr std::uintptr_t virtualCallContextRVA = 0xCBFD15;
	constexpr std::uintptr_t helperEpilogueRVA = 0xCBFD52;
	constexpr std::uintptr_t helperTailContextRVA = 0xCBFDB2;
	constexpr std::size_t patchedInstructionSize = 5;
	constexpr std::uint8_t expectedHelperEntry[] = {
		0x48, 0x89, 0x5C, 0x24, 0x10,
		0x57,
		0x48, 0x83, 0xEC, 0x20,
		0x83, 0xBA, 0xF0, 0x00, 0x00, 0x00, 0x00,
		0x48, 0x8B, 0xFA,
		0x48, 0x8B, 0xD9
	};
	constexpr std::uint8_t expectedVirtualCallContext[] = {
		0x41, 0x83, 0xF9, 0x06,
		0x75, 0x24,
		0x48, 0x8B, 0x07,
		0x48, 0x8B, 0xD3,
		0x48, 0x8B, 0xCF,
		0xFF, 0x90, 0xA8, 0x01, 0x00, 0x00
	};
	constexpr std::uint8_t expectedHelperEpilogue[] = {
		0x89, 0xB3, 0x9C, 0x00, 0x00, 0x00,
		0x48, 0x8B, 0x74, 0x24, 0x30,
		0x48, 0x8B, 0x5C, 0x24, 0x38,
		0x48, 0x83, 0xC4, 0x20,
		0x5F,
		0xC3
	};
	constexpr std::uint8_t expectedHelperTailContext[] = {
		0x83, 0xB9, 0x9C, 0x00, 0x00, 0x00, 0x00,
		0x74, 0xDC,
		0x41, 0x83, 0xC8, 0xFF,
		0xE9, 0x9C, 0xFE, 0xFF, 0xFF
	};
	constexpr std::uint8_t expectedVirtualCallPrefix[] = {
		0x41, 0x83, 0xF9, 0x06,
		0x75, 0x24,
		0x48, 0x8B, 0x07,
		0x48, 0x8B, 0xD3,
		0x48, 0x8B, 0xCF
	};

	const auto moduleBase = REL::Module::get().base();
	const auto helperEntry = moduleBase + helperEntryRVA;
	const auto virtualCallContext = moduleBase + virtualCallContextRVA;
	const auto helperEpilogue = moduleBase + helperEpilogueRVA;
	const auto helperTailContext = moduleBase + helperTailContextRVA;
	const auto helperSignaturesMatch =
		MatchesInstructions(helperEntry, expectedHelperEntry) &&
		MatchesInstructions(helperEpilogue, expectedHelperEpilogue) &&
		MatchesInstructions(helperTailContext, expectedHelperTailContext);
	const auto guardDecision = LightLimitFixVRHookPolicy::DecideSceneGraphGuard(
		helperSignaturesMatch,
		MatchesInstructions(virtualCallContext, expectedVirtualCallContext),
		IsEngineFixesLoaded(),
		MatchesInstructions(virtualCallContext, expectedVirtualCallPrefix),
		LightLimitFixVRHookPolicy::HasExternalBranchPrefix(
			reinterpret_cast<const std::uint8_t*>(virtualCallContext + std::size(expectedVirtualCallPrefix)),
			2));
	if (guardDecision == LightLimitFixVRHookPolicy::SceneGraphGuardDecision::kRejectUnknownSite) {
		logger::error("[LLF] VR scene-graph culling-object guard not installed: unexpected SkyrimVR.exe instructions");
		return;
	}
	if (guardDecision == LightLimitFixVRHookPolicy::SceneGraphGuardDecision::kSkipCompatibleExternalGuard) {
		logger::info("[LLF] Engine Fixes VR culling freed-object guard detected; skipping duplicate scene-graph guard");
		return;
	}

	VRSceneGraphCullingObjectGuard code{ helperEntry + patchedInstructionSize };
	code.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto guard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
	trampoline.write_branch<patchedInstructionSize>(helperEntry, guard);

	logger::info("[LLF] Installed VR scene-graph culling-object guard");
}

void LightLimitFix::Hooks::InstallVRShadowMapCameraGuard()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::error("[LLF] VR shadow-map camera guard not installed: unsupported Skyrim VR runtime {}", REL::Module::get().version().string());
		return;
	}

	// Skyrim VR 1.4.15's shared shadow-map helper trusts
	// ShadowmapDescriptorVR::camera[0] and NiCamera::viewFrustumArray. Reproduced
	// Windhelm-to-Dragonsreach transitions supplied both stale camera storage and a
	// valid camera whose late-use frustum array had become null, then crashed at
	// SkyrimVR.exe+134C61A. The entry, late-use context, and epilogue were verified
	// against the live, decrypted runtime image.
	constexpr std::uintptr_t helperEntryRVA = 0x134C370;
	constexpr std::uintptr_t cameraUseContextRVA = 0x134C5F9;
	constexpr std::uintptr_t cameraLateFrustumLoadRVA = 0x134C613;
	constexpr std::uintptr_t helperEpilogueRVA = 0x134C99E;
	constexpr std::size_t patchedInstructionSize = 6;
	constexpr std::size_t lateFrustumLoadInstructionSize = 7;
	constexpr std::uint8_t expectedHelperEntry[] = {
		0x48, 0x8B, 0xC4,
		0x55,
		0x41, 0x54,
		0x41, 0x55,
		0x41, 0x56,
		0x41, 0x57,
		0x48, 0x8D, 0xA8, 0xE8, 0xFE, 0xFF, 0xFF,
		0x48, 0x81, 0xEC, 0xF0, 0x01, 0x00, 0x00
	};
	constexpr std::uint8_t expectedCameraUseContext[] = {
		0x48, 0x8B, 0x7B, 0x40,
		0xC7, 0x45, 0x98, 0x00, 0x00, 0x00, 0x00,
		0xC7, 0x45, 0x9C, 0x00, 0x00, 0x80, 0x3F,
		0x48, 0xC7, 0x45, 0xA0, 0x00, 0x00, 0x80, 0x3F,
		0x48, 0x8B, 0x87, 0x80, 0x01, 0x00, 0x00,
		0x0F, 0x10, 0x00
	};
	constexpr std::uint8_t expectedLateFrustumLoad[] = {
		0x48, 0x8B, 0x87, 0x80, 0x01, 0x00, 0x00
	};
	constexpr std::uint8_t expectedHelperEpilogue[] = {
		0x4C, 0x8D, 0x9C, 0x24, 0xF0, 0x01, 0x00, 0x00,
		0x49, 0x8B, 0x5B, 0x38,
		0x49, 0x8B, 0x73, 0x40,
		0x49, 0x8B, 0x7B, 0x48,
		0x41, 0x0F, 0x28, 0x73, 0xF0,
		0x41, 0x0F, 0x28, 0x7B, 0xE0,
		0x45, 0x0F, 0x28, 0x43, 0xD0,
		0x45, 0x0F, 0x28, 0x4B, 0xC0,
		0x45, 0x0F, 0x28, 0x53, 0xB0,
		0x45, 0x0F, 0x28, 0x5B, 0xA0,
		0x45, 0x0F, 0x28, 0x63, 0x90,
		0x49, 0x8B, 0xE3,
		0x41, 0x5F,
		0x41, 0x5E,
		0x41, 0x5D,
		0x41, 0x5C,
		0x5D,
		0xC3
	};

	const auto moduleBase = REL::Module::get().base();
	const auto helperEntry = moduleBase + helperEntryRVA;
	const auto cameraUseContext = moduleBase + cameraUseContextRVA;
	const auto cameraLateFrustumLoad = moduleBase + cameraLateFrustumLoadRVA;
	const auto helperEpilogue = moduleBase + helperEpilogueRVA;
	if (!MatchesInstructions(helperEntry, expectedHelperEntry) ||
		!MatchesInstructions(cameraUseContext, expectedCameraUseContext) ||
		!MatchesInstructions(cameraLateFrustumLoad, expectedLateFrustumLoad) ||
		!MatchesInstructions(helperEpilogue, expectedHelperEpilogue) ||
		!LightLimitFixVRHookPolicy::HasExpectedShadowMapXmmRestorePrefixes(expectedHelperEpilogue, std::size(expectedHelperEpilogue))) {
		logger::error("[LLF] VR shadow-map camera guard not installed: unexpected SkyrimVR.exe instructions");
		return;
	}

	const auto readOnlySegment = REL::Module::get().segment(REL::Segment::rdata);
	constexpr auto requiredVtableBytes = sizeof(std::uintptr_t);
	if (readOnlySegment.address() == 0 || readOnlySegment.size() < requiredVtableBytes) {
		logger::error("[LLF] VR shadow-map camera guard not installed: SkyrimVR.exe segment bounds unavailable");
		return;
	}

	const auto readOnlyStart = readOnlySegment.address();
	const auto readOnlyLastVtable = readOnlyStart + readOnlySegment.size() - requiredVtableBytes;
	std::array<std::uintptr_t, kVRShadowCameraVtableRVAs.size()> allowedCameraVtables{};
	for (std::size_t i = 0; i < kVRShadowCameraVtableRVAs.size(); ++i) {
		const auto vtable = moduleBase + kVRShadowCameraVtableRVAs[i];
		if (vtable < readOnlyStart || vtable > readOnlyLastVtable) {
			logger::error("[LLF] VR shadow-map camera guard not installed: camera vtable outside SkyrimVR.exe read-only segment");
			return;
		}

		const auto firstVirtual = *reinterpret_cast<const std::uintptr_t*>(vtable);
		if (!IsExecutableAddress(reinterpret_cast<const void*>(firstVirtual))) {
			logger::error("[LLF] VR shadow-map camera guard not installed: invalid camera vtable");
			return;
		}
		allowedCameraVtables[i] = vtable;
	}

	VRShadowMapCameraGuard code{ allowedCameraVtables, helperEntry + patchedInstructionSize };
	VRShadowMapCameraLateUseGuard lateUseCode{ allowedCameraVtables, helperEpilogue };
	code.ready();
	lateUseCode.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto guard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
	const auto lateUseGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(lateUseCode));
	trampoline.write_branch<patchedInstructionSize>(helperEntry, guard);
	trampoline.write_call<5>(cameraLateFrustumLoad, lateUseGuard);
	REL::safe_fill(cameraLateFrustumLoad + 5, REL::NOP, lateFrustumLoadInstructionSize - 5);

	logger::info("[LLF] Installed VR shadow-map camera guard");
}

void LightLimitFix::Hooks::InstallVRRoomLightCullingProcessGuards()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::error("[LLF] VR room-light culling-process guards not installed: unsupported Skyrim VR runtime {}", REL::Module::get().version().string());
		return;
	}

	// Skyrim VR 1.4.15's room-light culling loops dereference and call through
	// BSLight::cullingProcess without validating its concrete type. The duplicated
	// loops cover the BSMultiBoundRoom and BSPortalSharedNode parent paths. These
	// RVAs, contexts, and recovery branches were verified against the live,
	// decrypted runtime image after transitions supplied both a non-canonical
	// pointer and a readable base NiCullingProcess whose +0xE8 entry was data.
	constexpr std::uintptr_t firstRoomLightContextRVA = 0x12F8C30;
	constexpr std::uintptr_t firstPortalGraphEntryProcessLoadRVA = 0x12F8C61;
	constexpr std::uintptr_t firstNoPortalGraphEntryRVA = 0x12F8C71;
	constexpr std::uintptr_t firstCullingProcessLoadRVA = 0x12F8C81;
	constexpr std::uintptr_t firstSkipLightRVA = 0x12F8CE0;
	constexpr std::uintptr_t secondRoomLightContextRVA = 0x12F8D40;
	constexpr std::uintptr_t secondPortalGraphEntryProcessLoadRVA = 0x12F8D71;
	constexpr std::uintptr_t secondNoPortalGraphEntryRVA = 0x12F8D81;
	constexpr std::uintptr_t secondCullingProcessLoadRVA = 0x12F8D91;
	constexpr std::uintptr_t secondSkipLightRVA = 0x12F8DF0;
	constexpr std::size_t cullingProcessLoadInstructionSize = 7;
	constexpr std::uint8_t expectedFirstRoomLightContext[] = { 0x48, 0x8B, 0x8D, 0xA8, 0x01, 0x00, 0x00, 0x4A, 0x8B, 0x1C, 0xF1 };
	constexpr std::uint8_t expectedSecondRoomLightContext[] = { 0x48, 0x8B, 0x87, 0x50, 0x01, 0x00, 0x00, 0x4A, 0x8B, 0x1C, 0xF0 };
	constexpr std::uint8_t expectedFirstPortalGraphEntryProcessLoad[] = {
		0x48, 0x8B, 0x83, 0x28, 0x01, 0x00, 0x00,
		0x48, 0x8B, 0xB0, 0x90, 0x01, 0x03, 0x00,
		0xEB, 0x02
	};
	constexpr std::uint8_t expectedSecondPortalGraphEntryProcessLoad[] = {
		0x48, 0x8B, 0x83, 0x28, 0x01, 0x00, 0x00,
		0x48, 0x8B, 0xA8, 0x90, 0x01, 0x03, 0x00,
		0xEB, 0x02
	};
	constexpr std::uint8_t expectedFirstNoPortalGraphEntry[] = { 0x33, 0xF6, 0x41, 0x8B, 0x85, 0x0C, 0x01, 0x00, 0x00 };
	constexpr std::uint8_t expectedSecondNoPortalGraphEntry[] = { 0x33, 0xED, 0x41, 0x8B, 0x85, 0x0C, 0x01, 0x00, 0x00 };
	constexpr std::uint8_t expectedCullingProcessLoad[] = {
		0x48, 0x8B, 0x8B, 0x28, 0x01, 0x00, 0x00,
		0x48, 0x8D, 0x54, 0x24, 0x48,
		0x48, 0x8B, 0x01,
		0xFF, 0x90, 0xE8, 0x00, 0x00, 0x00
	};
	constexpr std::uint8_t expectedFirstSkipLight[] = { 0x41, 0xFF, 0xC6, 0x44, 0x3B, 0xB5, 0xB8, 0x01, 0x00, 0x00 };
	constexpr std::uint8_t expectedSecondSkipLight[] = { 0x41, 0xFF, 0xC6, 0x44, 0x3B, 0xB7, 0x60, 0x01, 0x00, 0x00 };

	const auto moduleBase = REL::Module::get().base();
	const auto firstRoomLightContext = moduleBase + firstRoomLightContextRVA;
	const auto firstPortalGraphEntryProcessLoad = moduleBase + firstPortalGraphEntryProcessLoadRVA;
	const auto firstNoPortalGraphEntry = moduleBase + firstNoPortalGraphEntryRVA;
	const auto firstCullingProcessLoad = moduleBase + firstCullingProcessLoadRVA;
	const auto firstSkipLight = moduleBase + firstSkipLightRVA;
	const auto secondRoomLightContext = moduleBase + secondRoomLightContextRVA;
	const auto secondPortalGraphEntryProcessLoad = moduleBase + secondPortalGraphEntryProcessLoadRVA;
	const auto secondNoPortalGraphEntry = moduleBase + secondNoPortalGraphEntryRVA;
	const auto secondCullingProcessLoad = moduleBase + secondCullingProcessLoadRVA;
	const auto secondSkipLight = moduleBase + secondSkipLightRVA;

	if (!MatchesInstructions(firstRoomLightContext, expectedFirstRoomLightContext) ||
		!MatchesInstructions(firstPortalGraphEntryProcessLoad, expectedFirstPortalGraphEntryProcessLoad) ||
		!MatchesInstructions(firstNoPortalGraphEntry, expectedFirstNoPortalGraphEntry) ||
		!MatchesInstructions(firstCullingProcessLoad, expectedCullingProcessLoad) ||
		!MatchesInstructions(firstSkipLight, expectedFirstSkipLight) ||
		!MatchesInstructions(secondRoomLightContext, expectedSecondRoomLightContext) ||
		!MatchesInstructions(secondPortalGraphEntryProcessLoad, expectedSecondPortalGraphEntryProcessLoad) ||
		!MatchesInstructions(secondNoPortalGraphEntry, expectedSecondNoPortalGraphEntry) ||
		!MatchesInstructions(secondCullingProcessLoad, expectedCullingProcessLoad) ||
		!MatchesInstructions(secondSkipLight, expectedSecondSkipLight)) {
		logger::error("[LLF] VR room-light culling-process guards not installed: unexpected SkyrimVR.exe instructions");
		return;
	}

	const auto readOnlySegment = REL::Module::get().segment(REL::Segment::rdata);
	constexpr auto requiredVtableBytes = kVRBSCullingProcessVisibilityVtableOffset + sizeof(std::uintptr_t);
	if (readOnlySegment.address() == 0 || readOnlySegment.size() < requiredVtableBytes) {
		logger::error("[LLF] VR room-light culling-process guards not installed: SkyrimVR.exe segment bounds unavailable");
		return;
	}

	const auto readOnlyStart = readOnlySegment.address();
	const auto readOnlyLastVtable = readOnlyStart + readOnlySegment.size() - requiredVtableBytes;
	std::array<std::uintptr_t, kVRBSCullingProcessVtableSpecs.size()> allowedVtables{};
	for (std::size_t i = 0; i < kVRBSCullingProcessVtableSpecs.size(); ++i) {
		const auto& vtableSpec = kVRBSCullingProcessVtableSpecs[i];
		const auto vtable = moduleBase + vtableSpec.vtableRVA;
		if (vtable < readOnlyStart || vtable > readOnlyLastVtable) {
			logger::error("[LLF] VR room-light culling-process guards not installed: BSCullingProcess vtable outside SkyrimVR.exe read-only segment");
			return;
		}

		const auto callTarget = *reinterpret_cast<const std::uintptr_t*>(vtable + kVRBSCullingProcessVisibilityVtableOffset);
		const auto expectedCallTarget = moduleBase + vtableSpec.visibilityTargetRVA;
		if (callTarget != expectedCallTarget || !IsExecutableAddress(reinterpret_cast<const void*>(callTarget))) {
			logger::error("[LLF] VR room-light culling-process guards not installed: invalid BSCullingProcess visibility target");
			return;
		}

		allowedVtables[i] = vtable;
	}

	VRRoomLightCullingProcessGuard firstPortalGraphEntryCode{ allowedVtables, VRRoomLightCullingUse::kPortalGraphEntry, firstNoPortalGraphEntry };
	firstPortalGraphEntryCode.ready();
	VRRoomLightCullingProcessGuard firstVirtualCallCode{ allowedVtables, VRRoomLightCullingUse::kVirtualCall, firstSkipLight };
	firstVirtualCallCode.ready();
	VRRoomLightCullingProcessGuard secondPortalGraphEntryCode{ allowedVtables, VRRoomLightCullingUse::kPortalGraphEntry, secondNoPortalGraphEntry };
	secondPortalGraphEntryCode.ready();
	VRRoomLightCullingProcessGuard secondVirtualCallCode{ allowedVtables, VRRoomLightCullingUse::kVirtualCall, secondSkipLight };
	secondVirtualCallCode.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto firstPortalGraphEntryGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(firstPortalGraphEntryCode));
	const auto firstVirtualCallGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(firstVirtualCallCode));
	const auto secondPortalGraphEntryGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(secondPortalGraphEntryCode));
	const auto secondVirtualCallGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(secondVirtualCallCode));
	trampoline.write_call<5>(firstPortalGraphEntryProcessLoad, firstPortalGraphEntryGuard);
	REL::safe_fill(firstPortalGraphEntryProcessLoad + 5, REL::NOP, cullingProcessLoadInstructionSize - 5);
	trampoline.write_call<5>(firstCullingProcessLoad, firstVirtualCallGuard);
	REL::safe_fill(firstCullingProcessLoad + 5, REL::NOP, cullingProcessLoadInstructionSize - 5);
	trampoline.write_call<5>(secondPortalGraphEntryProcessLoad, secondPortalGraphEntryGuard);
	REL::safe_fill(secondPortalGraphEntryProcessLoad + 5, REL::NOP, cullingProcessLoadInstructionSize - 5);
	trampoline.write_call<5>(secondCullingProcessLoad, secondVirtualCallGuard);
	REL::safe_fill(secondCullingProcessLoad + 5, REL::NOP, cullingProcessLoadInstructionSize - 5);

	logger::info("[LLF] Installed VR room-light culling-process guards");
}

void LightLimitFix::Hooks::InstallVRRoomLightEntryGuards()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::error("[LLF] VR room-light entry guards not installed: unsupported Skyrim VR runtime {}", REL::Module::get().version().string());
		return;
	}

	// Skyrim VR 1.4.15 has three BSLight loops in this routine: the
	// BSMultiBoundRoom array, the BSPortalSharedNode array, and the fallback
	// pointer list. Each checks the entry for nullptr, then calls
	// BSLight::IsShadowLight through its vtable without validating that the
	// entry still owns a live BSLight. The reproduced Dragonsreach transition
	// supplied a readable stale entry whose heap-shaped vtable had a null
	// IsShadowLight slot.
	constexpr std::uintptr_t roomCallContextRVA = 0x12F91E0;
	constexpr std::uintptr_t roomVirtualCallRVA = 0x12F91E6;
	constexpr std::uintptr_t roomSkipLightRVA = 0x12F9210;
	constexpr std::uintptr_t portalCallContextRVA = 0x12F9260;
	constexpr std::uintptr_t portalVirtualCallRVA = 0x12F9266;
	constexpr std::uintptr_t portalSkipLightRVA = 0x12F9290;
	constexpr std::uintptr_t fallbackCallContextRVA = 0x12F92DB;
	constexpr std::uintptr_t fallbackVirtualCallRVA = 0x12F92E1;
	constexpr std::uintptr_t fallbackSkipLightRVA = 0x12F9315;
	constexpr std::size_t isShadowLightVtableOffset = 0x18;
	constexpr std::size_t patchedInstructionSize = 5;
	constexpr std::uint8_t expectedArrayCallContext[] = {
		0x48, 0x8B, 0x07,
		0x48, 0x8B, 0xCF,
		0xFF, 0x50, 0x18,
		0x84, 0xC0,
		0x75, 0x23
	};
	constexpr std::uint8_t expectedFallbackCallContext[] = {
		0x48, 0x8B, 0x07,
		0x48, 0x8B, 0xCF,
		0xFF, 0x50, 0x18,
		0x84, 0xC0,
		0x75, 0x2D
	};
	constexpr std::uint8_t expectedRoomSkipLight[] = {
		0xFF, 0xC3,
		0x3B, 0x9D, 0xB8, 0x01, 0x00, 0x00,
		0x72, 0xB6
	};
	constexpr std::uint8_t expectedPortalSkipLight[] = {
		0xFF, 0xC3,
		0x3B, 0x9D, 0x60, 0x01, 0x00, 0x00,
		0x72, 0xB6
	};
	constexpr std::uint8_t expectedFallbackSkipLight[] = {
		0x48, 0x8D, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00,
		0x48, 0x8B, 0xCD
	};

	const auto moduleBase = REL::Module::get().base();
	const auto roomCallContext = moduleBase + roomCallContextRVA;
	const auto roomVirtualCall = moduleBase + roomVirtualCallRVA;
	const auto roomSkipLight = moduleBase + roomSkipLightRVA;
	const auto portalCallContext = moduleBase + portalCallContextRVA;
	const auto portalVirtualCall = moduleBase + portalVirtualCallRVA;
	const auto portalSkipLight = moduleBase + portalSkipLightRVA;
	const auto fallbackCallContext = moduleBase + fallbackCallContextRVA;
	const auto fallbackVirtualCall = moduleBase + fallbackVirtualCallRVA;
	const auto fallbackSkipLight = moduleBase + fallbackSkipLightRVA;

	if (!MatchesInstructions(roomCallContext, expectedArrayCallContext) ||
		!MatchesInstructions(roomSkipLight, expectedRoomSkipLight) ||
		!MatchesInstructions(portalCallContext, expectedArrayCallContext) ||
		!MatchesInstructions(portalSkipLight, expectedPortalSkipLight) ||
		!MatchesInstructions(fallbackCallContext, expectedFallbackCallContext) ||
		!MatchesInstructions(fallbackSkipLight, expectedFallbackSkipLight)) {
		logger::error("[LLF] VR room-light entry guards not installed: unexpected SkyrimVR.exe instructions");
		return;
	}

	const auto readOnlySegment = REL::Module::get().segment(REL::Segment::rdata);
	constexpr auto requiredVtableBytes = isShadowLightVtableOffset + sizeof(std::uintptr_t);
	if (readOnlySegment.address() == 0 || readOnlySegment.size() < requiredVtableBytes) {
		logger::error("[LLF] VR room-light entry guards not installed: SkyrimVR.exe segment bounds unavailable");
		return;
	}

	const auto readOnlyStart = readOnlySegment.address();
	const auto readOnlyLastVtable = readOnlyStart + readOnlySegment.size() - requiredVtableBytes;
	std::array<std::uintptr_t, kVRBSLightVtableRVAs.size()> allowedVtables{};
	std::array<std::uintptr_t, kVRBSLightVtableRVAs.size()> allowedCallTargets{};
	std::size_t allowedCallTargetCount = 0;
	for (std::size_t i = 0; i < kVRBSLightVtableRVAs.size(); ++i) {
		const auto vtable = moduleBase + kVRBSLightVtableRVAs[i];
		if (vtable < readOnlyStart || vtable > readOnlyLastVtable) {
			logger::error("[LLF] VR room-light entry guards not installed: BSLight vtable outside SkyrimVR.exe read-only segment");
			return;
		}

		const auto callTarget = *reinterpret_cast<const std::uintptr_t*>(vtable + isShadowLightVtableOffset);
		if (!IsExecutableAddress(reinterpret_cast<const void*>(callTarget))) {
			logger::error("[LLF] VR room-light entry guards not installed: invalid BSLight::IsShadowLight target");
			return;
		}

		allowedVtables[i] = vtable;
		const auto knownCallTargetsEnd = allowedCallTargets.begin() + allowedCallTargetCount;
		if (std::find(allowedCallTargets.begin(), knownCallTargetsEnd, callTarget) == knownCallTargetsEnd) {
			allowedCallTargets[allowedCallTargetCount++] = callTarget;
		}
	}

	VRRoomLightEntryGuard roomCode{ allowedVtables, allowedCallTargets, allowedCallTargetCount, roomSkipLight };
	roomCode.ready();
	VRRoomLightEntryGuard portalCode{ allowedVtables, allowedCallTargets, allowedCallTargetCount, portalSkipLight };
	portalCode.ready();
	VRRoomLightEntryGuard fallbackCode{ allowedVtables, allowedCallTargets, allowedCallTargetCount, fallbackSkipLight };
	fallbackCode.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto roomGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(roomCode));
	const auto portalGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(portalCode));
	const auto fallbackGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(fallbackCode));
	trampoline.write_call<patchedInstructionSize>(roomVirtualCall, roomGuard);
	trampoline.write_call<patchedInstructionSize>(portalVirtualCall, portalGuard);
	trampoline.write_call<patchedInstructionSize>(fallbackVirtualCall, fallbackGuard);

	logger::info("[LLF] Installed VR room-light entry guards");
}

void LightLimitFix::Hooks::InstallVREffectShaderLightGuards()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
		logger::error("[LLF] VR effect-shader light guards not installed: unsupported Skyrim VR runtime {}", REL::Module::get().version().string());
		return;
	}

	// Skyrim VR 1.4.15 BSEffectShader::SetupGeometry reads both the first
	// scene light and up to four additional lights without null checks. These
	// RVAs and their recovery branches were verified against the live,
	// decrypted runtime image because the Steam executable is encrypted on disk.
	constexpr std::uintptr_t firstLightFlagsRVA = 0x1342102;
	constexpr std::uintptr_t firstLightLoadRVA = 0x1342115;
	constexpr std::uintptr_t additionalLightCountRVA = 0x13421F1;
	constexpr std::uintptr_t additionalLightLoadRVA = 0x134223C;
	constexpr std::uintptr_t finishAdditionalLightsRVA = 0x134253D;
	constexpr std::uintptr_t skipLightsRVA = 0x13425B9;
	constexpr std::uint8_t expectedFirstLightFlags[] = { 0x80, 0x3D, 0xBC, 0x0F, 0x0E, 0x02, 0x00 };
	constexpr std::uint8_t expectedFirstLightLoad[] = { 0x48, 0x8B, 0x00, 0x48, 0x8B, 0x48, 0x48 };
	constexpr std::uint8_t expectedAdditionalLightCount[] = { 0x48, 0x89, 0x45, 0x28 };
	constexpr std::uint8_t expectedAdditionalLightLoad[] = { 0x48, 0x8B, 0x0C, 0x18, 0x4C, 0x8B, 0x71, 0x48, 0x49, 0x8B, 0xCD };
	constexpr std::uint8_t expectedFinishAdditionalLights[] = { 0x48, 0x8B, 0x5D, 0x28, 0x83, 0xFB, 0x04 };
	constexpr std::uint8_t expectedSkipLights[] = { 0xF6, 0x86, 0x90, 0x00, 0x00, 0x00, 0x80 };

	const auto moduleBase = REL::Module::get().base();
	const auto firstLightFlags = moduleBase + firstLightFlagsRVA;
	const auto firstLightLoad = moduleBase + firstLightLoadRVA;
	const auto additionalLightCount = moduleBase + additionalLightCountRVA;
	const auto additionalLightLoad = moduleBase + additionalLightLoadRVA;
	const auto finishAdditionalLights = moduleBase + finishAdditionalLightsRVA;
	const auto skipLights = moduleBase + skipLightsRVA;

	if (!MatchesInstructions(firstLightFlags, expectedFirstLightFlags) ||
		!MatchesInstructions(firstLightLoad, expectedFirstLightLoad) ||
		!MatchesInstructions(additionalLightCount, expectedAdditionalLightCount) ||
		!MatchesInstructions(additionalLightLoad, expectedAdditionalLightLoad) ||
		!MatchesInstructions(finishAdditionalLights, expectedFinishAdditionalLights) ||
		!MatchesInstructions(skipLights, expectedSkipLights)) {
		logger::error("[LLF] VR effect-shader light guards not installed: unexpected SkyrimVR.exe instructions");
		return;
	}

	const auto readOnlySegment = REL::Module::get().segment(REL::Segment::rdata);
	const auto resolveVtables = [&](const auto& a_rvas, auto& a_addresses) {
		constexpr auto requiredVtableBytes = sizeof(std::uintptr_t);
		if (readOnlySegment.address() == 0 || readOnlySegment.size() < requiredVtableBytes) {
			return false;
		}

		const auto readOnlyStart = readOnlySegment.address();
		const auto readOnlyLastVtable = readOnlyStart + readOnlySegment.size() - requiredVtableBytes;
		for (std::size_t i = 0; i < a_rvas.size(); ++i) {
			const auto vtable = moduleBase + a_rvas[i];
			if (vtable < readOnlyStart || vtable > readOnlyLastVtable) {
				return false;
			}

			const auto firstVirtual = *reinterpret_cast<const std::uintptr_t*>(vtable);
			if (!IsExecutableAddress(reinterpret_cast<const void*>(firstVirtual))) {
				return false;
			}
			a_addresses[i] = vtable;
		}
		return true;
	};

	std::array<std::uintptr_t, kVRBSLightVtableRVAs.size()> allowedBSLightVtables{};
	std::array<std::uintptr_t, kVRNiLightVtableRVAs.size()> allowedNiLightVtables{};
	if (!resolveVtables(kVRBSLightVtableRVAs, allowedBSLightVtables) ||
		!resolveVtables(kVRNiLightVtableRVAs, allowedNiLightVtables)) {
		logger::error("[LLF] VR effect-shader light guards not installed: invalid engine light vtables");
		return;
	}

	VREffectShaderFirstLightGuard firstLightCode{ allowedBSLightVtables, allowedNiLightVtables, skipLights };
	firstLightCode.ready();
	VREffectShaderAdditionalLightGuard additionalLightCode{ allowedBSLightVtables, allowedNiLightVtables, finishAdditionalLights };
	additionalLightCode.ready();

	auto& trampoline = SKSE::GetTrampoline();
	const auto firstLightGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(firstLightCode));
	const auto additionalLightGuard = reinterpret_cast<std::uintptr_t>(trampoline.allocate(additionalLightCode));
	trampoline.write_call<5>(firstLightLoad, firstLightGuard);
	REL::safe_fill(firstLightLoad + 5, REL::NOP, sizeof(expectedFirstLightLoad) - 5);
	trampoline.write_call<5>(additionalLightLoad, additionalLightGuard);
	REL::safe_fill(additionalLightLoad + 5, REL::NOP, sizeof(expectedAdditionalLightLoad) - 5);

	logger::info("[LLF] Installed VR effect-shader scene-light guards");
}

void LightLimitFix::PostPostLoad()
{
	globals::features::llf::particleLights.GetConfigs();
	particleLightsReferences.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 4u);
	queuedParticleEmitters.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	currentParticleEmitters.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedBillboardLights.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	currentBillboardLights.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedEmitterIndices.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	queuedBillboardIndices.reserve(static_cast<std::size_t>(MAX_LIGHTS) * 2u);
	Hooks::Install();
}

void LightLimitFix::DataLoaded()
{
	if (auto gameSettings = globals::game::gameSettingCollection) {
		if (auto iMagicLightMaxCount = gameSettings->GetSetting("iMagicLightMaxCount")) {
			iMagicLightMaxCount->data.i = MAXINT32;
			logger::info("[LLF] Unlocked magic light limit");
		}
	}
}

void LightLimitFix::ClearShaderCache()
{
	clusterBuildingCS.Reset();
	clusterCullingCS.Reset();
	CompileComputeShaders();
}

void LightLimitFix::CompileComputeShaders()
{
	clusterBuildingCS.Get(
		L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", {}, "cs_5_0",
		"main", "LightLimitFix::ClusterBuildingCS");
	clusterCullingCS.Get(
		L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", {}, "cs_5_0",
		"main", "LightLimitFix::ClusterCullingCS");
}

float LightLimitFix::CalculateLightDistance(float3 a_lightPosition, float a_radius)
{
	return (a_lightPosition.x * a_lightPosition.x) + (a_lightPosition.y * a_lightPosition.y) + (a_lightPosition.z * a_lightPosition.z) - (a_radius * a_radius);
}

void LightLimitFix::AddCachedParticleLights(
	eastl::vector<LightData>& lightsData,
	LightLimitFix::LightData& light,
	const ResolvedBillboardLight* a_billboardLight)
{
	if (lightsData.size() >= MAX_LIGHTS) {
		return;
	}

	static float& lightFadeStart = *reinterpret_cast<float*>(REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = *reinterpret_cast<float*>(REL::RelocationID(527669, 414583).address());
	const float3 luminanceWeights = float3(0.3f, 0.59f, 0.11f);

	// NEW: hard distance cutoff for particle lights
	if (settings.MaxParticleDistance > 0.0f) {
		float maxDist = settings.MaxParticleDistance;
		float maxDistSq = maxDist * maxDist;

		const auto& pos = light.positionWS[0].data;  // camera-relative
		float distSq = (pos.x * pos.x) + (pos.y * pos.y) + (pos.z * pos.z);

		if (distSq > maxDistSq) {
			// Too far away: don't add this particle light at all
			return;
		}
	}

	float distance = CalculateLightDistance(light.positionWS[0].data, light.radius);

	float dimmer = 0.0f;

	if (distance < lightFadeStart || lightFadeEnd == 0.0f || lightFadeEnd <= lightFadeStart) {
		dimmer = 1.0f;
	} else if (distance <= lightFadeEnd) {
		dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));
	} else {
		dimmer = 0.0f;
	}

	light.fade *= dimmer;
	const float luminanceScale = light.fade;
	if ((light.color.x + light.color.y + light.color.z) * luminanceScale > 1e-4 && light.radius > 1e-4) {
		if (a_billboardLight) {
			ApplyLegacyParticleLightFlicker(light, *a_billboardLight, eyeCount);
		}

		light.invRadius = 1.f / light.radius;
		lightsData.push_back(light);

		if (cachedParticleLights.size() < MAX_LIGHTS) {
			CachedParticleLight cachedParticleLight{};
			cachedParticleLight.grey = float3(light.color.x, light.color.y, light.color.z).Dot(luminanceWeights) * luminanceScale;
			cachedParticleLight.radius = light.radius;
			cachedParticleLight.position = { light.positionWS[0].data.x + eyePositionCached[0].x, light.positionWS[0].data.y + eyePositionCached[0].y, light.positionWS[0].data.z + eyePositionCached[0].z };

			cachedParticleLights.push_back(cachedParticleLight);
		}
	}
}

float3 LightLimitFix::Saturation(float3 color, float saturation)
{
	float grey = color.Dot(float3(0.3f, 0.59f, 0.11f));
	color.x = std::max(std::lerp(grey, color.x, saturation), 0.0f);
	color.y = std::max(std::lerp(grey, color.y, saturation), 0.0f);
	color.z = std::max(std::lerp(grey, color.z, saturation), 0.0f);
	return color;
}

void LightLimitFix::UpdateLights()
{
	ZoneScopedN("LLF::UpdateLights");

	auto clearCachedParticleLights = [&]() {
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();
	};

	auto context = globals::d3d::context;
	if (!context || !lights || !lights->resource) {
		clearCachedParticleLights();
		return;
	}

	auto smState = globals::game::smState;
	auto& isl = globals::features::inverseSquareLighting;
	auto clearAndUpdate = [&]() {
		lightCount = 0;
		clearCachedParticleLights();
		UpdateStructure();
	};

	if (!smState) {
		clearAndUpdate();
		return;
	}

	auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode) {
		clearAndUpdate();
		return;
	}

	// Cache data since cameraData can become invalid in first-person

	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		auto eyePosition = globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
		eyePositionCached[eyeIndex] = { eyePosition.x, eyePosition.y, eyePosition.z };
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);
	const bool isInterior = LocationContext::Get().inInterior;
	RefreshJsonPlacedLightCacheFrame();

	// Process point lights

	roomNodes.clear();

	auto addRoom = [&](RE::NiNode* node, LightData& light) {
		if (!node) {
			return;
		}

		constexpr std::size_t kMaxRoomFlags = 128;
		uint8_t roomIndex = 0;
		if (auto it = roomNodes.find(node); it == roomNodes.cend()) {
			if (roomNodes.size() >= kMaxRoomFlags) {
				return;
			}
			roomIndex = static_cast<uint8_t>(roomNodes.size());
			roomNodes.insert_or_assign(node, roomIndex);
		} else {
			roomIndex = it->second;
		}
		light.roomFlags.SetBit(roomIndex, 1);
	};

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						// light.color *= runtimeData.fade;
						light.fade = runtimeData.fade;
					}

					SetEngineLightFlags(light, bsLight);
					light.fade *= bsLight->lodDimmer;
					const bool isPortalStrict = !IsGlobalLight(bsLight);

					if (isPortalStrict) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							if (roomPtr) {
								addRoom(static_cast<RE::NiNode*>(roomPtr), light);
							}
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							if (portalPtr && portalPtr->portalSharedNode) {
								addRoom(static_cast<RE::NiNode*>(portalPtr->portalSharedNode.get()), light);
							}
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}
					ApplyJsonPlacedLightIntensityScale(light, bsLight, niLight, isPortalStrict, isInterior);

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(maskIndex, shadowLight);
						light.shadowMaskIndex = maskIndex;
						light.lightFlags.set(LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						SetLightPosition(light, niLight->world.translate);

						if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4 &&
							lightsData.size() < MAX_LIGHTS) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	{
		CS_PROFILE_CPU_SCOPE("LightLimitFix::SceneLightsCPU");
		for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
			addLight(e);
		}
		for (auto& e : shadowSceneNode->GetRuntimeData().activeShadowLights) {
			addLight(e);
		}
	}

	{
		CS_PROFILE_CPU_SCOPE("LightLimitFix::ParticleLightsCPU");
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();

		LightData clusteredLight{};
		uint32_t clusteredLights = 0;

		auto flushClusteredLight = [&]() {
			if (!clusteredLights) {
				return;
			}

			const float clusterCount = static_cast<float>(clusteredLights);
			clusteredLight.radius /= clusterCount;
			clusteredLight.positionWS[0].data /= clusterCount;
			clusteredLight.positionWS[1].data = clusteredLight.positionWS[0].data;

			if (eyeCount == 2) {
				const auto eyePositionOffset = eyePositionCached[0] - eyePositionCached[1];
				clusteredLight.positionWS[1].data.x += eyePositionOffset.x;
				clusteredLight.positionWS[1].data.y += eyePositionOffset.y;
				clusteredLight.positionWS[1].data.z += eyePositionOffset.z;
			}

			clusteredLight.lightFlags.set(LightFlags::Simple);
			clusteredLight.lightFlags.set(LightFlags::Particle);
			clusteredLight.lightFlags.set(LightFlags::AffectWater);
			AddCachedParticleLights(lightsData, clusteredLight);

			clusteredLights = 0;
			clusteredLight = {};
		};

		auto processParticleEmitter = [&](const ParticleEmitterLight& a_particleEmitter) {
			if (!a_particleEmitter.node ||
				IsParticleEmitterBeyondDistance(a_particleEmitter.node.get(), eyePositionCached[0], settings.MaxParticleDistance)) {
				return;
			}

			auto* particleSystem = a_particleEmitter.node.get();
			auto* particleData = a_particleEmitter.particleData.get();
			if (!particleSystem || !particleData) {
				return;
			}

			auto& particleSystemRuntimeData = particleSystem->GetParticleSystemRuntimeData();
			auto& particleRuntimeData = particleData->GetParticlesRuntimeData();
			if (!particleRuntimeData.radii || !particleRuntimeData.sizes || !particleRuntimeData.positions) {
				return;
			}

			std::uint32_t numVertices = static_cast<std::uint32_t>(particleData->GetActiveVertexCount());
			const std::uint32_t runtimeMaxVertices = static_cast<std::uint32_t>(particleRuntimeData.maxNumVertices);
			const std::uint32_t runtimeNumVertices = static_cast<std::uint32_t>(particleRuntimeData.numVertices);
			if (runtimeMaxVertices == 0) {
				return;
			}
			numVertices = std::min(numVertices, runtimeMaxVertices);
			if (runtimeNumVertices > 0) {
				numVertices = std::min(numVertices, runtimeNumVertices);
			}

			const std::uint32_t maxPerEmitter = static_cast<std::uint32_t>(std::max(1, settings.MaxParticlesPerEmitter));
			numVertices = std::min(numVertices, maxPerEmitter);
			const float saturation = ResolveParticleSaturation(settings.ParticleLightsSaturation, a_particleEmitter.saturationMult);

			for (std::uint32_t p = 0; p < numVertices; p++) {
				if (lightsData.size() >= MAX_LIGHTS) {
					break;
				}

				const float radius = particleRuntimeData.radii[p] * particleRuntimeData.sizes[p];

				auto initialPosition = particleRuntimeData.positions[p];
				if (!particleSystemRuntimeData.isWorldspace) {
					// Detect first-person meshes.
					if ((particleSystem->GetModelData().modelBound.radius * particleSystem->world.scale) != particleSystem->worldBound.radius) {
						const auto& center = particleSystem->worldBound.center;
						initialPosition = { initialPosition.x + center.x, initialPosition.y + center.y, initialPosition.z + center.z };
					} else {
						const auto& translate = particleSystem->world.translate;
						initialPosition = { initialPosition.x + translate.x, initialPosition.y + translate.y, initialPosition.z + translate.z };
					}
				}

				RE::NiPoint3 positionWS{
					initialPosition.x - eyePositionCached[0].x,
					initialPosition.y - eyePositionCached[0].y,
					initialPosition.z - eyePositionCached[0].z
				};

				if (clusteredLights) {
					const auto averageRadius = clusteredLight.radius / static_cast<float>(clusteredLights);
					const float radiusDiff = std::abs(averageRadius - radius);

					const auto averagePosition = clusteredLight.positionWS[0].data / static_cast<float>(clusteredLights);
					const float positionDiff = positionWS.GetDistance({ averagePosition.x, averagePosition.y, averagePosition.z });
					if ((radiusDiff + positionDiff) > settings.ParticleClusterThreshold ||
						!settings.EnableParticleLightsOptimization) {
						flushClusteredLight();
					}
					if (lightsData.size() >= MAX_LIGHTS) {
						break;
					}
				}

				float alpha = a_particleEmitter.color.alpha;
				float3 color{
					a_particleEmitter.color.red,
					a_particleEmitter.color.green,
					a_particleEmitter.color.blue
				};
				if (particleRuntimeData.color) {
					alpha *= particleRuntimeData.color[p].alpha;
					color.x *= particleRuntimeData.color[p].red;
					color.y *= particleRuntimeData.color[p].green;
					color.z *= particleRuntimeData.color[p].blue;
				}
				clusteredLight.color += Saturation(color, saturation) * alpha * settings.ParticleBrightness;
				clusteredLight.radius += radius * a_particleEmitter.radiusMult * settings.ParticleRadius;
				clusteredLight.positionWS[0].data.x += positionWS.x;
				clusteredLight.positionWS[0].data.y += positionWS.y;
				clusteredLight.positionWS[0].data.z += positionWS.z;
				clusteredLights++;
			}
		};

		auto processBillboard = [&](const ResolvedBillboardLight& a_billboardLight) {
			// Preserve submission order and budget priority: an emitter cluster
			// sequenced before this billboard must be emitted first.
			flushClusteredLight();
			if (lightsData.size() >= MAX_LIGHTS) {
				return;
			}

			LightData light{};
			light.color = a_billboardLight.color;
			light.radius = a_billboardLight.radius;
			SetLightPosition(light, a_billboardLight.position);
			light.lightFlags.set(LightFlags::Simple);
			light.lightFlags.set(LightFlags::Particle);
			light.lightFlags.set(LightFlags::AffectWater);
			AddCachedParticleLights(lightsData, light, &a_billboardLight);
		};

		std::lock_guard<std::mutex> currentLock{ currentParticleLightsMutex };
		std::size_t emitterIndex = 0;
		std::size_t billboardIndex = 0;
		while (emitterIndex < currentParticleEmitters.size() || billboardIndex < currentBillboardLights.size()) {
			if (lightsData.size() >= MAX_LIGHTS) {
				break;
			}

			const bool processEmitter =
				billboardIndex >= currentBillboardLights.size() ||
				(emitterIndex < currentParticleEmitters.size() &&
					currentParticleEmitters[emitterIndex].sequence <= currentBillboardLights[billboardIndex].sequence);
			if (processEmitter) {
				processParticleEmitter(currentParticleEmitters[emitterIndex++]);
			} else {
				processBillboard(currentBillboardLights[billboardIndex++]);
			}
		}

		flushClusteredLight();
	}

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	{
		CS_PROFILE_CPU_SCOPE("LightLimitFix::UploadLightsCPU");
		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(LightData) * lightCount;
		if (bytes > 0) {
			memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
		}
		context->Unmap(lights->resource.get(), 0);
	}

	UpdateStructure();
}

void LightLimitFix::UpdateStructure()
{
	auto context = globals::d3d::context;
	if (!context || !lightBuildingCB || !lightCullingCB || !clusters || !lights ||
		!lightIndexCounter || !lightIndexList || !lightGrid ||
		!contactShadowIndexCounter || !contactShadowIndexList || !contactShadowGrid) {
		return;
	}
	auto* clusterBuilding = clusterBuildingCS.Get(
		L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", {}, "cs_5_0",
		"main", "LightLimitFix::ClusterBuildingCS");
	auto* clusterCulling = clusterCullingCS.Get(
		L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", {}, "cs_5_0",
		"main", "LightLimitFix::ClusterCullingCS");
	if (!clusterBuilding || !clusterCulling) {
		const UINT zero[4]{ 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightGrid->uav.get(), zero);
		context->ClearUnorderedAccessViewUint(contactShadowGrid->uav.get(), zero);
		return;
	}

	if (globals::game::cameraNear) {
		lightsNear = *globals::game::cameraNear;
	}
	if (globals::game::cameraFar) {
		lightsFar = *globals::game::cameraFar;
	}

	auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
	if (REL::Module::IsVR())
		renderSize.x *= .5;
	clusterSize[0] = ((uint)renderSize.x + 63) / 64;
	clusterSize[1] = ((uint)renderSize.y + 63) / 64;
	clusterSize[2] = 32;

	{
		LightBuildingCB updateData{};
		updateData.LightsNear = lightsNear;
		updateData.LightsFar = lightsFar;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightBuildingCB->Update(updateData);

		ID3D11Buffer* buffer = lightBuildingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

		context->CSSetShader(clusterBuilding, nullptr, 0);
		{
			CS_GPU_PASS("LightLimitFix::ClusterBuild");
			context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		}

		ID3D11UnorderedAccessView* null_uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
	}

	{
		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		updateData.ContactShadowFlags = PackContactShadowFlags(settings);
		updateData.ContactShadowParams = PackContactShadowParams(settings);
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightCullingCB->Update(updateData);

		UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounter->uav.get(), counterReset);
		context->ClearUnorderedAccessViewUint(contactShadowIndexCounter->uav.get(), counterReset);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = {
			lightIndexCounter->uav.get(),
			lightIndexList->uav.get(),
			lightGrid->uav.get(),
			contactShadowIndexCounter->uav.get(),
			contactShadowIndexList->uav.get(),
			contactShadowGrid->uav.get()
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(clusterCulling, nullptr, 0);
		{
			CS_GPU_PASS("LightLimitFix::ClusterCull");
			context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		}
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[6] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 6, null_uavs, nullptr);
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	uint32_t numLights = 0;
	RE::BSLight* directionalLight = nullptr;
	RE::NiLight* directionalNiLight = nullptr;
	// BSLightingShader dereferences sceneLights[0]->light without validation.
	// Invalid UI 3D scene directional slots are skipped to avoid a CTD.
	const bool directionalSlotSafe = IsDirectionalSceneLightSafe(Pass, numLights, directionalLight, directionalNiLight);
	if (!directionalSlotSafe) {
		static bool everLogged = false;
		static std::uintptr_t lastLoggedNiLight = 0;
		static int distinctLogged = 0;
		const auto niLightValue = reinterpret_cast<std::uintptr_t>(directionalNiLight);
		if ((!everLogged || niLightValue != lastLoggedNiLight) && distinctLogged++ < 20) {
			everLogged = true;
			lastLoggedNiLight = niLightValue;
			logger::warn(
				"[LLF] BSLightingShader_SetupGeometry: directional sceneLights[0] unsafe "
				"(numLights={} BSLight=0x{:x} NiLight=0x{:x}); skipping engine SetupGeometry",
				numLights,
				reinterpret_cast<std::uintptr_t>(directionalLight),
				niLightValue);
		}
	}

	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	if (directionalSlotSafe) {
		func(This, Pass, RenderFlags);
	}
	singleton.BSLightingShader_SetupGeometry_After(Pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	// Cache validated pairs for this frame, but reread the NiLight pointer so a
	// recycled BSLight falls back to the full committed-range checks.
	if (Pass && Pass->sceneLights && Pass->numLights > 0) {
		std::uint8_t validCount = 0;
		auto& validationCache = globals::features::lightLimitFix.effectLightValidationCache;
		for (std::uint8_t i = 0; i < Pass->numLights; ++i) {
			RE::BSLight* bsLight = Pass->sceneLights[i];
			if (const auto cached = validationCache.find(bsLight); cached != validationCache.end()) {
				const auto currentNiLight = SafeReadNiLight(bsLight);
				if (currentNiLight == cached->second) {
					++validCount;
					continue;
				}
				validationCache.erase(cached);
			}

			if (!IsSafeLightRange(bsLight, kBSLightEngineReadSize)) {
				static int loggedBsLight = 0;
				if (loggedBsLight++ < 10) {
					logger::warn(
						"[LLF] BSEffectShader_SetupGeometry: unsafe BSLight at "
						"sceneLights[{}]=0x{:x} numLights={}; clamping to {}",
						i, reinterpret_cast<std::uintptr_t>(bsLight), Pass->numLights, validCount);
				}
				break;
			}

			RE::NiLight* niLight = SafeReadNiLight(bsLight);
			if (!IsSafeLightRange(niLight, kNiLightEngineReadSize)) {
				static int loggedNiLight = 0;
				if (loggedNiLight++ < 10) {
					logger::warn(
						"[LLF] BSEffectShader_SetupGeometry: unsafe NiLight at "
						"sceneLights[{}] (BSLight=0x{:x} NiLight=0x{:x}); clamping to {}",
						i,
						reinterpret_cast<std::uintptr_t>(bsLight),
						reinterpret_cast<std::uintptr_t>(niLight),
						validCount);
				}
				break;
			}

			validationCache.insert_or_assign(bsLight, niLight);
			++validCount;
		}
		if (validCount < Pass->numLights) {
			Pass->numLights = validCount;
		}
	}

	func(This, Pass, RenderFlags);
	ExternalEmittance::UpdatePermutation(Pass);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

void LightLimitFix::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	// Cloud Shadows can leave cubemap depth in t17. Water contact shadows need
	// the current 16-bit scene depth for their screen-space raymarch.
	auto* srv = Util::GetCurrentSceneDepthSRV(true);
	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
	auto& singleton = globals::features::lightLimitFix;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

float LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::thunk(
	RE::ShadowSceneNode* shadowSceneNode,
	RE::NiPoint3& targetPosition,
	int& numHits,
	float& sunLightLevel,
	float& lightLevel,
	RE::NiLight& refLight,
	int32_t shadowBitMask)
{
	auto ret = func(shadowSceneNode, targetPosition, numHits, sunLightLevel, lightLevel, refLight, shadowBitMask);
	globals::features::lightLimitFix.AddParticleLightLuminance(targetPosition, numHits, ret);
	return ret;
}
