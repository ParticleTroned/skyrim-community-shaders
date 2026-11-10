#include "RenderTargetStats.h"

#include "Utils/ProfilerTiming.h"
#include "Utils/ResourceName.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

void RenderTargetStats::ResetFrame(Frame& frame)
{
	frame.draws.fill(0);
	frame.count = 0;
	frame.currentBinding = -1;
	frame.open = false;
	frame.tracked = false;
	frame.limited = false;
	frame.failed = false;
}

int RenderTargetStats::FindOrAddBinding(uint32_t depthStencil, uint32_t renderTarget)
{
	for (size_t i = 0; i < bindings.size(); ++i)
		if (bindings[i].depthStencil == depthStencil && bindings[i].renderTarget == renderTarget)
			return static_cast<int>(i);
	if (bindings.size() >= kMaxBindings) {
		bindingLimitHit = true;
		return -1;
	}
	bindings.push_back({ depthStencil, renderTarget });
	return static_cast<int>(bindings.size()) - 1;
}

void RenderTargetStats::BeginSpan(Frame& frame, ID3D11DeviceContext* context, uint32_t binding)
{
	if (frame.count == kMaxSpans) {
		frame.limited = true;
		return;
	}
	if (resourceFailure) {
		frame.failed = true;
		return;
	}
	if (frame.spans.size() <= frame.count)
		frame.spans.resize(frame.count + 1);
	auto& span = frame.spans[frame.count];
	if (!span.begin || !span.end) {
		winrt::com_ptr<ID3D11Device> device;
		context->GetDevice(device.put());
		const D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
		winrt::com_ptr<ID3D11Query> begin, end;
		if (!device || FAILED(device->CreateQuery(&desc, begin.put())) || FAILED(device->CreateQuery(&desc, end.put()))) {
			resourceFailure = frame.failed = true;
			return;
		}
		Util::SetResourceName(begin.get(), "Profiler::RenderTargetBegin[%u]", frame.count);
		Util::SetResourceName(end.get(), "Profiler::RenderTargetEnd[%u]", frame.count);
		span.begin = std::move(begin);
		span.end = std::move(end);
	}
	span.binding = binding;
	context->End(span.begin.get());
	frame.open = true;
}

void RenderTargetStats::CloseSpan(Frame& frame, ID3D11DeviceContext* context)
{
	if (!frame.open)
		return;
	context->End(frame.spans[frame.count].end.get());
	++frame.count;
	frame.open = false;
}

void RenderTargetStats::CountDraw(Frame& frame, ID3D11DeviceContext* context, uint32_t depthStencil, uint32_t renderTarget) noexcept
try {
	if (resourceFailure) {
		frame.failed = true;
		return;
	}
	if (frame.currentBinding < 0 || bindings[frame.currentBinding].depthStencil != depthStencil || bindings[frame.currentBinding].renderTarget != renderTarget) {
		CloseSpan(frame, context);
		frame.currentBinding = FindOrAddBinding(depthStencil, renderTarget);
		if (frame.currentBinding < 0) {
			frame.limited = true;
			return;
		}
		BeginSpan(frame, context, static_cast<uint32_t>(frame.currentBinding));
	}
	auto& draws = frame.draws[frame.currentBinding];
	if (draws != std::numeric_limits<uint32_t>::max())
		++draws;
} catch (const std::exception&) {
	frame.failed = resourceFailure = true;
}

bool RenderTargetStats::ResolveFrame(Frame& frame, ID3D11DeviceContext* context, uint64_t frequency, bool clockValid)
{
	if (!frame.tracked)
		return true;
	std::array<double, kMaxBindings> gpuMs{};
	bool valid = clockValid && frequency != 0 && !frame.failed;
	if (valid) {
		const double ticksToMs = 1000.0 / static_cast<double>(frequency);
		for (uint32_t i = 0; i < frame.count; ++i) {
			const auto& span = frame.spans[i];
			uint64_t begin = 0, end = 0;
			const auto beginResult = context->GetData(span.begin.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const auto endResult = context->GetData(span.end.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (beginResult == S_FALSE || endResult == S_FALSE)
				return false;
			const double ms = end >= begin ? static_cast<double>(end - begin) * ticksToMs : -1.0;
			if (beginResult != S_OK || endResult != S_OK || !Util::ProfilerTiming::IsValidSample(ms)) {
				valid = false;
				break;
			}
			gpuMs[span.binding] += ms;
		}
	}
	spanLimitHit = frame.limited;
	lastFrameValid = valid;
	if (valid)
		++resolvedFrames;
	else
		++invalidFrames;
	float total = 0.0f;
	for (size_t i = 0; i < bindings.size(); ++i) {
		auto& row = bindings[i];
		row.drawCalls = row.drawCalls * (1.0f - kSmoothing) + static_cast<float>(frame.draws[i]) * kSmoothing;
		row.gpuMs = valid ? row.gpuMs * (1.0f - kSmoothing) + static_cast<float>(gpuMs[i]) * kSmoothing : 0.0f;
		total += row.gpuMs;
	}
	timedMs = total;
	frame.tracked = false;
	return true;
}

void RenderTargetStats::MarkFrameUnavailable()
{
	++invalidFrames;
	lastFrameValid = false;
}

void RenderTargetStats::Clear()
{
	bindings.clear();
	timedMs = 0.0f;
	resolvedFrames = invalidFrames = 0;
	spanLimitHit = bindingLimitHit = resourceFailure = false;
	lastFrameValid = false;
}

std::vector<RenderTargetStats::Row> RenderTargetStats::GetRows() const
{
	auto rows = bindings;
	std::ranges::sort(rows, [](const Row& left, const Row& right) { return left.gpuMs > right.gpuMs; });
	return rows;
}
