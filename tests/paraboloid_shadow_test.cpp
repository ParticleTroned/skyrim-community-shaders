#define NOMINMAX
#include "Utils/ParaboloidShadow.h"
#include "Utils/ShaderInclude.h"
#include "d3d11_shader_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <d3dcompiler.h>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <string>

namespace
{
	using D3D11ShaderTest::Check;
	using D3D11ShaderTest::ConstantBuffer;
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Mode = ParaboloidShadow::Mode;
	using Vec = std::array<float, 3>;
	using Matrix = std::array<float, 16>;
	constexpr Matrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	struct Vertex
	{
		std::array<float, 4> position;
		std::array<float, 2> uv;
		std::array<float, 4> color;
	};
	struct Output
	{
		std::array<float, 4> position, texture;
		std::array<float, 4> depth;
		Vec light;
		float alpha, clip, cull;
		UINT eye;
	};
	void Require(bool value, const std::source_location location = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error("Paraboloid shadow regression at line " + std::to_string(location.line()));
	}
	void Near(float actual, float expected, const std::source_location location = std::source_location::current())
	{
		if (!std::isfinite(actual) || std::abs(actual - expected) > 3e-5f * std::max(1.0f, std::abs(expected)))
			throw std::runtime_error("Paraboloid shadow mismatch at line " + std::to_string(location.line()) +
									 " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
	}
	Vec Mix(Vec a, Vec b, float t)
	{
		for (size_t i = 0; i < a.size(); ++i)
			a[i] = std::lerp(a[i], b[i], t);
		return a;
	}
	// Reference triangle lists preserve all four subtriangles, independently of strip ordering.
	void Subdivide(std::vector<Vec>& result, Vec a, Vec b, Vec c)
	{
		const auto ab = Mix(a, b, .5f), ac = Mix(a, c, .5f), bc = Mix(b, c, .5f);
		for (auto vertex : { a, ab, ac, ab, b, bc, ac, bc, c, ab, bc, ac })
			result.push_back(vertex);
	}
	std::vector<Vec> Reference(const std::array<Vec, 3>& vertices, float hemisphere, Mode mode)
	{
		std::vector<Vec> polygon;
		for (size_t i = 0; i < 3; ++i) {
			const auto a = vertices[i], b = vertices[(i + 1) % 3];
			const bool front = a[2] * hemisphere >= 0;
			if (front)
				polygon.push_back(a);
			if (front != (b[2] * hemisphere >= 0)) {
				auto intersection = Mix(a, b, a[2] / (a[2] - b[2]));
				intersection[2] = 0;
				polygon.push_back(intersection);
			}
		}
		// The quad diagonal starts at the edge entering the retained hemisphere.
		if (polygon.size() == 4 && vertices[0][2] * hemisphere >= 0) {
			const auto enter = std::find_if(polygon.begin(), polygon.end(), [](Vec p) { return p[2] == 0; });
			const auto after = (size_t(enter - polygon.begin()) + 1) % 4;
			const auto start = polygon[after][2] * hemisphere > 0 ? size_t(enter - polygon.begin()) : after;
			std::rotate(polygon.begin(), polygon.begin() + start, polygon.end());
		}
		std::vector<Vec> result;
		for (size_t i = 1; i + 1 < polygon.size(); ++i) {
			if (mode == Mode::Full)
				Subdivide(result, polygon[0], polygon[i], polygon[i + 1]);
			else
				result.insert(result.end(), { polygon[0], polygon[i], polygon[i + 1] });
		}
		return result;
	}
	ComPtr<ID3DBlob> Compile(bool vr, const char* stage, const std::vector<D3D_SHADER_MACRO>& extra = {})
	{
		std::vector<D3D_SHADER_MACRO> defines{
			{ "RENDER_SHADOWMAP", "1" }, { "RENDER_SHADOWMAP_PB", "1" },
			{ "TEXTURE", "1" }, { "ALPHA_TEST", "1" }, { "VC", "1" }, { stage, "1" }
		};
		if (vr)
			defines.push_back({ "VR", "1" });
		defines.insert(defines.end(), extra.begin(), extra.end());
		defines.push_back({ nullptr, nullptr });
		Util::CustomInclude includes{ "package/Shaders" };
		ComPtr<ID3DBlob> code, errors;
		const auto result = D3DCompileFromFile(L"package/Shaders/Utility.hlsl", defines.data(), &includes, "main",
			std::strcmp(stage, "VSHADER") == 0 ? "vs_5_0" : "gs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(result);
		return code;
	}

	struct Fixture
	{
		ID3D11DeviceContext* context;
		bool vr;
		Mode mode;
		bool offsetDepth, reverseZ;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11GeometryShader> geometry;
		ComPtr<ID3D11InputLayout> layout;
		ComPtr<ID3D11ShaderReflection> reflection;
		ComPtr<ID3D11Buffer> input, output, readback;
		ComPtr<ID3D11Query> query;
		std::vector<std::unique_ptr<ConstantBuffer>> constants;

		Fixture(ID3D11Device* device, ID3D11DeviceContext* context, bool vr, Mode mode = Mode::Full, bool offsetDepth = false, bool reverseZ = false) : context(context), vr(vr), mode(mode), offsetDepth(offsetDepth), reverseZ(reverseZ)
		{
			std::vector<D3D_SHADER_MACRO> extra;
			if (offsetDepth)
				extra.push_back({ "OFFSET_DEPTH", "1" });
			if (reverseZ)
				extra.push_back({ "REVERSE_Z", "1" });
			const auto vs = Compile(vr, "VSHADER", extra);
			if (mode == Mode::ClipOnly)
				extra.push_back({ "PARABOLOID_CLIP_ONLY", "1" });
			const auto gs = Compile(vr, "GEOMETRYSHADER", extra);
			ComPtr<ID3D11ShaderReflection> gsReflection;
			Check(D3DReflect(gs->GetBufferPointer(), gs->GetBufferSize(), IID_PPV_ARGS(&gsReflection)));
			D3D11_SHADER_DESC gsDesc{};
			Check(gsReflection->GetDesc(&gsDesc));
			Require(gsDesc.GSMaxOutputVertexCount == (mode == Mode::ClipOnly ? 4u : 12u));
			Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "ParaboloidTest::VS");
			std::vector<D3D11_SO_DECLARATION_ENTRY> elements{
				{ 0, "SV_POSITION", 0, 0, 4, 0 }, { 0, "TEXCOORD", 0, 0, 4, 0 },
				{ 0, "TEXCOORD", 2, 0, 4, 0 }, { 0, "TEXCOORD", 6, 0, 3, 0 }, { 0, "TEXCOORD", 4, 0, 1, 0 }
			};
			if (vr) {
				elements.push_back({ 0, "SV_ClipDistance", 0, 0, 1, 0 });
				elements.push_back({ 0, "SV_CullDistance", 0, 0, 1, 0 });
				elements.push_back({ 0, "EYEIDX", 0, 0, 1, 0 });
			}
			const UINT stride = sizeof(Output);
			Check(device->CreateGeometryShaderWithStreamOutput(gs->GetBufferPointer(), gs->GetBufferSize(), elements.data(),
				UINT(elements.size()), &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &geometry));
			Util::SetResourceName(geometry.Get(), "ParaboloidTest::GS");
			Check(ParaboloidShadow::Attach(*vertex.Get(), geometry.Get(), mode));
			const D3D11_INPUT_ELEMENT_DESC attributes[]{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
			};
			Check(device->CreateInputLayout(attributes, UINT(std::size(attributes)), vs->GetBufferPointer(), vs->GetBufferSize(), &layout));
			Util::SetResourceName(layout.Get(), "ParaboloidTest::InputLayout");
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(Vertex) * 3;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			Check(device->CreateBuffer(&desc, nullptr, &input));
			Util::SetResourceName(input.Get(), "ParaboloidTest::Input");
			desc.ByteWidth = sizeof(Output) * 48;
			desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
			Check(device->CreateBuffer(&desc, nullptr, &output));
			Util::SetResourceName(output.Get(), "ParaboloidTest::Output");
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&desc, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "ParaboloidTest::Readback");
			const D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_SO_STATISTICS, 0 };
			Check(device->CreateQuery(&queryDesc, &query));
			Util::SetResourceName(query.Get(), "ParaboloidTest::Statistics");
			Check(D3DReflect(vs->GetBufferPointer(), vs->GetBufferSize(), IID_PPV_ARGS(&reflection)));
			for (const auto name : { "PerTechnique", "PerMaterial", "PerGeometry", "FrameBuffer::PerFrame" })
				constants.push_back(std::make_unique<ConstantBuffer>(device, reflection.Get(), name));
			constants[1]->SetVariable("TexcoordOffset", std::array<float, 4>{ 0, 0, 1, 1 });
			Matrix projection = identity;
			projection[10] = reverseZ ? -1.f : 1.f;
			projection[14] = 1.f;
			if (vr) {
				constants[3]->SetVariable("FrameBuffer::CameraProj", std::array{ projection, projection });
				constants[2]->SetVariable("World", std::array{ identity, identity });
				constants[3]->SetVariable("FrameBuffer::CameraViewProj", std::array{ identity, identity });
				constants.push_back(std::make_unique<ConstantBuffer>(device, reflection.Get(), "VRValues"));
				constants[4]->SetVariable("EyeOffsetScale", std::array<float, 2>{ -.5f, .5f });
				constants[4]->SetVariable("EyeClipEdge", std::array<float, 8>{ 1, 0, 0, 1, -1, 0, 0, 1 });
			} else {
				constants[3]->SetVariable("FrameBuffer::CameraProj", projection);
				constants[2]->SetVariable("World", identity);
				constants[3]->SetVariable("FrameBuffer::CameraViewProj", identity);
			}
		}

