#pragma once

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <winrt/base.h>

namespace Util
{
	/** A retained preparation result whose shader is published before its terminal state. */
	struct ShaderPreparation
	{
		enum class State : std::uint8_t
		{
			Pending,
			Ready,
			Failed,
			Stale
		};

		/** Publish completion once; abandoned producers must also release boundary waiters. */
		void Finish(State a_state) noexcept
		{
			state.store(a_state, std::memory_order_release);
			state.notify_all();
		}

		std::atomic<State> state{ State::Pending };
		winrt::com_ptr<ID3D11DeviceChild> shader;
		winrt::com_ptr<ID3D11Device> device;
		std::uint64_t compilationGeneration = 0;
		std::uint64_t sourceGeneration = 0;
		std::uint64_t shaderDefinesGeneration = 0;
		bool developerMode = false;
		bool partialPrecision = false;
		bool avoidFlowControl = false;
	};
}
