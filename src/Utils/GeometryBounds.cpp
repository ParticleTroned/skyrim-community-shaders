#include "GeometryBounds.h"

#include <RE/B/BSGeometry.h>
#include <RE/N/NiSkinData.h>
#include <RE/N/NiSkinInstance.h>

namespace Util
{
	GeometryBounds ReadGeometryBounds(
		const RE::BSGeometry& a_geometry, GeometryBoundsBudget& a_budget,
		bool a_refineSkin, bool& a_refined) noexcept
	{
		a_refined = false;
		const auto& world = a_geometry.worldBound;
		const auto fallback = GeometryBounds::FromCenterExtent(
			{ world.center.x, world.center.y, world.center.z }, { world.radius, world.radius, world.radius });
		if (!a_refineSkin)
			return fallback;
		// Morphing and special geometry can leave their bind-pose bone spheres.
		const auto type = a_geometry.GetType();
		if (type != RE::BSGeometry::Type::kTriShape && type != RE::BSGeometry::Type::kSubIndexTriShape)
			return fallback;
		const auto* skin = a_geometry.GetGeometryRuntimeData().skinInstance.get();
		const auto* data = skin ? skin->skinData.get() : nullptr;
		if (!data || !data->GetBoneDataAddress(0) || (!skin->bones && !skin->boneWorldTransforms))
			return fallback;

		return RefineGeometryBounds(fallback, data->GetBoneCount(), a_budget, [&](std::uint32_t index) noexcept {
			const auto* transform = skin->boneWorldTransforms ? skin->boneWorldTransforms[index] : nullptr;
			if (!transform && skin->bones && skin->bones[index])
				transform = &skin->bones[index]->world;
			if (!transform)
				return GeometryBounds{};
			const auto& bound = data->GetBoneDataBound(index);
			std::array<std::array<float, 3>, 3> rotation{};
			for (std::size_t row = 0; row < 3; ++row)
				for (std::size_t column = 0; column < 3; ++column)
					rotation[row][column] = transform->rotate.entry[row][column];
			return TransformGeometrySphere(
				{ bound.center.x, bound.center.y, bound.center.z }, bound.radius, rotation,
				{ transform->translate.x, transform->translate.y, transform->translate.z }, transform->scale); }, a_refined);
	}
}
