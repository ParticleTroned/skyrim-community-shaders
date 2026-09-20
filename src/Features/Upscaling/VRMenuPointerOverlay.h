#pragma once

#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

/** Captures the native pointer synchronously for a depth-independent, final UI overlay. */
class VRMenuPointerOverlay
{
public:
	struct DrawArguments
	{
		UINT indexCount = 0, instanceCount = 1, startIndex = 0;
		INT baseVertex = 0;
		UINT startInstance = 0;
		bool instanced = false;
	};

	/** Replays into a private layer without changing the caller's pipeline or suppressing its native draw. */
	bool Capture(ID3D11DeviceContext* context, ID3D11PixelShader* capturePS, const DrawArguments& draw,
		uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight,
		uint32_t frame, uint32_t generation, float offsetX, float offsetY, const char** reason);
	/** Bind the private layer only for final composition; restore its sole input slot on every exit. */
	class CompositeBinding
	{
	public:
		CompositeBinding(ID3D11DeviceContext* context, ID3D11ShaderResourceView* layer);
		~CompositeBinding();
		CompositeBinding(const CompositeBinding&) = delete;
		CompositeBinding& operator=(const CompositeBinding&) = delete;

	private:
		ID3D11DeviceContext* context = nullptr;
		winrt::com_ptr<ID3D11ShaderResourceView> previous;
	};
	/** Borrow this frame's layer for CompositeBinding only; never publish or retain it in other pipelines. */
	ID3D11ShaderResourceView* GetLayer(uint32_t frame, uint32_t generation) const noexcept;
	/** Invalidates this frame after a caller-side rejection without releasing reusable resources. */
	void Invalidate(uint32_t frame, uint32_t generation) noexcept;
	/** Releases resources and invalidates all captured content. */
	void Reset() noexcept;

private:
	bool EnsureResources(ID3D11Device* device, uint32_t width, uint32_t height, const char** reason);
	winrt::com_ptr<ID3D11Device> owner;
	winrt::com_ptr<ID3D11Device> failedOwner;
	winrt::com_ptr<ID3D11Texture2D> texture;
	winrt::com_ptr<ID3D11RenderTargetView> target;
	winrt::com_ptr<ID3D11ShaderResourceView> layer;
	winrt::com_ptr<ID3D11BlendState> blend;
	winrt::com_ptr<ID3D11DepthStencilState> depth;
	uint32_t width = 0, height = 0, capturedFrame = 0, capturedGeneration = 0;
	uint32_t failedWidth = 0, failedHeight = 0;
	bool attempted = false, captured = false, failed = false;
	char creationFailure[128]{};
};
