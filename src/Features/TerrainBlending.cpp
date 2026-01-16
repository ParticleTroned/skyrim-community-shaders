#include "TerrainBlending.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>

#include "Deferred.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/Game.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainBlending::Settings,
	Enable,
	BlendRange,
	BlendShapeMode,
	BlendMode,
	DitherMode,
	EdgeStart,
	EdgeEnd,
	EdgeSlopeMode,
	SkipEdgeSamplesWhenNoGap,
	AngleStartDeg,
	AngleEndDeg,
	AngleRangeScale,
	AngleGainScale,
	BypassAngleEdge,
	ReplayCullDistance,
	ReplayCullMinPixels)

namespace
{
	struct RollingAverage
	{
		struct Sample
		{
			double timeSec;
			float value;
		};

		std::deque<Sample> samples;
		double sum = 0.0;

		void Reset()
		{
			samples.clear();
			sum = 0.0;
		}

		float Push(double nowSec, float value)
		{
			constexpr double kWindowSec = 5.0;
			samples.push_back({ nowSec, value });
			sum += value;
			const double cutoff = nowSec - kWindowSec;
			while (!samples.empty() && samples.front().timeSec < cutoff) {
				sum -= samples.front().value;
				samples.pop_front();
			}
			const size_t count = samples.size();
			return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : value;
		}
	};

	struct TbDebugStats
	{
		uint32_t prepassEnter = 0;
		uint32_t prepassExit = 0;
		uint32_t terrainPasses = 0;
		uint32_t terrainAccepted = 0;
		uint32_t terrainRejected = 0;
		uint32_t terrainToggleTrue = 0;
		uint32_t terrainToggleFalse = 0;
		uint32_t queuedTerrain = 0;
		uint32_t queuedExtra = 0;
		uint32_t blendDispatch = 0;
		uint32_t renderCalls = 0;
		uint32_t renderCallsWithWork = 0;
		size_t maxTerrainQueue = 0;
		size_t maxExtraQueue = 0;
		float terrainDistMin = 0.0f;
		float terrainDistMax = 0.0f;
		bool terrainDistInit = false;
		uint32_t mainWidth = 0;
		uint32_t mainHeight = 0;
		uint32_t mainArraySize = 0;
		int mainFormat = 0;
		int mainSrvDim = 0;
		bool mainInfoValid = false;
		float depthBlendGpuMs = 0.0f;
		float replayGpuMs = 0.0f;
		float depthBlendGpuMsLast = 0.0f;
		float replayGpuMsLast = 0.0f;
		float depthBlendCpuMs = 0.0f;
		float replayCpuMs = 0.0f;
		float depthBlendCpuMsLast = 0.0f;
		float replayCpuMsLast = 0.0f;
		RollingAverage depthBlendGpuAvg{};
		RollingAverage replayGpuAvg{};
		RollingAverage depthBlendCpuAvg{};
		RollingAverage replayCpuAvg{};

		void ResetCounts()
		{
			prepassEnter = 0;
			prepassExit = 0;
			terrainPasses = 0;
			terrainAccepted = 0;
			terrainRejected = 0;
			terrainToggleTrue = 0;
			terrainToggleFalse = 0;
			queuedTerrain = 0;
			queuedExtra = 0;
			blendDispatch = 0;
			renderCalls = 0;
			renderCallsWithWork = 0;
			maxTerrainQueue = 0;
			maxExtraQueue = 0;
			terrainDistMin = 0.0f;
			terrainDistMax = 0.0f;
			terrainDistInit = false;
			depthBlendGpuMs = 0.0f;
			replayGpuMs = 0.0f;
			depthBlendGpuMsLast = 0.0f;
			replayGpuMsLast = 0.0f;
			depthBlendCpuMs = 0.0f;
			replayCpuMs = 0.0f;
			depthBlendCpuMsLast = 0.0f;
			replayCpuMsLast = 0.0f;
			depthBlendGpuAvg.Reset();
			replayGpuAvg.Reset();
			depthBlendCpuAvg.Reset();
			replayCpuAvg.Reset();
		}
	};

	std::atomic<bool> g_tbStatsEnabled{ false };
	TbDebugStats g_tbStats{};

	double GetTbNowSeconds()
	{
		using clock = std::chrono::steady_clock;
		return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	}

	void UpdateTbStat(RollingAverage& history, float& smoothedMs, float& lastMs, float sampleMs)
	{
		lastMs = sampleMs;
		smoothedMs = history.Push(GetTbNowSeconds(), sampleMs);
	}

	constexpr uint32_t kTbGpuQueryRingSize = 4;

	struct TbGpuTimers
	{
		struct QueryPair
		{
			ID3D11Query* disjoint = nullptr;
			ID3D11Query* start = nullptr;
			ID3D11Query* end = nullptr;
			bool issued = false;
		};

		struct QueryRing
		{
			QueryPair slots[kTbGpuQueryRingSize]{};
			uint32_t nextIndex = 0;
		};

		QueryRing depth{};
		QueryRing replay{};
		bool ready = false;
		uint32_t lastResolvedFrame = std::numeric_limits<uint32_t>::max();

		void Release()
		{
			ReleaseRing(depth);
			ReleaseRing(replay);
			ready = false;
			lastResolvedFrame = std::numeric_limits<uint32_t>::max();
		}

		bool Ensure(ID3D11Device* device)
		{
			if (ready) {
				return true;
			}
			if (!device) {
				return false;
			}

			Release();
			for (auto& slot : depth.slots) {
				if (!CreatePair(device, slot)) {
					Release();
					return false;
				}
			}
			for (auto& slot : replay.slots) {
				if (!CreatePair(device, slot)) {
					Release();
					return false;
				}
			}

			ready = true;
			return true;
		}

		bool IsReady() const
		{
			return ready;
		}

		QueryPair* Begin(ID3D11DeviceContext* context, QueryRing& ring)
		{
			if (!ready || !context) {
				return nullptr;
			}
			auto* slot = AcquireSlot(ring);
			if (!slot) {
				return nullptr;
			}
			context->Begin(slot->disjoint);
			context->End(slot->start);
			slot->issued = true;
			return slot;
		}

