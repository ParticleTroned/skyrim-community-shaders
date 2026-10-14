#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <source_location>

namespace
{
	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Billboard tint assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	namespace RE
	{
		struct NiColorA
		{
			float red = 1, green = 1, blue = 1, alpha = 1;
		};
		struct BSEffectShaderMaterial
		{
			NiColorA baseColor;
			float baseColorScale = 1;
		};
		struct BSShaderProperty
		{
			enum class EShaderPropertyFlag
			{
				kVertexAlpha
			};
		};
		struct BSEffectShaderProperty
		{
			NiColorA* emittanceColor = nullptr;
			struct Flags
			{
				bool vertexAlpha = false;
				bool any(BSShaderProperty::EShaderPropertyFlag) const { return vertexAlpha; }
			} flags;
		};
		namespace BSGraphics::Vertex
		{
			enum class Flags
			{
				VF_COLORS
			};
			enum class Attribute
			{
				VA_COLOR
			};
		}
		struct RendererData
		{
			struct VertexDesc
			{
				std::uint32_t size = 4, offset = 0;
				bool colors = true;
				std::uint32_t GetSize() const { return size; }
				bool HasFlag(BSGraphics::Vertex::Flags) const { return colors; }
				std::uint32_t GetAttributeOffset(BSGraphics::Vertex::Attribute) const { return offset; }
			} vertexDesc;
			const std::uint8_t* rawVertexData = nullptr;
		};
		struct BSGeometry
		{
			struct RuntimeData
			{
				RendererData* rendererData = nullptr;
			} runtime;
			struct TriShape
			{
				struct RuntimeData
				{
					std::uint32_t vertexCount = 0;
				} runtime;
				RuntimeData& GetTrishapeRuntimeData() { return runtime; }
			} triShape;
			bool isTriShape = true;
			std::optional<NiColorA> siblingTint;
			unsigned siblingQueries = 0;
			RuntimeData& GetGeometryRuntimeData() { return runtime; }
			TriShape* AsTriShape() { return isTriShape ? &triShape : nullptr; }
		};
	}

	namespace ParticleLights
	{
		struct Config
		{
			RE::NiColorA colorMult;
		};
		struct GradientConfig
		{
			RE::NiColorA color;
		};
	}
	struct LightLimitFix
	{
		struct ParticleLightReference
		{
			bool applyEffectMaterialTint = false;
			RE::NiColorA baseColor;
			ParticleLights::Config config;
			bool hasGradientConfig = false;
			ParticleLights::GradientConfig gradientConfig;
		};
	};

	bool TryGetBillboardSiblingEmissiveTint(RE::BSGeometry* a_geometry, RE::NiColorA& a_tint)
	{
		++a_geometry->siblingQueries;
		if (!a_geometry->siblingTint) {
			return false;
		}
		a_tint = *a_geometry->siblingTint;
		return true;
	}

#include "billboard_tint_near_white_under_test.h"

#include "billboard_tint_fallback_under_test.h"

#include "billboard_tint_material_under_test.h"

#include "billboard_tint_vertex_color_under_test.h"

#include "billboard_tint_vertex_scan_under_test.h"

#include "billboard_tint_resolve_under_test.h"

	constexpr RE::NiColorA white{ 1, 1, 1, 1 };
	constexpr RE::NiColorA red{ 1, 0, 0, 1 };
	constexpr RE::NiColorA green{ 0, 1, 0, 1 };
	constexpr RE::NiColorA blue{ 0, 0, 1, 1 };
	constexpr RE::NiColorA dark{ 0, 0, 0, 1 };

	void RequireColor(RE::NiColorA a_actual, RE::NiColorA a_expected)
	{
		Require(std::abs(a_actual.red - a_expected.red) < 1e-6f);
		Require(std::abs(a_actual.green - a_expected.green) < 1e-6f);
		Require(std::abs(a_actual.blue - a_expected.blue) < 1e-6f);
		Require(std::abs(a_actual.alpha - a_expected.alpha) < 1e-6f);
	}

	struct Fixture
	{
		LightLimitFix::ParticleLightReference reference;
		RE::BSGeometry geometry;
		RE::BSEffectShaderMaterial material;
		RE::BSEffectShaderProperty property;
		RE::RendererData renderer;
		std::array<VertexColor, 3> vertices{};

		void SetVertices(std::array<VertexColor, 3> a_vertices)
		{
			vertices = a_vertices;
			geometry.runtime.rendererData = &renderer;
			renderer.rawVertexData = reinterpret_cast<const std::uint8_t*>(vertices.data());
			geometry.triShape.runtime.vertexCount = static_cast<std::uint32_t>(vertices.size());
		}
		void Run()
		{
			ResolveBillboardTint(reference, &geometry, &material, &property);
		}
	};

