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
		RE::TESObjectREFR* ObjectOwner(const RE::NiAVObject* object)
		{
			for (unsigned depth = 0; object && depth < 32; ++depth, object = object->parent) {
				if (auto* owner = Util::GetOwnUserData(object))
					return owner;
			}
			return nullptr;
		}
		std::uint32_t OwnerHandle(const RE::NiAVObject* object, bool actor)
		{
			const auto owner = ObjectOwner(object);
			return owner && owner->IsActor() == actor ? owner->GetHandle().native_handle() : 0;
		}
		void CaptureFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light)
		{
			context.captured = true;
			const auto* source = light.light.get();
			if (!source || !light.GetIsParabolicLight())
				return;
			auto* owner = ObjectOwner(source);
			if (!owner)
				return;
			const bool carried = owner->IsActor();
			if (carried && !light.dynamic)
				return;
			auto origin = source->world.translate;
			if (!carried) {
				const auto offset = source->local.translate;
				if (!Util::IsFinite(offset.x) || !Util::IsFinite(offset.y) || !Util::IsFinite(offset.z))
					return;
				// Only placed flames use the wobble anchor; carried holders must enclose the actual light.
				const double offsetSquared = double(offset.x) * offset.x + double(offset.y) * offset.y + double(offset.z) * offset.z;
				if (source->parent && offsetSquared <= FlameFlickerMaxOffset * FlameFlickerMaxOffset)
					origin = source->parent->world.translate;
			}
			if (!Util::IsFinite(origin.x) || !Util::IsFinite(origin.y) || !Util::IsFinite(origin.z))
				return;
			context.ownerHandle = owner->GetHandle().native_handle();
			context.origin = { origin.x, origin.y, origin.z };
			context.carried = carried;
			context.source = source;
		}
		bool MatchesFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light, const RE::BSGeometry& geometry)
		{
			const auto& bound = geometry.worldBound;
			if (!Util::IsFinite(bound.radius) || bound.radius <= 0 || !Util::IsFinite(bound.center.x) ||
				!Util::IsFinite(bound.center.y) || !Util::IsFinite(bound.center.z))
				return false;
			if (!context.captured)
				CaptureFlameHolder(context, light);
			if (!context.ownerHandle || context.source != light.light.get())
				return false;
			const std::array<double, 3> delta{ double(bound.center.x) - context.origin[0],
				double(bound.center.y) - context.origin[1], double(bound.center.z) - context.origin[2] };
			if (context.carried) {
				if (delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2] > double(bound.radius) * bound.radius ||
					geometry.GetGeometryRuntimeData().skinInstance)
					return false;
			} else if (!HoldsFlame(delta, bound.radius, geometry.world.scale)) {
				return false;
			}
			return OwnerHandle(&geometry, context.carried) == context.ownerHandle;
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
	bool IsCarriedShadowLight(RE::BSShadowLight& light) noexcept
	{
		__try {
			if (!light.dynamic || !light.GetIsParabolicLight())
				return false;
			const auto* owner = ObjectOwner(light.light.get());
			return owner && owner->IsActor();
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			return false;
		}
	}
}