		void End(ID3D11DeviceContext* context, QueryPair* slot)
		{
			if (!context || !slot) {
				return;
			}
			context->End(slot->end);
			context->End(slot->disjoint);
		}

		QueryPair* BeginDepth(ID3D11DeviceContext* context)
		{
			return Begin(context, depth);
		}

		QueryPair* BeginReplay(ID3D11DeviceContext* context)
		{
			return Begin(context, replay);
		}

		void EndDepth(ID3D11DeviceContext* context, QueryPair* slot)
		{
			End(context, slot);
		}

		void EndReplay(ID3D11DeviceContext* context, QueryPair* slot)
		{
			End(context, slot);
		}

		void Resolve(ID3D11DeviceContext* context, TbDebugStats& stats)
		{
			if (!ready || !context) {
				return;
			}

			if (auto* state = globals::state) {
				if (state->frameCount == lastResolvedFrame) {
					return;
				}
				lastResolvedFrame = state->frameCount;
			}

			float sampleMs = 0.0f;
			for (auto& slot : depth.slots) {
				if (ResolvePair(context, slot, sampleMs)) {
					UpdateTbStat(stats.depthBlendGpuAvg, stats.depthBlendGpuMs, stats.depthBlendGpuMsLast, sampleMs);
				}
			}
			for (auto& slot : replay.slots) {
				if (ResolvePair(context, slot, sampleMs)) {
					UpdateTbStat(stats.replayGpuAvg, stats.replayGpuMs, stats.replayGpuMsLast, sampleMs);
				}
			}
		}

	private:
		static void ReleasePair(QueryPair& pair)
		{
			if (pair.disjoint) {
				pair.disjoint->Release();
				pair.disjoint = nullptr;
			}
			if (pair.start) {
				pair.start->Release();
				pair.start = nullptr;
			}
			if (pair.end) {
				pair.end->Release();
				pair.end = nullptr;
			}
			pair.issued = false;
		}

		static void ReleaseRing(QueryRing& ring)
		{
			for (auto& slot : ring.slots) {
				ReleasePair(slot);
			}
			ring.nextIndex = 0;
		}

		static bool CreatePair(ID3D11Device* device, QueryPair& pair)
		{
			D3D11_QUERY_DESC desc{};
			desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			if (FAILED(device->CreateQuery(&desc, &pair.disjoint))) {
				return false;
			}
			desc.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(device->CreateQuery(&desc, &pair.start))) {
				return false;
			}
			if (FAILED(device->CreateQuery(&desc, &pair.end))) {
				return false;
			}
			return true;
		}

		static QueryPair* AcquireSlot(QueryRing& ring)
		{
			for (uint32_t i = 0; i < kTbGpuQueryRingSize; ++i) {
				uint32_t index = (ring.nextIndex + i) % kTbGpuQueryRingSize;
				auto& slot = ring.slots[index];
				if (!slot.issued) {
					ring.nextIndex = (index + 1) % kTbGpuQueryRingSize;
					return &slot;
				}
			}
			return nullptr;
		}

		static bool ResolvePair(ID3D11DeviceContext* context, QueryPair& pair, float& outMs)
		{
			if (!pair.issued) {
				return false;
			}

			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
			if (context->GetData(pair.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				return false;
			}

			UINT64 start = 0;
			if (context->GetData(pair.start, &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				return false;
			}

			UINT64 end = 0;
			if (context->GetData(pair.end, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
				return false;
			}

			pair.issued = false;

			if (disjoint.Disjoint || disjoint.Frequency == 0 || end < start) {
				return false;
			}

			outMs = static_cast<float>((end - start) * 1000.0 / static_cast<double>(disjoint.Frequency));
			return true;
		}
	};

	TbGpuTimers g_tbGpuTimers{};

	bool TbStatsEnabled()
	{
		return g_tbStatsEnabled.load(std::memory_order_relaxed);
	}

	TerrainBlending::Settings MakeDefaultSettings()
	{
		TerrainBlending::Settings defaults{};
		defaults.BlendMode = 0;
		defaults.BlendShapeMode = 0;

		return defaults;
	}
}

ID3D11VertexShader* TerrainBlending::GetTerrainVertexShader()
{
	if (!terrainVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" } }, "vs_5_0");
	}
	return terrainVertexShader;
}

ID3D11VertexShader* TerrainBlending::GetTerrainOffsetVertexShader()
{
	if (!terrainOffsetVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainOffsetVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" }, { "OFFSET_DEPTH", "" } }, "vs_5_0");
	}
	return terrainOffsetVertexShader;
}

ID3D11ComputeShader* TerrainBlending::GetDepthBlendShader()
{
	if (!depthBlendShader) {
		logger::debug("Compiling DepthBlend.hlsl");
		depthBlendShader = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl", {}, "cs_5_0");
	}
	return depthBlendShader;
}


void TerrainBlending::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc;
		mainDepth.texture->GetDesc(&texDesc);
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, NULL, &terrainDepth.texture));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		mainDepth.depthSRV->GetDesc(&srvDesc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(terrainDepth.texture, &srvDesc, &terrainDepth.depthSRV));

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		mainDepth.views[0]->GetDesc(&dsvDesc);
		DX::ThrowIfFailed(device->CreateDepthStencilView(terrainDepth.texture, &dsvDesc, &terrainDepth.views[0]));

		g_tbStats.mainWidth = texDesc.Width;
		g_tbStats.mainHeight = texDesc.Height;
		g_tbStats.mainArraySize = texDesc.ArraySize;
		g_tbStats.mainFormat = static_cast<int>(texDesc.Format);
		g_tbStats.mainSrvDim = static_cast<int>(srvDesc.ViewDimension);
		g_tbStats.mainInfoValid = true;
	}

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		blendedDepthTexture = new Texture2D(texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		main.SRV->GetDesc(&srvDesc);
		srvDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		main.UAV->GetDesc(&uavDesc);
		uavDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateUAV(uavDesc);

		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		depthSRVBackup = mainDepth.depthSRV;

		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		prepassSRVBackup = zPrepassCopy.depthSRV;
	}

	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = false;
		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &terrainDepthStencilState));
	}
}

