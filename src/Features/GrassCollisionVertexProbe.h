#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <cstdint>
#	include <d3d11_1.h>
#	include <memory>
#	include <nlohmann/json.hpp>

class GrassCollisionVertexProbe
{
public:
	using DirectDraw = void (*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
	using IndirectDraw = void (*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
	struct Draw
	{
		UINT indices = 0, instances = 0, firstIndex = 0, firstInstance = 0;
		INT baseVertex = 0;
		ID3D11Buffer* arguments = nullptr;
		UINT argumentOffset = 0;
	};

	GrassCollisionVertexProbe();
	~GrassCollisionVertexProbe();
	/** @brief Start a bounded request without touching the rendering context. */
	void Request(uint64_t a_generation, uint32_t a_frame);
	/** @brief Read paired stream-output observations; empty samples are not a bending verdict. */
	nlohmann::json Read() const;
	/** @brief Replay bounded real vertex inputs with the current field and a zero-field baseline. */
	void Capture(ID3D11DeviceContext1* a_context, ID3D11VertexShader* a_vertex,
		const void* a_bytecode, size_t a_bytes, uint32_t a_descriptor, uint32_t a_frame,
		const Draw& a_draw, DirectDraw a_direct, IndirectDraw a_indirect);
	/** @brief Poll GPU results without flushing or waiting. */
	void Poll(ID3D11DeviceContext1* a_context, uint32_t a_frame);
	/** @brief Prevent recursive observation of diagnostic replays. */
	bool IsCapturing() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
