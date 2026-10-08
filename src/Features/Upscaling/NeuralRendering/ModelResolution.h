#pragma once

#include "ModelResolutionPolicy.h"
#include "Renderer.h"

#include <array>
#include <memory>
#include <span>

namespace NeuralRendering
{
	/** Model-grid preparation with private reconstruction retained across native resets. */
	class ModelResolution
	{
		struct Work;

	public:
		/** Retained views of admitted renderer-owned model textures. */
		struct Targets
		{
			Color::Texture color, depth, motion, output;
		};

		struct Batch
		{
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> arguments{};
			std::array<std::shared_ptr<Work>, kMaximumRegionEvaluations> resources{};
			std::array<Targets, kMaximumRegionEvaluations> targets{};
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> reconstructionShader;
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
			std::size_t count = 0;
		};

		/** Project validated source geometry without allocating or dispatching. */
		static bool Project(const RendererApplyArgs&, RendererApplyArgs&) noexcept;
		/** Shared preparation preserves only the raw, unmasked stateless contract. */
		static bool CanUseSharedTargets(std::span<const RendererApplyArgs>, bool colorProcessing, bool compactInputs) noexcept;
		/** Count additional private textures before memory admission; shared aliases are excluded. */
		std::optional<std::uint64_t> AdditionalBytes(std::span<const RendererApplyArgs>, bool sharedTargets = false) const;
		[[nodiscard]] std::optional<std::uint64_t> RetainedBytes(std::uint32_t) const noexcept;

		/** Prepare both eyes into validated shared targets or private proxies before native evaluation. */
		bool Prepare(std::span<const RendererApplyArgs>, Batch&, HRESULT&, std::span<const Targets> = {});
		/** Reconstruct both private full-size results before committing any caller output. */
		bool Reconstruct(std::span<const RendererApplyArgs>, const Batch&, HRESULT&);
		/** Commit only the original owned rectangles after successful reconstruction. */
		void Commit(std::span<const RendererApplyArgs>, const Batch&) const;
		/** Release caches; outstanding batch ownership remains valid. */
		void Reset() noexcept;
		void ReleaseSlot(std::uint32_t) noexcept;
		/** Drop proxy caches when caller-owned slots return to their original resolution. */
		void ReleaseUnscaledSlots(std::span<const RendererApplyArgs>) noexcept;

	private:
		bool EnsureShaders(ID3D11Device*, HRESULT&);
		std::array<std::shared_ptr<Work>, kLogicalFeatureSlotCount> slots_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> prepare_, reconstruct_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
	};
}
