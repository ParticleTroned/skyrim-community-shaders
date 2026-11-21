#include "Features/LightLimitFix/ShadowCullingDrawScope.h"
#include "Utils/ScopedFlag.h"
#include <Windows.h>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>

namespace RE
{
	struct TESObjectREFR
	{
		std::uint32_t handle = 1;
		bool actor = false;
		unsigned handleReads = 0;
		bool IsActor() const { return actor; }
		struct Handle
		{
			std::uint32_t value;
			~Handle() { value = 0; }
			std::uint32_t native_handle() const { return value; }
		};
		Handle GetHandle()
		{
			++handleReads;
			return { handle };
		}
	};
	struct NiAVObject
	{
		struct Point
		{
			float x = 0, y = 0, z = 0;
		};
		struct Transform
		{
			Point translate;
			float scale = 1;
		} world, local;
		NiAVObject* parent = nullptr;
		TESObjectREFR* owner = nullptr;
		bool accessFault = false;
	};
	class NiLight : public NiAVObject
	{};
	class BSGeometry : public NiAVObject
	{
	public:
		struct RuntimeData
		{
			const void* skinInstance = nullptr;
		} runtimeData;
		const RuntimeData& GetGeometryRuntimeData() const { return runtimeData; }
		struct
		{
			Point center;
			float radius = 5;
		} worldBound;
	};
	class BSShadowLight
	{
	public:
		struct
		{
			NiLight* value = nullptr;
			NiLight* get() const { return value; }
		} light;
		bool parabolic = true, dynamic = true;
		bool GetIsParabolicLight() { return parabolic; }
	};
}
namespace Util
{
	RE::TESObjectREFR* GetOwnUserData(const RE::NiAVObject* object)
	{
		if (object->accessFault)
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		return object->owner;
	}
}
#include "flame_holder_under_test.h"
namespace C = LightLimitFixDetail::ShadowCulling;

void Require(bool condition, std::source_location location = std::source_location::current())
{
	if (!condition)
		throw std::runtime_error("Flame holder regression at line " + std::to_string(location.line()));
}

