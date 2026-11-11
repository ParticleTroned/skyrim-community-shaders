#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <d3d11.h>
#	include <nlohmann/json_fwd.hpp>
#	include <cstdint>
#	include <string_view>

namespace CSX::Diagnostics::GpuCrash
{
	/** Arms bounded, nonblocking completion queries on the renderer's immediate-context thread. */
	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
	/** Cheap admission flag; remains enabled after failure so later native failures can be logged. */
	bool IsEnabled() noexcept;
	/** Records GPU queue boundaries independently of profiler capture admission. */
	std::uint64_t BeginPass(std::string_view name) noexcept;
	void EndPass(std::uint64_t token) noexcept;
	/** Polls completed boundaries and checks device health without flushing or waiting. */
	void Checkpoint(std::string_view name) noexcept;
	/** Preserves the first failure and flushes its journal before normal failure handling continues. */
	void ReportFailure(HRESULT result, std::string_view operation) noexcept;
	/** Records CPU draw intent; it does not claim GPU execution or completion. */
	void RecordDraw(std::string_view name, std::uint32_t technique, std::uintptr_t geometry) noexcept;
	/** Captures bindings at the actual context dispatch; indirect group counts remain unknown. */
	void RecordDispatch(std::string_view name, std::uint32_t technique, UINT x, UINT y, UINT z, bool replacement,
		ID3D11Buffer* indirectArgs = nullptr, UINT indirectOffset = 0) noexcept;
	/** Returns cached diagnostic status without issuing graphics commands. */
	nlohmann::json Status();
}
#endif
