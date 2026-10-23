#define NOMINMAX
#include "Features/VR/ViewRefreshPolicy.h"
#include "d3d11_shader_test.h"
#include <DirectXPackedVector.h>
#include <array>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <iostream>
#include <limits>

namespace
{
	using namespace CSX::ViewRefresh;
	using D3D11ShaderTest::Check;
	using DirectX::SimpleMath::Vector4;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	constexpr uint32_t Width = 16, Height = 9;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	struct Texture
	{
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};
	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11InfoQueue> debug;
		std::array<ComPtr<ID3D11ComputeShader>, 3> shaders;
		std::array<ComPtr<ID3D11ShaderReflection>, 3> reflections;
		Texture color, depth, distance, owner, output;
		std::array<Vector4, Width * 2 * Height> colors;
		std::array<float, Width * 2 * Height> depths;
		Matrix projection{ DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1, 1, 100) };

		Texture Create(uint32_t width, DXGI_FORMAT format, bool writable, const char* name)
		{
			Texture result;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = Height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			Check(device->CreateTexture2D(&desc, nullptr, result.resource.GetAddressOf()));
			Util::SetResourceName(result.resource.Get(), "ViewRefreshTest::%s", name);
			Check(device->CreateShaderResourceView(result.resource.Get(), nullptr, result.srv.GetAddressOf()));
			Util::SetResourceName(result.srv.Get(), "ViewRefreshTest::%s SRV", name);
			if (writable) {
				Check(device->CreateUnorderedAccessView(result.resource.Get(), nullptr, result.uav.GetAddressOf()));
				Util::SetResourceName(result.uav.Get(), "ViewRefreshTest::%s UAV", name);
			}
			return result;
		}

		Fixture()
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
				nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
			Check(device.As(&debug));
			const char* entries[] = { "SelectDepth", "SelectSource", "ResolveColor" };
			for (uint32_t i = 0; i < 3; ++i) {
				ComPtr<ID3DBlob> code, error;
				const auto result = D3DCompileFromFile(L"package/Shaders/VR/ViewRefresh.hlsl", nullptr, nullptr,
					entries[i], "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
					0, code.GetAddressOf(), error.GetAddressOf());
				if (error)
					std::cerr << static_cast<const char*>(error->GetBufferPointer());
				Check(result);
				Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shaders[i].GetAddressOf()));
				Util::SetResourceName(shaders[i].Get(), "ViewRefreshTest::%s", entries[i]);
				Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflections[i].GetAddressOf())));
			}
			color = Create(Width * 2, DXGI_FORMAT_R32G32B32A32_FLOAT, false, "Color");
			depth = Create(Width * 2, DXGI_FORMAT_R32_FLOAT, false, "Depth");
			distance = Create(Width, DXGI_FORMAT_R32_UINT, true, "Distance");
			owner = Create(Width, DXGI_FORMAT_R32_UINT, true, "Owner");
			output = Create(Width, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "Output");
			for (uint32_t y = 0; y < Height; ++y)
				for (uint32_t x = 0; x < Width * 2; ++x)
					colors[y * Width * 2 + x] = Vector4(static_cast<float>(x % Width), static_cast<float>(y), x < Width ? 1.0f : 2.0f, 0.75f);
			context->UpdateSubresource(color.resource.Get(), 0, nullptr, colors.data(), sizeof(Vector4) * Width * 2, 0);
		}

		float RawDepth(float z) const
		{
			const auto clip = Vector4::Transform(Vector4(0, 0, z, 1), projection);
			return clip.z / clip.w;
		}

		std::vector<Vector4> Run(uint32_t eye, const EyeTransform& transform, DirectX::SimpleMath::Vector2 depthUvOffset = {})
		{
			context->UpdateSubresource(depth.resource.Get(), 0, nullptr, depths.data(), sizeof(float) * Width * 2, 0);
			const UINT clear[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
			context->ClearUnorderedAccessViewUint(distance.uav.Get(), clear);
			context->ClearUnorderedAccessViewUint(owner.uav.Get(), clear);
			ID3D11ShaderResourceView* inputs[] = { color.srv.Get(), depth.srv.Get() };
			ID3D11UnorderedAccessView* outputs[] = { distance.uav.Get(), owner.uav.Get(), output.uav.Get() };
			context->CSSetShaderResources(0, 2, inputs);
			context->CSSetUnorderedAccessViews(0, 3, outputs, nullptr);
			for (uint32_t i = 0; i < 3; ++i) {
				D3D11ShaderTest::ConstantBuffer constants(device.Get(), reflections[i].Get(), "Refresh");
				constants.SetVariable("SourceClipToView", transform.sourceClipToView);
				constants.SetVariable("SourceViewToTargetView", transform.sourceViewToTargetView);
				constants.SetVariable("TargetViewToClip", transform.targetViewToClip);
				constants.SetVariable("TargetClipToSourceSkyClip", transform.targetClipToSourceSkyClip);
				constants.SetVariable("EyeSize", std::array{ Width, Height });
				constants.SetVariable("EyeIndex", eye);
				constants.SetVariable("ReverseDepth", projection._43 > 0 ? 1u : 0u);
				constants.SetVariable("DepthUvOffset", depthUvOffset);
				constants.Bind(context.Get());
				context->CSSetShader(shaders[i].Get(), nullptr, 0);
				context->Dispatch((Width + 7) / 8, (Height + 7) / 8, 1);
			}
			ID3D11ShaderResourceView* noInputs[2]{};
			ID3D11UnorderedAccessView* noOutputs[3]{};
			context->CSSetShaderResources(0, 2, noInputs);
			context->CSSetUnorderedAccessViews(0, 3, noOutputs, nullptr);
			D3D11_TEXTURE2D_DESC desc{};
			output.resource->GetDesc(&desc);
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.Usage = D3D11_USAGE_STAGING;
			ComPtr<ID3D11Texture2D> staging;
			Check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			Util::SetResourceName(staging.Get(), "ViewRefreshTest::Readback");
			context->CopyResource(staging.Get(), output.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<Vector4> result(Width * Height);
			for (uint32_t y = 0; y < Height; ++y) {
				const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
				for (uint32_t x = 0; x < Width; ++x) {
					float values[4];
					for (uint32_t c = 0; c < 4; ++c)
						values[c] = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + c]);
					result[y * Width + x] = Vector4(values);
				}
			}
			context->Unmap(staging.Get(), 0);
			for (uint64_t i = 0; i < debug->GetNumStoredMessages(); ++i) {
				SIZE_T size = 0;
				Check(debug->GetMessage(i, nullptr, &size));
				std::vector<std::byte> bytes(size);
				auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
				Check(debug->GetMessage(i, message, &size));
				if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
					throw std::runtime_error(message->pDescription);
			}
			debug->ClearStoredMessages();
			return result;
		}
	};

	void Verify()
	{
		Fixture gpu;
		EyeTransform identity;
		Require(BuildEyeTransform(gpu.projection, Matrix::Identity, Matrix::Identity, Matrix::Identity, 100, identity), "Identity setup failed");
		for (float raw : { gpu.RawDepth(10), 1.0f, std::numeric_limits<float>::quiet_NaN() }) {
			gpu.depths.fill(raw);
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const auto output = gpu.Run(eye, identity);
				for (uint32_t y = 0; y < Height; ++y)
					for (uint32_t x = 0; x < Width; ++x)
						Require((output[y * Width + x] - gpu.colors[y * Width * 2 + eye * Width + x]).Length() < 0.001f,
							"Identity, sky, alpha or eye isolation failed");
			}
		}
		EyeTransform translated;
		Require(BuildEyeTransform(gpu.projection, Matrix::Identity, Matrix::Identity, Matrix::CreateTranslation(-0.025f, 0, 0), 100, translated), "Translation setup failed");
		gpu.depths.fill(gpu.RawDepth(10));
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const auto output = gpu.Run(eye, translated);
			for (uint32_t y = 0; y < Height; ++y)
				for (uint32_t x = 2; x < Width; ++x)
					Require((output[y * Width + x] - gpu.colors[y * Width * 2 + eye * Width + x - 2]).Length() < 0.001f, "Depth-aware translation failed");
			for (const auto& pixel : output)
				Require(pixel.z == static_cast<float>(eye + 1) && pixel.w == 0.75f, "Hole fill crossed the stereo seam");
		}
		gpu.depths.fill(gpu.RawDepth(20));
		for (uint32_t y = 0; y < Height; ++y)
			gpu.depths[y * Width * 2 + 4] = gpu.RawDepth(10);
		const auto occlusion = gpu.Run(0, translated);
		for (uint32_t y = 0; y < Height; ++y)
			Require(occlusion[y * Width + 6].x == 4, "Far surface overwrote nearer geometry");
		auto compressed = identity;
		compressed.sourceViewToTargetView = Matrix::CreateScale(0.5f, 1, 1);
		gpu.depths.fill(gpu.RawDepth(10));
		const auto reference = gpu.Run(0, compressed);
		for (uint32_t repeat = 0; repeat < 4; ++repeat) {
			const auto output = gpu.Run(0, compressed);
			for (uint32_t pixel = 0; pixel < Width * Height; ++pixel)
				Require(output[pixel] == reference[pixel], "Equal-depth ownership is nondeterministic");
		}
		gpu.depths.fill(1);
		const auto sky = gpu.Run(0, translated);
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x)
				Require(sky[y * Width + x] == gpu.colors[y * Width * 2 + x], "Sky incorrectly follows head translation");
		EyeTransform rotated;
		Require(BuildEyeTransform(gpu.projection, Matrix::Identity, Matrix::Identity, Matrix::CreateRotationY(0.1f), 100, rotated), "Sky rotation setup failed");
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const auto output = gpu.Run(eye, rotated);
			Require(output[4 * Width + 8] == gpu.colors[4 * Width * 2 + eye * Width + 7], "Sky rotation has the wrong direction or eye");
		}
		gpu.depths.fill(1);
		for (uint32_t eye = 0; eye < 2; ++eye)
			for (uint32_t y = 0; y < Height; ++y)
				for (uint32_t x : { 4u, 7u })
					gpu.depths[y * Width * 2 + eye * Width + x] = gpu.RawDepth(10);
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const auto output = gpu.Run(eye, translated, { 1.0f / Width, 0 });
			Require(output[4 * Width + 6].x == 3, "Hole fill used an unjittered depth lookup");
			for (const auto& pixel : output)
				Require(pixel.z == static_cast<float>(eye + 1), "Jittered depth lookup crossed eyes");
		}
		gpu.projection = Matrix(DirectX::XMMatrixPerspectiveFovRH(DirectX::XM_PIDIV2, 1, 1, 100));
		Require(BuildEyeTransform(gpu.projection, Matrix::Identity, Matrix::Identity, Matrix::Identity, 100, identity), "Right-handed setup failed");
		gpu.depths.fill(gpu.RawDepth(-10));
		const auto rightHanded = gpu.Run(0, identity);
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x)
				Require(rightHanded[y * Width + x] == gpu.colors[y * Width * 2 + x], "Right-handed geometry failed");
		gpu.projection = Matrix(DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1, 100, 1));
		Require(BuildEyeTransform(gpu.projection, Matrix::Identity, Matrix::Identity, Matrix::Identity, 100, identity), "Reversed-depth setup failed");
		for (float raw : { gpu.RawDepth(10), 0.0f }) {
			gpu.depths.fill(raw);
			const auto output = gpu.Run(1, identity);
			for (uint32_t y = 0; y < Height; ++y)
				for (uint32_t x = 0; x < Width; ++x)
					Require(output[y * Width + x] == gpu.colors[y * Width * 2 + Width + x], "Reversed-depth geometry or sky failed");
		}
	}
}

int main()
{
	try {
		Verify();
		std::cout << "View Refresh SM5 shaders: stereo identity, sky, invalid depth, translation, near-surface ownership, deterministic ties, HDR/alpha and debug-layer checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
