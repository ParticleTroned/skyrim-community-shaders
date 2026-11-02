#pragma once

#include "ModelResolutionPolicy.h"
#include "Renderer.h"
#include "TextureInfo.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "PackedCropDiagnostics.h"
#endif

#include <array>
#include <limits>
#include <memory>
#include <span>
#include <utility>

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

		/** Descriptions captured by renderer validation; ownership and depth-view facts stay paired. */
		struct Sources
		{
			TextureInfo color, depth, motion, output;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthView;
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDescription{};
		};

	private:
		struct AllocationKey
		{
			UpscalingDLSS::Extent sourceSize{}, modelSize{}, modelGuideSize{};
			DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN, outputFormat = DXGI_FORMAT_UNKNOWN, motionFormat = DXGI_FORMAT_UNKNOWN;
			bool hasMask = false;
			ModelResolution::Sharing sharing = ModelResolution::Sharing::Private;
			bool selectedOutput = false;

			bool operator==(const AllocationKey&) const = default;

			std::optional<std::uint64_t> Bytes() const noexcept;
		};

	public:
		/** Single-use snapshot for one synchronous render-owner operation, never a frame cache. */
		class Preparation
		{
		public:
			Preparation() = default;
			Preparation(const Preparation&) = delete;
			Preparation& operator=(const Preparation&) = delete;
			Preparation(Preparation&&) = delete;
			Preparation& operator=(Preparation&&) = delete;
			[[nodiscard]] std::span<const RendererApplyArgs> Projected() const { return { projected_.data(), count_ }; }

		private:
			friend class ModelResolution;
			const ModelResolution* owner_ = nullptr;
			bool ready_ = false;
			std::size_t count_ = 0;
			Sharing sharing_ = Sharing::Private;
			std::uint64_t additional_ = 0;
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> inputs_{}, projected_{};
			std::array<Sources, kMaximumRegionEvaluations> sources_{};
			std::array<ModelResolutionGeometry, kMaximumRegionEvaluations> geometry_{};
			std::array<AllocationKey, kMaximumRegionEvaluations> allocations_{};
			Microsoft::WRL::ComPtr<ID3D11Device> device_;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		};

		struct Batch
		{
			std::array<Sources, kMaximumRegionEvaluations> sources{};
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> arguments{};
			std::array<std::shared_ptr<Work>, kMaximumRegionEvaluations> resources{};
			std::array<std::uint64_t, kMaximumRegionEvaluations> contentSerials{};
			std::array<Targets, kMaximumRegionEvaluations> targets{};
			std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, kMaximumRegionEvaluations> selectedOutputs{};
			std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, kMaximumRegionEvaluations> selectionMasks{};
			std::array<ComputeSubrect, kMaximumRegionEvaluations> selectionSupports{};
			std::array<bool, kMaximumRegionEvaluations> providerSelections{};
			std::array<std::optional<SceneCategorySelection>, kMaximumRegionEvaluations> sceneSelections{};
			Microsoft::WRL::ComPtr<ID3D11Buffer> sceneConstants;
			std::array<Microsoft::WRL::ComPtr<ID3D11Resource>, kMaximumRegionEvaluations> selectionSources{}, selectionDestinations{};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> reconstructionShader;
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> preparationShader, sharedPreparationShader;
			Microsoft::WRL::ComPtr<ID3D11Buffer> peripheralConstants;
			std::array<PeripheralCompressionPlan, kMaximumRegionEvaluations> peripheralPlans{};
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> packedCoverage;
			GuidedReconstructionDecision reconstructionDecision{};
			std::size_t count = 0;
		};

		/** Freeze capacity before memory admission; speculative upgrades preserve the working policy. */
		void PlanPackedCrops(std::span<RendererApplyArgs>, bool speculative = false) noexcept;
		/** Project shared peripheral geometry without compiling shaders or allocating GPU resources. */
		bool PlanPeripheralCompression(std::span<RendererApplyArgs>, bool compactInputs) const;
		/** Projects a full-view prewarm contract through the live scale/compression policies without GPU work. */
		[[nodiscard]] std::optional<PrewarmKey> ProjectPrewarm(const PrewarmRequest&, std::uint32_t effectivePercent) const;
		/** Freeze optional mapping and shader ownership after scale/actor planning and before admission. */
		bool PreparePeripheralCompression(std::span<RendererApplyArgs>, bool compactInputs, HRESULT&);
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
			,
			ModelResolutionGeometry* geometry = nullptr) noexcept;
		/** Shared preparation retains private actor selection and native protection semantics. */
		static bool CanUseSharedTargets(std::span<const RendererApplyArgs>, bool compactInputs) noexcept;
		/** Freeze owned sources, effective geometry and demand before admission; no GPU writes. */
		bool PlanPreparation(std::span<const RendererApplyArgs>, Sharing, Preparation&, std::span<const Sources> = {}
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			std::span<PackedCropAdmissionDiagnostics* const> = {}
#endif
		) const;
		/** Return demand from this operation's validated plan without repeating source queries. */
		std::optional<std::uint64_t> AdditionalBytes(const Preparation&) const;
		/** Consume a plan once; changed packing/demand returns ERROR_RETRY for fresh admission. */
		bool Prepare(Preparation&, Batch&, HRESULT&, std::span<const Targets> = {});
		/** Count additional private textures before memory admission; shared aliases are excluded. */
		std::optional<std::uint64_t> AdditionalBytes(std::span<const RendererApplyArgs>, Sharing sharing = Sharing::Private) const;
		[[nodiscard]] std::optional<std::uint64_t> RetainedBytes(std::uint32_t) const noexcept;

		/** Prepare both eyes into validated shared targets or private proxies before native evaluation. */
		bool Prepare(std::span<const RendererApplyArgs>, Batch&, HRESULT&, std::span<const Targets> = {});
		/** Reconstruct retained targets only for the prepared source transaction and selection. */
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
		bool EnsureSharedPreparationShader(bool peripheral, Microsoft::WRL::ComPtr<ID3D11ComputeShader>&, HRESULT&);
		bool EnsureCoverage(ID3D11Device*, HRESULT&);
		bool EnsureSelectionShader(bool guarded, HRESULT&);
		bool EnsureGuidedShader(bool selected, bool guarded, bool peripheral, Microsoft::WRL::ComPtr<ID3D11ComputeShader>&, HRESULT&);
		bool EnsurePeripheralShaders(const RendererApplyArgs&, bool selected, PeripheralCompressionShaders&, HRESULT&, bool faceDetail);
		bool BuildPeripheralPlans(std::span<RendererApplyArgs>, bool compactInputs, std::span<PeripheralCompressionPlan>) const;
		void DispatchCoverage(std::span<const RendererApplyArgs>, Batch&);
		template <UINT ConstantBuffers>
		void DispatchPreparation(std::span<const RendererApplyArgs>, const Batch&);
		template <std::size_t Bindings, UINT ConstantBuffers = 1>
		void DispatchReconstruction(std::span<const RendererApplyArgs>, const Batch&);