void TerrainBlending::PostPostLoad()
{
	Hooks::Install();
}

void TerrainBlending::DataLoaded()
{
	auto bEnableLandFade = RE::GetINISetting("bEnableLandFade:Display");
	bEnableLandFade->data.b = false;
}

TerrainBlending::PerFrame TerrainBlending::GetCommonBufferData()
{
	PerFrame data{};
	data.BlendRange = settings.BlendRange;
	data.BlendShapeMode = settings.BlendShapeMode;
	data.BlendMode = std::min<uint>(settings.BlendMode, 1u);
	data.DitherMode = std::min<uint>(settings.DitherMode, 1u);
	data.EdgeStart = std::max(0.0f, settings.EdgeStart);
	data.EdgeEnd = std::max(data.EdgeStart + 1e-3f, settings.EdgeEnd);
	data.EdgeSlopeMode = std::min<uint>(settings.EdgeSlopeMode, 2u);
	data.SkipEdgeSamplesWhenNoGap = settings.SkipEdgeSamplesWhenNoGap ? 1u : 0u;
	float angleStartDeg = std::max(0.0f, settings.AngleStartDeg);
	float angleEndDeg = std::max(angleStartDeg + 1e-3f, settings.AngleEndDeg);
	constexpr float kDegToRad = 3.14159265359f / 180.0f;
	data.AngleStartCos = std::cos(angleStartDeg * kDegToRad);
	data.AngleEndCos = std::cos(angleEndDeg * kDegToRad);
	data.AngleRangeScale = std::max(0.0f, settings.AngleRangeScale);
	data.AngleGainScale = std::max(0.0f, settings.AngleGainScale);
	data.BypassAngleEdge = settings.BypassAngleEdge ? 1u : 0u;
	return data;
}

