#pragma once
#include <atomic>
#include <d3d11_1.h>

#include "Buffer.h"
#include "Utils/DepthPyramid.h"

/** @brief Current main-depth max pyramid; incompatible targets fail open. */
class GrassHiZ
{
public:
	void SetupResources();
	void Reset();
	bool Build(ID3D11DeviceContext1* context, uint32_t frame);
	ID3D11ShaderResourceView* SRV() const { return pyramid.SRV(); }
	uint32_t Width() const { return width; }
	uint32_t Height() const { return height; }
	uint32_t Mips() const { return mipCount; }
	std::array<float, 4> Scale() const { return scale; }
	float DepthViewportSlack() const { return depthViewportSlack; }
#ifdef DEVBENCH_BRIDGE_ENABLED
	enum class Failure : size_t
	{
		Resources,
		MissingTarget,
		TargetView,
		TargetMismatch,
		TargetFormat,
		TargetLayout,
		TargetSRV,
		SourceTexture,
		DepthState,
		SourceLayout,
		Viewport,
		Extent,
		Count
	};
	void SetDiagnosticsEnabled(bool enabled) { diagnostics.store(enabled, std::memory_order_relaxed); }
	std::array<uint64_t, size_t(Failure::Count)> FailureCounts() const;
	Failure LastFailure() const { return lastFailure.load(std::memory_order_relaxed); }
	struct FailureObservation
	{
		uint32_t depthEnabled, depthFunction, viewportCount, sourceWidth, sourceHeight;
		float viewportX, viewportY, viewportWidth, viewportHeight, minDepth, maxDepth;
	};
	FailureObservation ObservedFailures() const;
#endif

private:
	void Allocate(uint32_t width, uint32_t height, uint32_t eyes);
#ifdef DEVBENCH_BRIDGE_ENABLED
	bool RecordFailure(Failure reason);
	void ObserveDepthState(const D3D11_DEPTH_STENCIL_DESC& state);
	void ObserveViewport(const D3D11_VIEWPORT& viewport, uint32_t count, const D3D11_TEXTURE2D_DESC& source);
	std::atomic_bool diagnostics{ false };
	std::atomic<Failure> lastFailure{ Failure::Count };
	std::array<std::atomic_uint64_t, size_t(Failure::Count)> failures{};
	std::array<std::atomic_uint32_t, 11> observed{};
#endif
	DepthPyramid pyramid;
	winrt::com_ptr<ID3D11Resource> sourceIdentity;
	winrt::com_ptr<ID3D11Resource> depthViewIdentity;
	winrt::com_ptr<ID3D11ShaderResourceView> depthView;
	uint32_t width = 0, height = 0, mipCount = 0, builtFrame = UINT32_MAX;
	uint32_t activeWidth = 0, activeHeight = 0;
	std::array<float, 4> scale{};
	float depthViewportSlack = 0;
};
