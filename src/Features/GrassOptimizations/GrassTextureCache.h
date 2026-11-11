#pragma once

#include "Buffer.h"
#include "Utils/LazyShader.h"

#include <cstddef>
#include <cstdint>
#include <d3d11_1.h>
#include <memory>
#include <unordered_map>

/** @brief Render-thread cache of static grass atlas markers, with pixel-shader fallback. */
class GrassTextureCache
{
public:
	/** @brief Return the GPU normal length for this exact texture view, or nullptr on failure. */
	ID3D11ShaderResourceView* Get(ID3D11DeviceContext1* context, ID3D11ShaderResourceView* source, uint32_t frame);
	/** @brief Release texture owners after a full frame without a cache lookup. */
	void EndFrame(uint32_t frame);
	/** @brief Invalidate classification and retry failed resources after a shader-cache reset. */
	void Reset();

private:
	struct Entry
	{
		winrt::com_ptr<ID3D11ShaderResourceView> source;
		std::unique_ptr<Buffer> normalLength;
		uint32_t lastFrame = 0;
	};
	static constexpr std::size_t kMaxTextures = 256;
	std::unordered_map<ID3D11ShaderResourceView*, Entry> entries;
	Util::LazyShader<ID3D11ComputeShader> shader;
	bool failed = false;
};
