#include "Features/Upscaling/NeuralRendering/ComputeStateGuard.h"
#include "d3d_resource_naming.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdexcept>

namespace
{
	using Microsoft::WRL::ComPtr;
	using NeuralRendering::ComputeStateGuard;
	using NeuralRendering::ComputeStatePolicy;
	enum class BufferBinding
	{
		None,
		Whole,
		Window
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	enum class ContextInterfaces
	{
		Native,
		Context4Only,
		BaseOnly
	};

	class ScopedContextQueries
	{
	public:
		ScopedContextQueries(ID3D11DeviceContext* context, ContextInterfaces interfaces) : context_(context), interfaces_(interfaces)
		{
			Require(active_ == nullptr, "Query interception must not nest");
			Require(SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context4_))) && context4_.Get() == context,
				"WARP must expose Context4 on the tested context address");
			originalTable_ = *reinterpret_cast<void***>(context);
			std::copy_n(originalTable_, table_.size(), table_.begin());
			query_ = reinterpret_cast<Query>(table_[0]);
			table_[0] = reinterpret_cast<void*>(&QueryInterface);
			active_ = this;
			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(context_), table_.data());
		}
		~ScopedContextQueries()
		{
			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(context_), originalTable_);
			active_ = nullptr;
		}
		ScopedContextQueries(const ScopedContextQueries&) = delete;
		ScopedContextQueries& operator=(const ScopedContextQueries&) = delete;

		void CheckQueries() const
		{
			Require(context1Queries_ == 1, "Guard must query Context1 exactly once");
			Require(context4Queries_ == (interfaces_ == ContextInterfaces::Native ? 0u : 1u),
				"Context4 must be queried only after Context1 rejection, without retrying Context1");
		}

	private:
		using Query = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, REFIID, void**);
		static HRESULT STDMETHODCALLTYPE QueryInterface(ID3D11DeviceContext* context, REFIID id, void** result)
		{
			if (!result)
				return E_POINTER;
			auto& owner = *active_;
			if (id == __uuidof(ID3D11DeviceContext1)) {
				++owner.context1Queries_;
				if (owner.interfaces_ != ContextInterfaces::Native) {
					*result = nullptr;
					return E_NOINTERFACE;
				}
			} else if (id == __uuidof(ID3D11DeviceContext4)) {
				++owner.context4Queries_;
				if (owner.interfaces_ == ContextInterfaces::BaseOnly) {
					*result = nullptr;
					return E_NOINTERFACE;
				}
			}
			return owner.query_(context, id, result);
		}

		// The test owns this context; all 149 SDK Context4 slots except QueryInterface stay native.
		std::array<void*, 149> table_{};
		ComPtr<ID3D11DeviceContext4> context4_;
		ID3D11DeviceContext* context_;
		void** originalTable_ = nullptr;
		Query query_ = nullptr;
		ContextInterfaces interfaces_;
		UINT context1Queries_ = 0, context4Queries_ = 0;
		static inline ScopedContextQueries* active_ = nullptr;
	};

	ULONG ReferenceCount(ID3D11DeviceContext* context)
	{
		context->AddRef();
		return context->Release();
	}

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11DeviceContext1> context1;
		std::array<ComPtr<ID3D11UnorderedAccessView>, 5> uavs;
		std::array<ComPtr<ID3D11ShaderResourceView>, 6> srvs;
		ComPtr<ID3D11ComputeShader> shader;
		ComPtr<ID3D11ComputeShader> temporaryShader;
		ComPtr<ID3D11Buffer> constantBuffer;
		ComPtr<ID3D11Buffer> temporaryBuffer;
		ComPtr<ID3D11Predicate> predicate;

		Fixture()
		{
			const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
						&requested, 1, D3D11_SDK_VERSION, &device, nullptr, &context)),
				"Create WARP device");
			Require(SUCCEEDED(context.As(&context1)), "Query WARP D3D11.1 context");
			constexpr char code[] =
				"cbuffer First : register(b0) { float first; };"
				"cbuffer Second : register(b1) { float second; };"
				"RWTexture2D<float> output : register(u0);"
				"[numthreads(1,1,1)] void main() { output[uint2(0,0)] = first + second; }";
			ComPtr<ID3DBlob> bytecode;
			Require(SUCCEEDED(D3DCompile(code, sizeof(code), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &bytecode, nullptr)), "Compile shader");
			Require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Create original shader");
			constexpr char temporaryCode[] = "[numthreads(2,1,1)] void main() {}";
			bytecode.Reset();
			Require(SUCCEEDED(D3DCompile(temporaryCode, sizeof(temporaryCode), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &bytecode, nullptr)), "Compile temporary shader");
			Require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &temporaryShader)), "Create temporary shader");
			Require(shader.Get() != temporaryShader.Get(), "Distinct shaders required to test restoration");
			Util::SetResourceName(shader.Get(), "ComputeStateGuardTest::OriginalShader");
			Util::SetResourceName(temporaryShader.Get(), "ComputeStateGuardTest::TemporaryShader");
			for (UINT i = 0; i < uavs.size(); ++i) {
				auto texture = Texture(D3D11_BIND_UNORDERED_ACCESS, i);
				Require(SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &uavs[i])), "Create UAV");
				Util::SetResourceName(uavs[i].Get(), "ComputeStateGuardTest::Output%u UAV", i);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto texture = Texture(D3D11_BIND_SHADER_RESOURCE, i);
				Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &srvs[i])), "Create SRV");
				Util::SetResourceName(srvs[i].Get(), "ComputeStateGuardTest::Input%u SRV", i);
			}
			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = 1024;
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			std::array<float, 256> values{};
			values[0] = 3;
			values[64] = 42;
			values[128] = 84;
			const D3D11_SUBRESOURCE_DATA initial{ values.data(), 0, 0 };
			Require(SUCCEEDED(device->CreateBuffer(&bufferDesc, &initial, &constantBuffer)), "Create original constant buffer");
			Util::SetResourceName(constantBuffer.Get(), "ComputeStateGuardTest::OriginalConstants");
			Require(SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &temporaryBuffer)), "Create temporary constant buffer");
			Util::SetResourceName(temporaryBuffer.Get(), "ComputeStateGuardTest::TemporaryConstants");
			const D3D11_QUERY_DESC predicateDesc{ D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
			Require(SUCCEEDED(device->CreatePredicate(&predicateDesc, &predicate)), "Create predicate");
			Util::SetResourceName(predicate.Get(), "ComputeStateGuardTest::Predicate");
		}

		ComPtr<ID3D11Texture2D> Texture(UINT bindFlags, UINT index)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = 4;
			desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = bindFlags;
			ComPtr<ID3D11Texture2D> texture;
			Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "Create texture");
			Util::SetResourceName(texture.Get(), "ComputeStateGuardTest::%s%u",
				bindFlags == D3D11_BIND_UNORDERED_ACCESS ? "Output" : "Input", index);
			return texture;
		}

		void Bind(BOOL predicateValue, BufferBinding binding = BufferBinding::Whole, UINT bufferCount = 3)
		{
			for (UINT i = 0; i < uavs.size(); ++i) {
				auto* view = uavs[i].Get();
				context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto* view = srvs[i].Get();
				context->CSSetShaderResources(i, 1, &view);
			}
			auto* buffer = binding == BufferBinding::None ? nullptr : constantBuffer.Get();
			for (UINT slot = 0; slot < bufferCount; ++slot) {
				context->CSSetConstantBuffers(slot, 1, &buffer);
				if (binding == BufferBinding::Window) {
					const UINT first = 16 * (slot + 1), count = 16;
					context1->CSSetConstantBuffers1(slot, 1, &buffer, &first, &count);
				}
			}
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->SetPredication(predicate.Get(), predicateValue);
		}

		void CheckWindow(BufferBinding binding)
		{
			const UINT expectedCount = binding == BufferBinding::Window ? 16u : 4096u;
			auto* expectedBuffer = binding == BufferBinding::None ? nullptr : constantBuffer.Get();
			for (UINT slot = 0; slot < 3; ++slot) {
				const UINT expectedFirst = binding == BufferBinding::Window ? 16u * (slot + 1) : 0u;
				ComPtr<ID3D11Buffer> buffer;
				UINT first = 0, count = 0;
				context1->CSGetConstantBuffers1(slot, 1, &buffer, &first, &count);
				if (first != expectedFirst || count != expectedCount)
					std::fprintf(stderr, "Slot %u: expected constant window %u/%u, observed %u/%u\n", slot, expectedFirst, expectedCount, first, count);
				Require(buffer.Get() == expectedBuffer && first == expectedFirst && count == expectedCount,
					"Constant buffer offset/range restoration");
			}
		}

		void CheckViews(UINT clearedUavs, UINT clearedSrvs)
		{
			for (UINT i = 0; i < uavs.size(); ++i) {
				ComPtr<ID3D11UnorderedAccessView> actual;
				context->CSGetUnorderedAccessViews(i, 1, &actual);
				Require(actual.Get() == (i < clearedUavs ? nullptr : uavs[i].Get()), "UAV identity/coverage");
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				ComPtr<ID3D11ShaderResourceView> actual;
				context->CSGetShaderResources(i, 1, &actual);
				Require(actual.Get() == (i < clearedSrvs ? nullptr : srvs[i].Get()), "SRV identity/coverage");
			}
		}

		void CheckState(ID3D11Buffer* expectedBuffer, ID3D11Predicate* expectedPredicate, BOOL expectedValue, bool temporaryShaderExpected = false)
		{
			ComPtr<ID3D11Buffer> actualBuffer;
			context->CSGetConstantBuffers(0, 1, &actualBuffer);
			Require(actualBuffer.Get() == expectedBuffer, "Constant buffer identity");
			ComPtr<ID3D11Predicate> actualPredicate;
			BOOL actualValue = !expectedValue;
			context->GetPredication(&actualPredicate, &actualValue);
			Require(actualPredicate.Get() == expectedPredicate && actualValue == expectedValue, "Predicate identity/value");
			ComPtr<ID3D11ComputeShader> actualShader;
			UINT classCount = 0;
			context->CSGetShader(&actualShader, nullptr, &classCount);
			Require(actualShader.Get() == (temporaryShaderExpected ? temporaryShader.Get() : shader.Get()) && classCount == 0, "Shader identity and class state");
		}
	};

	template <UINT SRVCount, UINT UAVCount, ComputeStatePolicy Policy = ComputeStatePolicy::ClearBindingsAndPredication, UINT ConstantBuffers = 1>
	void CheckGuard(ContextInterfaces interfaces, BOOL predicateValue, bool unwind, BufferBinding binding)
	{
		using Guard = ComputeStateGuard<SRVCount, UAVCount, Policy, ConstantBuffers>;
		constexpr bool isolated = Policy == ComputeStatePolicy::ClearBindingsAndPredication;
		Fixture fixture;
		fixture.Bind(predicateValue, binding);
		fixture.CheckWindow(binding);
		auto* originalBuffer = binding == BufferBinding::None ? nullptr : fixture.constantBuffer.Get();
		fixture.CheckViews(0, 0);
		fixture.CheckState(originalBuffer, fixture.predicate.Get(), predicateValue);
		ScopedContextQueries queries(fixture.context.Get(), interfaces);
		const auto references = ReferenceCount(fixture.context.Get());
		struct UnwindProbe
		{};
		bool unwound = false;
		try {
			Guard guard(fixture.context.Get());
			Require(guard.Captured(), "Non-null context captured");
			fixture.CheckViews(isolated ? UAVCount : 0, isolated ? SRVCount : 0);
			fixture.CheckState(originalBuffer, isolated ? nullptr : fixture.predicate.Get(), isolated ? FALSE : predicateValue);
			fixture.Bind(!predicateValue, BufferBinding::Whole, ConstantBuffers);
			auto* temporary = fixture.temporaryBuffer.Get();
			for (UINT slot = 0; slot < ConstantBuffers; ++slot)
				fixture.context->CSSetConstantBuffers(slot, 1, &temporary);
			fixture.context->CSSetShader(fixture.temporaryShader.Get(), nullptr, 0);
			guard.Unbind();
			fixture.CheckViews(UAVCount, SRVCount);
			fixture.CheckState(temporary, fixture.predicate.Get(), !predicateValue, true);
			// Different bindings ensure scope exit has to restore the captured views.
			for (UINT i = 0; i < UAVCount; ++i) {
				auto* view = UAVCount == 1 ? nullptr : fixture.uavs[UAVCount - i - 1].Get();
				fixture.context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
			for (UINT i = 0; i < SRVCount; ++i) {
				auto* srv = fixture.srvs[SRVCount - i].Get();
				fixture.context->CSSetShaderResources(i, 1, &srv);
			}
			if (unwind)
				throw UnwindProbe{};
		} catch (const UnwindProbe&) {
			unwound = true;
		}
		Require(unwound == unwind, "Expected exception path exercised");
		fixture.CheckViews(0, 0);
		fixture.CheckState(originalBuffer, fixture.predicate.Get(), isolated ? predicateValue : !predicateValue);
		fixture.CheckWindow(binding);
		queries.CheckQueries();
		Require(ReferenceCount(fixture.context.Get()) == references, "Guard must release its queried context interfaces");
		Guard missingContext(nullptr);
		Require(!missingContext.Captured(), "Null context is not captured");
		missingContext.Unbind();
	}

	void CheckRestoredDispatch(ContextInterfaces interfaces)
	{
		Fixture fixture;
		fixture.Bind(FALSE, BufferBinding::Window);
		ScopedContextQueries queries(fixture.context.Get(), interfaces);
		{
			ComputeStateGuard<4, 4, ComputeStatePolicy::ClearBindingsAndPredication, 2> guard(fixture.context.Get());
			fixture.Bind(TRUE, BufferBinding::Whole, 2);
		}
		fixture.CheckWindow(BufferBinding::Window);
		queries.CheckQueries();
		fixture.context->SetPredication(nullptr, FALSE);
		fixture.context->Dispatch(1, 1, 1);
		ComPtr<ID3D11Resource> output;
		fixture.uavs[0]->GetResource(&output);
		ComPtr<ID3D11Texture2D> texture;
		Require(SUCCEEDED(output.As(&texture)), "Output texture interface");
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> readback;
		Require(SUCCEEDED(fixture.device->CreateTexture2D(&desc, nullptr, &readback)), "Create readback");
		Util::SetResourceName(readback.Get(), "ComputeStateGuardTest::Readback");
		fixture.context->CopyResource(readback.Get(), texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Require(SUCCEEDED(fixture.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Map restored dispatch result");
		float result = 0;
		std::memcpy(&result, mapped.pData, sizeof(result));
		fixture.context->Unmap(readback.Get(), 0);
		Require(result == 126, "Restored shader must read 42 + 84 from the two original buffer windows");
	}
}

int main()
{
	try {
		for (auto interfaces : { ContextInterfaces::Native, ContextInterfaces::Context4Only, ContextInterfaces::BaseOnly }) {
			for (BOOL predicateValue : { FALSE, TRUE }) {
				for (bool unwind : { false, true }) {
					for (auto binding : { BufferBinding::Window, BufferBinding::Whole, BufferBinding::None }) {
						if (interfaces == ContextInterfaces::BaseOnly && binding == BufferBinding::Window)
							continue;
						CheckGuard<1, 1>(interfaces, predicateValue, unwind, binding);
						CheckGuard<3, 1>(interfaces, predicateValue, unwind, binding);
						CheckGuard<4, 4>(interfaces, predicateValue, unwind, binding);
						CheckGuard<5, 1>(interfaces, predicateValue, unwind, binding);
						CheckGuard<1, 1, ComputeStatePolicy::PreserveBindings>(interfaces, predicateValue, unwind, binding);
						CheckGuard<4, 3, ComputeStatePolicy::PreserveBindings>(interfaces, predicateValue, unwind, binding);
						CheckGuard<4, 4, ComputeStatePolicy::ClearBindingsAndPredication, 2>(interfaces, predicateValue, unwind, binding);
					}
				}
			}
		}
		CheckRestoredDispatch(ContextInterfaces::Native);
		CheckRestoredDispatch(ContextInterfaces::Context4Only);
		std::puts("ComputeStateGuard: 224 restoration cases and 2 buffer-window dispatch checks passed (native, Context4-only, base-only)");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ComputeStateGuard: %s\n", error.what());
		return 1;
	}
}
