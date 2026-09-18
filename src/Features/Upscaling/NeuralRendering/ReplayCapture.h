#pragma once

#include "ComputeSubrect.h"
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace NeuralRendering::Replay
{
	using Json = nlohmann::json;
	struct Eye
	{
		std::uint32_t slot = 0;
		ComputeSubrect outputSubrect{};
		std::array<float, 2> motionVectorScale{};
		bool featureUpscaling = false;
		ID3D11Texture2D* color = nullptr;
		ID3D11Texture2D* depth = nullptr;
		ID3D11Texture2D* motion = nullptr;
		ID3D11Texture2D* output = nullptr;
		Json metadata = Json::object();
	};
	struct Batch
	{
		Json metadata = Json::object();
		Json runtime = Json::object();
		Json adapter = Json::object();
		std::vector<Eye> eyes;
		// The renderer proves initialized full canvases, native evaluation and no ControlMask.
		bool supported = false;
		std::string unsupportedReason;
	};

	/** Cheap render-thread gate; ordinary rendering does not allocate capture state. */
	bool IsArmed() noexcept;
	/** Owns a bounded capture; a sequence requires consecutive source world frames. */
	Json Request(std::uint32_t frameCount = 1, std::uint32_t timeoutMs = 30000,
		const std::filesystem::path& evidenceRoot = "Data/SKSE/Plugins/CSX/NRReplay");
	Json Cancel(std::string_view requestId);
	/** Marks an active request failed when its caller cannot assemble provenance. */
	void Fail(std::string_view reason) noexcept;
	/** Polls CPU file completion and timeout without touching a graphics context. */
	Json Status();
	/** Copies native inputs/output before reconstruction; only the render owner calls this. */
	void OfferBatch(ID3D11Device*, ID3D11DeviceContext*, const Batch&) noexcept;
}
