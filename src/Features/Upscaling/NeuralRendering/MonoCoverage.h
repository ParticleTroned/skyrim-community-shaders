#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <optional>
#include <wrl/client.h>

namespace NeuralRendering
{
	/** GPU-visible admission protects the whole scene; bounded readbacks describe the decision later. */
	class MonoCoverage
	{
	public:
		struct Proof { std::uint32_t frame = 0; bool covered = false; };
		bool Initialize(ID3D11Device*);
		void Clear(ID3D11DeviceContext*) const;
		void QueueReadback(ID3D11DeviceContext*, std::uint32_t frame);
		std::optional<Proof> Poll(ID3D11DeviceContext*);
		ID3D11UnorderedAccessView* Output() const noexcept { return output_.Get(); }
		ID3D11ShaderResourceView* Input() const noexcept { return input_.Get(); }
		void Abandon() noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D11Buffer> flags_;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_;
		struct Readback
		{
			Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
			Microsoft::WRL::ComPtr<ID3D11Query> ready;
			std::uint32_t frame = 0;
			bool pending = false;
		};
		std::array<Readback, 3> readbacks_{};
	};
}
