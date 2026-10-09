#include "Features/VR/WandBeamRenderer.h"
#include "Features/VR/WandBeamShader.h"
#include "GpuPass.h"
#include "Util.h"
#include "Utils/ComputeState.h"

#include <array>
#include <cstring>
#include <d3dcompiler.h>
#include <winrt/base.h>

namespace WandBeamRenderer
{
	namespace
	{
		struct Constants
		{
			std::array<float, 4> endpoints{};
			std::array<float, 4> viewport{};
			std::array<float, 4> colour{};
			std::array<std::uint32_t, 2> origin{};
			std::array<std::uint32_t, 2> size{};
			float radius = RadiusPixels;
			float padding[3]{};
		};
		static_assert(sizeof(Constants) == 80);

		struct Resources
		{
			winrt::com_ptr<ID3D11Device> device;
			winrt::com_ptr<ID3D11VertexShader> vertex;
			winrt::com_ptr<ID3D11PixelShader> pixel;
			winrt::com_ptr<ID3D11ComputeShader> compute;
			winrt::com_ptr<ID3D11Buffer> constants;
			bool failed = false;
		};
		Resources resources;

		bool Compile(const char* a_entry, const char* a_profile, winrt::com_ptr<ID3DBlob>& a_blob)
		{
			winrt::com_ptr<ID3DBlob> error;
			const HRESULT result = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1,
				"VRWandBeam", nullptr, nullptr, a_entry, a_profile,
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				a_blob.put(), error.put());
			if (FAILED(result)) {
				logger::error("VR wand beam {} compilation failed: {}", a_entry,
					error ? static_cast<const char*>(error->GetBufferPointer()) : "No compiler diagnostic");
				return false;
			}
			return true;
		}

		bool Initialize(ID3D11Device* a_device)
		{
			if (resources.device.get() != a_device) {
				resources = {};
				resources.device.copy_from(a_device);
			}
			if (resources.failed)
				return false;
			if (resources.constants)
				return true;

			resources.failed = true;
			Resources candidate;
			candidate.device.copy_from(a_device);
			winrt::com_ptr<ID3DBlob> vertex, pixel, compute;
			if (!Compile("VSMain", "vs_5_0", vertex) || !Compile("PSMain", "ps_5_0", pixel) || !Compile("CSMain", "cs_5_0", compute))
				return false;
			if (FAILED(a_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, candidate.vertex.put()))) {
				logger::error("VR: Failed to create wand beam vertex shader");
				return false;
			}
			Util::SetResourceName(candidate.vertex.get(), "VR::WandBeamVS");
			if (FAILED(a_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, candidate.pixel.put()))) {
				logger::error("VR: Failed to create wand beam pixel shader");
				return false;
			}
			Util::SetResourceName(candidate.pixel.get(), "VR::WandBeamPS");
			if (FAILED(a_device->CreateComputeShader(compute->GetBufferPointer(), compute->GetBufferSize(), nullptr, candidate.compute.put()))) {
				logger::error("VR: Failed to create wand beam compute shader");
				return false;
			}
			Util::SetResourceName(candidate.compute.get(), "VR::WandBeamCS");
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = sizeof(Constants);
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(a_device->CreateBuffer(&description, nullptr, candidate.constants.put()))) {
				logger::error("VR: Failed to create wand beam constant buffer");
				return false;
			}
			Util::SetResourceName(candidate.constants.get(), "VR::WandBeamCB");
			resources = std::move(candidate);
			return true;
		}

		bool Upload(ID3D11Device* a_device, ID3D11DeviceContext1* a_context,
			const WandBeamGeometry::ScreenBeam& a_beam, const WandBeamGeometry::Viewport& a_viewport, const std::array<float, 4>& a_colour)
		{
			if (!a_device || !a_context || !Initialize(a_device))
				return false;
			Constants constants;
			for (std::size_t component = 0; component < a_colour.size(); ++component) {
				if (!std::isfinite(a_colour[component]) || a_colour[component] < 0.0f || a_colour[component] > 1.0f)
					return false;
			}
			constants.colour = a_colour;
			constants.endpoints = a_beam.endpoints;
			constants.viewport = { a_viewport.x, a_viewport.y, a_viewport.width, a_viewport.height };
			constants.origin = a_beam.origin;
			constants.size = a_beam.size;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(a_context->Map(resources.constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				logger::error("VR: Wand beam constant-buffer upload failed; disabling beam rendering");
				resources.failed = true;
				return false;
			}
			std::memcpy(mapped.pData, &constants, sizeof(constants));
			a_context->Unmap(resources.constants.get(), 0);
			return true;
		}
	}

	bool Draw(ID3D11Device* a_device, ID3D11DeviceContext1* a_context,
		const WandBeamGeometry::ScreenBeam& a_beam, const WandBeamGeometry::Viewport& a_viewport, const std::array<float, 4>& a_colour)
	{
		if (!Upload(a_device, a_context, a_beam, a_viewport, a_colour))
			return false;
		CS_GPU_PASS("VR::WandBeam");
		ID3D11Buffer* constants = resources.constants.get();
		a_context->IASetInputLayout(nullptr);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_context->VSSetShader(resources.vertex.get(), nullptr, 0);
		a_context->VSSetConstantBuffers(0, 1, &constants);
		a_context->PSSetShader(resources.pixel.get(), nullptr, 0);
		a_context->PSSetConstantBuffers(0, 1, &constants);
		a_context->Draw(6, 0);
		return true;
	}

	bool Composite(ID3D11Device* a_device, ID3D11DeviceContext1* a_context,
		ID3D11UnorderedAccessView* a_target, const WandBeamGeometry::ScreenBeam& a_beam,
		const WandBeamGeometry::Viewport& a_viewport, const std::array<float, 4>& a_colour)
	{
		if (!a_target || !Upload(a_device, a_context, a_beam, a_viewport, a_colour))
			return false;
		CS_GPU_PASS("VR::WandBeam");
		D3DState::ComputeState state(a_context, 1);
		ID3D11Buffer* constants = resources.constants.get();
		a_context->CSSetShader(resources.compute.get(), nullptr, 0);
		a_context->CSSetConstantBuffers(0, 1, &constants);
		a_context->CSSetUnorderedAccessViews(0, 1, &a_target, nullptr);
		a_context->Dispatch((a_beam.size[0] + 7) / 8, (a_beam.size[1] + 7) / 8, 1);
		return true;
	}
}
