#pragma once

#include <dxgi.h>
#include <functional>

namespace Hooks
{
	/** @brief Recovers the result returned by a CSX base Present through an SL proxy. */
	class SwapChainPresentResultCapture
	{
	public:
		SwapChainPresentResultCapture();
		~SwapChainPresentResultCapture();

		SwapChainPresentResultCapture(
			const SwapChainPresentResultCapture&) = delete;
		SwapChainPresentResultCapture& operator=(
			const SwapChainPresentResultCapture&) = delete;

		[[nodiscard]] HRESULT Resolve(HRESULT a_proxyResult) const;

	private:
		friend void RecordSwapChainBasePresentResult(HRESULT a_result);

		SwapChainPresentResultCapture* previous = nullptr;
		HRESULT baseResult = S_OK;
		bool basePresentInvoked = false;
	};

	/** @brief Records the exact CSX result for the active outer Present call. */
	void RecordSwapChainBasePresentResult(HRESULT a_result);

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

	void Install();
	void InstallEarlyHooks();
	bool RecreateRenderTargets();
	/** @brief Runs the shared outer lifecycle for Present and Present1. */
	HRESULT RunSwapChainPresent(
		IDXGISwapChain* a_swapChain,
		UINT a_syncInterval,
		UINT a_flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_presentChain);
}
