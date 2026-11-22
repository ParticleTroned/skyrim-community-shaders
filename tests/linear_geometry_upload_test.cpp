#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

struct ID3D11Buffer
{};
struct ConstantBuffer
{
	template <class T>
	ConstantBuffer(T, const char*)
	{}
	ID3D11Buffer resource;
	std::array<std::byte, 16> bytes{};
	unsigned uploads = 0;
	bool fail = false;
	template <class T>
	void Update(const T& data)
	{
		if (fail)
			throw std::runtime_error("upload failed");
		std::memcpy(bytes.data(), &data, sizeof(data));
		++uploads;
	}
	ID3D11Buffer* CB() { return &resource; }
};
template <class T>
int ConstantBufferDesc()
{
	return 0;
}
namespace RE
{
	struct BSLightingShaderProperty
	{
		float emissiveMult = 1;
		void* GetRTTI() { return this; }
	};
	struct BSRenderPass
	{
		BSLightingShaderProperty* shaderProperty = nullptr;
	};
}
namespace globals
{
	namespace rtti
	{
		struct Type
		{
			void* value = nullptr;
			void* get() const { return value; }
		} BSLightingShaderPropertyRTTI;
	}
	namespace d3d
	{
		struct Context
		{
			unsigned binds = 0;
			ID3D11Buffer* bound = nullptr;
			void PSSetConstantBuffers(unsigned slot, unsigned count, ID3D11Buffer** buffer)
			{
				if (slot != 8 || count != 1)
					throw std::runtime_error("wrong binding");
				bound = *buffer;
				++binds;
			}
		} testContext;
		Context* context = &testContext;
	}
}
constexpr unsigned kPerGeometryCBRegister = 8;
struct LinearLighting
{
#include "linear_geometry_payload.h"
	ConstantBuffer* PerGeometryCB = nullptr;
	std::array<std::byte, sizeof(PerGeometryData)> lastUploadedPerGeometryBytes{};
	bool perGeometryUploadValid = false;
	bool enabled = true;
	bool IsRuntimeEnabled() const { return enabled; }
	void SetupResources();
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* pass);
	~LinearLighting() { delete PerGeometryCB; }
};
#include "linear_geometry_resources.h"
#include "linear_geometry_upload.h"

void Require(bool value)
{
	if (!value)
		throw std::runtime_error("geometry upload contract failed");
}
int main()
{
	LinearLighting lighting;
	RE::BSLightingShaderProperty property;
	RE::BSRenderPass pass{ &property };
	globals::rtti::BSLightingShaderPropertyRTTI.value = &property;
	lighting.SetupResources();
	auto& context = globals::d3d::testContext;
	for (std::uint32_t bits : { 0u, 0x80000000u, 0x3f800000u, 0x7f800000u, 0x7fc00001u, 0x7fc00002u, 1u }) {
		property.emissiveMult = std::bit_cast<float>(bits);
		const auto uploads = lighting.PerGeometryCB->uploads;
		const auto binds = context.binds;
		lighting.BSLightingShader_SetupGeometry(&pass);
		context.bound = nullptr;
		lighting.BSLightingShader_SetupGeometry(&pass);
		Require(lighting.PerGeometryCB->uploads == uploads + 1 && context.binds == binds + 2);
		Require(context.bound == lighting.PerGeometryCB->CB());
		Require(std::memcmp(lighting.PerGeometryCB->bytes.data(), &property.emissiveMult, sizeof(float)) == 0);
	}
	const auto binds = context.binds;
	lighting.enabled = false;
	lighting.BSLightingShader_SetupGeometry(&pass);
	lighting.enabled = true;
	lighting.BSLightingShader_SetupGeometry(nullptr);
	Require(context.binds == binds);
	property.emissiveMult = 2;
	lighting.PerGeometryCB->fail = true;
	bool failed = false;
	try {
		lighting.BSLightingShader_SetupGeometry(&pass);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed && !lighting.perGeometryUploadValid && context.binds == binds);
	lighting.PerGeometryCB->fail = false;
	lighting.BSLightingShader_SetupGeometry(&pass);
	Require(lighting.perGeometryUploadValid);
	delete lighting.PerGeometryCB;
	lighting.PerGeometryCB = nullptr;
	lighting.SetupResources();
	lighting.BSLightingShader_SetupGeometry(&pass);
	Require(lighting.PerGeometryCB->uploads == 1);
	pass.shaderProperty = nullptr;
	lighting.BSLightingShader_SetupGeometry(&pass);
	const float defaultEmission = 1;
	Require(std::memcmp(lighting.PerGeometryCB->bytes.data(), &defaultEmission, sizeof(float)) == 0);
}