int main()
{
	try {
		Require(C::HoldsFlame({ 0, 0, 0 }, 17, 1));
		Require(C::HoldsFlame({ 0, 0, -29 }, 5, 1));
		Require(!C::HoldsFlame({ 0, 0, -30 }, 5, 1));
		Require(!C::HoldsFlame({ 0, 0, 10 }, 5, 1));
		Require(!C::HoldsFlame({ 10, 0, -10 }, 5, 1));
		Require(!C::HoldsFlame({ 0, 0, 0 }, 24, 1));
		for (const double scale : { 0.5, 1.0, 2.0 }) {
			Require(C::HoldsFlame({ 0, 0, -29 * scale }, 5 * scale, scale));
			Require(!C::HoldsFlame({ 0, 0, 0 }, 24 * scale, scale));
		}
		for (const double invalid : { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
			Require(!C::HoldsFlame({ 0, 0, 0 }, invalid, 1));
			Require(!C::HoldsFlame({ 0, 0, 0 }, 5, invalid));
		}
		Require(!C::HoldsFlame({ std::numeric_limits<double>::quiet_NaN(), 0, 0 }, 5, 1));

		RE::TESObjectREFR owner, otherOwner{ 2 };
		RE::NiAVObject root;
		root.owner = &owner;
		RE::NiLight source, otherSource;
		source.parent = &root;
		RE::BSShadowLight light;
		light.light.value = &source;
		RE::BSGeometry holder;
		holder.parent = &root;
		const auto fresh = [&] { C::FlameHolderContext context; return C::ShouldSkipFlameHolder(context, light, holder); };
		Require(fresh());
		holder.owner = &otherOwner;
		Require(!fresh());
		holder.owner = nullptr;
		owner.actor = true;
		Require(fresh());
		holder.runtimeData.skinInstance = &holder;
		Require(!fresh());
		holder.runtimeData.skinInstance = nullptr;
		// Carried geometry uses its world sphere, including larger rigid held meshes.
		holder.worldBound.radius = 40;
		holder.worldBound.center.x = 40;
		Require(fresh());
		holder.worldBound.center.x = 40.01f;
		Require(!fresh());
		holder.worldBound.center = {};
		holder.owner = &otherOwner;
		Require(!fresh());
		otherOwner.actor = true;
		Require(!fresh());
		otherOwner.actor = false;
		holder.owner = nullptr;
		owner.handle = 0;
		Require(!fresh());
		owner.handle = 1;
		RE::NiAVObject attachedObject;
		attachedObject.owner = &otherOwner;
		attachedObject.parent = &root;
		holder.parent = &attachedObject;
		Require(!fresh());
		holder.parent = &root;
		holder.worldBound.radius = 5;
		// The carried check must not substitute the parent's flame-wobble anchor.
		source.local.translate.x = source.world.translate.x = 16;
		Require(!fresh());
		holder.worldBound.center.x = 16;
		Require(fresh());
		source.local.translate = source.world.translate = holder.worldBound.center = {};
		for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			holder.worldBound.radius = invalid;
			Require(!fresh());
		}
		holder.worldBound.radius = 5;
		for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			holder.worldBound.center.x = invalid;
			Require(!fresh());
			holder.worldBound.center = {};
			source.world.translate.x = invalid;
			Require(!fresh());
			source.world.translate = {};
		}
		{
			C::FlameHolderContext context;
			Require(C::ShouldSkipFlameHolder(context, light, holder));
			const auto reads = owner.handleReads;
			Require(C::ShouldSkipFlameHolder(context, light, holder));
			Require(owner.handleReads == reads + 1);
			// Bounds rejection must precede any caster ownership traversal.
			holder.worldBound.center.x = 6;
			holder.accessFault = true;
			Require(!C::ShouldSkipFlameHolder(context, light, holder));
			Require(context.ownerHandle == owner.handle && owner.handleReads == reads + 1);
			holder.worldBound.center = {};
			Require(!C::ShouldSkipFlameHolder(context, light, holder));
			Require(context.captured && !context.ownerHandle);
			holder.accessFault = false;
			Require(!C::ShouldSkipFlameHolder(context, light, holder));
			Require(fresh());
		}
		Require(C::IsCarriedShadowLight(light));
		light.dynamic = false;
		Require(!fresh());
		Require(!C::IsCarriedShadowLight(light));
		light.dynamic = true;
		light.parabolic = false;
		Require(!fresh());
		Require(!C::IsCarriedShadowLight(light));
		light.parabolic = true;
		light.light.value = nullptr;
		Require(!C::IsCarriedShadowLight(light));
		light.light.value = &source;
		root.accessFault = true;
		Require(!fresh());
		Require(!C::IsCarriedShadowLight(light));
		root.accessFault = false;
		source.parent = &source;
		Require(!C::IsCarriedShadowLight(light));
		source.parent = &root;
		owner.actor = false;
		Require(!C::IsCarriedShadowLight(light));
		holder.parent = nullptr;
		Require(!fresh());
		holder.parent = &root;
		light.parabolic = false;
		Require(!fresh());
		light.parabolic = true;
		light.light.value = nullptr;
		Require(!fresh());
		light.light.value = &source;
		// Native flame wobble must not repeatedly change a holder's classification.
		source.local.translate.x = source.world.translate.x = 16;
		Require(fresh());
		source.local.translate.x = source.world.translate.x = 17;
		Require(!fresh());
		source.local.translate = source.world.translate = {};
		root.world.translate.x = std::numeric_limits<float>::infinity();
		Require(!fresh());
		root.world.translate = {};
		root.accessFault = true;
		Require(!fresh());
		root.accessFault = false;
		// Bounded traversal fails open even for a malformed parent cycle.
		holder.parent = &holder;
		Require(!fresh());
		holder.parent = &root;
		{
			const C::DrawScope scope{ &light };
			auto* context = C::DrawScope::FlameHolder();
			Require(C::ShouldSkipFlameHolder(*context, light, holder));
			const auto reads = owner.handleReads;
			Require(C::ShouldSkipFlameHolder(*context, light, holder));
			Require(owner.handleReads == reads + 1);
			light.light.value = &otherSource;
			Require(!C::ShouldSkipFlameHolder(*context, light, holder));
			light.light.value = &source;
			{
				const C::DrawScope nested{ nullptr };
				Require(!C::DrawScope::Current() && C::DrawScope::FlameHolder() != context);
				Require(!C::DrawScope::FlameHolder()->captured);
			}
			Require(C::DrawScope::FlameHolder() == context && context->captured);
		}
		Require(!C::DrawScope::Current() && !C::DrawScope::FlameHolder());
		{
			const C::DrawScope nextRender{ &light };
			Require(!C::DrawScope::FlameHolder()->captured);
		}
		constexpr std::uint32_t carried = 1u << 31;
		std::uint32_t flags = 4;
		{
			const Util::ScopedFlag outer(flags, carried, true);
			Require(flags == (carried | 4));
			{
				const Util::ScopedFlag unrelatedLight(flags, carried, false);
				Require(flags == 4);
				flags |= 8;
			}
			Require(flags == (carried | 12));
			bool caught = false;
			try {
				const Util::ScopedFlag nested(flags, carried, false);
				throw std::runtime_error("Scope unwind fixture");
			} catch (const std::runtime_error&) {
				caught = true;
			}
			Require(caught && flags == (carried | 12));
		}
		Require(flags == 12);
		std::cout << "Flame holder ownership, bounds and render lifetime passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
