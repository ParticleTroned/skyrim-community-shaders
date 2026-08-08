#pragma once

#include <shared_mutex>

namespace Hooks
{
	struct BSShader_BeginTechnique
	{
		static bool thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct BSGraphics_SetDirtyStates
	{
		static void thunk(bool isCompute);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately1
	{
		static void thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Draw through the shared RenderPassImmediately call-site owner without re-entering particle/Terrain Blending routing.
	void DrawRenderPassImmediately(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags);
	void Install();
	void InstallEarlyHooks();
	std::shared_mutex& GetRenderTargetRecreationMutex();
	bool RecreateRenderTargets();
	using VRRenderTargetRecreatePreparation = void (*)(void*);
	// Runs synchronously after Skyrim's native target creator returns and before
	// global or CSX render-target state is reinitialized. Returning true proves
	// that recovery setup may run; it does not by itself prove that every output
	// slot was replaced by this generation.
	using VRRenderTargetRecreateCheckpoint = bool (*)(void*, bool) noexcept;
	bool RecreateRenderTargetsForVRRenderScale(
		VRRenderTargetRecreatePreparation a_beforeEngineCreate = nullptr,
		VRRenderTargetRecreateCheckpoint a_afterEngineCreate = nullptr,
		void* a_context = nullptr,
		bool* a_engineCreateEntered = nullptr);
}
