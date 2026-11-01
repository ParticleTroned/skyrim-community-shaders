#pragma once
#include <cstdint>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <array>
#	include <d3d11.h>
#	include <string>
#	include <string_view>
#endif
namespace StreamingTextures
{
	/** First failed invariant in the texture and view contract. */
	enum class TextureRejection : std::uint8_t
	{
		None,
		UnorderedAccess,
		MissingView,
		NotTexture2D,
		PowerOfTwo,
		Dimensions,
		MipChain,
		ArraySize,
		SampleCount,
		Usage,
		BindFlags,
		CpuAccess,
		MiscFlags,
		Compression,
		TypelessFormat,
		ViewDimension,
		ViewFirstMip,
		ViewMipCount,
		ViewFormat,
		ViewResource,
		PayloadSize,
		EngineDimensions,
		EngineMipCount,
		Count
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Developer-only resource sample; descriptor availability is explicit. */
	struct TextureInspection
	{
		TextureRejection reason = TextureRejection::None;
		bool unorderedAccess = false, viewAvailable = false, descriptorAvailable = false;
		std::uint32_t engineWidth = 0, engineHeight = 0, engineMips = 0;
		D3D11_TEXTURE2D_DESC texture{};
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	};
	/** Bounded cumulative contract checks and one representative per result. */
	struct TextureDiagnostics
	{
		static constexpr std::array names{
			std::string_view("eligible"), std::string_view("unorderedAccess"), std::string_view("missingView"),
			std::string_view("notTexture2D"), std::string_view("powerOfTwo"), std::string_view("dimensions"),
			std::string_view("mipChain"), std::string_view("arraySize"), std::string_view("sampleCount"),
			std::string_view("usage"), std::string_view("bindFlags"), std::string_view("cpuAccess"),
			std::string_view("miscFlags"), std::string_view("compression"), std::string_view("typelessFormat"),
			std::string_view("viewDimension"), std::string_view("viewFirstMip"), std::string_view("viewMipCount"),
			std::string_view("viewFormat"), std::string_view("viewResource"), std::string_view("payloadSize"), std::string_view("engineDimensions"),
			std::string_view("engineMipCount")
		};
		static_assert(names.size() == static_cast<std::size_t>(TextureRejection::Count));
		std::array<std::uint64_t, names.size()> reasons{};
		std::array<TextureInspection, names.size()> samples{};
		std::array<std::string, names.size()> texturePaths{};

		void Observe(const TextureInspection& inspection, std::string_view texturePath)
		{
			const auto reason = static_cast<std::size_t>(inspection.reason);
			if (!reasons[reason]++) {
				samples[reason] = inspection;
				texturePaths[reason] = texturePath.substr(0, 256);
			}
		}
	};
#endif
}
