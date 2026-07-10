#pragma once

#include <BS_thread_pool.hpp>
#include <d3d11_1.h>
#include <winrt/base.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace RE
{
	class BSShaderAccumulator;
	class BSRenderPass;
}

/**
 * @brief Multithreads the per-pass geometry constant-buffer fill for
 *        BSLightingShader / BSUtilityShader.
 *
 * The render thread's dominant per-pass cost is not the draw: it is the
 * `context->Map(geometryCB, D3D11_MAP_WRITE_DISCARD)` at the top of
 * `SetupGeometry`. Every BSLighting/BSUtility pass DISCARD-maps the *shared*
 * geometry constant buffer, writes its transforms/lights/material constants,
 * unmaps, and binds it at VS/PS constant slot 2. There are ~21k of these maps per
 * frame and the DXVK constant-buffer allocator behind them was the render thread's
 * single hottest cluster.
 *
 * Instead of the expensive per-pass Map/DISCARD, this gives each pass its own slice
 * of one big per-frame dynamic constant buffer that is mapped **once** per frame.
 * The slices are filled **in parallel** by a worker pool (plain CPU writes into
 * disjoint offsets - no `Map` calls on any thread), and the serial render binds
 * each pass's pre-filled slice by offset (`VSSetConstantBuffers1`) instead of
 * mapping and writing it again.
 *
 * Because only the constant-buffer *contents* move off-thread (a pure function of
 * the pass + camera + that pass's shader constant table), this needs no per-thread
 * render state and no deferred contexts - draws and dirty-state bookkeeping stay
 * serial and unchanged. See docs/development/parallel-shader-setup.md.
 *
 * Status: infrastructure only, off by default. The per-pass fill math and the
 * offset-bind hook (P1/P2) are validated pixel-identical before this is enabled.
 */
class ParallelShaderSetup
{
public:
	static ParallelShaderSetup* GetSingleton()
	{
		static ParallelShaderSetup singleton;
		return &singleton;
	}

	// D3D11.1 constant-buffer offset binding requires 16-constant (256-byte)
	// alignment for the first-constant offset, so every pass slice is padded up.
	static constexpr uint32_t kSliceAlignment = 256;
	// Geometry constant group is small (a few matrices + light arrays); 1 KB per
	// pass slice comfortably covers the largest BSLighting geometry CB.
	static constexpr uint32_t kSliceBytes = 1024;
	// Upper bound on filled passes per frame; the arena is sized for this.
	static constexpr uint32_t kMaxPasses = 16384;

	[[nodiscard]] bool IsEnabled() const { return enabled; }
	void SetEnabled(bool a_enabled) { enabled = a_enabled; }
	[[nodiscard]] bool IsInitialized() const { return initialized; }
	[[nodiscard]] size_t WorkerCount() const { return pool ? pool->get_thread_count() : 0; }

	/**
	 * @brief Create the worker pool and the per-frame constant-buffer arena. No-op
	 *        unless enabled (CS_PARALLEL_SETUP=1). Safe to call repeatedly.
	 */
	void Setup();

	/** @brief Release the arena and pool. */
	void Shutdown();

	/** @brief Read the CS_PARALLEL_SETUP env override once (dev toggle). */
	void ApplyEnvOverride();

	/** @brief Install the RenderGeometryGroup collection hook (SE-only, enabled-only). */
	void InstallHooks();

	/**
	 * @brief Batch-render entry hook (BSShaderAccumulator::RenderGeometryGroup). At
	 *        this point the batch renderer holds every accumulated pass. Step 1:
	 *        collect + count the group's BSUtility passes to validate the walk; no
	 *        rendering change. Later phases parallel-fill their CB slices here.
	 */
	void OnRenderGeometryGroup(RE::BSShaderAccumulator* a_accumulator);

	/** @brief BSUtilityShader::SetupGeometry hook body (pre-original): volume + harvest. */
	void OnUtilitySetupGeometry(RE::BSRenderPass* a_pass);

	/** @brief BSUtilityShader::SetupGeometry hook body (post-original): bind the pass's
	 *         pre-filled arena slice at VS slot 2, overriding the engine's bind. */
	void BindUtilitySlice(RE::BSRenderPass* a_pass);

	/** @brief True if this pass will be served from our arena (bind enabled + slice
	 *         pre-filled this frame). Also gates redirecting the engine's DISCARD map. */
	[[nodiscard]] bool WillBindSlice(RE::BSRenderPass* a_pass) const;

	/** @brief Diagnostic: diff our fill for this pass against the engine's captured
	 *         geometry-CB bytes; logs which byte ranges match/differ. */
	void CompareFill(RE::BSRenderPass* a_pass);

	/** @brief The shared per-frame constant buffer that holds every pass slice. */
	[[nodiscard]] ID3D11Buffer* Arena() const { return arena.get(); }

	/**
	 * @brief Reserve the next 256-byte-aligned slice for a pass.
	 * @param o_cpuPtr  receives the CPU write pointer (valid only while mapped).
	 * @return byte offset of the slice within the arena, or UINT32_MAX if full.
	 */
	uint32_t AllocateSlice(uint8_t*& o_cpuPtr);

private:
	ParallelShaderSetup() = default;

	// Reused scratch for the per-accumulator BSUtility pass collection (render thread only).
	std::vector<RE::BSRenderPass*> collectBuffer;
	std::unordered_map<RE::BSRenderPass*, uint32_t> passToSlice;   // pass -> arena byte offset (this frame)
	winrt::com_ptr<ID3D11DeviceContext1> context1;                // for VSSetConstantBuffers1
	winrt::com_ptr<ID3D11Buffer> stagingCB;                       // diagnostic readback of the geom CB
	bool arenaFreshFrame = true;                                  // DISCARD the arena on the frame's first fill

	bool enabled = false;
	bool initialized = false;

	// One big dynamic CB mapped once per frame; slices handed out via AllocateSlice.
	winrt::com_ptr<ID3D11Buffer> arena;
	uint8_t* arenaMapped = nullptr;         // valid between per-frame map/unmap
	std::atomic<uint32_t> arenaOffset{ 0 };  // bump allocator, reset each frame

	std::unique_ptr<BS::thread_pool<>> pool;
};
