#pragma once

namespace LocationContext
{
	struct State
	{
		bool inInterior = true;
	};

	State Get();
	bool HasInteriorCell();
	bool IsInteriorWithSun();

	constexpr bool AllowsInteriorOnly(bool a_interiorOnly, bool a_inInterior) noexcept
	{
		return !a_interiorOnly || a_inInterior;
	}

	inline bool AllowsInteriorOnly(bool a_interiorOnly, const State& a_state) noexcept
	{
		return AllowsInteriorOnly(a_interiorOnly, a_state.inInterior);
	}

	bool AllowsInteriorOnly(bool a_interiorOnly);

	constexpr bool AllowsEnabledLocations(bool a_enableInteriors, bool a_enableExteriors, bool a_inInterior) noexcept
	{
		return a_inInterior ? a_enableInteriors : a_enableExteriors;
	}

	inline bool AllowsEnabledLocations(bool a_enableInteriors, bool a_enableExteriors, const State& a_state) noexcept
	{
		return AllowsEnabledLocations(a_enableInteriors, a_enableExteriors, a_state.inInterior);
	}

	bool AllowsEnabledLocations(bool a_enableInteriors, bool a_enableExteriors);

	constexpr bool IsDisabledByLocation(bool a_disableInteriors, bool a_disableExteriors, bool a_inInterior) noexcept
	{
		return (a_disableInteriors && a_inInterior) || (a_disableExteriors && !a_inInterior);
	}

	inline bool IsDisabledByLocation(bool a_disableInteriors, bool a_disableExteriors, const State& a_state) noexcept
	{
		return IsDisabledByLocation(a_disableInteriors, a_disableExteriors, a_state.inInterior);
	}

	bool IsDisabledByLocation(bool a_disableInteriors, bool a_disableExteriors);

	template <class T>
	constexpr T SelectInteriorExterior(bool a_inInterior, const T& a_interiorValue, const T& a_exteriorValue)
	{
		return a_inInterior ? a_interiorValue : a_exteriorValue;
	}

	template <class T>
	constexpr T SelectInteriorExterior(const State& a_state, const T& a_interiorValue, const T& a_exteriorValue)
	{
		return SelectInteriorExterior(a_state.inInterior, a_interiorValue, a_exteriorValue);
	}
}