		void Verify(const std::array<Vec, 3>& positions, float hemisphere, bool stereo, UINT eye, float inverseRadius = .1f)
		{
			context->ClearState();
			constants[0]->SetVariable("ParabolaParam", std::array<float, 2>{ inverseRadius, hemisphere });
			if (vr)
				constants[4]->SetVariable("StereoEnabled", stereo ? 1.0f : 0.0f);
			for (const auto& constant : constants) {
				context->UpdateSubresource(constant->buffer.Get(), 0, nullptr, constant->bytes.data(), 0, 0);
				auto* raw = constant->buffer.Get();
				context->VSSetConstantBuffers(constant->slot, 1, &raw);
			}
			std::array<Vertex, 3> vertices{};
			for (size_t i = 0; i < vertices.size(); ++i) {
				const auto& p = positions[i];
				vertices[i] = { { p[0], p[1], p[2], 1 }, { p[0], p[1] }, { 0, 0, 0, .5f + p[0] * .1f } };
			}
			context->UpdateSubresource(input.Get(), 0, nullptr, vertices.data(), 0, 0);
			const UINT stride = sizeof(Vertex), offset = 0;
			auto* raw = input.Get();
			context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
			context->IASetInputLayout(layout.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			raw = output.Get();
			context->SOSetTargets(1, &raw, &offset);
			{
				const ParaboloidShadow::Scope scope(context, mode);
				ParaboloidShadow::BindVertexShader(context, vertex.Get());
				ParaboloidShadow::Scope::SyncConstants(false);
				context->Begin(query.Get());
				// SV_InstanceID starts at zero; the right eye requires a second instance.
				context->DrawInstanced(3, eye + 1, 0, 0);
				context->End(query.Get());
			}
			context->SOSetTargets(0, nullptr, nullptr);
			context->CopyResource(readback.Get(), output.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<Output, 48> outputVertices;
			std::memcpy(outputVertices.data(), mapped.pData, sizeof(outputVertices));
			context->Unmap(readback.Get(), 0);
			D3D11_QUERY_DATA_SO_STATISTICS statistics{};
			Require(context->GetData(query.Get(), &statistics, sizeof(statistics), 0) == S_OK);
			auto expected = Reference(positions, hemisphere, mode);
			Require(statistics.NumPrimitivesWritten * 3 == expected.size() * (eye + 1) && statistics.PrimitivesStorageNeeded == statistics.NumPrimitivesWritten);
			const auto result = std::span(outputVertices).subspan(expected.size() * eye, expected.size());
			for (size_t i = 0; i < expected.size(); ++i) {
				const auto& out = result[i];
				const auto& p = out.light;
				Require(p[2] * hemisphere >= -1e-6f);
				const float radius = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
				const float denominator = p[2] + hemisphere * radius;
				float x = denominator != 0 ? p[0] / denominator : 0;
				const float clip = stereo ? 1 + (eye == 0 ? x : -x) : 1;
				if (stereo)
					x = .5f * x + (eye == 0 ? -.5f : .5f);
				Near(out.position[0], x);
				Near(out.position[1], denominator != 0 ? p[1] / denominator : 0);
				Near(out.position[2], radius * inverseRadius + (offsetDepth ? (reverseZ ? -5.f : 5.f) : 0.f));
				Near(out.position[3], 1);
				Near(out.texture[0], p[0]);
				Near(out.texture[1], p[1]);
				Near(out.alpha, .5f + p[0] * .1f);
				Near(out.depth[0], radius * inverseRadius);
				Near(out.depth[1], 1);
				Near(out.depth[3], std::min(12.0f * inverseRadius, 12.0f / 256.0f));
				if (vr) {
					Near(out.clip, clip);
					Near(out.cull, clip);
					Require(out.eye == (stereo ? eye : 0));
				}
			}
			// Compare triangle multisets up to cyclic rotation, preserving winding.
			for (size_t i = 0; i < expected.size(); i += 3) {
				bool found = false;
				for (size_t j = i; j < expected.size() && !found; j += 3)
					for (size_t rotation = 0; rotation < 3 && !found; ++rotation) {
						bool equal = true;
						for (size_t corner = 0; corner < 3; ++corner)
							for (size_t axis = 0; axis < 3; ++axis)
								equal &= std::abs(result[i + corner].light[axis] - expected[j + (corner + rotation) % 3][axis]) < 1e-5f;
						if (equal) {
							for (size_t k = 0; k < 3; ++k)
								std::swap(expected[i + k], expected[j + k]);
							found = true;
						}
					}
				Require(found);
			}
		}
	};

	void CheckModeTransitions(ID3D11Device* device, ID3D11DeviceContext* ctx)
	{
		Fixture full(device, ctx, false, Mode::Full), clip(device, ctx, false, Mode::ClipOnly);
		const auto bound = [&] {
			ComPtr<ID3D11GeometryShader> shader;
			ctx->GSGetShader(&shader, nullptr, nullptr);
			return shader.Get();
		};
		const auto geometry = [&](Mode mode) { return mode == Mode::Full ? full.geometry.Get() : mode == Mode::ClipOnly ? clip.geometry.Get() :
			                                                                                                              nullptr; };
		for (unsigned present = 0; present < 4; ++present) {
			Check(ParaboloidShadow::Attach(*full.vertex.Get(), present & 1 ? full.geometry.Get() : nullptr, Mode::Full));
			Check(ParaboloidShadow::Attach(*full.vertex.Get(), present & 2 ? clip.geometry.Get() : nullptr, Mode::ClipOnly));
			const auto selected = [&](Mode mode) { return (mode == Mode::Full && (present & 1)) || (mode == Mode::ClipOnly && (present & 2)) ? geometry(mode) : nullptr; };
			for (const auto outer : { Mode::Full, Mode::ClipOnly, Mode::Off, Mode::Full }) {
				ctx->ClearState();
				{
					const ParaboloidShadow::Scope scope(ctx, outer);
					ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
					Require(bound() == selected(outer));
					for (const auto inner : { Mode::Full, Mode::ClipOnly, Mode::Off }) {
						{
							const ParaboloidShadow::Scope nested(ctx, inner);
							ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
							Require(bound() == selected(inner));
						}
						Require(bound() == selected(outer));
					}
					ParaboloidShadow::BindVertexShader(ctx, nullptr);
					Require(bound() == nullptr);
				}
				Require(bound() == nullptr);
				ctx->GSSetShader(clip.geometry.Get(), nullptr, 0);
				{
					const ParaboloidShadow::Scope foreign(ctx, outer);
					ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
					ParaboloidShadow::BindVertexShader(ctx, nullptr);
					ParaboloidShadow::Scope::SyncConstants(false);
					Require(bound() == clip.geometry.Get());
				}
				Require(bound() == clip.geometry.Get());
			}
		}
		ctx->ClearState();
		Check(ParaboloidShadow::Attach(*full.vertex.Get(), nullptr, Mode::ClipOnly));
		{
			const ParaboloidShadow::Scope scope(ctx, Mode::ClipOnly);
			ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
			Require(bound() == nullptr);
			// Worker augmentation becomes visible at the next scope, even for an unchanged VS.
			Check(ParaboloidShadow::Attach(*full.vertex.Get(), clip.geometry.Get(), Mode::ClipOnly));
			ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
			Require(bound() == nullptr);
		}
		{
			const ParaboloidShadow::Scope scope(ctx, Mode::ClipOnly);
			ParaboloidShadow::BindVertexShader(ctx, full.vertex.Get());
			Require(bound() == clip.geometry.Get());
		}
		const auto retained = ParaboloidShadow::Retain(full.vertex.Get(), Mode::ClipOnly);
		Require(!ParaboloidShadow::Retain(clip.vertex.Get(), Mode::Full));
		Require(!ParaboloidShadow::Retain(clip.vertex.Get(), Mode::Off));
		ctx->ClearState();
		full.vertex.Reset();
		clip.vertex.Reset();
		clip.geometry.Reset();
		Require(retained != nullptr);
		ComPtr<ID3D11Device> owner;
		retained->GetDevice(&owner);
		Require(owner.Get() == device);
	}

	void CheckOwnership(Fixture& f)
	{
		auto* ctx = f.context;
		const auto bound = [&] {
			ComPtr<ID3D11GeometryShader> shader;
			ctx->GSGetShader(&shader, nullptr, nullptr);
			return shader.Get();
		};
		ctx->ClearState();
		Require(ParaboloidShadow::Retain(f.vertex.Get(), f.mode).get() == f.geometry.Get());
		ID3D11Buffer* before = f.constants[1]->buffer.Get();
		ctx->GSSetConstantBuffers(0, 1, &before);
		{
			const ParaboloidShadow::Scope scope(ctx, f.mode);
			ParaboloidShadow::BindVertexShader(ctx, f.vertex.Get());
			Require(bound() == f.geometry.Get());
			ID3D11Buffer* active = f.constants[0]->buffer.Get();
			ctx->VSSetConstantBuffers(0, 1, &active);
			ParaboloidShadow::Scope::SyncConstants(false);
			ComPtr<ID3D11Buffer> copied;
			ctx->GSGetConstantBuffers(0, 1, &copied);
			Require(copied.Get() == active);
			{
				const ParaboloidShadow::Scope nested(ctx, Mode::Off);
				Require(bound() == nullptr);
				ParaboloidShadow::BindVertexShader(ctx, f.vertex.Get());
				Require(bound() == nullptr);
			}
			Require(bound() == f.geometry.Get());
			ParaboloidShadow::BindVertexShader(ctx, nullptr);
			Require(bound() == nullptr);
			ParaboloidShadow::BindVertexShader(ctx, f.vertex.Get());
			Require(bound() == f.geometry.Get());
		}
		Require(bound() == nullptr);
		ComPtr<ID3D11Buffer> restored;
		ctx->GSGetConstantBuffers(0, 1, &restored);
		Require(restored.Get() == before);
		ctx->GSSetShader(f.geometry.Get(), nullptr, 0);
		{
			const ParaboloidShadow::Scope foreign(ctx, f.mode);
			ParaboloidShadow::BindVertexShader(ctx, f.vertex.Get());
			ParaboloidShadow::BindVertexShader(ctx, nullptr);
			Require(bound() == f.geometry.Get());
		}
		Require(bound() == f.geometry.Get());
		ctx->ClearState();
		ComPtr<ID3D11DeviceContext1> ctx1;
		if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&ctx1)))) {
			ComPtr<ID3D11Device> device;
			ctx->GetDevice(&device);
			D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
			Check(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
			if (options.ConstantBufferOffsetting) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = 1024;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				ComPtr<ID3D11Buffer> buffer;
				Check(device->CreateBuffer(&desc, nullptr, &buffer));
				Util::SetResourceName(buffer.Get(), "ParaboloidTest::WindowedConstants");
				auto* raw = buffer.Get();
				for (const UINT slot : { 0u, 12u, 13u }) {
					const UINT beforeFirst = 32, activeFirst = 16, count = 16;
					ctx1->GSSetConstantBuffers1(slot, 1, &raw, &beforeFirst, &count);
					ctx1->VSSetConstantBuffers1(slot, 1, &raw, &activeFirst, &count);
					const auto verifyWindow = [&](UINT expectedFirst) {
						ComPtr<ID3D11Buffer> actual;
						UINT first, size;
						ctx1->GSGetConstantBuffers1(slot, 1, &actual, &first, &size);
						Require(actual == buffer && first == expectedFirst && size == count);
					};
					{
						const ParaboloidShadow::Scope windowed(ctx, f.mode);
						ParaboloidShadow::BindVertexShader(ctx, f.vertex.Get());
						ParaboloidShadow::Scope::SyncConstants(false);
						verifyWindow(activeFirst);
						{
							const ParaboloidShadow::Scope suspended(ctx, Mode::Off);
							ctx1->VSSetConstantBuffers1(slot, 1, &raw, &beforeFirst, &count);
							ParaboloidShadow::Scope::SyncConstants(false);
							verifyWindow(activeFirst);
						}
						ctx1->VSSetConstantBuffers1(slot, 1, &raw, &beforeFirst, &count);
						ParaboloidShadow::Scope::SyncConstants(false);
						verifyWindow(beforeFirst);
					}
					verifyWindow(beforeFirst);
					ctx->ClearState();
				}
			}
		}
	}
}