#ifdef DEVBENCH_BRIDGE_ENABLED
		void ServiceCoverageInspection(ID3D11DeviceContext*);
		PackedCropDiagnostics cropDiagnostics_;
#endif
		struct ShaderVariant
		{
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
			HRESULT failure = S_OK;
		};
		bool EnsureShaderVariant(ShaderVariant&, const wchar_t*, std::span<const std::pair<const char*, const char*>>, HRESULT&);
		bool SelectReconstructionVariant(const RendererApplyArgs&, Batch&, bool selected, bool peripheral, HRESULT&);
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::array<ShaderVariant, 12> preparationVariants_{};
		std::array<ShaderVariant, 128> reconstructionVariants_{};
#else
		std::array<ShaderVariant, 2> preparationVariants_{};
		std::array<ShaderVariant, 32> reconstructionVariants_{};
#endif
		std::shared_ptr<CoverageWork> coverage_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> coverageShader_, guardedReconstruct_;
		std::uint32_t lastCoverageFrame_ = std::numeric_limits<std::uint32_t>::max();
		bool lastBatchGuarded_ = false;
		std::array<std::shared_ptr<Work>, kLogicalFeatureSlotCount> slots_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> sceneConstants_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> prepare_, reconstruct_, selection_, guardedSelection_;
		std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>, 2> sharedPreparationShaders_{};
		std::array<bool, 2> sharedPreparationFailed_{};
		std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>, 8> guidedShaders_{};
		std::array<bool, 8> guidedShaderFailed_{};
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> peripheralPrepare_;
		std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>, 2> peripheralReconstruct_{};
		std::array<bool, 2> peripheralShaderFailed_{};
		Microsoft::WRL::ComPtr<ID3D11Buffer> peripheralConstants_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		bool compileFailed_ = false;
		HRESULT packingFailure_ = S_OK;
	};
}
