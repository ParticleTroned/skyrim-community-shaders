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
	/** A display-only game frame must not run postprocessing, even after resource invalidation. */
	bool IsReusingScene();
	/** Cheap guard for the final presentation hook while the feature is inactive. */
	bool HasPendingSubmission();
	/** Freeze world depth and camera constants before subsequent image-space/UI passes. */
	void CaptureWorld();
	/** Open the native stereo boundary; final colors arrive after DLSS/NR processing. */
	void BeginSubmitPair(uint64_t cycle);
	/** Reject prevents a failed peer from mixing original and corrected poses. */
	enum class Submission
	{
		Original,
		Refreshed,
		Reject
	};
	/** Capture final eye color before submission; optionally apply the pair's shared late pose. */
	Submission PrepareSubmission(vr::EVREye eye, uint64_t cycle, const vr::Texture_t* input,
		const vr::VRTextureBounds_t* bounds, vr::EVRSubmitFlags flags, vr::VRTextureWithPose_t& output);
	/** Get a prepared eye with the exact pose used by its reprojection. */
	bool GetSubmission(vr::EVREye eye, uint64_t cycle, vr::VRTextureWithPose_t& texture);
	/** Record the actual route and compositor result for an eye in the active pair. */
	void RecordSubmission(vr::EVREye eye, uint64_t cycle, bool accepted, bool refreshed);
	void EndSubmitPair(uint64_t cycle);
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Read-only settings, admission reason and counters for DevBench. */
	nlohmann::json Status();
#endif
}
