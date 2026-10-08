#pragma once

#include "ModelResolutionPolicy.h"
#include "Renderer.h"

#include <array>
#include <memory>
#include <span>

namespace NeuralRendering
{
	/** D3D11-only proxy resources; each admitted batch retains ownership across native resets. */
	class ModelResolution
	{
		struct Work;

	public:
		struct Batch
		{
			std::array<RendererApplyArgs, 2> arguments{};
			std::array<std::shared_ptr<Work>, 2> resources{};
			std::size_t count = 0;
		};

		/** Project validated source geometry without allocating or dispatching. */
		static bool Project(const RendererApplyArgs&, RendererApplyArgs&) noexcept;
		/** Count additional private textures before memory admission. */
		std::optional<std::uint64_t> AdditionalBytes(std::span<const RendererApplyArgs>) const;
		[[nodiscard]] std::optional<std::uint64_t> RetainedBytes(std::uint32_t) const noexcept;

		/** Prepare a complete proxy pair before the native adapter sees either eye. */
		bool Prepare(std::span<const RendererApplyArgs>, Batch&, HRESULT&);
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