int main()
try {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL level;
	Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context));
	CheckModeTransitions(device.Get(), context.Get());
	for (bool vr : { false, true })
		for (auto mode : { Mode::Full, Mode::ClipOnly })
			for (bool offsetDepth : { false, true })
				for (bool reverseZ : { false, true }) {
					Fixture fixture(device.Get(), context.Get(), vr, mode, offsetDepth, reverseZ);
					CheckOwnership(fixture);
					for (const auto& extra : std::vector<std::vector<D3D_SHADER_MACRO>>{
							 { { "SKINNED", "1" } }, { { "TREE_ANIM", "1" }, { "NORMALS", "1" } }, { { "OFFSET_DEPTH", "1" }, { "REVERSE_Z", "1" } } }) {
						Compile(vr, "VSHADER", extra);
						auto gsExtra = extra;
						if (mode == Mode::ClipOnly)
							gsExtra.push_back({ "PARABOLOID_CLIP_ONLY", "1" });
						Compile(vr, "GEOMETRYSHADER", gsExtra);
					}
					for (float hemisphere : { -1.0f, 1.0f })
						for (unsigned mask = 0; mask < 8; ++mask)
							for (bool reverse : { false, true })
								for (unsigned stereoMode = 0; stereoMode < (vr ? 3u : 1u); ++stereoMode) {
									std::array<Vec, 3> triangle{ Vec{ -.8f, .1f, (mask & 1) ? 1.0f : -1.0f }, Vec{ .7f, -.6f, (mask & 2) ? 2.0f : -2.0f }, Vec{ .2f, .9f, (mask & 4) ? .5f : -.5f } };
									if (reverse)
										std::swap(triangle[1], triangle[2]);
									fixture.Verify(triangle, hemisphere, stereoMode != 0, stereoMode == 2 ? 1 : 0);
								}
					fixture.Verify({ Vec{ 0, 0, 0 }, Vec{ 1, 0, 1 }, Vec{ 0, 1, 1 } }, 1, false, 0);
					fixture.Verify({ Vec{ 0, 0, 0 }, Vec{ 1, 0, 1 }, Vec{ 0, 1, 1 } }, 1, false, 0, 1.0f / 512.0f);
					fixture.Verify({ Vec{ 1, 0, 0 }, Vec{ 0, 1, 1 }, Vec{ -1, 0, 0 } }, 1, false, 0);
				}
	std::cout << "Paraboloid shadow: Full/ClipOnly clipping, fixed subdivision, winding, alpha, stereo and stage ownership passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
