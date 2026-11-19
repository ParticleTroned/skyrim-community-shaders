#pragma once

#include <dxgiformat.h>

namespace UpscalingComposite
{
	enum class OutputType
	{
		Unsupported,
		Float,
		Unorm
	};

	/** Classifies colour UAV formats supported by the composite's typed loads and stores. */
	[[nodiscard]] constexpr OutputType GetOutputType(DXGI_FORMAT format) noexcept
	{
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return OutputType::Float;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return OutputType::Unorm;
		default:
			return OutputType::Unsupported;
		}
	}
}
