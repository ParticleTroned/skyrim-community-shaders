#pragma once

#include "PointShadowCorrectionMode.h"
#include "RuntimeToggle.h"

#include <nlohmann/json.hpp>

namespace PointShadowCorrection
{
	inline void to_json(nlohmann::json& value, Mode mode)
	{
		Validate(mode);
		value = static_cast<std::uint32_t>(mode);
	}

	inline void from_json(const nlohmann::json& value, Mode& mode)
	{
		if (!value.is_number_integer() || value < 0 || value > 2)
			throw std::invalid_argument("PointShadowCorrection must be an integer in [0, 2]");
		mode = static_cast<Mode>(value.get<std::uint32_t>());
	}

	/** Publish a preference once per safe frame; availability never erases it. */
	class RuntimeState
	{
	public:
		Mode Requested() const { return requested.load(std::memory_order_acquire); }
		Mode Applied() const { return applied.load(std::memory_order_acquire); }
		Mode Effective(bool available) const { return available ? Applied() : Mode::Off; }
		bool Pending() const { return Requested() != Applied(); }
		void Request(Mode mode)
		{
			Validate(mode);
			requested.store(mode, std::memory_order_release);
		}

		/** Initialize before rendering starts; later settings loads preserve the applied value. */
		void ReplaceSetting(Mode& replacement)
		{
			Request(replacement);
			if (!frame)
				applied.store(replacement, std::memory_order_release);
			replacement = Applied();
		}

		template <class State>
		void Apply(Mode& setting, const State* state)
		{
			if (!state || frame == state->frameCount)
				return;
			frame = state->frameCount;
			if (Util::IsRuntimeToggleBlocked(state))
				return;
			setting = Requested();
			applied.store(setting, std::memory_order_release);
		}

	private:
		std::atomic<Mode> requested{ Mode::Full }, applied{ Mode::Full };
		std::optional<std::uint32_t> frame;
	};
}
