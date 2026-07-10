#pragma once

#include "Feature.h"

// -----------------------------------------------------------------------------
// OcclusionCulling — CommunityShaders Feature wrapper around the MOC port.
//
// V1: installs a hook on BSCullingProcess so that (a) once per frame, before the
// main cull walk, the MOC depth buffer is rebuilt from large static occluders, and
// (b) each processed scene object is occlusion-tested and skipped if provably
// hidden. Everything is gated behind the feature settings AND the env var
// CS_OCCLUSION=1 (off by default).
//
// SE 1.5.97 ONLY.
// -----------------------------------------------------------------------------

struct OcclusionCulling : public Feature
{
	static OcclusionCulling* GetSingleton()
	{
		static OcclusionCulling singleton;
		return &singleton;
	}

	struct Settings
	{
		bool  EnableOcclusionTesting = true;
		bool  EnableOccluderRendering = true;
		float OccluderMaxDistance = 20000.0f;
		float OccluderFirstLevelMinSize = 200.0f;
		// Raster budget per frame, closest-first (not a MOC library limit). With the
		// threaded raster + simplified meshes the default covers typical scenes fully.
		std::int32_t MaxOccludersPerFrame = 256;
		// CullingThreadpool worker count; applied at boot (pool is created once).
		std::int32_t RasterThreads = 2;
		// meshopt_simplify occluder meshes at cache time (~half the indices).
		bool SimplifyOccluders = true;
		// Only objects with at least this world-bound radius are occlusion-tested.
		float OccluderTestMinRadius = 50.0f;
	};

	Settings settings;

	// Master runtime gate, driven by the CS_OCCLUSION=1 env var (read once at load).
	// When false, the installed hooks are inert pass-throughs.
	bool envEnabled = false;

	virtual std::string GetName() override { return "Occlusion Culling"; }
	virtual std::string GetShortName() override { return "OcclusionCulling"; }

	/** @brief Installs the BSCullingProcess hooks and creates the MOC instance. */
	virtual void PostPostLoad() override;

	/** @brief Render-thread hook: env-gated verification dumps (CS_MOC_DUMP=1). */
	virtual void Prepass() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	// This feature has no shader .ini, so it is force-loaded in PostPostLoad. Neutralize
	// the disk-cache machinery (which assumes an ini version) so it can't crash/invalidate.
	virtual bool ValidateCache(CSimpleIniA&) override { return true; }
	virtual void WriteDiskCacheInfo(CSimpleIniA&) override {}

	/** @brief Pushes the current settings into the MOC runtime globals. */
	void SyncSettingsToMOC();

	/** @brief True when the master env gate + testing setting are both on. */
	bool IsActive() const;
};
