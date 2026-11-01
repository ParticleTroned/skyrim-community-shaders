#define NOMINMAX
#include "Features/Upscaling/TextureSourceDescriptions.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = winrt::com_ptr<T>;
	constexpr UINT eyeWidth = 13, sourceWidth = 2 * eyeWidth, sourceHeight = 11;
	constexpr UINT targetWidth = sourceWidth + 3, targetHeight = 13;
	using Pixel = std::array<float, 4>;

	struct EncodeInputs
	{
		std::array<uint32_t, sourceWidth * sourceHeight> depth{};
		std::array<std::array<Pixel, sourceWidth * sourceHeight>, 3> guides{};
		bool reversed;

		explicit EncodeInputs(bool reverse) : reversed(reverse)
		{
			const auto set = [&](UINT x, UINT y, float standardDepth, float mx, float my) {
				const auto index = y * sourceWidth + x;
				const auto packed = static_cast<uint32_t>(standardDepth * 0xffffffu);
				depth[index] = (reverse ? 0xffffffu - packed : packed) | ((index * 7u & 0xffu) << 24);
				guides[0][index] = { float(x % 7) * .1f, float(y % 3) * .1f, 0, 0 };
				guides[1][index] = { 0, 0, .2f + float(x % 3) * .1f, 0 };
				guides[2][index] = { mx, my, 0, 0 };
			};
			for (UINT y = 0; y < sourceHeight; ++y)
				for (UINT x = 0; x < sourceWidth; ++x)
					set(x, y, .75f, 1, 0);
			for (UINT eye = 0; eye < 2; ++eye) {
				const UINT base = eye * eyeWidth;
				set(base + 1, 0, .5f, 3, 4);
				set(base + 2, 0, .25f, 0, -5);  // Equal length must retain the earlier donor.
				set(base + 1, 3, .125f, 2, 0);
				set(base + 2, 3, .5f, 0, 4);  // Longer motion wins over nearer depth.
				set(base + 7, 3, .5f, -5, 0);
				set(base + 4, 4, .25f, 0, 5);  // Y-first traversal wins despite the smaller X.
				set(base + 7, 8, .75f, 0, -5);
				set(base + 5, 6, .5f, 3, 4);  // A donor tie must retain the centre vector.
				set(base + 9, 1, .75f, 80, 0);  // Equal depth is not a donor.
				set(base + 9, 4, .875f, 90, 0);  // Farther depth is not a donor.
			}
			set(eyeWidth - 1, 2, .125f, 50, 0);
			set(eyeWidth, 8, .125f, 100, 0);  // Opposite-eye motion must remain excluded.
			set(0, 0, 0, 1, 0);
			set(sourceWidth - 1, sourceHeight - 1, 1, 1, 0);
		}

		float Depth(UINT x, UINT y) const { return float(depth[y * sourceWidth + x] & 0xffffffu) / float(0xffffffu); }

		std::array<float, 5> Reference(UINT x, UINT y, UINT minX, UINT maxX, bool dlss, const Pixel& camera, bool hardening) const
		{
			const auto index = y * sourceWidth + x;
			const auto& original = guides[2][index];
			std::array<float, 2> motion{ original[0], original[1] };
			const float centerDepth = Depth(x, y);
			const auto inBounds = [&](int sx, int sy) {
				return sx >= int(minX) && sx < int(maxX) && sy >= 0 && sy < int(sourceHeight);
			};
			if (dlss) {
				float longest = motion[0] * motion[0] + motion[1] * motion[1];
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx) {
						const int sx = int(x) + dx, sy = int(y) + dy;
						if (!inBounds(sx, sy))
							continue;
						const auto donorDepth = Depth(sx, sy);
						if (!(reversed ? donorDepth > centerDepth : donorDepth < centerDepth))
							continue;
						const auto& donor = guides[2][sy * sourceWidth + sx];
						const float length = donor[0] * donor[0] + donor[1] * donor[1];
						if (length > longest) {
							longest = length;
							motion = { donor[0], donor[1] };
						}
					}
				const float linearDepth = camera[3] / (reversed ? camera[1] + centerDepth * camera[2] : camera[0] - centerDepth * camera[2]);
				const float t = std::clamp(1.f - linearDepth / 10240.f, 0.f, 1.f);
				const float nearFactor = t * t * (3.f - 2.f * t);
				for (unsigned component = 0; component < 2; ++component)
					motion[component] += (original[component] - motion[component]) * nearFactor;
			}
			float reactive = guides[0][index][0] * .1f + guides[0][index][1];
			float transparency = guides[1][index][2];
			if (hardening) {
				const float seam = std::clamp((2.f - std::abs(float(x) + .5f - eyeWidth)) / 2.f, 0.f, 1.f);
				const auto masked = [&](float value) { return reversed ? value >= 1.f - .001f : value <= .001f; };
				bool edge = false;
				for (const auto offset : { std::array{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
					const int sx = int(x) + offset[0], sy = int(y) + offset[1];
					if (inBounds(sx, sy))
						edge |= masked(Depth(sx, sy)) != masked(centerDepth);
				}
				const float scale = edge ? 0.f : 1.f - .75f * seam;
				motion[0] *= scale;
				motion[1] *= scale;
				const float mask = edge ? 1.f : seam * .6f;
				reactive = std::max(reactive, mask);
				transparency = std::max(transparency, mask);
			}
			return { reactive, transparency, motion[0], motion[1], centerDepth };
		}
	};

	struct Texture2D
	{
		D3D11_TEXTURE2D_DESC desc{};
		ComPtr<ID3D11Texture2D> resource;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};

#include "depth_encode_scope_exit.h"

#include "depth_encode_identity.h"
#include "depth_encode_texture_desc.h"
#include "depth_encode_validation.h"

	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct ShaderIncludes : ID3DInclude
	{
		Util::CustomInclude package{ "package/Shaders" };
		Util::CustomInclude feature{ "features/Upscaling/Shaders" };
		HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
		{
			return std::string_view(name).starts_with("Upscaling/") ?
			           feature.Open(type, name, parent, data, size) :
			           package.Open(type, name, parent, data, size);
		}
		HRESULT Close(LPCVOID data) override { return package.Close(data); }
	};

	Texture2D MakeTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
		UINT bindFlags, const char* name, const D3D11_SUBRESOURCE_DATA* initial = nullptr)
	{
		Texture2D result;
		auto& desc = result.desc;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = format;
		desc.BindFlags = bindFlags;
		Check(device->CreateTexture2D(&desc, initial, result.resource.put()));
		Util::SetResourceName(result.resource.get(), "DepthEncodeTest::%s", name);
		if (bindFlags & D3D11_BIND_SHADER_RESOURCE) {
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			Check(device->CreateShaderResourceView(result.resource.get(), &view, result.srv.put()));
			Util::SetResourceName(result.srv.get(), "DepthEncodeTest::%s SRV", name);
		}
		if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
			Check(device->CreateUnorderedAccessView(result.resource.get(), nullptr, result.uav.put()));
			Util::SetResourceName(result.uav.get(), "DepthEncodeTest::%s UAV", name);
		}
		return result;
	}

	ComPtr<ID3DBlob> Compile(const char* method, bool vr, bool depthOutput, bool reversed = false)
	{
		std::vector<D3D_SHADER_MACRO> defines{ { method, "1" }, { "COMPUTESHADER", "1" }, { "DX11", "1" }, { "WINPC", "1" } };
		if (vr)
			defines.push_back({ "VR", "1" });
		if (depthOutput)
			defines.push_back({ "DEPTH_OUTPUT", "1" });
		if (reversed)
			defines.push_back({ "REVERSE_Z", "1" });
		defines.push_back({ nullptr, nullptr });
		ShaderIncludes includes;
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"features/Upscaling/Shaders/Upscaling/EncodeTexturesCS.hlsl",
			defines.data(), &includes, "main", "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, code.put(), errors.put());
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	void CheckGuards(const Texture2D& source, Texture2D& output)
	{
		Require(IsVRDepthEncodeSourceValid(source.srv.get(), source.resource.get(), sourceWidth, sourceHeight), "Valid engine depth rejected");
		Require(!IsVRDepthEncodeSourceValid(source.srv.get(), output.resource.get(), sourceWidth, sourceHeight), "Wrong source identity accepted");
		Require(!IsVRDepthEncodeSourceValid(nullptr, source.resource.get(), 1, 1), "Missing SRV accepted");
		Require(!IsVRDepthEncodeSourceValid(source.srv.get(), nullptr, 1, 1), "Missing resource accepted");
		for (auto size : { std::array<UINT, 2>{ 0, 1 }, { 1, 0 }, { sourceWidth + 1, 1 }, { 1, sourceHeight + 1 }, { std::numeric_limits<UINT>::max(), std::numeric_limits<UINT>::max() } })
			Require(!IsVRDepthEncodeSourceValid(source.srv.get(), source.resource.get(), size[0], size[1]), "Invalid depth bounds accepted");
		Require(IsVRDepthEncodeTargetValid(&output, targetWidth, targetHeight), "Valid typed target rejected");
		Require(!IsVRDepthEncodeTargetValid(&output, targetWidth + 1, targetHeight), "Small target accepted");
		Require(!IsVRDepthEncodeTargetValid(&output, targetWidth, targetHeight + 1), "Short target accepted");
		Require(!IsVRDepthEncodeTargetValid(&output, 0, 1), "Empty target region accepted");
		Require(!IsVRDepthEncodeTargetValid(nullptr, 1, 1), "Missing target accepted");
		const auto desc = output.desc;
		output.desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Depth-stencil copy target accepted");
		output.desc = desc;
		output.desc.ArraySize = 2;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Array target accepted");
		output.desc = desc;
		output.desc.SampleDesc.Count = 2;
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Multisampled target accepted");
		output.desc = desc;
		auto savedUAV = std::move(output.uav);
		Require(!IsVRDepthEncodeTargetValid(&output, 1, 1), "Missing UAV accepted");
		output.uav = std::move(savedUAV);
	}

	void VerifyShader(ID3D11Device* device, ID3D11DeviceContext* context, const char* method, bool vr, bool reversed)
	{
		const bool dlss = std::string_view(method) == "DLSS";
		const EncodeInputs fixture(reversed);
		const Pixel farCamera{ 1, 1, 0, 10240 };
		const auto outsideCrop = fixture.Reference(3, 2, 0, eyeWidth, true, farCamera, false);
		const auto yFirst = fixture.Reference(6, 5, 0, eyeWidth, true, farCamera, false);
		const auto centerTie = fixture.Reference(7, 8, 0, eyeWidth, true, farCamera, false);
		Require(outsideCrop[2] == 3 && outsideCrop[3] == 4 && yFirst[2] == -5 && yFirst[3] == 0 &&
					centerTie[2] == 0 && centerTie[3] == -5,
			"Donor fixture must distinguish full-eye crop halos, Y-first ties and centre-vector ties");
		const D3D11_SUBRESOURCE_DATA initial{ fixture.depth.data(), sourceWidth * sizeof(uint32_t), 0 };
		auto source = MakeTexture(device, sourceWidth, sourceHeight, DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, "NativeDepth", &initial);
		std::array<Texture2D, 3> inputs;
		std::array<Texture2D, 4> outputs;
		for (UINT i = 0; i < inputs.size(); ++i) {
			const D3D11_SUBRESOURCE_DATA guideInitial{ fixture.guides[i].data(), sourceWidth * sizeof(Pixel), 0 };
			inputs[i] = MakeTexture(device, sourceWidth, sourceHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, "GuideInput", &guideInitial);
		}
		for (UINT i = 0; i < outputs.size(); ++i)
			outputs[i] = MakeTexture(device, targetWidth, targetHeight, i == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, "GuideOutput");
		CheckGuards(source, outputs[3]);

		auto code = Compile(method, vr, true, reversed);
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.put()));
		Util::SetResourceName(shader.get(), "DepthEncodeTest::%s CS", method);
		Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Check(reflection->GetResourceBindingDescByName("DepthOutput", &binding));
		Require(binding.BindPoint == 3, "Depth output must use the existing u3 slot");
		ConstantBuffer constants(device, reflection.get(), "UpscalingData");
		Require(constants.bytes.size() == 64 && constants.Offset("SourceSamplingXBounds") == 56,
			"Encoder constant-buffer layout changed");
		std::vector<std::unique_ptr<ConstantBuffer>> sharedBuffers;
		ConstantBuffer* cameraBuffer = nullptr;
		D3D11_SHADER_DESC reflected{};
		Check(reflection->GetDesc(&reflected));
		for (UINT i = 0; i < reflected.ConstantBuffers; ++i) {
			D3D11_SHADER_BUFFER_DESC buffer{};
			Check(reflection->GetConstantBufferByIndex(i)->GetDesc(&buffer));
			if (buffer.Type == D3D_CT_CBUFFER && std::string_view(buffer.Name) != "UpscalingData") {
				sharedBuffers.push_back(std::make_unique<ConstantBuffer>(device, reflection.get(), buffer.Name));
				if (std::string_view(buffer.Name) == "SharedData::SharedData")
					cameraBuffer = sharedBuffers.back().get();
			}
		}
		Require(!dlss || cameraBuffer, "DLSS depth-dependent blending lost its camera constants");
		std::array<ComPtr<ID3D11Texture2D>, 4> staging;
		for (unsigned output = 0; output < outputs.size(); ++output) {
			D3D11_TEXTURE2D_DESC stagingDesc = outputs[output].desc;
			stagingDesc.BindFlags = 0;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&stagingDesc, nullptr, staging[output].put()));
			Util::SetResourceName(staging[output].get(), "DepthEncodeTest::Readback%u", output);
		}

		constexpr std::array cameras{ Pixel{ 1, 1, 0, 0 }, Pixel{ 1, 1, 0, 5120 },
			Pixel{ 1, 1, 0, 10240 }, Pixel{ 2, 1, 1, 8192 } };
		const auto verifyCase = [&](UINT eye, UINT mode, bool hardening, UINT cameraIndex) {
			context->ClearState();
			auto restoreState = ScopeExit([&] { context->ClearState(); });
			const UINT fullWidth = vr ? eyeWidth : sourceWidth;
			const UINT x = mode == 3 ? fullWidth - 1 : (mode == 4 ? 4u : (mode ? 3u : 0u));
			const UINT y = mode == 3 ? sourceHeight - 1 : (mode == 4 ? 1u : (mode ? 2u : 0u));
			const UINT width = mode >= 3 ? 1u : (mode ? 7u : fullWidth);
			const UINT height = mode == 3 ? 1u : (mode == 4 ? 9u : (mode ? 5u : sourceHeight));
			const UINT outX = mode == 1 ? x : 0u, outY = mode == 1 ? y : 0u;
			constants.SetVariable("DispatchDim", std::array<float, 2>{ float(width), float(height) });
			constants.SetVariable("TrueSamplingDim", std::array<float, 2>{ float(sourceWidth), float(sourceHeight) });
			constants.SetVariable("InvTrueSamplingDim", std::array<float, 2>{ 1.0f / sourceWidth, 1.0f / sourceHeight });
			constants.SetVariable("SourceOffset", std::array<float, 2>{ float(eye * eyeWidth + x), float(y) });
			constants.SetVariable("OutputOffset", std::array<float, 2>{ float(outX), float(outY) });
			constants.SetVariable("SourceSamplingXBounds", std::array<float, 2>{ float(eye * eyeWidth), float(eye * eyeWidth + fullWidth) });
			constants.SetVariable("SeamCenterX", float(eyeWidth));
			constants.SetVariable("SeamHalfWidthPx", 2.f);
			constants.SetVariable("MaskDepthThreshold", .001f);
			constants.SetVariable("VRSeamHardening", hardening ? 1.f : 0.f);
			constants.Bind(context);
			const auto& camera = cameras[cameraIndex];
			if (cameraBuffer)
				cameraBuffer->SetVariable("SharedData::CameraData", camera);
			for (auto& buffer : sharedBuffers)
				buffer->Bind(context);
			const float sentinel[4]{ -1, -1, -1, -1 };
			for (auto& output : outputs)
				context->ClearUnorderedAccessViewFloat(output.uav.get(), sentinel);
			ID3D11ShaderResourceView* srvs[]{ inputs[0].srv.get(), inputs[1].srv.get(), inputs[2].srv.get(), source.srv.get() };
			ID3D11UnorderedAccessView* uavs[]{ outputs[0].uav.get(), outputs[1].uav.get(), outputs[2].uav.get(), outputs[3].uav.get() };
			context->CSSetShaderResources(0, 4, srvs);
			context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
			context->CSSetShader(shader.get(), nullptr, 0);
			context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
			context->ClearState();
			for (unsigned output = 0; output < outputs.size(); ++output) {
				context->CopyResource(staging[output].get(), outputs[output].resource.get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				Check(context->Map(staging[output].get(), 0, D3D11_MAP_READ, 0, &mapped));
				auto unmap = ScopeExit([&] { context->Unmap(staging[output].get(), 0); });
				for (UINT row = 0; row < targetHeight; ++row) {
					const auto* values = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch);
					for (UINT col = 0; col < targetWidth; ++col) {
						std::array<float, 5> expected{ -1, -1, -1, -1, -1 };
						if (col >= outX && col < outX + width && row >= outY && row < outY + height)
							expected = fixture.Reference(eye * eyeWidth + x + col - outX, y + row - outY,
								eye * eyeWidth, eye * eyeWidth + fullWidth, dlss, camera, hardening);
						const unsigned components = output == 2 ? 2 : 1;
						for (unsigned component = 0; component < components; ++component) {
							const float value = values[col * components + component];
							const float reference = expected[(output == 3 ? 4 : output) + component];
							Require(std::isfinite(value) && std::abs(value - reference) < (output == 3 ? 2e-7f : 2e-4f),
								"Encoder changed donor order, full-eye bounds, blending, hardening or output ownership");
						}
					}
				}
			}
		};
		for (UINT eye = 0; eye < (vr ? 2u : 1u); ++eye)
			for (UINT mode : { 0u, 1u, 2u, 3u, 4u })
				for (const bool hardening : { false, true })
					for (UINT cameraIndex = 0; cameraIndex < (dlss ? cameras.size() : 1u); ++cameraIndex)
						verifyCase(eye, mode, hardening, cameraIndex);
	}
}

