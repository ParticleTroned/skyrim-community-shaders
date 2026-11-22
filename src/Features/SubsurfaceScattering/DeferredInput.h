#pragma once

#include "../../Utils/ComIdentity.h"

#include <algorithm>
#include <d3d11.h>

namespace SSS
{
	/** One completed Burley dispatch; pending copies must resolve before composition. */
	class DeferredInput
	{
	public:
		struct alignas(16) Control
		{
			UINT width = 0;
			UINT height = 0;
			UINT enabled = 0;
			UINT padding = 0;

			bool operator==(const Control&) const = default;
		};
		static_assert(sizeof(Control) == 16);

		DeferredInput() = default;
		DeferredInput(const DeferredInput&) = delete;
		DeferredInput& operator=(const DeferredInput&) = delete;
		DeferredInput(DeferredInput&&) = default;
		DeferredInput& operator=(DeferredInput&&) = delete;

		/** Retains the exact legacy copy and the rectangle written by this dispatch. */
		DeferredInput(ID3D11DeviceContext* a_context, ID3D11Texture2D* a_source,
			ID3D11ShaderResourceView* a_srv, ID3D11UnorderedAccessView* a_sourceUAV,
			ID3D11Texture2D* a_destination, UINT a_producedWidth, UINT a_producedHeight, bool a_allowRegionCopy) :
			context(a_context), source(a_source), destination(a_destination), srv(a_srv), state(State::Pending)
		{
			source->GetDesc(&sourceDesc);
			destination->GetDesc(&destinationDesc);
			srv->GetDesc(&srvDesc);
			a_sourceUAV->GetDesc(&sourceUAVDesc);
			copyRegion = a_allowRegionCopy &&
			             (sourceDesc.Width != destinationDesc.Width || sourceDesc.Height != destinationDesc.Height);
			copyBox = { 0, 0, 0, sourceDesc.Width, sourceDesc.Height, 1 };
			if (copyRegion) {
				copyBox.right = std::min(copyBox.right, destinationDesc.Width);
				copyBox.bottom = std::min(copyBox.bottom, destinationDesc.Height);
			}
			producedWidth = std::min(a_producedWidth, sourceDesc.Width);
			producedHeight = std::min(a_producedHeight, sourceDesc.Height);
		}

		~DeferredInput() { Materialize(); }

		/** Copies at most once; callers release conflicting bindings before resolving. */
		void Materialize() noexcept
		{
			if (state != State::Pending || !context)
				return;
			if (copyRegion)
				context->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0, source.Get(), 0, &copyBox);
			else
				context->CopyResource(destination.Get(), source.Get());
			state = State::Copied;
		}

		/** Prevents a late fallback from overwriting the queued composite output. */
		void Consume() noexcept
		{
			if (state == State::Pending)
				state = State::Consumed;
		}

		/** Selects direct input only when all legacy copy pixels are produced and overwritten. */
		Control Prepare(ID3D11UnorderedAccessView* a_output, UINT a_compositeWidth, UINT a_compositeHeight)
		{
			if (CanConsume(a_output, a_compositeWidth, a_compositeHeight))
				return { copyBox.right, copyBox.bottom, 1, 0 };
			Materialize();
			return {};
		}

		/** Returns the retained temporary only while its copy remains pending. */
		ID3D11ShaderResourceView* View() const { return state == State::Pending ? srv.Get() : nullptr; }

	private:
		enum class State
		{
			Empty,
			Pending,
			Copied,
			Consumed
		};

		bool CanConsume(ID3D11UnorderedAccessView* a_output, UINT a_width, UINT a_height) const
		{
			if (state != State::Pending || !context || !a_output || !copyBox.right || !copyBox.bottom ||
				copyBox.right > producedWidth || copyBox.bottom > producedHeight ||
				copyBox.right > a_width || copyBox.bottom > a_height)
				return false;
			if (!copyRegion && (sourceDesc.Width != destinationDesc.Width || sourceDesc.Height != destinationDesc.Height))
				return false;
			if (sourceDesc.MipLevels != 1 || sourceDesc.ArraySize != 1 || sourceDesc.SampleDesc.Count != 1 ||
				destinationDesc.MipLevels != 1 || destinationDesc.ArraySize != 1 || destinationDesc.SampleDesc.Count != 1 ||
				sourceDesc.Format != destinationDesc.Format)
				return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> outputResource, viewResource;
			a_output->GetResource(outputResource.GetAddressOf());
			srv->GetResource(viewResource.GetAddressOf());
			if (!Util::SameIdentity(destination.Get(), outputResource.Get()) ||
				!Util::SameIdentity(source.Get(), viewResource.Get()) || Util::SameIdentity(source.Get(), destination.Get()))
				return false;
			D3D11_UNORDERED_ACCESS_VIEW_DESC outputDesc{};
			a_output->GetDesc(&outputDesc);
			if (outputDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || outputDesc.Texture2D.MipSlice != 0 ||
				sourceUAVDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || sourceUAVDesc.Texture2D.MipSlice != 0 ||
				srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || srvDesc.Texture2D.MostDetailedMip != 0 || srvDesc.Texture2D.MipLevels != 1 ||
				outputDesc.Format != sourceUAVDesc.Format || outputDesc.Format != srvDesc.Format)
				return false;
			// Exact typed-view agreement excludes sRGB conversion and integer reinterpretation.
			switch (outputDesc.Format) {
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				return true;
			default:
				return false;
			}
		}

		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source, destination;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		D3D11_TEXTURE2D_DESC sourceDesc{}, destinationDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_UNORDERED_ACCESS_VIEW_DESC sourceUAVDesc{};
		D3D11_BOX copyBox{};
		UINT producedWidth = 0, producedHeight = 0;
		bool copyRegion = false;
		State state = State::Empty;
	};
}
