#include "GeometryDemand.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Policy.h"
#include "State.h"
#include "Utils/Game.h"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace StreamingTextures
{
	RE::BSLightingShaderMaterialBase* StaticMaterial(RE::BSGeometry* geometry, std::uint32_t* categories)
	{
		std::uint32_t required = 0;
		if (categories)
			*categories = 0;
		if (!geometry || geometry->GetType().underlying() != static_cast<std::uint8_t>(RE::BSGeometry::Type::kTriShape))
			return nullptr;
		const auto& data = geometry->GetGeometryRuntimeData();
		const auto* property = data.shaderProperty.get();
		if (!property || !property->material || data.skinInstance || geometry->GetControllers())
			return nullptr;
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		auto allowed = static_cast<std::uint64_t>(Flag::kSpecular) | static_cast<std::uint64_t>(Flag::kReceiveShadows) |
		               static_cast<std::uint64_t>(Flag::kCastShadows) | static_cast<std::uint64_t>(Flag::kZBufferTest) |
		               static_cast<std::uint64_t>(Flag::kZBufferWrite) | static_cast<std::uint64_t>(Flag::kUniformScale) |
		               static_cast<std::uint64_t>(Flag::kVertexColors) | static_cast<std::uint64_t>(Flag::kNoFade);
		if (const auto* alpha = data.alphaProperty.get()) {
			if (alpha->GetControllers() || alpha->GetAlphaBlending() || !alpha->GetAlphaTesting())
				return nullptr;
			required |= AlphaTestedStatics;
			allowed |= static_cast<std::uint64_t>(Flag::kTwoSided);
		}
		constexpr auto emissive = static_cast<std::uint64_t>(Flag::kOwnEmit) |
		                          static_cast<std::uint64_t>(Flag::kExternalEmittance) | static_cast<std::uint64_t>(Flag::kGlowMap);
		const auto feature = property->material->GetFeature();
		if ((property->flags.underlying() & emissive) || feature == RE::BSShaderMaterial::Feature::kGlowMap)
			required |= EmissiveStatics;
		allowed |= emissive;
		if ((property->flags.underlying() & ~allowed) || property->GetControllers() ||
			property->material->GetType() != RE::BSShaderMaterial::Type::kLighting ||
			(feature != RE::BSShaderMaterial::Feature::kDefault && feature != RE::BSShaderMaterial::Feature::kGlowMap))
			return nullptr;
		auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(property->material);
		if (material->diffuseRenderTargetSourceIndex != -1 || material->materialAlpha != 1.0f ||
			material->refractionPower != 0 || material->rimSoftLightingTexture || material->specularBackLightingTexture)
			return nullptr;
		if (feature == RE::BSShaderMaterial::Feature::kGlowMap) {
			const auto* glow = static_cast<RE::BSLightingShaderMaterialGlowmap*>(material)->glowTexture.get();
			if (glow && (glow == material->diffuseTexture.get() || glow == material->normalTexture.get()))
				return nullptr;
		}
		if (categories)
			*categories = required;
		return material;
	}

	double UnitsPerUV(RE::BSGeometry* geometry, RE::BSLightingShaderMaterialBase* material)
	{
		const double invalid = std::numeric_limits<double>::infinity();
		const auto* shape = geometry ? geometry->AsTriShape() : nullptr;
		if (!shape || !material)
			return invalid;
		const auto& shapeData = shape->GetTrishapeRuntimeData();
		const auto* data = geometry->GetGeometryRuntimeData().rendererData;
		if (!data || !data->rawVertexData || !data->rawIndexData || !shapeData.vertexCount || !shapeData.triangleCount || shapeData.triangleCount > 4096)
			return invalid;
		const auto descriptor = data->vertexDesc;
		using Vertex = RE::BSGraphics::Vertex;
		if (!descriptor.HasFlag(Vertex::VF_VERTEX) || !descriptor.HasFlag(Vertex::VF_UV) || !descriptor.HasFlag(Vertex::VF_FULLPREC))
			return invalid;
		std::uint64_t bits = 0;
		static_assert(sizeof(bits) == sizeof(descriptor));
		std::memcpy(&bits, &descriptor, sizeof(bits));
		const auto stride = static_cast<std::uint32_t>(bits & 15) * 4;
		const auto uvOffset = descriptor.GetAttributeOffset(Vertex::VA_TEXCOORD0);
		if (stride < 16 || stride > 64 || uvOffset < 12 || uvOffset + 4 > stride)
			return invalid;
		if (!data->vertexBuffer || !data->indexBuffer)
			return invalid;
		D3D11_BUFFER_DESC vertices{}, indices{};
		reinterpret_cast<ID3D11Buffer*>(data->vertexBuffer)->GetDesc(&vertices);
		reinterpret_cast<ID3D11Buffer*>(data->indexBuffer)->GetDesc(&indices);
		if (std::uint64_t{ shapeData.vertexCount } * stride > vertices.ByteWidth ||
			std::uint64_t{ shapeData.triangleCount } * 3 * sizeof(std::uint16_t) > indices.ByteWidth)
			return invalid;
		const double scaleU = std::abs(material->texCoordScale[0].x), scaleV = std::abs(material->texCoordScale[0].y);
		if (!std::isfinite(scaleU) || !std::isfinite(scaleV) || scaleU < 0.0001 || scaleV < 0.0001)
			return invalid;
		double largest = 0;
		for (std::uint32_t triangle = 0; triangle < shapeData.triangleCount; ++triangle) {
			std::array<RE::NiPoint3, 3> position;
			std::array<std::array<double, 2>, 3> uv;
			for (unsigned vertex = 0; vertex < 3; ++vertex) {
				const auto index = data->rawIndexData[triangle * 3 + vertex];
				if (index >= shapeData.vertexCount)
					return invalid;
				const auto* bytes = data->rawVertexData + index * stride;
				std::memcpy(&position[vertex], bytes, sizeof(RE::NiPoint3));
				std::array<std::uint16_t, 2> packed;
				std::memcpy(packed.data(), bytes + uvOffset, sizeof(packed));
				uv[vertex] = { DirectX::PackedVector::XMConvertHalfToFloat(packed[0]) * scaleU,
					DirectX::PackedVector::XMConvertHalfToFloat(packed[1]) * scaleV };
			}
			const auto a = position[1] - position[0], b = position[2] - position[0];
			const double u1 = uv[1][0] - uv[0][0], v1 = uv[1][1] - uv[0][1];
			const double u2 = uv[2][0] - uv[0][0], v2 = uv[2][1] - uv[0][1];
			const double determinant = u1 * v2 - u2 * v1;
			if (!std::isfinite(determinant) || std::abs(determinant) < 1e-10)
				return invalid;
			double squared = 0;
			for (unsigned axis = 0; axis < 3; ++axis) {
				const double du = (a[axis] * v2 - b[axis] * v1) / determinant;
				const double dv = (b[axis] * u1 - a[axis] * u2) / determinant;
				squared += du * du + dv * dv;
			}
			if (!std::isfinite(squared))
				return invalid;
			largest = std::max(largest, std::sqrt(squared));
		}
		return largest;
	}

	double RequiredEdge(RE::BSGeometry* geometry, double unitsPerUV, const DemandContext& demand)
	{
		const auto invalid = std::numeric_limits<double>::infinity();
		if (!geometry || !StaticMaterial(geometry) || !globals::state || demand.frame == UINT32_MAX ||
			globals::state->frameCount - demand.frame > 1 || GetTickCount64() - demand.sampledAtMs > 250)
			return invalid;
		auto eyes = demand.eyes;
		for (unsigned i = 0; i < demand.eyeCount; ++i) {
			const auto& p = demand.positions[i];
			const auto offset = geometry->worldBound.center - RE::NiPoint3{ p[0], p[1], p[2] };
			// Include a movement horizon so walking does not immediately outrun an in-flight refill.
			const auto& f = demand.forward[i];
			const double depth = offset.x * f[0] + offset.y * f[1] + offset.z * f[2];
			eyes[i].distanceToBound = depth - geometry->worldBound.radius - 512.0;
			// Perspective magnification grows off-axis; the view cone bounds that derivative.
			const double cone = std::sqrt(1.0 + 1.0 / (eyes[i].projectionX * eyes[i].projectionX) +
										  1.0 / (eyes[i].projectionY * eyes[i].projectionY));
			eyes[i].projectionX *= cone;
			eyes[i].projectionY *= cone;
		}
		return TextureStreamingPolicy::RequiredEdge(eyes, demand.eyeCount, unitsPerUV * std::abs(geometry->world.scale), demand.mipBias);
	}
}
