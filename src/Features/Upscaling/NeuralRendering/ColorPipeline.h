#pragma once

#include "ColorPolicy.h"
#include "D3D12Interop.h"

#include <memory>
#include <string>

namespace NeuralRendering
{
	struct RendererApplyArgs;

	// Called only under Renderer::State's mutex and existing GPU ownership rules.
	class ColorPipeline
	{
	public:
		ColorPipeline();
		~ColorPipeline();
		ColorPipeline(const ColorPipeline&) = delete;
		ColorPipeline& operator=(const ColorPipeline&) = delete;

		bool LoadSettings();
		[[nodiscard]] const Color::Settings& Settings(InsertionPoint a_point) const noexcept;
		[[nodiscard]] const std::string& Description() const noexcept;
		bool Ensure(std::uint32_t a_slot, ID3D11Device* a_device,
			const SharedTexture& a_input, const SharedTexture& a_output);
		bool Prepare(const RendererApplyArgs& a_args, const ComputeSubrect& a_rect,
			const SharedTexture& a_input);
		bool Resolve(const RendererApplyArgs& a_args, const ComputeSubrect& a_rect,
			const SharedTexture& a_input, const SharedTexture& a_output);
		[[nodiscard]] ID3D11Texture2D* Output(std::uint32_t a_slot) const noexcept;
		// Diagnostic only: inference still executes; copy the exact prepared image
		// through D3D12 so transport can be checked without falsifying NGX outcomes.
		void RecordRoundTrip(ID3D12GraphicsCommandList* a_list,
			const SharedTexture& a_input, const SharedTexture& a_output,
			const ComputeSubrect& a_rect) const;
		void ResetResources(bool a_resetShaders);
		void ReloadSettings();
		void Abandon() noexcept;

	private:
		struct State;
		std::unique_ptr<State> state_;
	};
}