#include "depth_encode_source_descriptions.h"

void VerifySourceDescriptions(ID3D11Device* device)
{
	auto source = MakeTexture(device, 8, 4, DXGI_FORMAT_R32_FLOAT, 0, "DescriptionSource");
	auto replacement = MakeTexture(device, 8, 4, DXGI_FORMAT_R32_FLOAT, 0, "DescriptionReplacement");
	auto* original = source.resource.get();
	UpscalingTextures::TextureSourceDescription description;
	Require(description.Capture(original) && description.Matches(original), "Source capture failed");
	Require(!description.Matches(replacement.resource.get()), "Equal dimensions allowed a different source identity");
	source.resource = nullptr;
	D3D11_TEXTURE2D_DESC retained{};
	Require(TryGetTexture2DDesc(original, retained) && retained.Width == 8 && retained.Height == 4,
		"Description did not retain source ownership after the caller released it");
	Require(description.Capture(original) && description.Desc().Format == DXGI_FORMAT_R32_FLOAT,
		"Retained source capture did not preserve immutable geometry");
	Require(description.Capture(replacement.resource.get()) && description.Matches(replacement.resource.get()),
		"Description did not refresh for a replacement source");
	Require(!description.Capture(nullptr) && !description.Matches(replacement.resource.get()) && description.Desc().Width == 0,
		"Failed capture retained a reusable description");

	UpscalingTextures::VRInputSourceDescriptions inputs;
	auto* texture = replacement.resource.get();
	Require(inputs.Capture(texture, texture, texture, texture) && inputs.Matches(texture, texture, texture, texture),
		"VR source group capture failed");
	Require(!inputs.Capture(texture, texture, nullptr, texture) && !inputs.color.Matches(texture),
		"Partial VR capture retained an earlier operation's source ownership");
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		VerifySourceDescriptions(device.get());
		for (const bool vr : { false, true })
			for (const bool reversed : { false, true }) {
				VerifyShader(device.get(), context.get(), "DLSS", vr, reversed);
				VerifyShader(device.get(), context.get(), "FSR", vr, reversed);
			}
		auto flatDLSS = Compile("DLSS", false, false);
		ComPtr<ID3D11ShaderReflection> reflection;
		Check(D3DReflect(flatDLSS->GetBufferPointer(), flatDLSS->GetBufferSize(), IID_PPV_ARGS(reflection.put())));
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		Require(FAILED(reflection->GetResourceBindingDescByName("DepthOutput", &binding)), "Flat DLSS gained a depth UAV requirement");
		std::cout << "DLSS/FSR depth, motion donors, ties, crop halos, partial groups, hardening and output guards passed (flat/VR, standard/reversed depth).\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
