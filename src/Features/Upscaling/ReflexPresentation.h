#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ReflexPresentJournal.h"
#	include <d3d11.h>
#	include <nlohmann/json_fwd.hpp>

namespace ReflexDiagnostics::Presentation
{
	/** Only the VR native creation path may opt into the historical desktop policy. */
	inline bool PrepareFlip(bool isVR, bool requested, DXGI_SWAP_CHAIN_DESC& desc)
	{
		if (!isVR || !requested || desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality != 0 ||
			desc.BufferCount > 16 || (desc.Flags & DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE))
			return false;
		if (desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
			desc.BufferDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
			desc.BufferDesc.Format != DXGI_FORMAT_R10G10B10A2_UNORM &&
			desc.BufferDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
			return false;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		if (desc.BufferCount < 2)
			desc.BufferCount = 2;
		return true;
	}
	/** Consumes a one-launch request before attempting creation; failed trials use the caller descriptor. */
	HRESULT Create(decltype(&D3D11CreateDeviceAndSwapChain) create,
		IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
		const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk,
		const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swap, ID3D11Device** device,
		D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context);
	/** Descriptor, creation receipt, window state and native COM device identities. */
	nlohmann::json Environment();
	/** Persists or cancels the one-launch request. Never changes a live swap chain. */
	nlohmann::json Configure(const nlohmann::json& args);
	void DrawControls();

	/** Present calls use the same QPC window as the input-hook journal. */
	bool Start(uint64_t session, uint64_t start, uint64_t deadline);
	void Stop();
	bool Drained();
	nlohmann::json Read(uint64_t after, size_t limit);

	/** Times only the original Present call; observation overhead is recorded separately. */
	class PresentScope
	{
	public:
		PresentScope(IDXGISwapChain* swap, UINT sync, UINT flags, uint32_t completedFrame);
		~PresentScope();
		PresentScope(const PresentScope&) = delete;
		PresentScope& operator=(const PresentScope&) = delete;
		void BeginCall();
		void EndCall(HRESULT result);

	private:
		PresentRecord record_;
		bool active_ = false;
	};
}
#endif
