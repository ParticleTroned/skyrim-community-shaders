#pragma once

#include "ModelResolutionPolicy.h"
#include "Renderer.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "PackedCropDiagnostics.h"
#endif

#include <array>
#include <limits>
#include <memory>
#include <span>

namespace NeuralRendering
{
	/** Model-grid preparation with private reconstruction retained across native resets. */
	class ModelResolution
	{
		struct Work;
		struct CoverageWork;

	public:
		/** Shared roles are excluded from private allocation admission and cache ownership. */
		enum class Sharing
		{
			Private,
			Guides,
			All
		};
		/** Retained shared guides; colour and output must either both be supplied or both remain private. */
		struct Targets
		{
			Color::Texture color, depth, motion, output;
		};

		struct Batch
		{
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> arguments{};
			std::array<std::shared_ptr<Work>, kMaximumRegionEvaluations> resources{};
			std::array<Targets, kMaximumRegionEvaluations> targets{};
			std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, kMaximumRegionEvaluations> selectedOutputs{};
			std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, kMaximumRegionEvaluations> selectionMasks{};
			std::array<ComputeSubrect, kMaximumRegionEvaluations> selectionSupports{};
			std::array<bool, kMaximumRegionEvaluations> providerSelections{};
			std::array<Microsoft::WRL::ComPtr<ID3D11Resource>, kMaximumRegionEvaluations> selectionSources{}, selectionDestinations{};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> reconstructionShader;
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> packedCoverage;
			std::size_t count = 0;
		};

		/** Freeze capacity before memory admission; speculative upgrades preserve the working policy. */
		void PlanPackedCrops(std::span<RendererApplyArgs>, bool speculative = false) noexcept;
		/** Resolve optional shaders before native memory admission; device errors remain fatal. */
		bool PreparePacking(std::span<const RendererApplyArgs>, HRESULT&);
		[[nodiscard]] bool PackingAvailable() const noexcept { return SUCCEEDED(packingFailure_); }
		[[nodiscard]] HRESULT PackingFailure() const noexcept { return packingFailure_; }

		/** One reconstruction boundary handles either independent model scale or area restriction. */
		static bool Required(const RendererApplyArgs& args
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
			) noexcept;
		/** Project validated source geometry without allocating or dispatching. */
		static bool Project(const RendererApplyArgs&, RendererApplyArgs&
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
			) noexcept;
		/** Shared preparation retains private actor selection and native protection semantics. */
		static bool CanUseSharedTargets(std::span<const RendererApplyArgs>, bool compactInputs) noexcept;
		/** Count additional private textures before memory admission; shared aliases are excluded. */
		std::optional<std::uint64_t> AdditionalBytes(std::span<const RendererApplyArgs>, Sharing sharing = Sharing::Private) const;
		[[nodiscard]] std::optional<std::uint64_t> RetainedBytes(std::uint32_t) const noexcept;

		/** Prepare both eyes into validated shared targets or private proxies before native evaluation. */
		bool Prepare(std::span<const RendererApplyArgs>, Batch&, HRESULT&, std::span<const Targets> = {});
		/** Reconstruct into retained targets; reject filters changed since preparation before dispatch. */
		bool Reconstruct(std::span<const RendererApplyArgs>, const Batch&, HRESULT&);
		/** Commit only the original owned rectangles after successful reconstruction. */
		void Commit(std::span<const RendererApplyArgs>, const Batch&) const;
		/** Retain textures, constants and outstanding batches while rearming shader compilation. */
		void InvalidateShaders() noexcept;
		/** Release caches; outstanding batch ownership remains valid. */
		void Reset() noexcept;
		void ReleaseSlot(std::uint32_t) noexcept;
		/** Drop proxy caches only when the whole source transaction no longer needs them. */
		void ReleaseUnscaledSlots(std::span<const RendererApplyArgs>) noexcept;
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Explicit inspection samples the last coverage guard without waits or gameplay polling. */
		nlohmann::json GetPackedCoverageDiagnostics();
		/** Render-owner access to opt-in CPU allocation-transition evidence. */
		PackedCropDiagnostics& CropDiagnostics() noexcept { return cropDiagnostics_; }
#endif

	private:
		struct CropPlanning
		{
			PackedCropCapacityState state{};
			UpscalingDLSS::Extent source{}, model{}, guide{};
			std::optional<RenderingMode> mode;
		};
		// CPU policy survives resource retirement without retaining any GPU memory.
		std::array<CropPlanning, kLogicalFeatureSlotCount> cropPlanning_{};
		bool EnsureShaders(ID3D11Device*, HRESULT&);
		bool EnsureCoverage(ID3D11Device*, HRESULT&);
		bool EnsureSelectionShader(bool guarded, HRESULT&);
		void DispatchCoverage(std::span<const RendererApplyArgs>, Batch&);
		template <std::size_t Bindings>
		void DispatchReconstruction(std::span<const RendererApplyArgs>, const Batch&);
#ifdef DEVBENCH_BRIDGE_ENABLED
		void ServiceCoverageInspection(ID3D11DeviceContext*);
		PackedCropDiagnostics cropDiagnostics_;
#endif
		std::shared_ptr<CoverageWork> coverage_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> coverageShader_, guardedReconstruct_;
		std::uint32_t lastCoverageFrame_ = std::numeric_limits<std::uint32_t>::max();
		bool lastBatchGuarded_ = false;
		std::array<std::shared_ptr<Work>, kLogicalFeatureSlotCount> slots_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> prepare_, reconstruct_, selection_, guardedSelection_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		bool compileFailed_ = false;
		HRESULT packingFailure_ = S_OK;
	};
}
