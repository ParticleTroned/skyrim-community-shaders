#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace CSX::Diagnostics::ColourPipelineSceneObservation
{
	using MatrixRows = std::array<std::array<float, 4>, 4>;

	/** Render-thread observations; cache content identity and atomic scene state remain unknown. */
	struct Snapshot
	{
		std::uint32_t cpuFrame = 0;
		std::uint32_t eye = 0;
		std::uint64_t beginQpc = 0;
		std::uint64_t endQpc = 0;
		std::uint64_t qpcFrequency = 0;
		bool inWorld = false;
		std::uint32_t lastStartedWorldFrame = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t lastCompletedWorldFrame = std::numeric_limits<std::uint32_t>::max();
		std::array<MatrixRows, 5> matrices{};
		std::array<float, 3> positionAdjust{};
		std::array<float, 3> previousPositionAdjust{};
		bool imageSpaceAvailable = false;
		std::array<float, 9> hdr{};
		std::array<float, 3> cinematic{};
		std::array<float, 4> tint{};
	};

	/** Reject nonfinite vectors before JSON can silently replace their components. */
	template <std::size_t Size>
	inline bool Finite(const std::array<float, Size>& a_values) noexcept
	{
		return std::ranges::all_of(a_values, [](float a_value) { return std::isfinite(a_value); });
	}

	inline bool Finite(const MatrixRows& a_matrix) noexcept
	{
		return std::ranges::all_of(a_matrix, [](const auto& a_row) { return Finite(a_row); });
	}

	/** Preserve observed layout and nullable availability without asserting camera freshness. */
	inline nlohmann::json ToJson(const Snapshot& a_snapshot)
	{
		using json = nlohmann::json;
		const bool observed = a_snapshot.cpuFrame && a_snapshot.eye < 2 &&
		                      a_snapshot.beginQpc && a_snapshot.endQpc >= a_snapshot.beginQpc;
		const bool camera = observed && Finite(a_snapshot.positionAdjust) && Finite(a_snapshot.previousPositionAdjust) &&
		                    std::ranges::all_of(a_snapshot.matrices, [](const auto& a_matrix) { return Finite(a_matrix); });
		const bool hdr = observed && a_snapshot.imageSpaceAvailable && Finite(a_snapshot.hdr);
		const bool cinematic = observed && a_snapshot.imageSpaceAvailable && Finite(a_snapshot.cinematic);
		const bool tint = observed && a_snapshot.imageSpaceAvailable && Finite(a_snapshot.tint);
		const auto frame = [observed](std::uint32_t a_frame) -> json {
			return observed && a_frame != std::numeric_limits<std::uint32_t>::max() ? json(a_frame) : json(nullptr);
		};
		return {
			{ "schemaVersion", 1 },
			{ "available", observed },
			{ "observationCpuFrame", observed ? json(a_snapshot.cpuFrame) : json(nullptr) },
			{ "eyeIndex", observed ? json(a_snapshot.eye) : json(nullptr) },
			{ "beginQpc", observed ? json(a_snapshot.beginQpc) : json(nullptr) },
			{ "endQpc", observed ? json(a_snapshot.endQpc) : json(nullptr) },
			{ "qpcFrequency", observed && a_snapshot.qpcFrequency ? json(a_snapshot.qpcFrequency) : json(nullptr) },
			{ "clock", "QueryPerformanceCounter ticks" },
			{ "association", "sequential render-thread reads before this stage-eye staging copy; not an atomic engine snapshot" },
			{ "worldRender", {
								 { "inWorldAtObservation", observed ? json(a_snapshot.inWorld) : json(nullptr) },
								 { "lastStartedCpuFrame", frame(a_snapshot.lastStartedWorldFrame) },
								 { "lastCompletedCpuFrame", frame(a_snapshot.lastCompletedWorldFrame) },
								 { "continuousCameraEquivalenceProven", false },
							 } },
			{ "cameraCache", {
								 { "available", camera },
								 { "source", "globals::game::frameBufferCached typed eye accessors" },
								 { "contentCpuFrame", nullptr },
								 { "contentQpc", nullptr },
								 { "initialized", nullptr },
								 { "validation", "finite components only; geometric validity and cache initialization are not established" },
								 { "contentIdentity", "unknown; native per-frame buffer Unmap cache has no content-frame stamp" },
								 { "matrixLayout", "4 rows x 4 columns, native Matrix::m order; no transpose or convention conversion" },
								 { "matrixConvention", "engine CameraView/CameraProj storage; handedness and vector multiplication are not reinterpreted" },
								 { "view", camera ? json(a_snapshot.matrices[0]) : json(nullptr) },
								 { "projection", camera ? json(a_snapshot.matrices[1]) : json(nullptr) },
								 { "projectionUnjittered", camera ? json(a_snapshot.matrices[2]) : json(nullptr) },
								 { "viewProjectionUnjittered", camera ? json(a_snapshot.matrices[3]) : json(nullptr) },
								 { "previousViewProjectionUnjittered", camera ? json(a_snapshot.matrices[4]) : json(nullptr) },
								 { "positionAdjust", camera ? json(a_snapshot.positionAdjust) : json(nullptr) },
								 { "previousPositionAdjust", camera ? json(a_snapshot.previousPositionAdjust) : json(nullptr) },
								 { "positionConvention", "native per-eye adjusted coordinates; not absolute world-camera position" },
							 } },
			{ "imageSpaceParameters", {
										  { "managerAvailable", observed && a_snapshot.imageSpaceAvailable },
										  { "source", "ImageSpaceManager::GetImageSpaceData().baseData" },
										  { "units", "native engine parameter values, no conversion" },
										  { "hdr", {
													   { "available", hdr },
													   { "eyeAdaptSpeed", hdr ? json(a_snapshot.hdr[0]) : json(nullptr) },
													   { "eyeAdaptStrength", hdr ? json(a_snapshot.hdr[1]) : json(nullptr) },
													   { "bloomBlurRadius", hdr ? json(a_snapshot.hdr[2]) : json(nullptr) },
													   { "bloomThreshold", hdr ? json(a_snapshot.hdr[3]) : json(nullptr) },
													   { "bloomScale", hdr ? json(a_snapshot.hdr[4]) : json(nullptr) },
													   { "receiveBloomThreshold", hdr ? json(a_snapshot.hdr[5]) : json(nullptr) },
													   { "white", hdr ? json(a_snapshot.hdr[6]) : json(nullptr) },
													   { "sunlightScale", hdr ? json(a_snapshot.hdr[7]) : json(nullptr) },
													   { "skyScale", hdr ? json(a_snapshot.hdr[8]) : json(nullptr) },
												   } },
										  { "cinematic", {
															 { "available", cinematic },
															 { "saturation", cinematic ? json(a_snapshot.cinematic[0]) : json(nullptr) },
															 { "brightness", cinematic ? json(a_snapshot.cinematic[1]) : json(nullptr) },
															 { "contrast", cinematic ? json(a_snapshot.cinematic[2]) : json(nullptr) },
														 } },
										  { "tint", {
														{ "available", tint },
														{ "amount", tint ? json(a_snapshot.tint[0]) : json(nullptr) },
														{ "rgb", tint ? json::array({ a_snapshot.tint[1], a_snapshot.tint[2], a_snapshot.tint[3] }) : json(nullptr) },
													} },
										  { "internalAdaptiveExposure", { { "available", false }, { "value", nullptr }, { "reason", "not observed; parameters are not exposure history or SDK internals" } } },
										  { "sceneLightObjects", nullptr },
									  } },
		};
	}
}
