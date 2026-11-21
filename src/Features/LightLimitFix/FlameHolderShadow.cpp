#include "FlameHolderShadow.h"
#include "Features/InverseSquareLighting.h"
#include "Features/InverseSquareLighting/Common.h"
#include "Globals.h"
#include "Utils/Game.h"
#include <RE/B/BSGeometry.h>
#include <RE/B/BSShadowLight.h>
#include <RE/N/NiLight.h>
#include <RE/N/NiNode.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectLIGH.h>
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
		const RE::TESObjectLIGH* SourceLightRecord(RE::NiLight& source, RE::TESObjectREFR& owner)
		{
			// The creation hook retains source metadata even when ISL is toggled off.
			if (globals::features::inverseSquareLighting.loaded) {
				const auto* data = ISLCommon::RuntimeLightDataExt::Get(&source);
				// An invalid source ID must not substitute a different holder's light record.
				if (data->flags.any(LightLimitFix::LightFlags::Initialised) && data->lighFormId != 0)
					return RE::TESForm::LookupByID<RE::TESObjectLIGH>(data->lighFormId);
			}
			const auto* base = owner.GetObjectReference();
			return base ? base->As<RE::TESObjectLIGH>() : nullptr;
		}
		RE::NiPoint3 PlacedFlameOrigin(RE::NiLight& source, RE::TESObjectREFR& owner)
		{
			const auto actual = source.world.translate;
			const auto offset = source.local.translate;
			if (!source.parent || !Util::IsFinite(offset.x) || !Util::IsFinite(offset.y) || !Util::IsFinite(offset.z))
				return actual;
			const double offsetSquared = double(offset.x) * offset.x + double(offset.y) * offset.y + double(offset.z) * offset.z;
			if (offsetSquared == 0)
				return actual;
			const auto parent = source.parent->world.translate;
			if (!Util::IsFinite(parent.x) || !Util::IsFinite(parent.y) || !Util::IsFinite(parent.z))
				return actual;
			const auto* record = SourceLightRecord(source, owner);
			if (!record || record->GetNoFlicker())
				return actual;
			const double amplitude = record->data.flickerMovementAmplitude;
			if (!Util::IsFinite(amplitude) || amplitude <= 0)
				return actual;
			// Pulse movement reaches sqrt(1.25) times its authored amplitude.
			constexpr double FlickerReachPerAmplitude = 1.125;
			const double reach = amplitude * FlickerReachPerAmplitude;
			return offsetSquared <= reach * reach ? parent : actual;
		}
		void CaptureFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light)
		{
			context.captured = true;
			auto* source = light.light.get();
			if (!source || !light.GetIsParabolicLight())
				return;
			auto* owner = ObjectOwner(source);
			if (!owner)
				return;
			const bool carried = owner->IsActor();
			if (carried && !light.dynamic)
				return;
			auto origin = source->world.translate;
			if (!Util::IsFinite(origin.x) || !Util::IsFinite(origin.y) || !Util::IsFinite(origin.z))
				return;
			if (!carried)
				origin = PlacedFlameOrigin(*source, *owner);
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
