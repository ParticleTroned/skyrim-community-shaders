#include "Features/FurShells/DepthBinding.h"
#include "d3d11_shader_test.h"

#include <array>
#include <cstdio>
#include <d3dcompiler.h>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;
	using GetResources = decltype(&ID3D11DeviceContext::VSGetShaderResources);
	using SetResources = decltype(&ID3D11DeviceContext::VSSetShaderResources);
	constexpr std::array<std::pair<GetResources, SetResources>, 6> stages{ {
		{ &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::VSSetShaderResources },
		{ &ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::HSSetShaderResources },
		{ &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::DSSetShaderResources },
		{ &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::GSSetShaderResources },
		{ &ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::PSSetShaderResources },
		{ &ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::CSSetShaderResources },
	} };

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct CountedInput : ID3D11ShaderResourceView
	{
		ComPtr<ID3D11ShaderResourceView> view;
		unsigned queries = 0;
		bool fail = false;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override { return view->QueryInterface(iid, value); }
		ULONG STDMETHODCALLTYPE AddRef() override { return view->AddRef(); }
		ULONG STDMETHODCALLTYPE Release() override { return view->Release(); }
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** value) override { view->GetDevice(value); }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID key, UINT* size, void* data) override { return view->GetPrivateData(key, size, data); }
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID key, UINT size, const void* data) override { return view->SetPrivateData(key, size, data); }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID key, const IUnknown* data) override { return view->SetPrivateDataInterface(key, data); }
		void STDMETHODCALLTYPE GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* value) override { view->GetDesc(value); }
		void STDMETHODCALLTYPE GetResource(ID3D11Resource** value) override
		{
			++queries;
			if (fail)
				*value = nullptr;
			else
				view->GetResource(value);
		}
	};

	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> depth, readback, colorReadback;
		ComPtr<ID3D11DepthStencilView> writable, readOnly;
		ComPtr<ID3D11ShaderResourceView> depthInput, unrelatedInput;
		std::array<ComPtr<ID3D11RenderTargetView>, 8> targets;
		ComPtr<ID3D11Buffer> counterBuffer, counterReadback;
		ComPtr<ID3D11UnorderedAccessView> counter;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11DepthStencilState> prepass, shade;
		ComPtr<ID3D11BlendState> noColorWrite;
		FurRendering::DepthViewCache cache;

		Fixture(D3D_FEATURE_LEVEL level, bool reverse)
		{
			Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
				D3D11_SDK_VERSION, &device, nullptr, &context));
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = texture.Height = 8;
			texture.ArraySize = texture.MipLevels = 2;
			texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
			texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			Check(device->CreateTexture2D(&texture, nullptr, &depth));
			Util::SetResourceName(depth.Get(), "FurDepthTest::Depth");
			texture.BindFlags = 0;
			texture.Usage = D3D11_USAGE_STAGING;
			texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&texture, nullptr, &readback));
			Util::SetResourceName(readback.Get(), "FurDepthTest::DepthReadback");

			D3D11_DEPTH_STENCIL_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			view.Texture2DArray.ArraySize = view.Texture2DArray.FirstArraySlice = 1;
			Check(device->CreateDepthStencilView(depth.Get(), &view, &writable));
			Util::SetResourceName(writable.Get(), "FurDepthTest::Writable DSV");
			view.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
			Check(device->CreateDepthStencilView(depth.Get(), &view, &readOnly));
			Util::SetResourceName(readOnly.Get(), "FurDepthTest::ReadOnly DSV");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray.MipLevels = srv.Texture2DArray.ArraySize = 2;
			Check(device->CreateShaderResourceView(depth.Get(), &srv, &depthInput));
			Util::SetResourceName(depthInput.Get(), "FurDepthTest::Depth SRV");

			texture.ArraySize = texture.MipLevels = 1;
			texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			texture.Usage = D3D11_USAGE_DEFAULT;
			texture.CPUAccessFlags = 0;
			for (UINT i = 0; i < targets.size(); ++i) {
				ComPtr<ID3D11Texture2D> color;
				Check(device->CreateTexture2D(&texture, nullptr, &color));
				Util::SetResourceName(color.Get(), "FurDepthTest::Color%u", i);
				Check(device->CreateRenderTargetView(color.Get(), nullptr, &targets[i]));
				Util::SetResourceName(targets[i].Get(), "FurDepthTest::Color%u RTV", i);
				if (i == 0) {
					ComPtr<ID3D11Texture2D> unrelated;
					Check(device->CreateTexture2D(&texture, nullptr, &unrelated));
					Util::SetResourceName(unrelated.Get(), "FurDepthTest::Unrelated");
					Check(device->CreateShaderResourceView(unrelated.Get(), nullptr, &unrelatedInput));
					Util::SetResourceName(unrelatedInput.Get(), "FurDepthTest::Unrelated SRV");
				}
			}
			texture.BindFlags = 0;
			texture.Usage = D3D11_USAGE_STAGING;
			texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateTexture2D(&texture, nullptr, &colorReadback));
			Util::SetResourceName(colorReadback.Get(), "FurDepthTest::ColorReadback");
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = 256;
			buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			buffer.StructureByteStride = 4;
			Check(device->CreateBuffer(&buffer, nullptr, &counterBuffer));
			Util::SetResourceName(counterBuffer.Get(), "FurDepthTest::Counter");
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = DXGI_FORMAT_UNKNOWN;
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = 64;
			uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
			Check(device->CreateUnorderedAccessView(counterBuffer.Get(), &uav, &counter));
			Util::SetResourceName(counter.Get(), "FurDepthTest::Counter UAV");
			buffer = {};
			buffer.ByteWidth = 4;
			buffer.Usage = D3D11_USAGE_STAGING;
			buffer.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Check(device->CreateBuffer(&buffer, nullptr, &counterReadback));
			Util::SetResourceName(counterReadback.Get(), "FurDepthTest::CounterReadback");

			constexpr char vs[] = "float4 main(uint i:SV_VertexID):SV_Position{float2 uv=float2((i<<1)&2,i&2);return float4(uv*float2(2,-2)+float2(-1,1),0.5,1);}";
			constexpr char ps[] = "float4 main():SV_Target{return 1;}";
			ComPtr<ID3DBlob> code;
			Check(D3DCompile(vs, sizeof(vs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr));
			Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex));
			Util::SetResourceName(vertex.Get(), "FurDepthTest::VertexShader");
			code.Reset();
			Check(D3DCompile(ps, sizeof(ps), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, nullptr));
			Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel));
			Util::SetResourceName(pixel.Get(), "FurDepthTest::PixelShader");
			D3D11_DEPTH_STENCIL_DESC state{};
			state.DepthEnable = TRUE;
			state.DepthFunc = reverse ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
			state.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			state.StencilEnable = TRUE;
			state.StencilReadMask = 0xFF;
			state.StencilWriteMask = 0;
			state.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			state.FrontFace.StencilPassOp = state.FrontFace.StencilFailOp = state.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
			state.BackFace = state.FrontFace;
			Check(device->CreateDepthStencilState(&state, &prepass));
			Util::SetResourceName(prepass.Get(), "FurDepthTest::PrepassState");
			state.DepthFunc = D3D11_COMPARISON_EQUAL;
			state.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			Check(device->CreateDepthStencilState(&state, &shade));
			Util::SetResourceName(shade.Get(), "FurDepthTest::ShadeState");
			D3D11_BLEND_DESC blend{};
			Check(device->CreateBlendState(&blend, &noColorWrite));
			Util::SetResourceName(noColorWrite.Get(), "FurDepthTest::NoColorWrite");
		}

		UINT Color()
		{
			ComPtr<ID3D11Resource> color;
			targets[0]->GetResource(&color);
			context->CopyResource(colorReadback.Get(), color.Get());
			D3D11_MAPPED_SUBRESOURCE data{};
			Check(context->Map(colorReadback.Get(), 0, D3D11_MAP_READ, 0, &data));
			const auto result = *static_cast<UINT*>(data.pData);
			context->Unmap(colorReadback.Get(), 0);
			return result;
		}

		auto ResolveView()
		{
			return [this](ID3D11DepthStencilView* source) { return source == readOnly.Get() ? writable.Get() : nullptr; };
		}

		void CheckInputCache()
		{
			const auto identity = Util::GetComIdentity(depth.Get());
			FurRendering::detail::DepthInputCache inputs(identity.Get());
			CountedInput alias;
			alias.view = depthInput;
			for (unsigned i = 0; i < 6 * 128; ++i)
				Require(inputs.Aliases(&alias) == true, "Repeated inputs must retain their alias decision");
			Require(alias.queries == 1, "Resolve an identical SRV once per binding transaction");
			std::array<CountedInput, 129> crowded;
			for (size_t i = 0; i < crowded.size(); ++i)
				crowded[i].view = i % 2 ? depthInput : unrelatedInput;
			for (unsigned repeat = 0; repeat < 2; ++repeat)
				for (size_t i = 0; i < crowded.size(); ++i)
					Require(inputs.Aliases(&crowded[i]) == bool(i % 2), "Collisions must reclassify the exact input");
			CountedInput unavailable;
			unavailable.view = depthInput;
			unavailable.fail = true;
			Require(!inputs.Aliases(&unavailable).has_value(), "Unknown resource identity must fail closed");
			unavailable.fail = false;
			Require(inputs.Aliases(&unavailable) == true, "Failed identity queries must remain retryable");
			ComPtr<ID3D11Resource> other;
			unrelatedInput->GetResource(&other);
			const auto otherIdentity = Util::GetComIdentity(other.Get());
			FurRendering::detail::DepthInputCache next(otherIdentity.Get());
			Require(next.Aliases(&alias) == false && alias.queries == 2, "A new transaction must not reuse an old depth decision");
		}

		void CheckCache()
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			readOnly->GetDesc(&desc);
			desc.Texture2DArray.MipSlice = 1;
			desc.Flags = 0;
			ComPtr<ID3D11DepthStencilView> mipWritable;
			Check(device->CreateDepthStencilView(depth.Get(), &desc, &mipWritable));
			Util::SetResourceName(mipWritable.Get(), "FurDepthTest::NativeMip DSV");
			desc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
			ComPtr<ID3D11DepthStencilView> mipView;
			Check(device->CreateDepthStencilView(depth.Get(), &desc, &mipView));
			Util::SetResourceName(mipView.Get(), "FurDepthTest::Mip DSV");
			unsigned resolutions = 0;
			auto resolve = [&](ID3D11DepthStencilView* source) {
				++resolutions;
				return source == readOnly.Get() ? writable.Get() : mipWritable.Get();
			};
			const auto* view = cache.Resolve(mipView.Get(), resolve);
			Require(view && SUCCEEDED(view->status), "Resolve a depth-only read-only view");
			D3D11_DEPTH_STENCIL_VIEW_DESC actual{};
			view->writable->GetDesc(&actual);
			Require(actual.Texture2DArray.MipSlice == 1 && actual.Flags == 0, "Preserve mip selection and original stencil policy");
			winrt::com_ptr<ID3D11DepthStencilView> retained = view->writable;
			cache.Reset();
			const auto beforeReset = resolutions;
			view = cache.Resolve(mipView.Get(), resolve);
			Require(view && SUCCEEDED(view->status) && retained == view->writable && resolutions == beforeReset + 1, "Reset must resolve the native pair again without creating a new view");
			cache.Reset();
			for (size_t i = 0; i < FurRendering::DepthViewCache::MaximumViews; ++i) {
				ComPtr<ID3D11DepthStencilView> source;
				Check(device->CreateDepthStencilView(depth.Get(), &desc, &source));
				Util::SetResourceName(source.Get(), "FurDepthTest::BoundedSource%zu DSV", i);
				view = cache.Resolve(source.Get(), resolve);
				Require(view && SUCCEEDED(view->status), "Populate bounded view cache");
			}
			context->ClearState();
			context->OMSetRenderTargets(0, nullptr, readOnly.Get());
			{
				FurRendering::ScopedDepthWrite rotated(context.Get(), cache, resolve);
				Check(rotated.Status());
				for (size_t i = 0; i <= FurRendering::DepthViewCache::MaximumViews; ++i) {
					ComPtr<ID3D11DepthStencilView> source;
					Check(device->CreateDepthStencilView(depth.Get(), &desc, &source));
					Util::SetResourceName(source.Get(), "FurDepthTest::Replacement%zu DSV", i);
					view = cache.Resolve(source.Get(), resolve);
					Require(view && SUCCEEDED(view->status), "Cache turnover must retain fur rendering");
				}
			}
			ComPtr<ID3D11DepthStencilView> restored;
			context->OMGetRenderTargets(0, nullptr, &restored);
			Require(restored.Get() == readOnly.Get(), "Cache eviction during a pass must preserve output restoration");
			context->ClearState();
			cache.Reset();
			context->OMSetRenderTargets(0, nullptr, readOnly.Get());
			{
				FurRendering::ScopedDepthWrite wrongMip(context.Get(), cache, [&](auto*) { return mipWritable.Get(); });
				Require(FAILED(wrongMip.Status()), "A mismatched writable subresource must be rejected");
			}
			cache.Reset();
			{
				FurRendering::ScopedDepthWrite unknown(context.Get(), cache, [](auto*) -> ID3D11DepthStencilView* { return nullptr; });
				Require(FAILED(unknown.Status()), "Unknown depth domains must fail closed");
			}
			{
				FurRendering::ScopedDepthWrite recovered(context.Get(), cache, ResolveView());
				Check(recovered.Status());
			}
			restored.Reset();
			context->OMGetRenderTargets(0, nullptr, &restored);
			Require(restored.Get() == readOnly.Get(), "Rejected view lookup must not modify the output binding");
			context->ClearState();
			cache.Reset();
		}

		std::pair<float, unsigned char> Depth()
		{
			context->CopyResource(readback.Get(), depth.Get());
			D3D11_MAPPED_SUBRESOURCE data{};
			Check(context->Map(readback.Get(), 2, D3D11_MAP_READ, 0, &data));
			auto result = std::pair{ *static_cast<float*>(data.pData), static_cast<unsigned char*>(data.pData)[4] };
			context->Unmap(readback.Get(), 2);
			return result;
		}

		void CheckBindings(UINT targetCount, UINT uavSlot, bool unwind, bool reverse)
		{
			const UINT firstAlias = targetCount == 3 ? 127 : 5;
			context->ClearState();
			context->ClearDepthStencilView(writable.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, reverse ? 0.2f : 0.8f, 37);
			constexpr FLOAT clear[4]{};
			context->ClearRenderTargetView(targets[0].Get(), clear);
			std::array<ID3D11RenderTargetView*, 8> raw{};
			for (UINT i = 0; i < targetCount; ++i)
				if (i != 1)
					raw[i] = targets[i].Get();
			auto* uav = counter.Get();
			const UINT initial = 43;
			context->OMSetRenderTargetsAndUnorderedAccessViews(targetCount, raw.data(), readOnly.Get(), uavSlot, 1, &uav, &initial);
			for (const auto& [get, set] : stages) {
				auto* alias = depthInput.Get();
				auto* unrelated = unrelatedInput.Get();
				(context.Get()->*set)(127, 1, &alias);
				(context.Get()->*set)(firstAlias, 1, &alias);
				(context.Get()->*set)(9, 1, &unrelated);
			}
			D3D11_VIEWPORT viewport{ 0, 0, 8, 8, 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->OMSetDepthStencilState(prepass.Get(), 0);
			context->OMSetBlendState(noColorWrite.Get(), nullptr, ~0u);
			const auto* cached = cache.Resolve(readOnly.Get(), ResolveView());
			Require(cached && SUCCEEDED(cached->status), "Resolve writable depth view");
			Require(cache.Resolve(readOnly.Get(), ResolveView())->writable == cached->writable, "Reuse the cached view");
			try {
				FurRendering::ScopedDepthWrite binding(context.Get(), cache, ResolveView());
				Check(binding.Status());
				ComPtr<ID3D11DepthStencilView> bound;
				context->OMGetRenderTargets(0, nullptr, &bound);
				D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
				bound->GetDesc(&desc);
				Require(!(desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) && bound.Get() == writable.Get(), "Reuse the engine-owned writable DSV");
				Require(desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY && desc.Texture2DArray.FirstArraySlice == 1 && desc.Texture2DArray.ArraySize == 1, "Preserve depth subresource");
				for (const auto& [get, set] : stages) {
					ComPtr<ID3D11ShaderResourceView> alias, unrelated;
					(context.Get()->*get)(127, 1, &alias);
					(context.Get()->*get)(9, 1, &unrelated);
					Require(!alias && unrelated.Get() == unrelatedInput.Get(), "Unbind only depth aliases across every stage");
					(context.Get()->*get)(firstAlias, 1, alias.ReleaseAndGetAddressOf());
					Require(!alias, "Unbind low and high aliases around unrelated inputs");
				}
				{
					FurRendering::ScopedDepthWrite nested(context.Get(), cache, ResolveView());
					Check(nested.Status());
				}
				context->Draw(3, 0);
				for (const auto& [get, set] : stages) {
					ID3D11ShaderResourceView* empty = nullptr;
					(context.Get()->*set)(9, 1, &empty);
				}
				if (unwind)
					throw 1;
			} catch (int) {
				Require(unwind, "Unexpected unwind");
			}
			ComPtr<ID3D11DepthStencilView> restored;
			std::array<ID3D11RenderTargetView*, 8> restoredTargets{};
			context->OMGetRenderTargets(8, restoredTargets.data(), &restored);
			Require(restored.Get() == readOnly.Get(), "Restore original read-only DSV");
			for (size_t i = 0; i < raw.size(); ++i) {
				const bool equal = restoredTargets[i] == raw[i];
				if (restoredTargets[i])
					restoredTargets[i]->Release();
				Require(equal, "Preserve RTVs and empty slots");
			}
			for (const auto& [get, set] : stages) {
				ComPtr<ID3D11ShaderResourceView> alias;
				(context.Get()->*get)(127, 1, &alias);
				Require(alias.Get() == depthInput.Get(), "Restore every depth alias after its read-only output");
				(context.Get()->*get)(firstAlias, 1, alias.ReleaseAndGetAddressOf());
				Require(alias.Get() == depthInput.Get(), "Restore the first alias in each affected stage");
				(context.Get()->*get)(9, 1, alias.ReleaseAndGetAddressOf());
				Require(alias.Get() == unrelatedInput.Get(), "Restore unrelated inputs changed inside the scope");
			}
			ComPtr<ID3D11UnorderedAccessView> restoredCounter;
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, uavSlot, 1, &restoredCounter);
			Require(restoredCounter.Get() == counter.Get(), "Preserve pixel UAV binding");
			context->CopyStructureCount(counterReadback.Get(), 0, counter.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(counterReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			const auto savedCount = *static_cast<UINT*>(mapped.pData);
			context->Unmap(counterReadback.Get(), 0);
			Require(savedCount == initial, "Preserve hidden UAV counter");
			const auto [value, stencil] = Depth();
			Require(value == 0.5f && stencil == 37, "Write shell depth without changing stencil");
			context->OMSetDepthStencilState(shade.Get(), 0);
			context->OMSetBlendState(nullptr, nullptr, ~0u);
			context->Draw(3, 0);
			Require(Depth().first == 0.5f, "Shading must not change shell depth");
			Require(Color() == ~0u, "EQUAL shading must see the shell depth written by the prepass");
			context->ClearState();
			{
				FurRendering::ScopedDepthWrite absent(context.Get(), cache, ResolveView());
				Require(FAILED(absent.Status()), "Missing depth must fail closed");
			}
		}
	};
}

int main()
{
	try {
		for (auto level : { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 })
			for (bool reverse : { false, true }) {
				Fixture fixture(level, reverse);
				fixture.CheckInputCache();
				for (bool unwind : { false, true }) {
					fixture.CheckBindings(3, 3, unwind, reverse);
					if (level >= D3D_FEATURE_LEVEL_11_1)
						fixture.CheckBindings(8, 15, unwind, reverse);
				}
				fixture.CheckCache();
			}
		std::puts("Fur depth binding: writable depth, array slices, alias restoration, nesting, exceptions and UAV counters passed");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Fur depth binding: %s\n", error.what());
		return 1;
	}
}
