#pragma once

#include <cstdint>
#include <d3d11.h>
#include <nlohmann/json_fwd.hpp>
#include <openvr.h>

namespace CSX::ViewRefresh
{
	struct Settings
	{
		bool enabled = false;
		bool reuseScenes = true;
		bool latePose = false;

		bool Active() const { return enabled && (reuseScenes || latePose); }
		bool operator==(const Settings&) const = default;
	};

	/** Atomically replace configuration; an already prepared stereo pair finishes unchanged. */
	void Configure(Settings settings);
	Settings GetSettings();
	void Load(const nlohmann::json& settings);
	void Save(nlohmann::json& settings);
	void DrawSettings();
	void InstallDevBench();

	/** Observe the game's pose wait; this subsystem never owns compositor pacing. */
	void OnRenderPose(uint64_t cycle, vr::IVRCompositor* compositor, const vr::TrackedDevicePose_t* pose);
	/** Returns true only after both replacement eye images are completely prepared. */
	bool TryReuseWorld();
	/** Freeze world depth and camera constants before subsequent image-space/UI passes. */
	void CaptureWorld();
	/** Capture final stereo color and optionally correct a freshly rendered pair. */
	void BeginSubmitPair(ID3D11Texture2D* color, uint64_t cycle);
	/** Get a prepared eye with the exact pose used by its reprojection. */
	bool GetSubmission(vr::EVREye eye, uint64_t cycle, vr::VRTextureWithPose_t& texture);
	/** Record the actual route and compositor result for an eye in the active pair. */
	void RecordSubmission(vr::EVREye eye, uint64_t cycle, bool accepted, bool refreshed);
	void EndSubmitPair(uint64_t cycle);
	/** Read-only settings, admission reason and counters for the UI and DevBench. */
	nlohmann::json Status();
}