void TerrainBlending::DrawSettings()
{
	if (ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto tooltip = [](const char* text) {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(text);
			}
		};

		ImGui::Checkbox("Enable", &settings.Enable);
		tooltip("Toggle terrain overlay blending.");
		ImGui::Spacing();

		ImGui::Text("Performance");
		ImGui::Separator();
		ImGui::SliderFloat("Cull Distance", &settings.ReplayCullDistance, 0.0f, 8192.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Skip blending beyond this distance (0 disables culling).");
		ImGui::SliderFloat("Cull Min Pixels", &settings.ReplayCullMinPixels, 0.0f, 256.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Skip blending for tiny projected patches (0 disables culling).");
		ImGui::Checkbox("Bypass Angle/Edge", &settings.BypassAngleEdge);
		tooltip("Disable angle scaling and slope bias.");
		ImGui::Checkbox("Skip Edge Samples When No Gap", &settings.SkipEdgeSamplesWhenNoGap);
		tooltip("Avoid neighbor depth samples when there is no front-gap.");
		bool captureTimings = TbStatsEnabled();
		if (ImGui::Checkbox("Capture CPU/GPU Timings", &captureTimings)) {
			if (captureTimings != TbStatsEnabled()) {
				ToggleDebugCapture();
			}
		}
		tooltip("Record CPU/GPU timings for TB passes (debug). First value is a 5s rolling average.");
		if (captureTimings) {
			if (!g_tbGpuTimers.IsReady()) {
				ImGui::TextUnformatted("GPU timing queries unavailable.");
			} else {
				const float depthTotalMs = g_tbStats.depthBlendGpuMs + g_tbStats.depthBlendCpuMs;
				const float depthTotalLastMs = g_tbStats.depthBlendGpuMsLast + g_tbStats.depthBlendCpuMsLast;
				const float replayTotalMs = g_tbStats.replayGpuMs + g_tbStats.replayCpuMs;
				const float replayTotalLastMs = g_tbStats.replayGpuMsLast + g_tbStats.replayCpuMsLast;
				const float tbTotalGpuMs = g_tbStats.depthBlendGpuMs + g_tbStats.replayGpuMs;
				const float tbTotalGpuLastMs = g_tbStats.depthBlendGpuMsLast + g_tbStats.replayGpuMsLast;
				const float tbTotalCpuMs = g_tbStats.depthBlendCpuMs + g_tbStats.replayCpuMs;
				const float tbTotalCpuLastMs = g_tbStats.depthBlendCpuMsLast + g_tbStats.replayCpuMsLast;
				const float tbTotalMs = tbTotalGpuMs + tbTotalCpuMs;
				const float tbTotalLastMs = tbTotalGpuLastMs + tbTotalCpuLastMs;

				constexpr float kPassColWidth = 120.0f;
				constexpr float kTimingColWidth = 140.0f;
				if (ImGui::BeginTable("Capture CPU/GPU Timings", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
					ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, kPassColWidth);
					ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthFixed, kTimingColWidth);
					ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, kTimingColWidth);
					ImGui::TableSetupColumn("Total (ms)", ImGuiTableColumnFlags_WidthFixed, kTimingColWidth);
					ImGui::TableHeadersRow();

					auto timingRow = [](const char* label, float gpuMs, float gpuLast, float cpuMs, float cpuLast, float totalMs, float totalLast) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(label);
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%7.3f / %7.3f", gpuMs, gpuLast);
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%7.3f / %7.3f", cpuMs, cpuLast);
						ImGui::TableSetColumnIndex(3);
						ImGui::Text("%7.3f / %7.3f", totalMs, totalLast);
					};

					timingRow("DepthBlend",
						g_tbStats.depthBlendGpuMs, g_tbStats.depthBlendGpuMsLast,
						g_tbStats.depthBlendCpuMs, g_tbStats.depthBlendCpuMsLast,
						depthTotalMs, depthTotalLastMs);
					timingRow("Replay",
						g_tbStats.replayGpuMs, g_tbStats.replayGpuMsLast,
						g_tbStats.replayCpuMs, g_tbStats.replayCpuMsLast,
						replayTotalMs, replayTotalLastMs);
					timingRow("Total",
						tbTotalGpuMs, tbTotalGpuLastMs,
						tbTotalCpuMs, tbTotalCpuLastMs,
						tbTotalMs, tbTotalLastMs);
					ImGui::EndTable();
				}
			}
		}
		ImGui::Spacing();

		ImGui::Text("Blending");
		ImGui::Separator();
		ImGui::SliderFloat("Blend Range", &settings.BlendRange, 1.0f, 50.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Depth range over which the blend fades.");
		ImGui::Combo("Blend Shape", (int*)&settings.BlendShapeMode, "Linear\0Squared\0Sqrt\0");
		tooltip("Falloff curve for the blend.");
		ImGui::Combo("Blend Mode", (int*)&settings.BlendMode, "Alpha\0Stochastic\0");
		tooltip("Alpha blending or stochastic coverage.");
		if (settings.BlendMode == 1) {
			ImGui::Combo("Dither Mode", (int*)&settings.DitherMode, "Ordered 4x4\0Noise\0");
			tooltip("Ordered or noise dither for stochastic coverage.");
		}
		ImGui::Spacing();

		ImGui::Text("Edge Detection");
		ImGui::Separator();
		float edgeStartMax = std::max(1.0f, settings.BlendRange);
		float edgeEndMax = std::max(2.0f, settings.BlendRange * 2.0f);
		constexpr float edgeExponent = 4.0f;
		auto edgeSlider = [&](const char* label, float* value, float maxValue, const char* help) {
			float clampedValue = std::min(std::max(*value, 0.0f), maxValue);
			float normalized = maxValue > 0.0f ? std::pow(clampedValue / maxValue, 1.0f / edgeExponent) : 0.0f;
			if (ImGui::SliderFloat(label, &normalized, 0.0f, 1.0f, "%.6f", ImGuiSliderFlags_AlwaysClamp)) {
				clampedValue = std::pow(normalized, edgeExponent) * maxValue;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(help);
				ImGui::Separator();
				ImGui::Text("Real: %.6e", *value);
			}
			*value = clampedValue;
		};
		edgeSlider("Edge Start", &settings.EdgeStart, edgeStartMax, "Depth discontinuity where edge blending begins.");
		edgeSlider("Edge End", &settings.EdgeEnd, edgeEndMax, "Depth discontinuity where edge blending is full.");
		ImGui::Spacing();

		if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_None)) {
			ImGui::Combo("Edge Slope Mode", (int*)&settings.EdgeSlopeMode, "View\0Mesh\0None\0");
			tooltip("Slope source for edge bias.");
			ImGui::SliderFloat("Angle Start", &settings.AngleStartDeg, 0.0f, 45.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Angle where edge scaling begins.");
			ImGui::SliderFloat("Angle End", &settings.AngleEndDeg, 0.0f, 90.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Angle where edge scaling is full.");
			ImGui::SliderFloat("Angle Range Scale", &settings.AngleRangeScale, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Edge range multiplier at Angle End.");
			ImGui::SliderFloat("Angle Gain Scale", &settings.AngleGainScale, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Edge gain multiplier at Angle End.");
			ImGui::TreePop();
		}

		settings.EdgeStart = std::max(0.0f, settings.EdgeStart);
		settings.EdgeEnd = std::max(settings.EdgeEnd, settings.EdgeStart + 1e-3f);
	settings.EdgeSlopeMode = std::min<uint>(settings.EdgeSlopeMode, 2u);
	settings.BlendMode = std::min<uint>(settings.BlendMode, 1u);
	settings.DitherMode = std::min<uint>(settings.DitherMode, 1u);
		settings.AngleStartDeg = std::max(0.0f, settings.AngleStartDeg);
		settings.AngleEndDeg = std::max(settings.AngleEndDeg, settings.AngleStartDeg + 1e-3f);
		settings.AngleRangeScale = std::max(0.0f, settings.AngleRangeScale);
		settings.AngleGainScale = std::max(0.0f, settings.AngleGainScale);
		settings.ReplayCullDistance = std::max(0.0f, settings.ReplayCullDistance);
		settings.ReplayCullMinPixels = std::max(0.0f, settings.ReplayCullMinPixels);
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void TerrainBlending::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainBlending::RestoreDefaultSettings()
{
	settings = MakeDefaultSettings();
}

void TerrainBlending::TerrainShaderHacks()
{
	if (!settings.Enable) {
		if (renderTerrainDepth) {
			renderTerrainDepth = false;
			ResetTerrainDepth();
		}
		renderDepth = false;
		renderAltTerrain = false;
		return;
	}

	if (renderTerrainDepth) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;
		if (!renderAltTerrain) {
			auto dsv = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			auto shadowState = globals::game::shadowState;
			GET_INSTANCE_MEMBER(currentVertexShader, shadowState)
			context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
		} else {
			auto dsv = terrainDepth.views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			context->VSSetShader(GetTerrainOffsetVertexShader(), NULL, NULL);
		}
		renderAltTerrain = !renderAltTerrain;
	}
}

void TerrainBlending::ResetDepth()
{
	auto context = globals::d3d::context;

	auto dsv = terrainDepth.views[0];
	context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0u);
}

void TerrainBlending::ResetTerrainDepth()
{
	auto context = globals::d3d::context;

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto currentVertexShader = *globals::game::currentVertexShader;
	context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
}

void TerrainBlending::BlendPrepassDepths()
{
	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();
	if (TbStatsEnabled()) {
		g_tbStats.blendDispatch++;
		g_tbGpuTimers.Ensure(globals::d3d::device);
		g_tbGpuTimers.Resolve(context, g_tbStats);
	}

	const bool statsEnabled = TbStatsEnabled();
	std::chrono::steady_clock::time_point cpuStart{};
	if (statsEnabled) {
		cpuStart = std::chrono::steady_clock::now();
	}

	{
		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { blendedDepthTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		TbGpuTimers::QueryPair* depthQuery = nullptr;
		if (TbStatsEnabled()) {
			depthQuery = g_tbGpuTimers.BeginDepth(context);
		}
		context->CSSetShader(GetDepthBlendShader(), nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		g_tbGpuTimers.EndDepth(context, depthQuery);
	}

	if (statsEnabled) {
		const auto cpuEnd = std::chrono::steady_clock::now();
		const float cpuMs = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
		UpdateTbStat(g_tbStats.depthBlendCpuAvg, g_tbStats.depthBlendCpuMs, g_tbStats.depthBlendCpuMsLast, cpuMs);
	}

	{
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	}
	{
		ID3D11ShaderResourceView* nullViews[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
	}

	ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void TerrainBlending::ClearShaderCache()
{
	if (terrainVertexShader) {
		terrainVertexShader->Release();
		terrainVertexShader = nullptr;
	}
	if (terrainOffsetVertexShader) {
		terrainOffsetVertexShader->Release();
		terrainOffsetVertexShader = nullptr;
	}
	if (depthBlendShader) {
		depthBlendShader->Release();
		depthBlendShader = nullptr;
	}
	if (terrainScissorState) {
		terrainScissorState->Release();
		terrainScissorState = nullptr;
	}
	if (terrainScissorBaseState) {
		terrainScissorBaseState->Release();
		terrainScissorBaseState = nullptr;
	}
}

void TerrainBlending::Hooks::Main_RenderDepth::thunk(bool a1, bool a2)
{
	// Keep TB depth state in sync while avoiding VR shadow/aux depth phases and restoring SRVs when disabled.
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;

	singleton.averageEyePosition = Util::GetAverageEyePosition();

	if (!shaderCache || !shaderCache->IsEnabled() || !singleton.settings.Enable) {
		if (renderer) {
			auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
			mainDepth.depthSRV = singleton.depthSRVBackup;
			zPrepassCopy.depthSRV = singleton.prepassSRVBackup;
		}

		singleton.renderDepth = false;
		if (singleton.renderTerrainDepth) {
			singleton.renderTerrainDepth = false;
			singleton.ResetTerrainDepth();
		}
		singleton.renderAltTerrain = false;
	}

	func(a1, a2);
}

void TerrainBlending::Hooks::BSBatchRenderer__RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	// Detect the main depth-only prepass and queue terrain/extra passes for replay with optional culling.
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	const bool statsEnabled = TbStatsEnabled();
	const float replayCullDistance = std::max(0.0f, singleton.settings.ReplayCullDistance);
	const float replayCullMinPixels = std::max(0.0f, singleton.settings.ReplayCullMinPixels);
	float pixelsPerUnit = 0.0f;
	if (replayCullMinPixels > 0.0f && globals::state) {
		const float2 screenSize = Util::ConvertToDynamic(globals::state->screenSize);
		const float screenHeight = std::max(1.0f, screenSize.y);
		const float tanHalfFov = std::tan(Util::GetVerticalFOVRad() * 0.5f);
		if (tanHalfFov > 1e-4f) {
			pixelsPerUnit = (screenHeight * 0.5f) / tanHalfFov;
		}
	}
	auto ShouldCullByScreenSize = [&](const RE::NiPoint3& center, float radius) {
		if (replayCullMinPixels <= 0.0f || pixelsPerUnit <= 0.0f) {
			return false;
		}
		const float centerDist = std::max(center.GetDistance(singleton.averageEyePosition), 1.0f);
		const float pixelDiameter = (radius / centerDist) * pixelsPerUnit * 2.0f;
		return pixelDiameter < replayCullMinPixels;
	};

	if (!shaderCache || !shaderCache->IsEnabled() || !singleton.settings.Enable || !renderer || !context) {
		if (!singleton.settings.Enable) {
			if (singleton.renderTerrainDepth) {
				singleton.renderTerrainDepth = false;
				singleton.ResetTerrainDepth();
			}
			singleton.renderDepth = false;
			singleton.renderAltTerrain = false;
			singleton.terrainRenderPasses.clear();
			singleton.renderPasses.clear();
		}
		func(a_pass, a_technique, a_alphaTest, a_renderFlags);
		return;
	}

	if (renderer && context) {
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* dsv = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

		bool depthOnly = true;
		for (auto* rtv : rtvs) {
			if (rtv) {
				depthOnly = false;
				break;
			}
		}

		bool matchesMain = false;
		if (dsv) {
			ID3D11Resource* res = nullptr;
			dsv->GetResource(&res);
			if (res) {
				ID3D11Texture2D* tex = nullptr;
				if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
					matchesMain = (tex == mainDepth.texture);
					tex->Release();
				}
				res->Release();
			}
		}

		for (auto*& rtv : rtvs) {
			if (rtv) {
				rtv->Release();
				rtv = nullptr;
			}
		}
		if (dsv) {
			dsv->Release();
			dsv = nullptr;
		}

		const bool isMainDepthPrepass = depthOnly && matchesMain;

		if (isMainDepthPrepass && !singleton.renderDepth) {
			singleton.averageEyePosition = Util::GetAverageEyePosition();

			singleton.depthSRVBackup = mainDepth.depthSRV;
			singleton.prepassSRVBackup = zPrepassCopy.depthSRV;
			singleton.renderAltTerrain = false;
			if (singleton.terrainDepth.views[0]) {
				context->ClearDepthStencilView(singleton.terrainDepth.views[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
			}

			singleton.renderDepth = true;
			singleton.ResetDepth();
			if (statsEnabled) {
				g_tbStats.prepassEnter++;
			}
		}
		else if (!isMainDepthPrepass && singleton.renderDepth) {
			singleton.renderDepth = false;

			if (singleton.renderTerrainDepth) {
				singleton.renderTerrainDepth = false;
				singleton.ResetTerrainDepth();
			}

			singleton.BlendPrepassDepths();
			mainDepth.depthSRV = singleton.depthSRVBackup;
			zPrepassCopy.depthSRV = singleton.prepassSRVBackup;
			if (statsEnabled) {
				g_tbStats.prepassExit++;
			}
		}
	}

	if (shaderCache->IsEnabled()) {
		if (singleton.renderDepth) {
			bool inTerrain = a_pass->shaderProperty && a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape);

			if (inTerrain) {
				if (statsEnabled) {
					g_tbStats.terrainPasses++;
				}
				const auto& worldBound = a_pass->geometry->worldBound;
				const float terrainDist = worldBound.center.GetDistance(singleton.averageEyePosition) - worldBound.radius;
				if (statsEnabled) {
					if (!g_tbStats.terrainDistInit) {
						g_tbStats.terrainDistMin = terrainDist;
						g_tbStats.terrainDistMax = terrainDist;
						g_tbStats.terrainDistInit = true;
					} else {
						g_tbStats.terrainDistMin = std::min(g_tbStats.terrainDistMin, terrainDist);
						g_tbStats.terrainDistMax = std::max(g_tbStats.terrainDistMax, terrainDist);
					}
				}
				if ((replayCullDistance > 0.0f && terrainDist > replayCullDistance) ||
					ShouldCullByScreenSize(worldBound.center, worldBound.radius)) {
					inTerrain = false;
					if (statsEnabled) {
						g_tbStats.terrainRejected++;
					}
				} else if (statsEnabled) {
					g_tbStats.terrainAccepted++;
				}
			}

			if (singleton.renderTerrainDepth != inTerrain) {
				if (statsEnabled) {
					if (inTerrain) {
						g_tbStats.terrainToggleTrue++;
					} else {
						g_tbStats.terrainToggleFalse++;
					}
				}
				if (!inTerrain)
					singleton.ResetTerrainDepth();
				singleton.renderTerrainDepth = inTerrain;
			}

			if (inTerrain)
				func(a_pass, a_technique, a_alphaTest, a_renderFlags);
		} else if (globals::state->inWorld) {
			if (auto shaderProperty = a_pass->shaderProperty) {
				if (a_pass->shader->shaderType.get() == RE::BSShader::Type::Lighting) {
					float replayDist = 0.0f;
					bool replayCull = false;
					if (a_pass->geometry) {
						const auto& worldBound = a_pass->geometry->worldBound;
						replayDist = worldBound.center.GetDistance(singleton.averageEyePosition) - worldBound.radius;
						replayCull = (replayCullDistance > 0.0f && replayDist > replayCullDistance) ||
						             ShouldCullByScreenSize(worldBound.center, worldBound.radius);
					}
					if (shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
						if (!replayCull) {
							RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
							singleton.terrainRenderPasses.push_back(call);
							if (statsEnabled) {
								g_tbStats.queuedTerrain++;
							}
							return;
						}
					}

					if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample)) {
						if (!replayCull) {
							RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
							singleton.renderPasses.push_back(call);
							if (statsEnabled) {
								g_tbStats.queuedExtra++;
							}
							return;
						}
					}
				}
			}
		}
	}
	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void TerrainBlending::RenderTerrainBlendingPasses()
{
	// Replay queued passes with the blended depth mask, scissoring, and alpha/depth state overrides.
	if (!settings.Enable) {
		terrainRenderPasses.clear();
		renderPasses.clear();
		return;
	}

	struct ScopedReplayFlag
	{
		TerrainBlending& owner;
		State* state;
		explicit ScopedReplayFlag(TerrainBlending& a_owner) :
			owner(a_owner),
			state(globals::state)
		{
			owner.inTBReplay = true;
			if (state) {
				state->SetTerrainBlendingReplayActive(true);
			}
		}
		~ScopedReplayFlag()
		{
			owner.inTBReplay = false;
			if (state) {
				state->SetTerrainBlendingReplayActive(false);
			}
		}
	};

	ScopedReplayFlag replayFlag(*this);

	auto context = globals::d3d::context;
	auto device = globals::d3d::device;
	auto shadowState = globals::game::shadowState;
	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	const bool statsEnabled = TbStatsEnabled();
	if (statsEnabled) {
		g_tbGpuTimers.Ensure(device);
		g_tbGpuTimers.Resolve(context, g_tbStats);
	}
	const bool hasWork = !terrainRenderPasses.empty() || !renderPasses.empty();
	std::chrono::steady_clock::time_point replayCpuStart{};
	if (statsEnabled && hasWork) {
		replayCpuStart = std::chrono::steady_clock::now();
	}
	if (statsEnabled) {
		g_tbStats.renderCalls++;
		if (hasWork) {
			g_tbStats.renderCallsWithWork++;
		}
		g_tbStats.maxTerrainQueue = std::max(g_tbStats.maxTerrainQueue, terrainRenderPasses.size());
		g_tbStats.maxExtraQueue = std::max(g_tbStats.maxExtraQueue, renderPasses.size());
	}

	auto drawPass = [&](const RenderPass& renderPass) {
		Hooks::BSBatchRenderer__RenderPassImmediately::func(
			renderPass.a_pass,
			renderPass.a_technique,
			renderPass.a_alphaTest,
			renderPass.a_renderFlags);
	};

	Texture2D* maskTexture = blendedDepthTexture;
	ID3D11ShaderResourceView* views[1] = {
		maskTexture ? maskTexture->srv.get() : nullptr
	};
	context->PSSetShaderResources(55, ARRAYSIZE(views), views);

	if (hasWork) {
		TbGpuTimers::QueryPair* replayQuery = nullptr;
		if (statsEnabled) {
			replayQuery = g_tbGpuTimers.BeginReplay(context);
		}

		ID3D11DepthStencilState* prevDSS = nullptr;
		UINT prevStencilRef = 0;
		context->OMGetDepthStencilState(&prevDSS, &prevStencilRef);

		ID3D11RasterizerState* prevRS = nullptr;
		context->RSGetState(&prevRS);

		UINT prevScissorCount = 0;
		context->RSGetScissorRects(&prevScissorCount, nullptr);
		auto& prevScissorRects = prevScissorRectsCache;
		prevScissorRects.clear();
		if (prevScissorCount > 0) {
			prevScissorRects.resize(prevScissorCount);
			context->RSGetScissorRects(&prevScissorCount, prevScissorRects.data());
		}

		UINT viewportCount = 0;
		context->RSGetViewports(&viewportCount, nullptr);
		auto& viewports = viewportsCache;
		viewports.clear();
		if (viewportCount > 0) {
			viewports.resize(viewportCount);
			context->RSGetViewports(&viewportCount, viewports.data());
		}

		bool scissorActive = false;
		ID3D11RasterizerState* scissorState = prevRS;
		if (prevRS && viewportCount > 0) {
			D3D11_RASTERIZER_DESC rsDesc{};
			prevRS->GetDesc(&rsDesc);
			if (rsDesc.ScissorEnable) {
				scissorActive = true;
			} else if (device) {
				if (terrainScissorBaseState != prevRS) {
					if (terrainScissorState) {
						terrainScissorState->Release();
						terrainScissorState = nullptr;
					}
					if (terrainScissorBaseState) {
						terrainScissorBaseState->Release();
						terrainScissorBaseState = nullptr;
					}

					rsDesc.ScissorEnable = true;
					if (SUCCEEDED(device->CreateRasterizerState(&rsDesc, &terrainScissorState))) {
						terrainScissorBaseState = prevRS;
						terrainScissorBaseState->AddRef();
					}
				}
				if (terrainScissorState) {
					scissorState = terrainScissorState;
					scissorActive = true;
				}
			}
		}
		if (scissorState && scissorState != prevRS) {
			context->RSSetState(scissorState);
		}

		auto& scissorRects = scissorRectsCache;
		auto& fullScissorRects = fullScissorRectsCache;
		Matrix viewMat[2]{};
		Matrix projMat[2]{};
		Matrix viewProjMat[2]{};
		const bool vrEnabled = REL::Module::IsVR();
		const uint32_t eyeCount = (vrEnabled && viewportCount >= 2) ? 2u : 1u;
		if (scissorActive) {
			scissorRects.resize(viewportCount);
			fullScissorRects.resize(viewportCount);

			for (uint32_t i = 0; i < viewportCount; ++i) {
				const auto& vp = viewports[i];
				const float vpLeft = vp.TopLeftX;
				const float vpTop = vp.TopLeftY;
				const float vpRight = vp.TopLeftX + vp.Width;
				const float vpBottom = vp.TopLeftY + vp.Height;
				fullScissorRects[i].left = static_cast<LONG>(std::floor(vpLeft));
				fullScissorRects[i].top = static_cast<LONG>(std::floor(vpTop));
				fullScissorRects[i].right = static_cast<LONG>(std::ceil(vpRight));
				fullScissorRects[i].bottom = static_cast<LONG>(std::ceil(vpBottom));
			}

			auto& frameBuffer = globals::game::frameBufferCached;
			for (uint32_t eye = 0; eye < eyeCount; ++eye) {
				viewMat[eye] = frameBuffer.GetCameraView(eye);
				projMat[eye] = frameBuffer.GetCameraProjUnjittered(eye);
				viewProjMat[eye] = frameBuffer.GetCameraViewProjUnjittered(eye);
			}
		}

		auto setScissorForPass = [&](const RenderPass& renderPass) {
			if (!scissorActive || viewportCount == 0) {
				return;
			}
			if (!renderPass.a_pass || !renderPass.a_pass->geometry) {
				context->RSSetScissorRects(viewportCount, fullScissorRects.data());
				return;
			}

			const auto& worldBound = renderPass.a_pass->geometry->worldBound;
			const float radius = worldBound.radius;
			const float3 center = { worldBound.center.x, worldBound.center.y, worldBound.center.z };

			for (uint32_t i = 0; i < viewportCount; ++i) {
				const auto& vp = viewports[i];
				const uint32_t eyeIndex = (eyeCount > 1) ? (i % eyeCount) : 0u;
				const auto viewPos = DirectX::SimpleMath::Vector3::Transform(center, viewMat[eyeIndex]);

				if (viewPos.z <= 1e-3f) {
					scissorRects[i] = fullScissorRects[i];
					continue;
				}

				const auto clipPos = DirectX::SimpleMath::Vector4::Transform(float4(center.x, center.y, center.z, 1.0f), viewProjMat[eyeIndex]);
				if (clipPos.w <= 1e-3f) {
					scissorRects[i] = fullScissorRects[i];
					continue;
				}

				const float ndcX = clipPos.x / clipPos.w;
				const float ndcY = clipPos.y / clipPos.w;
				const float radiusNdcX = (radius * projMat[eyeIndex]._11) / viewPos.z;
				const float radiusNdcY = (radius * projMat[eyeIndex]._22) / viewPos.z;

				if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(radiusNdcX) || !std::isfinite(radiusNdcY)) {
					scissorRects[i] = fullScissorRects[i];
					continue;
				}

				const float centerPxX = (ndcX * 0.5f + 0.5f) * vp.Width + vp.TopLeftX;
				const float centerPxY = (-ndcY * 0.5f + 0.5f) * vp.Height + vp.TopLeftY;
				const float radiusPxX = std::abs(radiusNdcX) * 0.5f * vp.Width;
				const float radiusPxY = std::abs(radiusNdcY) * 0.5f * vp.Height;

				float left = centerPxX - radiusPxX;
				float right = centerPxX + radiusPxX;
				float top = centerPxY - radiusPxY;
				float bottom = centerPxY + radiusPxY;

				const float vpLeft = vp.TopLeftX;
				const float vpTop = vp.TopLeftY;
				const float vpRight = vp.TopLeftX + vp.Width;
				const float vpBottom = vp.TopLeftY + vp.Height;

				left = std::clamp(left, vpLeft, vpRight);
				right = std::clamp(right, vpLeft, vpRight);
				top = std::clamp(top, vpTop, vpBottom);
				bottom = std::clamp(bottom, vpTop, vpBottom);

				if (right <= left || bottom <= top) {
					scissorRects[i] = fullScissorRects[i];
					continue;
				}

				scissorRects[i].left = static_cast<LONG>(std::floor(left));
				scissorRects[i].top = static_cast<LONG>(std::floor(top));
				scissorRects[i].right = static_cast<LONG>(std::ceil(right));
				scissorRects[i].bottom = static_cast<LONG>(std::ceil(bottom));
			}

			context->RSSetScissorRects(viewportCount, scissorRects.data());
		};

		GET_INSTANCE_MEMBER(alphaBlendMode, shadowState)
		GET_INSTANCE_MEMBER(alphaBlendWriteMode, shadowState)
		GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState)

		alphaBlendWriteMode = 1;
		alphaBlendMode = 1;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		context->OMSetDepthStencilState(terrainDepthStencilState, 0xFF);

		for (auto& renderPass : terrainRenderPasses) {
			setScissorForPass(renderPass);
			drawPass(renderPass);
		}

		alphaBlendMode = 0;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

		for (auto& renderPass : renderPasses) {
			setScissorForPass(renderPass);
			drawPass(renderPass);
		}

		context->OMSetDepthStencilState(prevDSS, prevStencilRef);
		if (prevDSS) {
			prevDSS->Release();
			prevDSS = nullptr;
		}

		if (prevRS || scissorState) {
			context->RSSetState(prevRS);
		}
		if (prevScissorCount > 0 && !prevScissorRects.empty()) {
			context->RSSetScissorRects(prevScissorCount, prevScissorRects.data());
		}
		if (prevRS) {
			prevRS->Release();
			prevRS = nullptr;
		}

		terrainRenderPasses.clear();
		renderPasses.clear();

		g_tbGpuTimers.EndReplay(context, replayQuery);
	}

	if (statsEnabled && hasWork) {
		const auto replayCpuEnd = std::chrono::steady_clock::now();
		const float cpuMs = std::chrono::duration<float, std::milli>(replayCpuEnd - replayCpuStart).count();
		UpdateTbStat(g_tbStats.replayCpuAvg, g_tbStats.replayCpuMs, g_tbStats.replayCpuMsLast, cpuMs);
	}

}

void TerrainBlending::ToggleDebugCapture()
{
	const bool newEnabled = !g_tbStatsEnabled.load(std::memory_order_relaxed);
	g_tbStatsEnabled.store(newEnabled, std::memory_order_relaxed);
	g_tbStats.ResetCounts();
	if (newEnabled) {
		g_tbGpuTimers.Ensure(globals::d3d::device);
	} else {
		g_tbGpuTimers.Release();
	}
	logger::info("[TB] Debug stats capture {}", newEnabled ? "enabled" : "disabled");
}

void TerrainBlending::DumpDebugStats()
{
	const bool enabled = TbStatsEnabled();
	const float distMin = g_tbStats.terrainDistInit ? g_tbStats.terrainDistMin : -1.0f;
	const float distMax = g_tbStats.terrainDistInit ? g_tbStats.terrainDistMax : -1.0f;
	const char* depthInfo = g_tbStats.mainInfoValid ? "" : " (main depth info unavailable)";
	const float tbTotalLastMs = g_tbStats.depthBlendGpuMsLast + g_tbStats.replayGpuMsLast;
	const float tbTotalMs = g_tbStats.depthBlendGpuMs + g_tbStats.replayGpuMs;
	const float tbTotalCpuLastMs = g_tbStats.depthBlendCpuMsLast + g_tbStats.replayCpuMsLast;
	const float tbTotalCpuMs = g_tbStats.depthBlendCpuMs + g_tbStats.replayCpuMs;
	const float tbTotalCombinedLastMs = tbTotalLastMs + tbTotalCpuLastMs;
	const float tbTotalCombinedMs = tbTotalMs + tbTotalCpuMs;

	logger::info(
		"[TB][STAT] enabled={} prepass enter={} exit={} terrainPass={} accept={} reject={} toggleT={} toggleF={} queuedTerrain={} queuedExtra={} maxTerrainQueue={} maxExtraQueue={} blendDispatch={} renderCalls={} workCalls={} renderDepth={} renderTerrainDepth={} distMin={} distMax={} mainDepth={}x{} fmt={} array={} srvDim={} depthBlendGpuMs={} replayGpuMs={} totalGpuMs={} totalGpuMsSmoothed={} depthBlendCpuMs={} replayCpuMs={} totalCpuMs={} totalCpuMsSmoothed={} totalCombinedMs={} totalCombinedMsSmoothed={}{}",
		enabled,
		g_tbStats.prepassEnter,
		g_tbStats.prepassExit,
		g_tbStats.terrainPasses,
		g_tbStats.terrainAccepted,
		g_tbStats.terrainRejected,
		g_tbStats.terrainToggleTrue,
		g_tbStats.terrainToggleFalse,
		g_tbStats.queuedTerrain,
		g_tbStats.queuedExtra,
		g_tbStats.maxTerrainQueue,
		g_tbStats.maxExtraQueue,
		g_tbStats.blendDispatch,
		g_tbStats.renderCalls,
		g_tbStats.renderCallsWithWork,
		renderDepth,
		renderTerrainDepth,
		distMin,
		distMax,
		g_tbStats.mainWidth,
		g_tbStats.mainHeight,
		g_tbStats.mainFormat,
		g_tbStats.mainArraySize,
		g_tbStats.mainSrvDim,
		g_tbStats.depthBlendGpuMsLast,
		g_tbStats.replayGpuMsLast,
		tbTotalLastMs,
		tbTotalMs,
		g_tbStats.depthBlendCpuMsLast,
		g_tbStats.replayCpuMsLast,
		tbTotalCpuLastMs,
		tbTotalCpuMs,
		tbTotalCombinedLastMs,
		tbTotalCombinedMs,
		depthInfo);

	g_tbStats.ResetCounts();
}