	void TestFallbackPrecedence()
	{
		struct Case
		{
			RE::NiColorA material;
			std::optional<RE::NiColorA> sibling;
			RE::NiColorA expected;
			bool applyMaterial;
			unsigned siblingQueries;
		};
		const std::array cases{
			Case{ red, green, red, false, 0 },
			Case{ white, green, green, false, 1 },
			Case{ white, white, white, false, 1 },
			Case{ { 2, 2, 2, 1 }, RE::NiColorA{ 3, 3, 3, 1 }, { 2, 2, 2, 1 }, false, 1 },
			Case{ { 2, 2, 2, 1 }, green, green, false, 1 },
			Case{ white, std::nullopt, white, false, 1 },
			Case{ dark, white, white, false, 1 },
			Case{ dark, green, green, false, 1 },
			Case{ dark, std::nullopt, blue, true, 1 },
			Case{ { 0.5f, 0.5f, 0.5f, 0.1f }, green, { 0.5f, 0.5f, 0.5f, 1 }, false, 0 },
			Case{ { 3e-5f, 3e-5f, 3e-5f, 1 }, std::nullopt, blue, true, 1 }
		};
		for (const auto& test : cases) {
			Fixture fixture;
			fixture.reference.config.colorMult = { 0, 0, 1, 0.25f };
			fixture.material.baseColor = test.material;
			fixture.geometry.siblingTint = test.sibling;
			fixture.Run();
			RequireColor(fixture.reference.baseColor, test.expected);
			Require(fixture.reference.applyEffectMaterialTint == test.applyMaterial);
			Require(fixture.geometry.siblingQueries == test.siblingQueries);
		}

		Fixture gradient;
		gradient.reference.config.colorMult = red;
		gradient.reference.hasGradientConfig = true;
		gradient.reference.gradientConfig.color = { 0, 1, 0, 0.25f };
		gradient.material.baseColor = dark;
		gradient.Run();
		RequireColor(gradient.reference.baseColor, green);
		Require(gradient.reference.applyEffectMaterialTint);
		gradient.material.baseColor = red;
		gradient.Run();
		RequireColor(gradient.reference.baseColor, red);
		Require(!gradient.reference.applyEffectMaterialTint);
	}

	void TestVertexTintAndAlpha()
	{
		Fixture fixture;
		fixture.material.baseColor = blue;
		fixture.geometry.siblingTint = green;
		fixture.SetVertices({ VertexColor{ { 255, 0, 0, 128 } }, VertexColor{ { 0, 255, 0, 127 } }, VertexColor{ { 0, 0, 255, 128 } } });
		fixture.Run();
		RequireColor(fixture.reference.baseColor, red);
		Require(fixture.reference.applyEffectMaterialTint);
		Require(fixture.geometry.siblingQueries == 0);

		fixture.property.flags.vertexAlpha = true;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, { 1, 0, 0, 128 / 255.f });
		Require(fixture.geometry.siblingQueries == 0);

		fixture.vertices[0] = { { 255, 255, 255, 128 } };
		fixture.Run();
		RequireColor(fixture.reference.baseColor, blue);
		Require(!fixture.reference.applyEffectMaterialTint);
		Require(fixture.geometry.siblingQueries == 0);

		fixture.material.baseColor = white;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, green);
		Require(!fixture.reference.applyEffectMaterialTint);
		Require(fixture.geometry.siblingQueries == 1);

		fixture.vertices = {};
		fixture.Run();
		RequireColor(fixture.reference.baseColor, green);
		Require(fixture.geometry.siblingQueries == 2);
	}

	void TestDynamicInputs()
	{
		Fixture fixture;
		fixture.material.baseColor = red;
		fixture.geometry.siblingTint = green;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, red);
		Require(fixture.geometry.siblingQueries == 0);

		fixture.material.baseColor = white;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, green);
		Require(fixture.geometry.siblingQueries == 1);

		fixture.geometry.siblingTint = blue;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, blue);
		Require(fixture.geometry.siblingQueries == 2);

		RE::NiColorA emittance{ 1, 0, 0, 0.1f };
		fixture.property.emittanceColor = &emittance;
		fixture.material.baseColorScale = 2;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, { 2, 0, 0, 1 });
		Require(fixture.geometry.siblingQueries == 2);

		emittance = white;
		fixture.material.baseColorScale = 1;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, blue);
		Require(fixture.geometry.siblingQueries == 3);

		emittance = dark;
		fixture.geometry.siblingTint.reset();
		fixture.reference.config.colorMult = green;
		fixture.Run();
		RequireColor(fixture.reference.baseColor, green);
		Require(fixture.reference.applyEffectMaterialTint);
		Require(fixture.geometry.siblingQueries == 4);
	}

	void TestVertexLayoutValidation()
	{
		const std::array<std::uint8_t, 16> data{ 1, 2, 3, 4, 255, 0, 0, 128, 9, 8, 7, 6, 0, 255, 0, 255 };
		VertexColor output{ { 10, 20, 30, 40 } };
		Require(TryGetMaxAlphaVertexColor(data.data(), 8, 4, 2, output));
		Require(output.data[0] == 0 && output.data[1] == 255 && output.data[2] == 0 && output.data[3] == 255);
		Require(!TryGetMaxAlphaVertexColor(nullptr, 8, 4, 2, output));
		Require(!TryGetMaxAlphaVertexColor(data.data(), 3, 0, 2, output));
		Require(!TryGetMaxAlphaVertexColor(data.data(), 8, 5, 2, output));
		Require(!TryGetMaxAlphaVertexColor(data.data(), 8, 4, 0, output));
		const std::array<std::uint8_t, 4> transparent{ 255, 0, 0, 0 };
		Require(!TryGetMaxAlphaVertexColor(transparent.data(), 4, 0, 1, output));
		Require(output.data[0] == 0 && output.data[1] == 255 && output.data[3] == 255);
	}
}

int main()
{
	TestFallbackPrecedence();
	TestVertexTintAndAlpha();
	TestDynamicInputs();
	TestVertexLayoutValidation();
	return 0;
}
