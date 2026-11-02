#pragma once

#include "TextureSourceDescriptions.h"
#include "Utils/ComIdentity.h"

#include <array>
#include <cstdint>
#include <utility>

namespace UpscalingTextures
{
	enum class CenterInput : std::size_t
	{
		Color,
		Depth,
		Motion,
		Reactive,
		Transparency,
		Count
	};

	/** Owned full-eye sources; immutable geometry does not establish content freshness. */
	struct FoveatedInputBundle
	{
		static constexpr auto kCount = static_cast<std::size_t>(CenterInput::Count);
		using Sources = std::array<ID3D11Resource*, kCount>;
		using Views = std::array<ID3D11ShaderResourceView*, kCount>;
		struct Stamp
		{
			std::uint64_t content = 0, neuralGeneration = 0, depthGeneration = 0;
			std::uint32_t frame = 0, sourceFrame = 0, vendorGeneration = 0, eye = 0;
			bool reversedDepth = false;
			bool operator==(const Stamp&) const = default;
		};
		std::array<TextureSourceDescription, kCount> sources;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kCount> views;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		Stamp stamp{};

		/** Capture only single-subresource numeric inputs and exact full-resource views. */
		bool Capture(ID3D11Device* owner, ID3D11DeviceContext* commands, const Sources& inputs,
			const Views& inputViews, std::uint32_t width, std::uint32_t height, const Stamp& producer)
		{
			*this = {};
			if (!owner || !commands || !width || !height || !producer.content || producer.eye >= 2 ||
				commands->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return false;
			winrt::com_ptr<ID3D11Device> contextOwner;
			commands->GetDevice(contextOwner.put());
			if (!Util::SameIdentity(contextOwner.get(), owner))
				return false;
			FoveatedInputBundle candidate;
			for (std::size_t index = 0; index < kCount; ++index) {
				auto& source = candidate.sources[index];
				if (!source.Capture(inputs[index]))
					return false;
				const auto& desc = source.Desc();
				winrt::com_ptr<ID3D11Device> sourceOwner;
				inputs[index]->GetDevice(sourceOwner.put());
				if (desc.Width != width || desc.Height != height || desc.MipLevels != 1 || desc.ArraySize != 1 ||
					desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality != 0 || !Util::SameIdentity(sourceOwner.get(), owner))
					return false;
				if (index == static_cast<std::size_t>(CenterInput::Depth) &&
					(desc.Format != DXGI_FORMAT_R32_FLOAT || (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)))
					return false;
				if (!inputViews[index]) {
					if (index <= static_cast<std::size_t>(CenterInput::Motion))
						return false;
					continue;
				}
				winrt::com_ptr<ID3D11Resource> viewed;
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				inputViews[index]->GetResource(viewed.put());
				inputViews[index]->GetDesc(&view);
				if (!Util::SameIdentity(viewed.get(), inputs[index]) || view.Format != desc.Format ||
					view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view.Texture2D.MostDetailedMip != 0 || view.Texture2D.MipLevels != 1)
					return false;
				candidate.views[index].copy_from(inputViews[index]);
			}
			candidate.device.copy_from(owner);
			candidate.context.copy_from(commands);
			candidate.stamp = producer;
			*this = std::move(candidate);
			return true;
		}
		/** The producer must invalidate its content serial before any source overwrite. */
		bool Matches(ID3D11Device* owner, ID3D11DeviceContext* commands, const Sources& inputs,
			const Views& inputViews, const Stamp& producer) const
		{
			if (!device || stamp != producer || device.get() != owner || context.get() != commands)
				return false;
			for (std::size_t index = 0; index < kCount; ++index)
				if (!sources[index].Matches(inputs[index]) || views[index].get() != inputViews[index])
					return false;
			return true;
		}
	};
}
