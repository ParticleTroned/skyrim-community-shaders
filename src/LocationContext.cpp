#include "LocationContext.h"

#include "Features/InteriorSun.h"
#include "Utils/Game.h"

namespace LocationContext
{
	bool HasInteriorCell()
	{
		const auto* tes = RE::TES::GetSingleton();
		return tes && tes->interiorCell != nullptr;
	}

	State Get()
	{
		const bool inInterior = Util::IsInterior();

		return {
			.inInterior = inInterior,
		};
	}

	bool IsInteriorWithSun()
	{
		const auto* tes = RE::TES::GetSingleton();
		return InteriorSun::IsInteriorWithSun(tes ? tes->interiorCell : nullptr);
	}

	bool AllowsInteriorOnly(bool a_interiorOnly)
	{
		return !a_interiorOnly || Util::IsInterior();
	}

	bool AllowsEnabledLocations(bool a_enableInteriors, bool a_enableExteriors)
	{
		if (a_enableInteriors == a_enableExteriors)
			return a_enableInteriors;

		return AllowsEnabledLocations(a_enableInteriors, a_enableExteriors, Util::IsInterior());
	}

	bool IsDisabledByLocation(bool a_disableInteriors, bool a_disableExteriors)
	{
		if (!a_disableInteriors && !a_disableExteriors)
			return false;
		if (a_disableInteriors && a_disableExteriors)
			return true;

		return IsDisabledByLocation(a_disableInteriors, a_disableExteriors, Util::IsInterior());
	}
}
