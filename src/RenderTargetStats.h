#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <vector>
#include <winrt/base.h>

/** Render-thread target intervals sharing the profiler's frame clock and query ownership. */
class RenderTargetStats
{
public:
	static constexpr uint32_t kMaxSpans = 256;
	static constexpr uint32_t kMaxBindings = 64;
	static constexpr const char* kCoverage = "Approximate D3D11 intervals by engine depth target and first colour target. Includes intervening work and waits; not isolated target cost. Both VR eyes are combined. NR queue timings remain separate. Do not add this table to pass or frame totals.";

	struct Row
	{
		uint32_t depthStencil = 0;
		uint32_t renderTarget = 0;
		float drawCalls = 0.0f;
		float gpuMs = 0.0f;
	};

	struct Frame
	{
		struct Span
		{
			winrt::com_ptr<ID3D11Query> begin;
			winrt::com_ptr<ID3D11Query> end;
			uint32_t binding = 0;
		};
		std::vector<Span> spans;
		std::array<uint32_t, kMaxBindings> draws{};
		uint32_t count = 0;
		int currentBinding = -1;
		bool open = false;
		bool tracked = false;
		bool limited = false;
		bool failed = false;
	};

	/** Reset metadata only after the owning profiler has retired this frame. */
	static void ResetFrame(Frame& frame);
	/** Counts a draw and changes intervals only when the engine target binding changes. */
	void CountDraw(Frame& frame, ID3D11DeviceContext* context, uint32_t depthStencil, uint32_t renderTarget) noexcept;
	/** Close the last interval before the shared frame-disjoint query ends. */
	static void CloseSpan(Frame& frame, ID3D11DeviceContext* context);
	/** Nonblocking readback; false retains the owning frame until all timestamps are ready. */
	bool ResolveFrame(Frame& frame, ID3D11DeviceContext* context, uint64_t frequency, bool clockValid);
	/** Keep a busy or unavailable query ring from presenting stale GPU values as current. */
	void MarkFrameUnavailable();
	/** Clears published rows; the profiler separately releases every frame's query pool. */
	void Clear();
	std::vector<Row> GetRows() const;
	float GetTimedMs() const { return timedMs; }
	uint64_t GetResolvedFrames() const { return resolvedFrames; }
	uint64_t GetInvalidFrames() const { return invalidFrames; }
	bool IsSpanLimitHit() const { return spanLimitHit; }
	bool IsBindingLimitHit() const { return bindingLimitHit; }
	bool HasResourceFailure() const { return resourceFailure; }
	bool HasGpuSample() const { return lastFrameValid; }

private:
	static constexpr float kSmoothing = 0.05f;
	int FindOrAddBinding(uint32_t depthStencil, uint32_t renderTarget);
	void BeginSpan(Frame& frame, ID3D11DeviceContext* context, uint32_t binding);
	std::vector<Row> bindings;
	float timedMs = 0.0f;
	uint64_t resolvedFrames = 0;
	uint64_t invalidFrames = 0;
	bool spanLimitHit = false;
	bool bindingLimitHit = false;
	bool resourceFailure = false;
	bool lastFrameValid = false;
};
