#include "FlameHolderShadow.h"
#include "Utils/Game.h"
#include <RE/B/BSGeometry.h>
#include <RE/B/BSShadowLight.h>
#include <RE/N/NiLight.h>
#include <RE/N/NiNode.h>
#include <RE/T/TESObjectREFR.h>
#include <Windows.h>

namespace LightLimitFixDetail::ShadowCulling
{
	namespace
	{
		std::uint32_t PlacedOwnerHandle(const RE::NiAVObject* object)
		{
			for (unsigned depth = 0; object && depth < 32; ++depth, object = object->parent) {
				if (auto* owner = Util::GetOwnUserData(object))
					return owner->IsActor() ? 0 : owner->GetHandle().native_handle();
			}
			return 0;
		}
		void CaptureFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light)
		{
			context.captured = true;
			const auto* source = light.light.get();
			if (!source || !light.GetIsParabolicLight())
				return;
			auto origin = source->world.translate;
			const auto offset = source->local.translate;
			if (!Util::IsFinite(offset.x) || !Util::IsFinite(offset.y) || !Util::IsFinite(offset.z))
				return;
			// Small local offsets are native flame wobble; larger offsets may deliberately place the light.
			const double offsetSquared = double(offset.x) * offset.x + double(offset.y) * offset.y + double(offset.z) * offset.z;
			if (source->parent && offsetSquared <= FlameFlickerMaxOffset * FlameFlickerMaxOffset)
				origin = source->parent->world.translate;
			if (!Util::IsFinite(origin.x) || !Util::IsFinite(origin.y) || !Util::IsFinite(origin.z))
				return;
			context.ownerHandle = PlacedOwnerHandle(source);
			context.restOrigin = { origin.x, origin.y, origin.z };
			context.source = source;
		}
		bool MatchesFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light, const RE::BSGeometry& geometry)
		{
			const auto& bound = geometry.worldBound;
			const double scale = geometry.world.scale;
			if (!FlameHolderSizeEligible(bound.radius, scale))
				return false;
			if (!context.captured)
				CaptureFlameHolder(context, light);
			if (!context.ownerHandle || context.source != light.light.get())
				return false;
			const std::array<double, 3> delta{ double(bound.center.x) - context.restOrigin[0],
				double(bound.center.y) - context.restOrigin[1], double(bound.center.z) - context.restOrigin[2] };
			return HoldsFlame(delta, bound.radius, scale) && PlacedOwnerHandle(&geometry) == context.ownerHandle;
		}
	}
	bool ShouldSkipFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light, const RE::BSGeometry& geometry) noexcept
	{
		__try {
			return MatchesFlameHolder(context, light, geometry);
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			context = {};
			context.captured = true;
			return false;
		}
	}
}
